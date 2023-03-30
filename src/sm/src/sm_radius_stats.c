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

#include <ev.h>
#include <stdbool.h>

#include "module.h"
#include "ovsdb.h"
#include "ovsdb_table.h"
#include "memutil.h"
#include "util.h"
#include "sm.h"

struct sm_radius_data
{
    sm_radius_stats_t stats;

    ds_dlist_node_t node;
};

struct sm_radius_vif
{
    char vif_name[16];
    char vif_role[64];
    bool active;

    ds_tree_node_t node;
};

/* Timer for radius stats reporting */
static ev_timer radius_report_timer;

/* Flag to disable/enable Wifi_VIF_State schema monitor callback */
static bool wifi_vif_state_monitor_cb_enabled = false;

/* Flag to disable/enable "recycling" of allocated radius_data objects */
static bool empty_radius_data_queue_enabled = false;

static const char backend_name[] = "radius";

/* Object collections (linked lists and trees) inited statically */
static ds_tree_t vifs = DS_TREE_INIT(ds_str_cmp, struct sm_radius_vif, node);  /* EAP-enabled VIFs           */
static ds_dlist_t pending_data_q = DS_DLIST_INIT(struct sm_radius_data, node); /* pending data objects queue */
static ds_dlist_t empty_data_q = DS_DLIST_INIT(struct sm_radius_data, node);   /* empty data objects queue   */

/* OVSDB table objects */
static ovsdb_table_t table_Wifi_VIF_State;
static ovsdb_table_t table_Wifi_Inet_Config;

static bool
sm_radius_lookup_vif_state(const ovs_uuid_t *uuid,
                         struct schema_Wifi_VIF_State *vstate)
{
    json_t *where;

    where = ovsdb_tran_cond(OCLM_UUID, "_uuid", OFUNC_EQ, uuid->uuid);
    return ovsdb_table_select_one_where(&table_Wifi_VIF_State, where, vstate);
}

static bool
sm_radius_is_eap_VIF(struct schema_Wifi_VIF_State *vstate)
{
    int i;

    for (i = 0; i < vstate->wpa_key_mgmt_len; i++) {
        if (strstr(vstate->wpa_key_mgmt[i], "eap"))
            return true;
    }

    return false;
}

inline static void
sm_radius_handle_del_eap_VIF(struct schema_Wifi_VIF_State *vstate)
{
    struct sm_radius_vif *vif;

    vif = ds_tree_find(&vifs, vstate->if_name);

    vif->active = false;

    LOGI("%s: Client RADIUS stats reporting stopped on if_name: %s",
        backend_name, vstate->if_name);
}

inline static void
sm_radius_handle_new_eap_VIF(struct schema_Wifi_VIF_State *vstate)
{
    struct sm_radius_vif *vif;
    bool eap;

    json_t *where;
    struct schema_Wifi_Inet_Config iconf;

    vif = ds_tree_find(&vifs, vstate->if_name);
    eap = sm_radius_is_eap_VIF(vstate);

    /* No dups and only eap-enabled vifs! */
    if (eap)
    {
        if (!vif)
        {
            vif = MALLOC(sizeof(*vif));
            memset(vif, 0, sizeof(*vif));

            where = ovsdb_tran_cond(OCLM_STR, SCHEMA_COLUMN(Wifi_Inet_Config, if_name),
                        OFUNC_EQ, vif->vif_name);

            if (!ovsdb_table_select_one_where(&table_Wifi_Inet_Config, where, &iconf))
                LOGD("%s : Cannot retrieve \"role\" metadata for %s",
                    backend_name, vif->vif_name);
            else
                STRSCPY_WARN(vif->vif_role, iconf.role);

            STRSCPY_WARN(vif->vif_name, vstate->if_name);

            ds_tree_insert(&vifs, vif, vif->vif_name);

            LOGI("%s: Client RADIUS stats reporting started on if_name: %s",
                backend_name, vstate->if_name);
        }

        vif->active = true;
    }
    else if (vif)
    {
        /* Mark it inactive, since it's not eap-enabled */
        vif->active = false;

        LOGI("%s: Client RADIUS stats reporting stopped on if_name: %s",
            backend_name, vstate->if_name);
    }
}

