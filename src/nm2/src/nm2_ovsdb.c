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

#include "os.h"
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_cache.h"
#include "schema.h"
#include "log.h"
#include "ds.h"
#include "json_util.h"
#include "nm2.h"

#include "target.h"
#include "target_common.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB

#define SCHEMA_IF_TYPE_VIF "vif"

ovsdb_table_t table_Wifi_Inet_Config;
ovsdb_table_t table_Wifi_Inet_State;
ovsdb_table_t table_Wifi_Master_State;

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/

// Warning: if_name must be populated
void nm2_inet_state_update_cb(
        struct schema_Wifi_Inet_State *istate, schema_filter_t *filter)
{
    bool  ret;
    char msg[256];
    snprintf(msg, sizeof(msg), "Updating ovsdb inet state %s", istate->if_name);

    LOGI("%s", msg);
    if ((filter && filter->num <= 1) || !*istate->if_name) {
        LOGE("%s: no field selected %d", msg, filter ? filter->num : -1);
        return;
    }
    ret = ovsdb_table_update_f(&table_Wifi_Inet_State, istate, filter ? filter->columns : NULL);
    if (ret) {
        LOGD("%s: done", msg);
    }
    else {
        LOGE("%s: error", msg);
    }
}

// Warning: if_name must be populated
void nm2_master_state_update_cb(struct schema_Wifi_Master_State *mstate)
{
    bool  ret;
    char msg[256];
    char **filter = NULL;
    char *filter_vif[] = {"-", SCHEMA_COLUMN(Wifi_Master_State, port_state), NULL};

    snprintf(msg, sizeof(msg), "Updating ovsdb master state %s", mstate->if_name);
    LOGI("%s", msg);

    if (!*mstate->if_name) {
        // missing if_name
        LOGE("%s: missing if_name", msg);
        return;
    }

    if (!strcmp(mstate->if_type, SCHEMA_IF_TYPE_VIF)) {
        // port_state in master state for VIF type is updated by WM not NM
        // check and force correct target implementation
        if (!mstate->_partial_update || mstate->port_state_present) {
            // force blacklist port_state
            LOGI("%s %s: not updating port_state '%s' (%d)", msg, mstate->if_type,
                    mstate->port_state, mstate->port_state_exists);
            filter = filter_vif;
        }
    }

    ret = ovsdb_table_update_f(&table_Wifi_Master_State, mstate, filter);
    if (ret) {
        LOGD("%s: done", msg);
    }
    else {
        LOGE("%s: error", msg);
    }
}

// Warning: if_name must be populated
void nm2_inet_config_update_cb(
        struct schema_Wifi_Inet_Config *iconf, schema_filter_t *filter)
{
    bool  ret;
    char msg[256];
    snprintf(msg, sizeof(msg), "Updating ovsdb inet config %s", iconf->if_name);

    LOGI("%s", msg);
    if ((filter && filter->num <= 1) || !*iconf->if_name) {
        LOGE("%s: no field selected %d", msg, filter ? filter->num : -1);
        return;
    }
    ret = ovsdb_table_update_f(&table_Wifi_Inet_Config, iconf, filter ? filter->columns : NULL);
    if (ret) {
        LOGD("%s: done", msg);
    }
    else {
        LOGE("%s: error", msg);
    }
}

/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/
void nm2_update_Wifi_Inet_State(
        ovsdb_update_type_t             mon_type,
        struct schema_Wifi_Inet_Config *iconf)
{
    bool                            ret;
    struct schema_Wifi_Inet_State   istate;

    if (mon_type == OVSDB_UPDATE_DEL) {
        ovsdb_table_delete_simple(&table_Wifi_Inet_State,
                SCHEMA_COLUMN(Wifi_Inet_State, if_name), iconf->if_name);
        return;
    }
    else if (mon_type == OVSDB_UPDATE_NEW) {
        target_inet_state_register(iconf->if_name, nm2_inet_state_update_cb);
    }

    MEMZERO(istate);
    STRSCPY(istate.if_name,     iconf->if_name);
    STRSCPY(istate.if_type,     iconf->if_type);
    STRSCPY(istate.inet_config.uuid, iconf->_uuid.uuid);  istate.inet_config_exists = true;

    LOGD("Updating %s INET %s through %s",
            iconf->if_name, iconf->if_type,
            ovsdb_update_type_to_str(mon_type));

