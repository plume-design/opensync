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
#include "osn_types.h"
#include "schema.h"
#include "lte_info.h"
#include "osn_lte_modem.h"
#include "cm2.h"

#define WAN_DEFAULT_METRIC CONFIG_MANAGER_NM_ROUTE_BASE_METRIC
#define LTE_DEFAULT_METRIC WAN_DEFAULT_METRIC + 50
#define WAN_L3_FAIL_METRIC LTE_DEFAULT_METRIC + 10
#define LTE_CMU_DEFAULT_PRIORITY 2
#define DEFAULT_LTE_NAME "wwan0"
#define LTEM_INACTIVITY_PROBE 10000
#define WAN_L3_FAIL_LIMIT 3
#define WAN_L3_RECONNECT 3

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
    bool manager_enable;
    bool lte_failover_enable;
    bool ipv4_enable;
    bool ipv6_enable;
    bool force_use_lte;
    uint32_t active_simcard_slot;
    bool modem_enable;
    uint32_t report_interval;
    char apn[64];
    char lte_bands[32];
    bool enable_persist;
    char esim_activation_code[64];
} lte_config_info_t;

typedef struct lte_state_info_
{
    bool lte_failover_force;
    bool lte_failover_active;
    time_t lte_failover_start;
    time_t lte_failover_end;
    uint32_t lte_failover_count;
    uint64_t lte_bands;
    bool enable_persist;
    bool esim_download_in_progress;
    bool esim_download_complete;
    char esim_active_profile[32];
    bool lte_force_allow;
} lte_state_info_t;

typedef struct lte_route_info_
{
    char lte_if_name[C_IFNAME_LEN];
    char lte_ip_addr[C_IPV6ADDR_LEN];
    char lte_subnet[C_IPV6ADDR_LEN];
    char lte_netmask[C_IPV6ADDR_LEN];
    char lte_gw[C_IPV6ADDR_LEN];
    uint32_t lte_metric;
    char wan_if_name[C_IFNAME_LEN];
    char wan_subnet[C_IPV6ADDR_LEN];
    char wan_netmask[C_IPV6ADDR_LEN];
    char wan_gw[C_IPV6ADDR_LEN];
    uint32_t wan_metric;
    char lte_dns1[C_IPV6ADDR_LEN];
    char lte_dns2[C_IPV6ADDR_LEN];
    uint32_t wan_priority;
    uint32_t lte_priority;
    bool has_L3;
} lte_route_info_t;

struct client_entry
{
    char client_name[32];
    char client_addr[C_IPV6ADDR_LEN];
    ds_tree_node_t entry_node; // tree node structure
};


typedef struct ltem_handlers_
{
    bool (*ltem_mgr_init) (struct ev_loop *loop);
    int (*system_call) (const char *cmd);
} ltem_handlers_t;

