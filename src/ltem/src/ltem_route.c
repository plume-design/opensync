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

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include "memutil.h"
#include "log.h"
#include "json_util.h"
#include "os.h"
#include "ovsdb.h"
#include "target.h"
#include "network_metadata.h"

#include "ltem_mgr.h"

uint32_t
ltem_netmask_to_cidr( char *mask)
{
    uint32_t netmask;
    uint8_t ip_addr[4];
    uint32_t cidr = 0;

    sscanf(mask, "%d.%d.%d.%d",
           (int *)&ip_addr[0], (int *)&ip_addr[1],
           (int *)&ip_addr[2], (int *)&ip_addr[3]);
    memcpy(&netmask, ip_addr, sizeof(netmask));

    while ( netmask )
    {
        cidr += ( netmask & 0x01 );
        netmask = netmask >> 1;
    }
    return cidr;
}

void
ltem_make_route(char *subnet, char *netmask, char *route)
{
    uint32_t cidr;

    memset(route, 0, C_IPV6ADDR_LEN);
    cidr = ltem_netmask_to_cidr(netmask);
    sprintf(route, "%s/%d", subnet, cidr);
}

static int
ltem_route_exec_cmd(char *cmd)
{
    int res;
    res = system(cmd);

    LOGD("%s: cmd=%s, res=%d, errno=%s", __func__, cmd, res, strerror(errno));
    return res;
}

/*
 * This is for Phase II when we route individual clients
 */
int
ltem_create_lte_route_table(ltem_mgr_t *mgr)
{
    char cmd[1024];
    /* ip route add 0.0.0.0/0 dev wwan0 table 76 */
    snprintf(cmd, sizeof(cmd), "ip route add 0.0.0.0/0 dev wwan0 table 76");
    return (ltem_route_exec_cmd(cmd));
}

static int
client_cmp(const void *a, const void *b)
{
    const char *name_a = a;
    const char *name_b = b;
    return (strcmp(name_a, name_b));
}
void
ltem_client_table_update(ltem_mgr_t *mgr, struct schema_DHCP_leased_IP *dhcp_lease)
{
    struct client_entry *new_entry;
    struct client_entry *entry;

    new_entry = CALLOC(1, sizeof(struct client_entry));
    if (new_entry == NULL)
    {
        LOGE("%s: CALLOC failed", __func__);
        return;
    }
    if (dhcp_lease->hostname_present && dhcp_lease->inet_addr_present)
    {
        STRSCPY(new_entry->client_name, dhcp_lease->hostname);
        STRSCPY(new_entry->client_addr, dhcp_lease->inet_addr);
        LOGI("%s: New client entry %s:%s", __func__, dhcp_lease->hostname, dhcp_lease->inet_addr);
        entry = ds_tree_find(&mgr->client_table, new_entry);
        if (entry)
        {
            LOGI("%s: Existing client entry %s:%s", __func__, entry->client_name, entry->client_addr);
            return;
        }

        ds_tree_insert(&mgr->client_table, new_entry, new_entry);
        return;
    }

    LOGI("%s: hostname or inet_addr are NULL", __func__);
}

void
ltem_client_table_delete(ltem_mgr_t *mgr, struct schema_DHCP_leased_IP *dhcp_lease)
{
    struct client_entry to_del;
    struct client_entry *entry;

    if (dhcp_lease->hostname_present && dhcp_lease->inet_addr_present)
    {
        STRSCPY(to_del.client_name, dhcp_lease->hostname);
        STRSCPY(to_del.client_addr, dhcp_lease->inet_addr);
        entry = ds_tree_find(&mgr->client_table, &to_del);
        if (!entry) return;

        LOGI("%s: Delete client entry %s:%s", __func__, dhcp_lease->hostname, dhcp_lease->inet_addr);
        ds_tree_remove(&mgr->client_table, entry);
        FREE(entry);
        return;
    }
}

void
ltem_create_client_table(ltem_mgr_t *mgr)
{
    ds_tree_init(&mgr->client_table, client_cmp, struct client_entry,  entry_node);
}

