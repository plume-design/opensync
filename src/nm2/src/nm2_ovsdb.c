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

#define MODULE_ID LOG_MODULE_ID_OVSDB

ovsdb_table_t table_Wifi_Inet_Config;
ovsdb_table_t table_Wifi_Inet_State;
ovsdb_table_t table_Wifi_Master_State;

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/
static char *
wm2_inet_ovsdb_type_to_str(
        ovsdb_update_type_t               update_type)
{
    switch (update_type) {
        case OVSDB_UPDATE_DEL:
            return "remove";
        case OVSDB_UPDATE_NEW:
            return "insert";
        case OVSDB_UPDATE_MODIFY:
            return "modify";
        default:
            LOGE("Invalid ovsdb type enum %d", update_type);
            break;
    }
    return NULL;
}

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
    STRLCPY(istate.if_name,     iconf->if_name);
    STRLCPY(istate.if_type,     iconf->if_type);
    STRLCPY(istate.inet_config.uuid, iconf->_uuid.uuid);  istate.inet_config_exists = true;

    if (strcmp(iconf->if_type, "eth") == 0) {
        LOGI("Updating %s INET eth through %s",
             iconf->if_name,
             wm2_inet_ovsdb_type_to_str(mon_type));

        ret =
            target_eth_inet_state_get(
                iconf->if_name,
                &istate);
        if (true != ret) {
            return;
        }

        LOGN("Updated %s INET eth status %s",
             istate.if_name,
             istate.enabled ? "enabled":"disabled");
    }
    else if (strcmp(iconf->if_type, "bridge") == 0) {
        LOGI("Updating %s INET bridge through %s",
             iconf->if_name,
             wm2_inet_ovsdb_type_to_str(mon_type));

        ret =
            target_bridge_inet_state_get(
                iconf->if_name,
                &istate);
        if (true != ret) {
            return;
        }

        LOGN("Updated %s INET bridge status %s",
             istate.if_name,
             istate.enabled ? "enabled":"disabled");
    }
    else if (strcmp(iconf->if_type, "vif") == 0) {
        LOGI("Updating %s INET vif through %s",
             iconf->if_name,
             wm2_inet_ovsdb_type_to_str(mon_type));

        ret =
            target_vif_inet_state_get(
                iconf->if_name,
                &istate);
        if (true != ret) {
            return;
        }

        LOGN("Updated %s INET vif status %s",
             istate.if_name,
             istate.enabled ? "enabled":"disabled");
    }
    else if (strcmp(iconf->if_type, "gre") == 0) {
        STRLCPY(istate.gre_ifname, iconf->gre_ifname);
        istate.gre_ifname_exists = true;

        LOGI("Updating %s INET gre %s through %s",
             iconf->gre_ifname,
             iconf->gre_remote_inet_addr,
             wm2_inet_ovsdb_type_to_str(mon_type));

        ret =
            target_gre_inet_state_get(
                iconf->gre_ifname,
                iconf->gre_remote_inet_addr,
                &istate);
        if (true != ret) {
            return;
        }

        LOGN("Updated %s INET gre %s status %s",
             istate.gre_ifname,
             istate.gre_remote_inet_addr,
             istate.enabled ? "enabled":"disabled");
    }
    else if (strcmp(iconf->if_type, "vlan") == 0) {
        LOGI("Updating %s INET vlan through %s",
             iconf->if_name,
             wm2_inet_ovsdb_type_to_str(mon_type));

        ret = target_vlan_inet_state_get(iconf->if_name, &istate);
        if (true != ret) {
            return;
        }

        LOGN("Updated %s INET vlan -- status %s",
             istate.if_name,
             istate.enabled ? "enabled":"disabled");
    }
    else {
        LOG(WARNING, "Skip updating %s INET (Unhandled type %s)",
                iconf->if_name, iconf->if_type);
        return;
    }

    ovsdb_table_upsert(&table_Wifi_Inet_State, &istate, false);
}

void callback_Wifi_Inet_Config(
        ovsdb_update_monitor_t *mon, void *record,
        ovsdb_cache_row_t *row)
{
    struct schema_Wifi_Inet_Config *iconf = record;
    bool                            ret;

    (void)row;
    (void)mon;

    /* The schema contains value before delete, therefore
       disable the interfaces status */
    if (mon->mon_type == OVSDB_UPDATE_DEL) {
        iconf->enabled = false;
    }

    if (strcmp(iconf->if_type, "eth") == 0) {
        LOGI("Configuring %s INET eth through %s",
             iconf->if_name,
             wm2_inet_ovsdb_type_to_str(mon->mon_type));

        ret =
            target_vif_inet_config_set(
                    iconf->if_name,
                    iconf);
        if (true != ret) {
            return;
        }

        LOGN("Configured %s INET eth status %s",
             iconf->if_name,
             iconf->enabled ? "enabled":"disabled");
    }
    else if (strcmp(iconf->if_type, "vif") == 0) {
        LOGI("Configuring %s INET vif through %s",
             iconf->if_name,
             wm2_inet_ovsdb_type_to_str(mon->mon_type));

        ret =
            target_vif_inet_config_set(
                    iconf->if_name,
                    iconf);
        if (true != ret) {
            return;
        }

        LOGN("Configured %s INET vif status %s",
             iconf->if_name,
             iconf->enabled ? "enabled":"disabled");
    }
    else if (strcmp(iconf->if_type, "bridge") == 0) {
        LOGI("Configuring %s INET bridge through %s",
             iconf->if_name,
             wm2_inet_ovsdb_type_to_str(mon->mon_type));

        ret =
            target_bridge_inet_config_set(
                    iconf->if_name,
                    iconf);
        if (true != ret) {
            return;
        }

        LOGN("Configured %s INET bridge status %s",
             iconf->if_name,
             iconf->enabled ? "enabled":"disabled");
    }
    else if (strcmp(iconf->if_type, "gre") == 0) {
        LOGI("Configuring %s INET gre %s through %s",
             iconf->gre_ifname,
             iconf->gre_remote_inet_addr,
             wm2_inet_ovsdb_type_to_str(mon->mon_type));

        ret =
            target_gre_inet_config_set(
                    iconf->gre_ifname,
                    iconf->gre_remote_inet_addr,
                    iconf);
        if (true != ret) {
            return;
        }

        LOGN("Configured %s INET gre %s status %s",
             iconf->gre_ifname,
             iconf->gre_remote_inet_addr,
             iconf->enabled ? "enabled":"disabled");
    }
    else if (strcmp(iconf->if_type, "vlan") == 0) {
        LOGI("Configuring %s INET vlan through %s",
             iconf->if_name,
             wm2_inet_ovsdb_type_to_str(mon->mon_type));

        ret = target_vlan_inet_config_set(iconf->if_name, iconf);
        if (true != ret) {
            return;
        }

        LOGN("Configured %s INET vlan -- status %s",
             iconf->if_name,
             iconf->enabled ? "enabled":"disabled");
    }
    else {
        LOG(WARNING, "Skip configuring %s INET (Unhandled type %s)",
                iconf->if_name, iconf->if_type);
        return;
    }

    nm2_update_Wifi_Inet_State(mon->mon_type, iconf);
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

    // Initialize OVSDB monitor callbacks
    OVSDB_CACHE_MONITOR(Wifi_Inet_Config, false);

    return;
}
