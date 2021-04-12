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

#ifndef LTEM_MGR_H_INCLUDED
#define LTEM_MGR_H_INCLUDED

#include <ev.h>          // libev routines
#include <time.h>
#include <sys/sysinfo.h>

#include "const.h"
#include "ds_tree.h"
#include "schema.h"
#include "osn_types.h"

#define LTEM_ADDR_MAX_LEN 32

enum  ltem_header_ids
{
    LTEM_NO_HEADER          = -1,
    LTEM_HEADER_LOCATION_ID =  0,
    LTEM_HEADER_NODE_ID     =  1,
    LTEM_NUM_HEADER_IDS     =  2,
};

enum  ltem_wan_state
{
    LTEM_WAN_STATE_UNKNOWN,
    LTEM_WAN_STATE_UP,
    LTEM_WAN_STATE_DOWN,
    LTEM_WAN_STATE_NUM,
};

enum  ltem_lte_state
{
    LTEM_LTE_STATE_UNKNOWN,
    LTEM_LTE_STATE_INIT,
    LTEM_LTE_STATE_UP,
    LTEM_LTE_STATE_DOWN,
    LTEM_LTE_STATE_NUM,
};

typedef struct lte_config_info_
{
    char if_name[C_IFNAME_LEN];
    bool lte_failover;
    bool v4_enable;
    bool v6_enable;
    bool force_use_lte;
    char esim_download[32];
    char esim_active[32];
    char simcard_slot[16];
    bool modem_enable;
} lte_config_info_t;

typedef struct lte_state_info_
{
    char if_name[C_IFNAME_LEN];
    lte_config_info_t *lte_config;
    bool lte_failover;
    bool modem_present;
} lte_state_info_t;

typedef struct lte_state_info_rpt_
{
    char imei[32];
    char imsi[32];
    char net_info_state[64];
    char tdd[32];
    char mcc[32];
    char mnc[32];
    char lac[32];
    char cellid[32];
    char pcid[32];
    char uarfcn[32];
    char earfcn[32];
    char band[16];
    char ul_bandwidth[16];
    char dl_bandwidth[32];
} lte_state_info_rpt_t;

typedef struct lte_route_info_
{
    char lte_subnet[C_IPV6ADDR_LEN];
    char lte_netmask[C_IPV6ADDR_LEN];
    char wan_subnet[C_IPV6ADDR_LEN];
    char wan_netmask[C_IPV6ADDR_LEN];
    char wan_dns1[C_IPV6ADDR_LEN];
    char wan_dns2[C_IPV6ADDR_LEN];
    char lte_dns1[C_IPV6ADDR_LEN];
    char lte_dns2[C_IPV6ADDR_LEN];
    char wan_gw[C_IPV6ADDR_LEN];
    char lte_gw[C_IPV6ADDR_LEN];
    bool wan_dns_reachable;
    bool lte_dns_reachable;
} lte_route_info_t;

#define LTE_ATCMD(M)     \
    M(AT_IMEI, "AT+GSN") \
    M(AT_IMSI, "AT+CIMI") \
    M(AT_ICCID, "AT+QCCID") \
    M(AT_CREG, "AT+CREG") \
    M(AT_MAX, NULL)

enum lte_at_cmd
{
    #define _ENUM(sym, str) sym,
    LTE_ATCMD(_ENUM)
    #undef _ENUM
};

const char *lte_at_cmd_tostr(enum lte_at_cmd cmd);

struct client_entry
{
    char client_name[32];
    char client_addr[C_IPV6ADDR_LEN];
    ds_tree_node_t entry_node; // tree node structure
};

typedef struct ltem_mgr_
{
    struct ev_loop *loop;
    ev_timer timer;              // manager's event timer
    time_t periodic_ts;          // periodic timestamp
    time_t init_time;            // init time
    char pid[16];                // manager's pid
    struct sysinfo sysinfo;      /* system information */
    enum ltem_wan_state wan_state;
    enum ltem_lte_state lte_state;
    int lte_init_fd[2];
    ev_io lte_init_fd_watcher;
    ev_child lte_init_pid_watcher;
    lte_config_info_t *lte_config_info;
    lte_state_info_t *lte_state_info;
    lte_route_info_t *lte_route;
    ds_tree_t client_table;
    bool lte_cm_table_entry;
    bool lte_wfc_table_entry;
} ltem_mgr_t;

bool ltem_init_mgr(struct ev_loop *loop);
ltem_mgr_t* ltem_get_mgr(void);
void ltem_event_init(void);
void ltem_set_lte_state(enum ltem_lte_state lte_state);
void ltem_set_wan_state(enum ltem_wan_state wan_state);
char *ltem_get_lte_state_name(enum ltem_lte_state state);
char *ltem_get_wan_state_name(enum ltem_wan_state state);
void ltem_event_init(void);
int ltem_ovsdb_init(void);
bool ltem_init_lte(void);
int ltem_add_lte_route(ltem_mgr_t *mgr);
int ltem_restore_default_route(ltem_mgr_t *mgr);
char *ltem_get_modem_info(enum lte_at_cmd at_cmd);
int ltem_create_lte_route_table(ltem_mgr_t *mgr);
void ltem_create_client_table(ltem_mgr_t *mgr);
void ltem_client_table_update(ltem_mgr_t *mgr, struct schema_DHCP_leased_IP *dhcp_lease);
void ltem_client_table_delete(ltem_mgr_t *mgr, struct schema_DHCP_leased_IP *dhcp_lease);
void ltem_update_lte_subnet(ltem_mgr_t *mgr, char *lte_subnet);
void ltem_update_wan_subnet(ltem_mgr_t *mgr, char *wan_subnet);
void ltem_update_lte_netmask(ltem_mgr_t *mgr, char *lte_netmask);
void ltem_update_wan_netmask(ltem_mgr_t *mgr, char *wan_netmask);
int ltem_add_lte_client_routes(ltem_mgr_t *mgr);
int ltem_restore_default_client_routes(ltem_mgr_t *mgr);
int ltem_check_dns(char *server, char *hostname);
int ltem_update_resolv_conf(ltem_mgr_t *mgr);
int ltem_restore_resolv_conf(ltem_mgr_t *mgr);
int ltem_ovsdb_cmu_create_lte(ltem_mgr_t *mgr);
int ltem_ovsdb_cmu_disable_lte(ltem_mgr_t *mgr);
void ltem_ovsdb_cmu_check_lte(ltem_mgr_t *mgr);
int ltem_ovsdb_wifi_inet_create_config(ltem_mgr_t *mgr);
int ltem_ovsdb_lte_create_config(ltem_mgr_t *mgr);

#endif /* LTEM_MGR_H_INCLUDED */