static void
callback_Wifi_VIF_State(ovsdb_update_monitor_t *mon,
                          struct schema_Wifi_VIF_State *old_vstate,
                          struct schema_Wifi_VIF_State *new_vstate)
{
    struct schema_Wifi_VIF_State vstate;

    if (!wifi_vif_state_monitor_cb_enabled)
        return;

    if (!sm_radius_lookup_vif_state(&new_vstate->_uuid, &vstate))
        return;

    switch(mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            /* fall-through */
        case OVSDB_UPDATE_MODIFY:
            sm_radius_handle_new_eap_VIF(&vstate);
            break;
        case OVSDB_UPDATE_DEL:
            sm_radius_handle_del_eap_VIF(&vstate);
            break;
        case OVSDB_UPDATE_ERROR:
            /* fall-through */
        default:
            /* nop */
            return;
    }
}


sm_radius_stats_t *
sm_radius_new_stats_object(const char *vifname)
{
    struct sm_radius_data *new;
    struct sm_radius_vif *vif;

    if (!(new = ds_dlist_remove_head(&empty_data_q)))
    {
        new = MALLOC(sizeof(*new));
    }

    /* This assumes that the sm_radius_new_stats_object will be passed the same
     * char pointer that was passed to the sm_radius_collect_data target layer
     * implementation. Alternatively, this could be omitted by simply looking up
     * the vif by searching the vif tree with the vif_name as the key.
     */
    vif = CONTAINER_OF(
                   (void *)vifname, /* discard const */
                   struct sm_radius_vif,
                   vif_name);

    /* Copy metadata */
    STRSCPY_WARN(new->stats.vif_name, vif->vif_name);
    STRSCPY_WARN(new->stats.vif_role, vif->vif_role);

    /* Set the cleanup function */
    new->stats.cleanup = sm_radius_del_stats_object;

    return &new->stats;
}

void
sm_radius_add_stats_object(sm_radius_stats_t *ptr)
{
    struct sm_radius_data *data;

    data = CONTAINER_OF(
                    ptr,
                    struct sm_radius_data,
                    stats);

    ds_dlist_insert_tail(&pending_data_q, data);
}

void
sm_radius_del_stats_object(sm_radius_stats_t *ptr)
{
    struct sm_radius_data *data;

    data = CONTAINER_OF(
                    ptr,
                    struct sm_radius_data,
                    stats);

    if (empty_radius_data_queue_enabled)
    {
        memset(data, 0x00, sizeof(*data));
        ds_dlist_insert_tail(&empty_data_q, data);
    }
    else
    {
        FREE(data);
    }
}


static void
sm_radius_report_timer_cb(EV_P_ ev_timer *timer,
                             int events)
{
    struct sm_radius_data *qdata;
    struct sm_radius_vif *vif;
    sm_radius_stats_report_t report;
    int report_data_index;

    memset(&report, 0, sizeof(report));

    /* Count the number of pending reports */
    ds_tree_foreach(&vifs, vif)
    {
        if (!vif->active)
            continue;

        report.count += sm_radius_collect_data(vif->vif_name);
    }

    if (!report.count)
        return;

    report.data = MALLOC(report.count * sizeof(*report.data));
    report_data_index = 0;

    /* Transfer ownership of data from the pending queue to the report object */
    while((qdata = ds_dlist_remove_head(&pending_data_q)))
    {
        report.data[report_data_index] = &qdata->stats;
        report_data_index++;
    }

    sm_radius_stats_report(&report);
}

/****************************
 * REPORT REQUEST API HOOKS *
 ****************************/