typedef struct ltem_mgr_
{
    struct ev_loop *loop;
    ev_timer timer;                  /* manager's event timer */
    time_t periodic_ts;              /* periodic timestamp */
    time_t mqtt_periodic_ts;         /* periodic timestamp for MQTT reports */
    time_t state_periodic_ts;        /* periodic timestamp for Lte State updates */
    time_t lte_l3_state_periodic_ts; /* periodic timestamp for LTE L3 state check */
    time_t wan_l3_state_periodic_ts; /* periodic timestamp for WAN L3 state check */
    time_t lte_log_modem_info_ts;    /* periodic timestamp for logging modem info */
    time_t lte_healthcheck_ts;       /* periodic timestamp for LTE healthcheck */
    time_t wan_healthcheck_ts;       /* periodic timestamp for WAN healthcheck */
    time_t init_time;                /* init time */
    char pid[16];                    /* manager's pid */
    struct sysinfo sysinfo;          /* system information */
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
    ltem_handlers_t handlers;
    osn_lte_modem_info_t *modem_info;
    time_t mqtt_interval;
    char topic[256];
    char node_id[64];
    char location_id[64];
    int ipv6_ra_default_lifetime;
    char ipv6_prefix_valid_lifetime[64];
    time_t last_wan_healthcheck_success;
    int wan_failure_count;
    int wan_l3_reconnect_success;
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
int ltem_set_lte_route_metric(ltem_mgr_t *mgr);
int ltem_force_lte(ltem_mgr_t *mgr);
int ltem_restore_wan(ltem_mgr_t *mgr);
int ltem_create_lte_route_table(ltem_mgr_t *mgr);
void ltem_create_client_table(ltem_mgr_t *mgr);
void ltem_client_table_update(ltem_mgr_t *mgr, struct schema_DHCP_leased_IP *dhcp_lease);
void ltem_client_table_delete(ltem_mgr_t *mgr, struct schema_DHCP_leased_IP *dhcp_lease);
void ltem_update_wan_route(ltem_mgr_t *mgr, struct schema_Wifi_Route_Config *route_config);
void ltem_update_lte_route(ltem_mgr_t *mgr, char *if_name, char *lte_subnet, char *lte_gw, char *netmask);
int ltem_add_lte_client_routes(ltem_mgr_t *mgr);
int ltem_restore_default_client_routes(ltem_mgr_t *mgr);
int ltem_check_dns(const char *server, char *hostname);
const char *ltem_get_next_dns_server(void);
int ltem_update_resolv_conf(ltem_mgr_t *mgr);
int ltem_restore_resolv_conf(ltem_mgr_t *mgr);
int ltem_ovsdb_cmu_insert_lte(ltem_mgr_t *mgr);
int ltem_ovsdb_cmu_update_lte(ltem_mgr_t *mgr);
int ltem_ovsdb_cmu_disable_lte(ltem_mgr_t *mgr);
uint32_t ltem_ovsdb_cmu_get_wan_priority(ltem_mgr_t *mgr);
uint32_t ltem_ovsdb_cmu_get_lte_priority(ltem_mgr_t *mgr);
int ltem_ovsdb_wifi_inet_create_config(ltem_mgr_t *mgr);
int ltem_ovsdb_lte_create_config(ltem_mgr_t *mgr);
void ltem_ovsdb_update_awlan_node(struct schema_AWLAN_Node *new);
int ltem_ovsdb_update_wifi_route_config_metric(ltem_mgr_t *mgr, char *if_name, uint32_t metric);
void ltem_reset_modem(void);
int ltem_build_mqtt_report(time_t now);
int lte_set_mqtt_topic(void);
void lte_dump_modem_info(void);
int ltem_ovsdb_update_lte_state(ltem_mgr_t *mgr);
int lte_serialize_report(void);
void lte_mqtt_cleanup(void);
int ltem_ovsdb_cmu_update_lte_priority(ltem_mgr_t *mgr, uint32_t priority);
int ltem_update_esim(ltem_mgr_t *mgr);
bool ltem_init_lte_modem(void);
void ltem_fini_lte_modem(void);
char *ltem_ovsdb_get_if_type(char *if_name);
int ltem_ovsdb_check_lte_l3_state(ltem_mgr_t *mgr);
int ltem_dns_connect_check(char *if_name);
void ltem_ovsdb_config_lte(ltem_mgr_t *mgr);
void ltem_flush_flows(ltem_mgr_t *mgr);
void ltem_ovsdb_set_v6_failover(ltem_mgr_t *mgr);
void ltem_ovsdb_revert_v6_failover(ltem_mgr_t *mgr);
void ltem_set_failover(ltem_mgr_t *mgr);
int ltem_wan_healthcheck(ltem_mgr_t *mgr);
int ltem_set_lte_route_preferred(ltem_mgr_t *mgr);
int ltem_set_wan_route_preferred(ltem_mgr_t *mgr);

#endif /* LTEM_MGR_H_INCLUDED */