void
ltem_update_lte_route(ltem_mgr_t *mgr, char *if_name, char *lte_subnet, char *lte_gw, char *lte_netmask)
{
    STRSCPY(mgr->lte_route->lte_if_name, if_name);
    STRSCPY(mgr->lte_route->lte_subnet, lte_subnet);
    STRSCPY(mgr->lte_route->lte_gw, lte_gw);
    STRSCPY(mgr->lte_route->lte_netmask, lte_netmask);
    mgr->lte_route->lte_metric = LTE_DEFAULT_METRIC;
    LOGI("%s: lte_if_name[%s], lte_subnet[%s], lte_gw[%s] lte_netmask[%s], metric[%d]", __func__, if_name,
         lte_subnet, lte_gw, lte_netmask, mgr->lte_route->lte_metric);
}

void
ltem_update_wan_route(ltem_mgr_t *mgr, char *if_name, char *wan_subnet, char *wan_gw, char *wan_netmask)
{
    LOGI("%s: wan_if_name[%s], wan_subnet[%s], wan_gw[%s]", __func__, if_name, wan_subnet, wan_gw);
    STRSCPY(mgr->lte_route->wan_if_name, if_name);
    STRSCPY(mgr->lte_route->wan_subnet, wan_subnet);
    STRSCPY(mgr->lte_route->wan_gw, wan_gw);
    STRSCPY(mgr->lte_route->wan_netmask, wan_netmask);
    mgr->lte_route->wan_metric = WAN_DEFAULT_METRIC;
}

/*
 * This is for Phase II when we selectively route clients over LTE
 */
int
ltem_add_lte_client_routes(ltem_mgr_t *mgr)
{
    struct client_entry *entry;
    int res = 0;
    char cmd[1024];

    LOGI("%s: failover:%d", __func__, mgr->lte_state_info->lte_failover_active);
    entry = ds_tree_head(&mgr->client_table);
    while (entry)
    {
        /* ip rule add from `client_addr` lookup 76 */
        snprintf(cmd, sizeof(cmd), "ip rule add from %s lookup 76", entry->client_addr);
        res = ltem_route_exec_cmd(cmd);
        entry = ds_tree_next(&mgr->client_table, entry);
    }
    return res;
}

/*
 * This will be used in Phase II when we selectively route clients over LTE
 * during failover.
 */
int
ltem_restore_default_client_routes(ltem_mgr_t *mgr)
{
    struct client_entry *entry;
    int res = 0;
    char cmd[1024];

    LOGI("%s: failover:%d", __func__, mgr->lte_state_info->lte_failover_active);

    entry = ds_tree_head(&mgr->client_table);
    while (entry)
    {
        /* ip rule del from `client_addr` lookup table 76 */
        snprintf(cmd, sizeof(cmd), "ip rule del from %s lookup 76", entry->client_addr);
        res = ltem_route_exec_cmd(cmd);
        entry = ds_tree_next(&mgr->client_table, entry);
    }

    return res;
}

int
ltem_set_lte_route_metric(ltem_mgr_t *mgr)
{
    lte_route_info_t *route;

    route = mgr->lte_route;
    if (!route) return -1;

    if (route->lte_gw[0])
    {
        /*
         * Route updates are now handled by NM via the Wifi_Route_Config table.
         */
        return ltem_ovsdb_update_wifi_route_config_metric(mgr, route->lte_if_name, route->lte_metric);
    }
    else
    {
        LOGI("%s: lte_gw[%s] not set", __func__, route->lte_gw);
    }

    return 0;
}

int
ltem_force_lte_route(ltem_mgr_t *mgr)
{
    lte_route_info_t *route;

    route = mgr->lte_route;
    if (!route) return -1;

    if (route->wan_gw[0])
    {
        LOGI("%s: if_name[%s]", __func__, route->wan_if_name);
        route->wan_metric = WAN_L3_FAIL_METRIC;
        /*
         * Route updates are now handled by NM via the Wifi_Route_Config table.
         */
        return ltem_ovsdb_update_wifi_route_config_metric(mgr, route->wan_if_name, route->wan_metric);
    }

    return 0;
}

int
ltem_restore_default_wan_route(ltem_mgr_t *mgr)
{
    lte_route_info_t *route;

    route = mgr->lte_route;
    if (!route) return -1;

    if (route->wan_gw[0])
    {
        LOGI("%s: if_name[%s]", __func__, route->wan_if_name);
        route->wan_metric = WAN_DEFAULT_METRIC;
        /*
         * Route updates are now handled by NM via the Wifi_Route_Config table.
         */
        return ltem_ovsdb_update_wifi_route_config_metric(mgr, route->wan_if_name, route->wan_metric);
    }

    return 0;
}