static void
sm_radius_request_start(sm_report_type_t report_type,
                      const sm_stats_request_t *request)
{
    switch (report_type)
    {
        case STS_REPORT_RADIUS:
        {
            struct schema_Wifi_VIF_State *vstate;
            int i, count;

            /* Fetch all Wifi_VIF_State columns */
            vstate = ovsdb_table_select_where(&table_Wifi_VIF_State, NULL, &count);

            if (!(count && vstate))
                break;

            /* Loop through all Wifi_VIF_State columns to find eap-enabled VIFs */
            for (i = 0; i < count; i++)
            {
                if (sm_radius_is_eap_VIF(vstate + i))
                    sm_radius_handle_new_eap_VIF(vstate + i);
            }

            /* Start monitoring the Wifi_VIF_State w.r.t. to eap-enablement */
            wifi_vif_state_monitor_cb_enabled = true;

            /* Enable recycling of allocated radius_data objects */
            empty_radius_data_queue_enabled = true;

            ev_timer_init(&radius_report_timer, sm_radius_report_timer_cb,
                request->sampling_interval, request->sampling_interval);
            ev_timer_start(EV_DEFAULT_ &radius_report_timer);

            FREE(vstate);
            break;
        }

        default:
            LOGD("%s: No request_start action associated with report type %d.", backend_name, report_type);
    }
}

static void
sm_radius_request_update(sm_report_type_t report_type,
                         const sm_stats_request_t *request)
{
    switch (report_type)
    {
        case STS_REPORT_RADIUS:
            radius_report_timer.repeat = request->sampling_interval;
            break;

        default:
            LOGD("%s: No request_update action associated with report type %d.", backend_name, report_type);
    }
}

static void
sm_radius_request_stop(sm_report_type_t report_type,
                       const sm_stats_request_t *request)
{
    switch (report_type)
    {
        case STS_REPORT_RADIUS:
        {
            struct sm_radius_vif *vif = NULL;
            struct sm_radius_data *data = NULL;
            struct sm_radius_data *tmp = NULL;
            ds_tree_iter_t iter;

            /* Disable the timer to prevent race conditions */
            ev_timer_stop(EV_DEFAULT_ &radius_report_timer);

            /* Disable the schema monitor callback and mem recycling */
            wifi_vif_state_monitor_cb_enabled = false;
            empty_radius_data_queue_enabled = false;

            ds_tree_foreach_iter(&vifs, vif, &iter)
            {
                ds_tree_iremove(&iter);
                FREE(vif);
            }

            /* Flush both data queues */

            ds_dlist_foreach_safe(&pending_data_q, data, tmp)
            {
                ds_dlist_remove(&pending_data_q, data);
                sm_radius_del_stats_object(&data->stats);
            }

            ds_dlist_foreach_safe(&empty_data_q, data, tmp)
            {
                ds_dlist_remove(&pending_data_q, data);
                sm_radius_del_stats_object(&data->stats);
            }
            break;
        }

        default:
            LOGD("%s: No request_stop action associated with report_type %d.", backend_name, report_type);
    }
}

/*************************
 * MODULE SETUP/TEARDOWN *
 *************************/

void
sm_radius_start(void *data)
{
    static const sm_backend_funcs_t g_sm_radius_ops = {
        .start = sm_radius_request_start,
        .update = sm_radius_request_update,
        .stop = sm_radius_request_stop,
    };

    OVSDB_TABLE_INIT_NO_KEY(Wifi_VIF_State);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Inet_Config);

    OVSDB_TABLE_MONITOR_F(Wifi_VIF_State, C_VPACK("wpa_key_mgmt"));

    sm_backend_register(backend_name, &g_sm_radius_ops);
}

void
sm_radius_stop(void *data)
{
    sm_backend_unregister(backend_name);
}

MODULE(sm_radius, sm_radius_start, sm_radius_stop)
