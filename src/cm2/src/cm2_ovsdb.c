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
#include "target.h"
#include "target_common.h"
#include "cm2.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB

#define OVSDB_FILTER_LEN                25

ovsdb_table_t table_Open_vSwitch;
ovsdb_table_t table_Manager;
ovsdb_table_t table_SSL;
ovsdb_table_t table_AWLAN_Node;

void callback_AWLAN_Node(ovsdb_update_monitor_t *mon, void *record)
{
    struct schema_AWLAN_Node *awlan = record;
    bool valid;

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        g_state.have_awlan = false;
    }
    else
    {
        g_state.have_awlan = true;
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, manager_addr)))
    {
        // manager_addr changed
        valid = cm2_set_addr(CM2_DEST_MANAGER, awlan->manager_addr);
        g_state.addr_manager.updated = valid;
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, redirector_addr)))
    {
        // redirector_addr changed
        valid = cm2_set_addr(CM2_DEST_REDIR, awlan->redirector_addr);
        g_state.addr_redirector.updated = valid;
    }

    if (    ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, device_mode))
         || ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, factory_reset))
       )
    {
        target_device_config_set(awlan);
    }

    if (    ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, min_backoff))
         || ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, max_backoff))
       )
    {
	g_state.min_backoff = awlan->min_backoff;
	g_state.max_backoff = awlan->max_backoff;
    }

    cm2_update_state(CM2_REASON_AWLAN);
}

void callback_Manager(ovsdb_update_monitor_t *mon, void *record)
{
    struct schema_Manager *manager = record;
    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        g_state.have_manager = false;
        g_state.connected = false;
    }
    else
    {
        g_state.have_manager = true;
        g_state.connected = manager->is_connected;
        if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Manager, is_connected)))
        {
            // is_connected changed
            LOG(DEBUG, "Manager.is_connected = %s", str_bool(manager->is_connected));
        }
    }

    cm2_update_state(CM2_REASON_MANAGER);
}

bool cm2_ovsdb_set_Manager_target(char *target)
{
    struct schema_Manager manager;
    memset(&manager, 0, sizeof(manager));
    strlcpy(manager.target, target, sizeof(manager.target));
    char *filter[] = { "+", SCHEMA_COLUMN(Manager, target), NULL };
    int ret = ovsdb_table_update_where_f(&table_Manager, NULL, &manager, filter);
    return ret == 1;
}

bool cm2_ovsdb_set_AWLAN_Node_manager_addr(char *addr)
{
    struct schema_AWLAN_Node awlan;
    memset(&awlan, 0, sizeof(awlan));
    strlcpy(awlan.manager_addr, addr, sizeof(awlan.manager_addr));
    char *filter[] = { "+", SCHEMA_COLUMN(AWLAN_Node, manager_addr), NULL };
    int ret = ovsdb_table_update_where_f(&table_AWLAN_Node, NULL, &awlan, filter);
    return ret == 1;
}

static void
cm2_awlan_state_update_cb(
        struct schema_AWLAN_Node *awlan,
        schema_filter_t          *filter)
{
    bool ret;

    if(!filter || filter->num <= 0) {
       LOGE("Updating awlan_node (no filter fields)");
       return;
    }

    ret = ovsdb_table_update_f(
            &table_AWLAN_Node,
            awlan, filter->columns);
    if (!ret){
        LOGE("Updating awlan_node");
    }
}

// Initialize Open_vSwitch, SSL and Manager tables
int cm2_ovsdb_init_tables()
{
    // SSL and Manager tables have to be referenced by Open_vSwitch
    // so we use _with_parent() to atomically update (mutate) these references
    struct schema_Open_vSwitch ovs;
    struct schema_Manager manager;
    struct schema_SSL ssl;
    struct schema_AWLAN_Node awlan;
    bool success = false;
    int retval = 0;

    /* Update redirector address from target storage! */
    LOGD("Initializing CM tables "
            "(Init AWLAN_Node device config changes)");
    target_device_config_register(cm2_awlan_state_update_cb);

    // Open_vSwitch
    LOGD("Initializing CM tables "
            "(Init Open_vSwitch table)");
    memset(&ovs, 0, sizeof(ovs));
    success = ovsdb_table_upsert(&table_Open_vSwitch, &ovs, false);
    if (!success) {
        LOGE("Initializing CM tables "
             "(Failed to setup OvS table)");
        retval = -1;
    }

    // Manager
    LOGD("Initializing CM tables "
            "(Init OvS.Manager table)");
    memset(&manager, 0, sizeof(manager));
    manager.inactivity_probe = 30000;
    manager.inactivity_probe_exists = true;
    strcpy(manager.target, "");
    success = ovsdb_table_upsert_with_parent_where(&table_Manager,
            NULL, &manager, false, NULL,
            SCHEMA_TABLE(Open_vSwitch), NULL,
            SCHEMA_COLUMN(Open_vSwitch, manager_options));
    if (!success) {
        LOGE("Initializing CM tables "
                     "(Failed to setup Manager table)");
        retval = -1;
    }

    // SSL
    LOGD("Initializing CM tables "
            "(Init OvS.SSL table)");
    memset(&ssl, 0, sizeof(ssl));
    strcpy(ssl.ca_cert, "none");
    strcpy(ssl.certificate, target_tls_mycert_filename());
    strcpy(ssl.private_key, target_tls_privkey_filename());
    success = ovsdb_table_upsert_with_parent(&table_SSL,
            &ssl, false, NULL,
            SCHEMA_TABLE(Open_vSwitch), NULL,
            SCHEMA_COLUMN(Open_vSwitch, ssl));
    if (!success) {
        LOGE("Initializing CM tables "
                     "(Failed to setup SSL table)");
        retval = -1;
    }

    // AWLAN_Node
    g_state.min_backoff = 30;
    g_state.max_backoff = 60;
    LOGD("Initializing CM tables "
	 "(Init AWLAN_Node table)");
    memset(&awlan, 0, sizeof(awlan));
    awlan.min_backoff = g_state.min_backoff;
    awlan.max_backoff = g_state.max_backoff;
    char *filter[] = { "+",
		       SCHEMA_COLUMN(AWLAN_Node, min_backoff),
		       SCHEMA_COLUMN(AWLAN_Node, max_backoff),
		       NULL };
    success = ovsdb_table_update_where_f(&table_AWLAN_Node, NULL, &awlan, filter);
    if (!success) {
	LOGE("Initializing CM tables "
	     "(Failed to setup AWLAN_Node table)");
        retval = -1;
    }
    return retval;
}


int cm2_ovsdb_init(void)
{
    LOGI("Initializing CM tables");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(Open_vSwitch);
    OVSDB_TABLE_INIT_NO_KEY(Manager);
    OVSDB_TABLE_INIT_NO_KEY(SSL);
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(AWLAN_Node, false);

    char *filter[] = {"-", "_version", SCHEMA_COLUMN(Manager, status), NULL};
    OVSDB_TABLE_MONITOR_F(Manager, filter);

    // Initialize OvS tables
    if (cm2_ovsdb_init_tables())
    {
        LOGE("Initializing CM tables "
             "(Failed to setup tables)");
        return -1;
    }

    return 0;
}
