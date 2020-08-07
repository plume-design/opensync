/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "ovsdb.h"
#include "ovsdb_update.h"
#include "log.h"
#include "util.h"
#include "json_util.h"
#include "schema.h"
#include "os_proc.h"
#include "ovsdb_sync.h"
#include "osp_upg.h"
#include "um.h"
#include "ovsdb.h"
#include "ovsdb_table.h"

#define MODULE_ID LOG_MODULE_ID_UPG

/* ovsdb update callback    */
static void callback_AWLAN_Node(
        ovsdb_update_monitor_t *self,
        struct schema_AWLAN_Node *old_rec,
        struct schema_AWLAN_Node *awlan_node);

static void cb_upg(const osp_upg_op_t op,
                   const osp_upg_status_t status,
                   uint8_t completed);

int um_map_errno_osp_to_cloud(osp_upg_status_t osp_err);

/* AWLAN_Node  monitor                  */
static ovsdb_table_t table_AWLAN_Node;

static ev_timer  utimer;
static char     *upg_url;

/*
 * Map error codes between osp to cloud
 *
 * Cloud and osp uses different numbering for error code
 * so maping between them is needed.
 */
int um_map_errno_osp_to_cloud(osp_upg_status_t osp_err){
    switch (osp_err){
        case OSP_UPG_OK:
            return UM_ERR_OK;
        case OSP_UPG_ARGS:
            return UM_ERR_ARGS;
        case OSP_UPG_URL:
            return UM_ERR_URL;
        case OSP_UPG_DL_FW:
            return UM_ERR_DL_FW;
        case OSP_UPG_DL_MD5:
            return UM_ERR_DL_MD5;
        case OSP_UPG_MD5_FAIL:
            return UM_ERR_MD5_FAIL;
        case OSP_UPG_IMG_FAIL:
            return UM_ERR_IMG_FAIL;
        case OSP_UPG_FL_ERASE:
            return UM_ERR_FL_ERASE;
        case OSP_UPG_FL_WRITE:
            return UM_ERR_FL_WRITE;
        case OSP_UPG_FL_CHECK:
            return UM_ERR_FL_CHECK;
        case OSP_UPG_BC_SET:
            return UM_ERR_BC_SET;
        case OSP_UPG_APPLY:
            return UM_ERR_APPLY;
        case OSP_UPG_BC_ERASE:
            return UM_ERR_BC_ERASE;
        case OSP_UPG_SU_RUN:
            return UM_ERR_SU_RUN;
        case OSP_UPG_DL_NOFREE:
            return UM_ERR_DL_NOFREE;
        case OSP_UPG_WRONG_PARAM:
            return UM_ERR_WRONG_PARAM;
        case OSP_UPG_INTERNAL:
            return UM_ERR_INTERNAL;
        default:
            LOGE("Unknown error returning UM_ERR_INTERNAL");
            return UM_ERR_INTERNAL;
    }
}

/*
 * Update firmware_status field in AWLAN_Node table
 *
 * Note that this function doesn't check sanity of the status
 * It is possible to set 0 status as well with this function
 */
bool um_status_update(int status)
{
    json_t * res;
    struct schema_AWLAN_Node  anode;

    LOG(DEBUG, "Setting upgrade_status to %d", status);

    /* zero whole structure         */
    memset(&anode, 0, sizeof(struct schema_AWLAN_Node));

    /*set upgrade status            */
    anode.upgrade_status = status;

    /* fill the row with NODE data  */
    res = ovsdb_tran_call_s("AWLAN_Node",
                            OTR_UPDATE,
                            /* TODO: hack - to be fixed - single row table  */
                            ovsdb_tran_cond(OCLM_STR, "id", OFUNC_NEQ, "empty"),
                            ovsdb_row_filter(
                                schema_AWLAN_Node_to_json(&anode, NULL),
                                "upgrade_status",
                                NULL)
                           );

    if (res == NULL)
    {
        LOG(ERR, "Setting upgrade_status to %d failed", status);
        return false;
    }
    return true;
}