    if (strcmp(iconf->if_type, "eth") == 0) {
        ret =
            target_eth_inet_state_get(
                iconf->if_name,
                &istate);
        if (true != ret) {
            goto error;
        }
    }
    else if (strcmp(iconf->if_type, "bridge") == 0) {
        ret =
            target_bridge_inet_state_get(
                iconf->if_name,
                &istate);
        if (true != ret) {
            goto error;
        }
    }
    else if (strcmp(iconf->if_type, "vif") == 0) {
        ret =
            target_vif_inet_state_get(
                iconf->if_name,
                &istate);
        if (true != ret) {
            goto error;
        }
    }
    else if (strcmp(iconf->if_type, "gre") == 0) {
        STRSCPY(istate.gre_ifname, iconf->gre_ifname);
        istate.gre_ifname_exists = true;

        LOGD("Updating GRE i:%s r:%s",
             iconf->gre_ifname, iconf->gre_remote_inet_addr);

        ret =
            target_gre_inet_state_get(
                iconf->if_name,
                iconf->gre_remote_inet_addr,
                &istate);
        if (true != ret) {
            goto error;
        }

        LOGI("Updated %s INET gre %s status %s through %s",
             istate.if_name,
             istate.gre_remote_inet_addr,
             istate.enabled ? "enabled":"disabled",
             ovsdb_update_type_to_str(mon_type));
        goto update; // skip default log msg
    }
    else if (strcmp(iconf->if_type, "vlan") == 0) {
        ret = target_vlan_inet_state_get(iconf->if_name, &istate);
        if (true != ret) {
            goto error;
        }
    }
    else if (strcmp(iconf->if_type, "tap") == 0) {
        ret = target_tap_inet_state_get(iconf->if_name, &istate);
        if (true != ret) {
            goto error;
        }
    }
    else {
        LOG(WARNING, "Skip updating %s INET (Unhandled type %s)",
                iconf->if_name, iconf->if_type);
        return;
    }

    LOGI("Updated %s INET %s status %s through %s",
            istate.if_name, iconf->if_type,
            istate.enabled ? "enabled":"disabled",
            ovsdb_update_type_to_str(mon_type));

update:

    ovsdb_table_upsert(&table_Wifi_Inet_State, &istate, false);

    return;

error:

    LOGE("Updating %s INET %s through %s",
            iconf->if_name, iconf->if_type,
            ovsdb_update_type_to_str(mon_type));
}

void nm2_update_Wifi_Master_State(
        ovsdb_update_type_t             mon_type,
        struct schema_Wifi_Inet_Config *iconf)
{
    bool                            ret;
    struct schema_Wifi_Master_State mstate;

    if (mon_type == OVSDB_UPDATE_DEL) {
        ovsdb_table_delete_simple(&table_Wifi_Master_State,
                SCHEMA_COLUMN(Wifi_Master_State, if_name), iconf->if_name);
        return;
    }
    else if (mon_type == OVSDB_UPDATE_NEW) {
        target_master_state_register(iconf->if_name, nm2_master_state_update_cb);
    }

    MEMZERO(mstate);
    STRSCPY(mstate.if_name,     iconf->if_name);
    STRSCPY(mstate.if_type,     iconf->if_type);

