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

#include "ps_mgmt.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "osp_ps.h"
#include "log.h"

#define PS_STORE_GW_OFFLINE             "pm_gw_offline_store"
#define PS_LAST_PARENT_STORE            "pm_last_parent_store"

static ovsdb_table_t table_WAN_Config;
static ovsdb_table_t table_Lte_Config;

/* Returns number of rows deleted or -1 if an error was encountered */
int ps_mgmt_erase_wan_config(void)
{
    static bool global_init_wan = false;

    if (!global_init_wan)
    {
        OVSDB_TABLE_INIT_NO_KEY(WAN_Config);
        global_init_wan = true;
    }

    LOG(INFO, "Erasing WAN config from persistent storage.");
    return ovsdb_table_delete_where(
            &table_WAN_Config,
            ovsdb_where_simple_typed(
                    SCHEMA_COLUMN(WAN_Config, os_persist),
                    "true",
                    OCLM_BOOL));
}

/* Returns number of rows deleted or -1 if an error was encountered */
int ps_mgmt_erase_lte_config(void)
{
    static bool global_init_lte = false;

    if (!global_init_lte)
    {
        OVSDB_TABLE_INIT_NO_KEY(Lte_Config);
        global_init_lte = true;
    }

    LOG(INFO, "Erasing LTE config from persistent storage.");
    return ovsdb_table_delete_where(
        &table_Lte_Config,
        ovsdb_where_simple_typed(
                SCHEMA_COLUMN(Lte_Config, os_persist),
                "true",
                OCLM_BOOL));
}

bool ps_mgmt_erase_gw_offline_config(void)
{
    LOG(INFO, "Erasing GW Offline config from persistent storage.");
    return osp_ps_erase_store_name(PS_STORE_GW_OFFLINE, 0);
}

bool ps_mgmt_erase_last_parent_store(void)
{
    LOG(INFO, "Erasing last parent store: %s", PS_LAST_PARENT_STORE);
    return osp_ps_erase_store_name(PS_LAST_PARENT_STORE, 0);
}
