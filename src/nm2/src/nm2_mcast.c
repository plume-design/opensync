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

#include "ovsdb_table.h"
#include "os_util.h"
#include "reflink.h"
#include "synclist.h"
#include "ovsdb_sync.h"
#include "target.h"

#include "nm2.h"

/*
 * ===========================================================================
 *  IGMP_Config table
 * ===========================================================================
 */
static ovsdb_table_t    table_IGMP_Config;
static void             callback_IGMP_Config(ovsdb_update_monitor_t *mon,
                                             struct schema_IGMP_Config *old,
                                             struct schema_IGMP_Config *new);

static ovsdb_table_t    table_MLD_Config;
static void             callback_MLD_Config(ovsdb_update_monitor_t *mon,
                                             struct schema_MLD_Config *old,
                                             struct schema_MLD_Config *new);

/*
 * Initialize table monitors
 */
void nm2_mcast_init(void)
{
    LOG(INFO, "Initializing NM Multicast sys config monitoring.");
    OVSDB_TABLE_INIT_NO_KEY(IGMP_Config);
    OVSDB_TABLE_MONITOR(IGMP_Config,  false);
    OVSDB_TABLE_INIT_NO_KEY(MLD_Config);
    OVSDB_TABLE_MONITOR(MLD_Config,  false);
    return;
}

/*
 * OVSDB monitor update callback for IGMP_Config
 */
void callback_IGMP_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_IGMP_Config *old,
        struct schema_IGMP_Config *new)
{

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
        {
            target_set_igmp_mcproxy_sys_params(new);
            break;
        }
        case OVSDB_UPDATE_DEL:
        {
            target_set_igmp_mcproxy_sys_params(NULL);
            break;
        }
        default:
            LOGW("%s:mon upd error: %d", __func__, mon->mon_type);
            return;
    }
    return;
}

/*
 * OVSDB monitor update callback for MLD_Config
 */
void callback_MLD_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_MLD_Config *old,
        struct schema_MLD_Config *new)
{

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
        {
            target_set_mld_mcproxy_sys_params(new);
            break;
        }
        case OVSDB_UPDATE_DEL:
        {
            target_set_mld_mcproxy_sys_params(NULL);
            break;
        }
        default:
            LOGW("%s:mon upd error: %d", __func__, mon->mon_type);
            return;
    }
    return;
}

void nm2_mcast_uplink_notify(const char *ifname, bool is_uplink, bool is_wan, const char *bridge)
{
    LOG(INFO, "mcast: Flagging interface %s as uplink=%s wan=%s",
            ifname, is_uplink ? "true" : "false", is_wan ? "true" : "false");

    if (!target_set_mcast_uplink(ifname, is_uplink, is_wan, bridge))
    {
        LOG(ERR, "mcast: Error setting interface %s uplink flag to %s.",
                ifname, is_uplink ? "true" : "false");
    }
}