void um_start_download(char* url, int timeout)
{
    int status = UM_ERR_OK;

    if(timeout == 0)
    {
        timeout = UM_DEFAULT_DL_TMO;
    }

    LOG(DEBUG, "(%s): Start download: %s", __func__, url);

    if (false == osp_upg_check_system())
    {
        status = um_map_errno_osp_to_cloud(osp_upg_errno());
        LOGE("System not ready for upgrade errno: %d", osp_upg_errno());
        goto exit;
    }

    if (false == osp_upg_dl(url, timeout, cb_upg))
    {
        status = um_map_errno_osp_to_cloud(osp_upg_errno());
        LOGE("Error downloading %s: Errno: %d mapped %d", url, osp_upg_errno(), status);
        goto exit;
    }

    status = UM_STS_FW_DL_START;
exit:
    if (false == um_status_update(status))
    {
        LOG(ERR, "Failed to update upgrade_status");
    }
}

/**
 * Timer callback used to start upgrade when time is up
 **/
void cb_start_upgrade(EV_P_ ev_timer * w, int events)
{
    (void)events;
    int     status;

    /* stop timer watcher */
    ev_timer_stop(EV_A_ w);

    LOG(DEBUG, "(%s): Start upgrade", __func__);

    /* Check if no upgrade in progress */
    if (false == osp_upg_check_system())
    {
        status = um_map_errno_osp_to_cloud(osp_upg_errno());
        LOGE("System not ready for upgrade errno: %d", osp_upg_errno());
        goto exit;
    }

    if (false == osp_upg_upgrade(w->data, cb_upg))
    {
        status = um_map_errno_osp_to_cloud(osp_upg_errno());
        LOGE("Error in upgrading. Errno: %d mapped %d", osp_upg_errno(), status);
        goto exit;
    }

    status = UM_STS_FW_WR_START;
exit:
    /* Clear timer data */
    if (w->data)
    {
         free(w->data);
         w->data = NULL;
    }

    if (false == um_status_update(status))
    {
        LOG(ERR, "Failed to update upgrade_status");
    }
}

/**
 * * @brief Callback invoked by target layer during download & upgrade process
 * * @param[in] op - operation: download, download CS file or upgrade
 * * @param[in] status status
 * * @param[in] completed percentage of completed work 0 - 100%
 * */
static void cb_upg(const osp_upg_op_t op,
                   const osp_upg_status_t status,
                   uint8_t completed)
{
    int ret_status = UM_ERR_INTERNAL;
    switch (op) {
        case OSP_UPG_DL:
            if (status == OSP_UPG_OK)
            {
                LOG(INFO, "Download successfully completed");
                ret_status = UM_STS_FW_DL_END;
            }
            else
            {
                if(upg_url)
                {
                    free(upg_url);
                    upg_url = NULL;
                }
                LOG(ERR, "Error in downloading. Errno: %d", osp_upg_errno());
                ret_status = um_map_errno_osp_to_cloud(osp_upg_errno());
            }
            break;
        case OSP_UPG_UPG:
            if (status == OSP_UPG_OK)
            {
                /* After successful upgrade commit needs to be called to change bootconfig */
                LOG(INFO, "Upgrade successfully completed");

                if (false == um_status_update(UM_STS_FW_WR_END))
                {
                    LOG(ERR, "Failed to update upgrade_status");
                }
                if (false == um_status_update(UM_STS_FW_BC_START))
                {
                    LOG(ERR, "Failed to update upgrade_status");
                }
                if (false == osp_upg_commit())
                {
                    LOG(ERR, "Error in applying upgrade");
                    ret_status = um_map_errno_osp_to_cloud(osp_upg_errno());
                }
                else
                {
                    LOG(DEBUG, "Commit successful.");
                    ret_status = UM_STS_FW_BC_END;
                }
            }
            else
            {
                LOG(ERR, "Error in upgrading: Errno: %d", osp_upg_errno());
                ret_status = um_map_errno_osp_to_cloud(osp_upg_errno());
            }
            break;
        default:
            LOG(ERR, "Invalid state");
            break;
    }

    if (false == um_status_update(ret_status))
    {
        LOG(ERR, "Failed to update upgrade_status");
    }
}

/*
 * Monitor callback
 */