    if (strcmp(iconf->if_type, "eth") == 0) {
        LOGD("Updating %s MASTER eth through %s",
             iconf->if_name,
             ovsdb_update_type_to_str(mon_type));

        ret =
            target_eth_master_state_get(
                iconf->if_name,
                &mstate);
        if (true != ret) {
            return;
        }

        LOGI("Updated %s MASTER eth status through %s",
             mstate.if_name,
             ovsdb_update_type_to_str(mon_type));
    }
    else if (strcmp(iconf->if_type, "bridge") == 0) {
        LOGD("Updating %s MASTER bridge through %s",
             iconf->if_name,
             ovsdb_update_type_to_str(mon_type));

        ret =
            target_bridge_master_state_get(
                iconf->if_name,
                &mstate);
        if (true != ret) {
            return;
        }

        LOGI("Updated %s MASTER bridge status through %s",
             mstate.if_name,
             ovsdb_update_type_to_str(mon_type));
    }
    else if (strcmp(iconf->if_type, "vif") == 0) {
        LOGD("Updating %s MASTER vif through %s",
             iconf->if_name,
             ovsdb_update_type_to_str(mon_type));

        ret =
            target_vif_master_state_get(
                iconf->if_name,
                &mstate);
        if (true != ret) {
            return;
        }

        LOGI("Updated %s MASTER vif status through %s",
             mstate.if_name,
             ovsdb_update_type_to_str(mon_type));
    }
    else if (strcmp(iconf->if_type, "gre") == 0) {
        LOGD("Updating %s MASTER gre %s through %s",
             iconf->if_name,
             iconf->gre_remote_inet_addr,
             ovsdb_update_type_to_str(mon_type));

        ret =
            target_gre_master_state_get(
                iconf->if_name,
                iconf->gre_remote_inet_addr,
                &mstate);
        if (true != ret) {
            return;
        }

        LOGI("Updated %s MASTER gre status through %s",
             mstate.if_name,
             ovsdb_update_type_to_str(mon_type));
    }
    else if (strcmp(iconf->if_type, "vlan") == 0) {
        LOGD("Updating %s MASTER vlan through %s",
             iconf->if_name,
             ovsdb_update_type_to_str(mon_type));

        ret = target_vlan_master_state_get(iconf->if_name, &mstate);
        if (true != ret) {
            return;
        }

        LOGI("Updated %s MASTER vlan -- status through %s",
             mstate.if_name,
             ovsdb_update_type_to_str(mon_type));
    }
    else if (strcmp(iconf->if_type, "tap") == 0) {
        LOGD("Updating %s MASTER tap through %s",
             iconf->if_name,
             ovsdb_update_type_to_str(mon_type));

        ret = target_tap_master_state_get(iconf->if_name, &mstate);
        if (true != ret) {
            return;
        }

        LOGI("Updated %s MASTER tap -- status through %s",
             mstate.if_name,
             ovsdb_update_type_to_str(mon_type));
    }
    else {
        LOG(WARNING, "Skip updating %s MASTER (Unhandled type %s)",
                iconf->if_name, iconf->if_type);
        return;
    }

    ovsdb_table_upsert(&table_Wifi_Master_State, &mstate, false);
}


void callback_Wifi_Inet_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Inet_Config *old_rec,
        struct schema_Wifi_Inet_Config *iconf,
        ovsdb_cache_row_t *row)
{
    bool                            ret;

    (void)old_rec;
    (void)row;
    (void)mon;

    /* The schema contains value before delete, therefore
       disable the interfaces status */
    if (mon->mon_type == OVSDB_UPDATE_DEL) {
        iconf->enabled = false;
    }

    LOGD("Configuring %s INET %s through %s",
            iconf->if_name, iconf->if_type,
            ovsdb_update_type_to_str(mon->mon_type));

    if (strcmp(iconf->if_type, "eth") == 0) {
        ret =
            target_eth_inet_config_set(
                    iconf->if_name,
                    iconf);
        if (true != ret) {
            goto error;
        }

    }
    else if (strcmp(iconf->if_type, "vif") == 0) {
        ret =
            target_vif_inet_config_set(
                    iconf->if_name,
                    iconf);
        if (true != ret) {
            goto error;
        }
    }
    else if (strcmp(iconf->if_type, "bridge") == 0) {
        ret =
            target_bridge_inet_config_set(
                    iconf->if_name,
                    iconf);
        if (true != ret) {
            goto error;
        }
    }
    else if (strcmp(iconf->if_type, "gre") == 0) {
        LOGD("Configuring %s INET gre %s through %s",
             iconf->if_name,
             iconf->gre_remote_inet_addr,
             ovsdb_update_type_to_str(mon->mon_type));

        ret =
            target_gre_inet_config_set(
                    iconf->if_name,
                    iconf->gre_remote_inet_addr,
                    iconf);
        if (true != ret) {
            goto error;
        }

        LOGN("Configured %s INET gre %s status %s through %s",
             iconf->if_name,
             iconf->gre_remote_inet_addr,
             iconf->enabled ? "enabled":"disabled",
             ovsdb_update_type_to_str(mon->mon_type));
        goto update; // skip default log msg
    }
    else if (strcmp(iconf->if_type, "vlan") == 0) {
        ret = target_vlan_inet_config_set(iconf->if_name, iconf);
        if (true != ret) {
            goto error;
        }
    }
    else if (strcmp(iconf->if_type, "tap") == 0) {
        ret = target_tap_inet_config_set(iconf->if_name, iconf);
        if (true != ret) {
            goto error;
        }
    }
    else {
        LOG(WARNING, "Skip configuring %s INET (Unhandled type %s)",
                iconf->if_name, iconf->if_type);
        return;
    }

    LOGN("Configured %s INET %s status %s through %s",
            iconf->if_name, iconf->if_type,
            iconf->enabled ? "enabled" : "disabled",
            ovsdb_update_type_to_str(mon->mon_type));

update:

    nm2_update_Wifi_Inet_State(mon->mon_type, iconf);
    nm2_update_Wifi_Master_State(mon->mon_type, iconf);

    return;

error:

    LOGE("Configuring %s INET %s through %s",
            iconf->if_name, iconf->if_type,
            ovsdb_update_type_to_str(mon->mon_type));
}

