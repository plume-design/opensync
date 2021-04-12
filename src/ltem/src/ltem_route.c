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
#include "log.h"         // logging routines
#include "json_util.h"   // json routines
#include "os.h"          // OS helpers
#include "ovsdb.h"       // OVSDB helpers
#include "target.h"      // target API
#include "network_metadata.h"  // network metadata API

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
    if (!res) return res;

    LOGI("%s: cmd=%s, errno %s", __func__, cmd, strerror(errno));
    return res;
}

int
ltem_create_lte_route_table(ltem_mgr_t *mgr)
{
    char cmd[1024];
    /* ip route add 0.0.0.0/0 dev wwan0 table 76 */
    snprintf(cmd, sizeof(cmd), "ip route add 0.0.0.0/0 dev wwan0 table 76");
    return (ltem_route_exec_cmd(cmd));
}

static int
client_cmp(void *a, void *b)
{
    char *name_a = a;
    char *name_b = b;
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
    if (dhcp_lease->hostname != NULL && dhcp_lease->inet_addr != NULL)
    {
        strncpy(new_entry->client_name, dhcp_lease->hostname, sizeof(new_entry->client_name));
        strncpy(new_entry->client_addr, dhcp_lease->inet_addr, sizeof(new_entry->client_addr));
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

    if (dhcp_lease->hostname != NULL && dhcp_lease->inet_addr != NULL)
    {
        strncpy(to_del.client_name, dhcp_lease->hostname, sizeof(to_del.client_name));
        strncpy(to_del.client_addr, dhcp_lease->inet_addr, sizeof(to_del.client_addr));
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
ltem_update_lte_subnet(ltem_mgr_t *mgr, char *lte_subnet)
{
    strncpy(mgr->lte_route->lte_subnet, lte_subnet,
            sizeof(mgr->lte_route->lte_subnet));
}

void
ltem_update_wan_subnet(ltem_mgr_t *mgr, char *wan_subnet)
{
    strncpy(mgr->lte_route->wan_subnet, wan_subnet,
            sizeof(mgr->lte_route->wan_subnet));
}

void
ltem_update_lte_netmask(ltem_mgr_t *mgr, char *lte_netmask)
{
    strncpy(mgr->lte_route->lte_netmask, lte_netmask,
            sizeof(mgr->lte_route->lte_netmask));
}

void
ltem_update_wan_netmask(ltem_mgr_t *mgr, char *wan_netmask)
{
    strncpy(mgr->lte_route->wan_netmask, wan_netmask,
            sizeof(mgr->lte_route->wan_netmask));
}

int
ltem_restore_resolv_conf(ltem_mgr_t *mgr)
{
    char *wwan0_dns_path = "/var/tmp/dns/wwan0.resolv";
    char *resolv_path = "/var/tmp/resolv.conf";
    char *resolv_backup = "/var/tmp/resolv.bak";
    char cmd[1024];
    int res;

    /*
     * Check to see if the .bak exists
     */
    if (!fopen(resolv_backup, "r"))
    {
        LOGI("%s: %s doesn't exist yet", __func__, resolv_backup);
        return 0;
    }
    /*
     * Restore the original resolv.conf
     */
    snprintf(cmd, sizeof(cmd), "cp %s %s", resolv_backup, resolv_path);
    res = ltem_route_exec_cmd(cmd);
    if (res)
    {
        LOGE("%s: cmd %s failed", __func__, cmd);
        return res;
    }
    snprintf(cmd, sizeof(cmd), "rm %s", wwan0_dns_path);
    res = ltem_route_exec_cmd(cmd);
    if (res)
    {
        LOGE("%s: cmd %s failed", __func__, cmd);
    }

    return res;
}

int
ltem_update_resolv_conf(ltem_mgr_t *mgr)
{
    char *wwan0_resolv_path = "/var/tmp/wwan0.resolv";
    char *wwan0_dns_path = "/var/tmp/dns/wwan0.resolv";
    char *resolv_path = "/var/tmp/resolv.conf";
    char *resolv_backup = "/var/tmp/resolv.bak";
    char cmd[1024];
    int res;

    /*
     * This is really an ugly hack. We can't have the wwan0 dns in resolv.conf before switchover
     * because we don't want any traffic on the LTE interface before switchover.
     * So we store the wwan0 dns address in a tmp file. Once switchover happens we write to
     * resolv.conf (saving the old resolv.conf). Since dns_sub.sh runs periodically and it puts the wwan0
     * dns address at the top of resolv.conf, we also write /var/tmp/dns/wwan0.resolv. Once we switch back to eth0,
     * we unwind all that was done.
     */
    snprintf(cmd, sizeof(cmd), "cp %s %s", resolv_path, resolv_backup);
    res = ltem_route_exec_cmd(cmd);
    if (res)
    {
        LOGE("%s: cmd %s failed", __func__, cmd);
        return res;
    }
    snprintf(cmd, sizeof(cmd), "cat %s > %s", wwan0_resolv_path, resolv_path);
    res = ltem_route_exec_cmd(cmd);
    if (res)
    {
        LOGE("%s: cmd %s failed", __func__, cmd);
        return res;
    }
    snprintf(cmd, sizeof(cmd), "cp %s %s", wwan0_resolv_path, wwan0_dns_path);
    res = ltem_route_exec_cmd(cmd);
    if (res)
    {
        LOGE("%s: cmd %s failed", __func__, cmd);
        return res;
    }

    return res;
}


int
ltem_add_lte_client_routes(ltem_mgr_t *mgr)
{
    struct client_entry *entry;
    int res = 0;
    char cmd[1024];

    LOGI("%s: failover:%d", __func__, mgr->lte_state_info->lte_failover);
    entry = ds_tree_head(&mgr->client_table);
    while (entry)
    {
        /* ip rule add from `client_addr` lookup 76 */
        snprintf(cmd, sizeof(cmd), "ip rule add from %s lookup 76", entry->client_addr);
        res = ltem_route_exec_cmd(cmd);
        entry = ds_tree_next(&mgr->client_table, entry);
    }
    if (!res)
    {
        res = ltem_update_resolv_conf(mgr);
    }
    return res;
}

int
ltem_restore_default_client_routes(ltem_mgr_t *mgr)
{
    struct client_entry *entry;
    int res = 0;
    char cmd[1024];

    LOGI("%s: failover:%d", __func__, mgr->lte_state_info->lte_failover);

    entry = ds_tree_head(&mgr->client_table);
    while (entry)
    {
        /* ip rule del from `client_addr` lookup table 76 */
        snprintf(cmd, sizeof(cmd), "ip rule del from %s lookup 76", entry->client_addr);
        res = ltem_route_exec_cmd(cmd);
        entry = ds_tree_next(&mgr->client_table, entry);
    }
    res = ltem_restore_resolv_conf(mgr);

    return res;
}

int
ltem_add_lte_route(ltem_mgr_t *mgr)
{
    int res = 0;
    char cmd[1024];

    LOGI("%s: failover:%d", __func__, mgr->lte_state_info->lte_failover);
    /* route add default dev wwan0 metric 1*/
    snprintf(cmd, sizeof(cmd), "route add default dev wwan0 metric 1");
    res = ltem_route_exec_cmd(cmd);
    if (!res)
    {
        res = ltem_update_resolv_conf(mgr);
    }
    return res;
}

int
ltem_restore_default_route(ltem_mgr_t *mgr)
{
    int res = 0;
    char cmd[1024];

    LOGI("%s: failover:%d", __func__, mgr->lte_state_info->lte_failover);

    /* route delete default dev wwan0 metric 1*/
    snprintf(cmd, sizeof(cmd), "route delete default dev wwan0 metric 1");
    res = ltem_route_exec_cmd(cmd);
    if (res)
    {
        LOGI("%s: cmd failed: %s, errno: %s", __func__, cmd, strerror(errno));
        return res;
    }

    res = ltem_restore_resolv_conf(mgr);

    if (mgr->lte_route->wan_gw[0])
    {
        /* route add default gw [gw] dev eth0 */
        snprintf(cmd, sizeof(cmd), "route add default gw %s dev eth0", mgr->lte_route->wan_gw);
        res = ltem_route_exec_cmd(cmd);
        if (res)
        {
            LOGI("%s: cmd failed: %s, errno: %s", __func__, cmd, strerror(errno));
            return res;
        }
    }
    return res;
}