static void callback_AWLAN_Node(
        ovsdb_update_monitor_t *self,
        struct schema_AWLAN_Node *old_rec,
        struct schema_AWLAN_Node *awlan_node)
{
    LOG(DEBUG, "Firmaware url %s: %s",    awlan_node->firmware_url_changed     ? "changed" : "same", awlan_node->firmware_url);
    LOG(DEBUG, "Firmaware pass %s: %s",   awlan_node->firmware_pass_changed    ? "changed" : "same", awlan_node->firmware_pass);
    LOG(DEBUG, "Upgrade timer %s: %d",    awlan_node->upgrade_timer_changed    ? "changed" : "same", awlan_node->upgrade_timer);
    LOG(DEBUG, "Upgrade dl timer %s: %d", awlan_node->upgrade_dl_timer_changed ? "changed" : "same", awlan_node->upgrade_dl_timer);


    switch (self->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if(awlan_node->firmware_url_changed){
                LOG(INFO, "Upgrade firmware_url %s, timer %d",
                            awlan_node->firmware_url,
                            awlan_node->upgrade_timer);
                if (strlen(awlan_node->firmware_url) > 0)
                {
                    if(upg_url == NULL || strncmp(upg_url, awlan_node->firmware_url, sizeof(awlan_node->firmware_url))){
                        upg_url = strdup(awlan_node->firmware_url);
                        um_start_download(upg_url, awlan_node->upgrade_dl_timer);
                    }
                    else
                    {
                        LOG(NOTICE, "Upgrade already in progress");
                    }
                }
                else
                {
                    if(upg_url)
                    {
                        free(upg_url);
                        upg_url = NULL;
                    }
                    LOG(NOTICE, "URL is empty");
                }
            }

            if(awlan_node->firmware_pass_changed){
                //TODO Is there something that needs to be done here?
            }

            if(awlan_node->upgrade_dl_timer_changed){
                //TODO Is there something that needs to be done here?
            }

            if(awlan_node->upgrade_timer_changed){
                if (awlan_node->upgrade_timer > 0)
                {
                    /* if there is active timer, stop it to set new value   */
                    if (utimer.active)
                    {
                        LOG(DEBUG, "Timer is active - stop it");
                        ev_timer_stop(EV_DEFAULT, &utimer);
                    }

                    if (utimer.data)
                    {
                        free(utimer.data);
                        utimer.data = NULL;
                    }

                    /* check for firmware password */
                    if (strlen(awlan_node->firmware_pass) > 0)
                    {
                        utimer.data = strdup(awlan_node->firmware_pass);
                    }

                    /*
                     * start upgrade timer,
                     * on time out it is going to start upgrade process
                     */
                    ev_timer_set(&utimer, awlan_node->upgrade_timer, 0);
                    ev_timer_start(EV_DEFAULT , &utimer);

                    LOG(NOTICE, "Upgrade about to start in %d seconds",
                                                            awlan_node->upgrade_timer);
                }
                else
                {
                    /* check if timer is active, stop the timer in that case    */
                    if (utimer.active)
                    {
                        LOG(NOTICE, "Stopping pending upgrade");
                        ev_timer_stop(EV_DEFAULT, &utimer);
                    }
                }
            }
            break;
        case OVSDB_UPDATE_DEL:
            LOG(ERR, "AWLAN_Node table single row deleted !!! ");
            break;

        default:
            LOG(ERR, "CB unknown monitor type event: %d", self->mon_type);
            break;
    }
}


/*
 * Send echo request to verify ovsdb sanity
 */
bool um_ovsdb_echo()
{
    char * tst[] = {"UPGRADE MANGER"};

    /* blocking echo    */
    return ovsdb_echo_call_s_argv(1, tst);

}


/*
 * Handle UM cloud communication
 */
bool um_ovsdb()
{
    bool    ret = false;
    char   *filter[] = { "+",
                         SCHEMA_COLUMN(AWLAN_Node, firmware_url),
                         SCHEMA_COLUMN(AWLAN_Node, firmware_pass),
                         SCHEMA_COLUMN(AWLAN_Node, upgrade_timer),
                         NULL };
    /*
     * Connect to the database
     */
    if (!ovsdb_init("UM"))
    {
        LOG(ERR, "Error initializing OVSDB.");
    }

    /*
     * Check if echo works, verify DB communication
     */
    if (false == um_ovsdb_echo())
    {
        goto exit;
    }
    else
    {
        LOG(NOTICE, "Echo OK");
    }

    /* initialize upgrade timer, with some default value          */
    ev_timer_init (&utimer, cb_start_upgrade, 60, 0);

    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_MONITOR_F(AWLAN_Node, filter);

    ret = true;

exit:
    return ret;
}