static void
nm2_inet_config_clear(
            ds_dlist_t     *inets)
{
    ds_dlist_iter_t             inet_iter;
    target_inet_config_init_t  *inet;

    for (   inet = ds_dlist_ifirst(&inet_iter, inets);
            inet != NULL;
            inet = ds_dlist_inext(&inet_iter)) {
        ds_dlist_iremove(&inet_iter);
        free(inet);
        inet = NULL;
    }
}
void
nm2_inet_config_init(
        ovsdb_table_t  *table_Wifi_Inet_Config)
{
    ds_dlist_t                          inets;
    target_inet_config_init_t          *inet;

    if (!target_inet_config_init(&inets)) {
        LOGW("Initializing inet state (not found)");
        return;
    }

    ds_dlist_foreach(&inets, inet) {
        ovsdb_table_upsert(table_Wifi_Inet_Config, &inet->iconfig, false);
        LOGI("Added inet %s to config", inet->iconfig.if_name);
    }

    nm2_inet_config_clear(&inets);
}

static void
nm2_inet_state_clear(
            ds_dlist_t     *inets)
{
    ds_dlist_iter_t             inet_iter;
    target_inet_state_init_t   *inet;

    for (   inet = ds_dlist_ifirst(&inet_iter, inets);
            inet != NULL;
            inet = ds_dlist_inext(&inet_iter)) {
        ds_dlist_iremove(&inet_iter);
        free(inet);
        inet = NULL;
    }
}

void
nm2_inet_state_init(
        ovsdb_table_t  *table_Wifi_Inet_State)
{
    ds_dlist_t                          inets;
    target_inet_state_init_t           *inet;

    if (!target_inet_state_init(&inets)) {
        LOGW("Initializing inet state (not found)");
        return;
    }

    ds_dlist_foreach(&inets, inet) {
        /* Skip disabled inets */
        if (inet->istate.enabled) {
            ovsdb_table_upsert(table_Wifi_Inet_State, &inet->istate, false);
            LOGI("Added inet %s to state", inet->istate.if_name);
        }

        /* State/IP address can come later for some inet's */
        target_inet_state_register(inet->istate.if_name, nm2_inet_state_update_cb);
    }

    nm2_inet_state_clear(&inets);

    return;
}

static void
nm2_master_state_clear(
            ds_dlist_t     *inets)
{
    ds_dlist_iter_t             inet_iter;
    target_master_state_init_t *inet;

    for (   inet = ds_dlist_ifirst(&inet_iter, inets);
            inet != NULL;
            inet = ds_dlist_inext(&inet_iter)) {
        ds_dlist_iremove(&inet_iter);
        free(inet);
    }
}

void
nm2_master_state_init(
        ovsdb_table_t  *table_Wifi_Master_State)
{
    ds_dlist_t                          inets;
    target_master_state_init_t         *inet;

    if (!target_master_state_init(&inets)) {
        LOGW("Initializing master state (not found)");
        return;
    }

    ds_dlist_foreach(&inets, inet) {
        ovsdb_table_upsert(table_Wifi_Master_State, &inet->mstate, false);
        LOGI("Added inet %s to master", inet->mstate.if_name);

        /* Master state change can come later for some inet's */
        target_master_state_register(inet->mstate.if_name, nm2_master_state_update_cb);
    }

    nm2_master_state_clear(&inets);

    return;
}

/******************************************************************************
 *  PUBLIC API definitions
 *****************************************************************************/

void nm2_ovsdb_init(void)
{
    LOGI("Initializing NM tables");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT(Wifi_Inet_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_Inet_State, if_name);
    OVSDB_TABLE_INIT(Wifi_Master_State, if_name);

    nm2_inet_config_init(&table_Wifi_Inet_Config);
    nm2_inet_state_init(&table_Wifi_Inet_State);
    nm2_master_state_init(&table_Wifi_Master_State);

    // Initialize OVSDB monitor callbacks
    OVSDB_CACHE_MONITOR(Wifi_Inet_Config, false);

    return;
}
