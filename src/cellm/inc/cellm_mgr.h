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

#ifndef CELLM_MGR_H_INCLUDED
#define CELLM_MGR_H_INCLUDED

#include <ev.h>  // libev routines
#include <time.h>
#include <sys/sysinfo.h>

#include "const.h"
#include "ds_tree.h"
#include "osn_types.h"
#include "schema.h"
#include "lte_info.h"
#include "osn_cell_modem.h"
#include "cm2.h"

#define WAN_DEFAULT_METRIC       CONFIG_MANAGER_NM_ROUTE_BASE_METRIC
#define LTE_DEFAULT_METRIC       WAN_DEFAULT_METRIC + 50
#define WAN_L3_FAIL_METRIC       LTE_DEFAULT_METRIC + 10
#define LTE_CMU_DEFAULT_PRIORITY 2
#define DEFAULT_LTE_NAME         "wwan0"
#define LTEM_INACTIVITY_PROBE    10000
#define WAN_L3_FAIL_LIMIT        3
#define WAN_L3_RECONNECT         3
#define DEFAULT_LTE_MTU          1420

// Intervals and timeouts in seconds
#define CELLM_TIMER_INTERVAL      1
#define CELLM_MQTT_INTERVAL       60
#define CELLM_MODEM_INFO_INTERVAL 60

enum cellm_header_ids
{
    CELLM_NO_HEADER = -1,
    CELLM_HEADER_LOCATION_ID = 0,
    CELLM_HEADER_NODE_ID = 1,
    CELLM_NUM_HEADER_IDS = 2,
};

enum cellm_wan_state
{
    CELLM_WAN_STATE_UNKNOWN,
    CELLM_WAN_STATE_UP,
    CELLM_WAN_STATE_DOWN,
    CELLM_WAN_STATE_NUM,
};

enum cellm_state
{
    CELLM_STATE_UNKNOWN,
    CELLM_STATE_INIT,
    CELLM_STATE_UP,
    CELLM_STATE_DOWN,
    CELLM_STATE_NUM,
};

typedef struct cellm_config_info_
{
    char if_name[C_IFNAME_LEN];
    bool manager_enable;
    bool cellm_failover_enable;
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
} cellm_config_info_t;

typedef struct cellm_state_info_
{
    bool cellm_failover_force;
    bool cellm_failover_active;
    time_t cellm_failover_start;
    time_t cellm_failover_end;
    uint32_t cellm_failover_count;
    uint64_t cellm_bands;
    bool enable_persist;
    bool esim_download_in_progress;
    bool esim_download_complete;
    char esim_active_profile[32];
    bool lte_force_allow;
} cellm_state_info_t;

typedef struct cellm_route_info_
{
    char cellm_if_name[C_IFNAME_LEN];
    char cellm_ip_addr[C_IPV6ADDR_LEN];
    char cellm_subnet[C_IPV6ADDR_LEN];
    char cellm_netmask[C_IPV6ADDR_LEN];
    char cellm_gw[C_IPV6ADDR_LEN];
    uint32_t cellm_metric;
    char wan_if_name[C_IFNAME_LEN];
    char wan_subnet[C_IPV6ADDR_LEN];
    char wan_netmask[C_IPV6ADDR_LEN];
    char wan_gw[C_IPV6ADDR_LEN];
    uint32_t wan_metric;
    char cellm_dns1[C_IPV6ADDR_LEN];
    char cellm_dns2[C_IPV6ADDR_LEN];
    uint32_t wan_priority;
    uint32_t cellm_priority;
    bool has_L3;
} cellm_route_info_t;

struct client_entry
{
    char client_name[32];
    char client_addr[C_IPV6ADDR_LEN];
    ds_tree_node_t entry_node;  // tree node structure
};

typedef struct cellm_handlers_
{
    bool (*cellm_mgr_init)(struct ev_loop *loop);
    int (*system_call)(const char *cmd);
} cellm_handlers_t;

typedef struct cellm_mgr_
{
    struct ev_loop *loop;
    ev_timer timer;                  /* manager's event timer */
    time_t periodic_ts;              /* periodic timestamp */
    time_t mqtt_periodic_ts;         /* periodic timestamp for MQTT reports */
    time_t state_periodic_ts;        /* periodic timestamp for Lte State updates */
    time_t lte_l3_state_periodic_ts; /* periodic timestamp for LTE L3 state check */
    time_t wan_l3_state_periodic_ts; /* periodic timestamp for WAN L3 state check */
    time_t log_modem_info_ts;        /* periodic timestamp for logging modem info */
    time_t lte_healthcheck_ts;       /* periodic timestamp for LTE healthcheck */
    time_t wan_healthcheck_ts;       /* periodic timestamp for WAN healthcheck */
    time_t init_time;                /* init time */
    char pid[16];                    /* manager's pid */
    struct sysinfo sysinfo;          /* system information */
    enum cellm_wan_state wan_state;
    enum cellm_state cellm_state;
    int cellm_init_fd[2];
    ev_io cellm_init_fd_watcher;
    ev_child cellm_init_pid_watcher;
    cellm_config_info_t *cellm_config_info;
    cellm_state_info_t *cellm_state_info;
    cellm_route_info_t *cellm_route;
    ds_tree_t client_table;
    bool cellm_cm_table_entry;
    bool cellm_wfc_table_entry;
    cellm_handlers_t handlers;
    osn_cell_modem_info_t *modem_info;
    time_t mqtt_interval;
    char topic[256];
    char node_id[64];
    char location_id[64];
    int ipv6_ra_default_lifetime;
    char ipv6_prefix_valid_lifetime[64];
    time_t last_wan_healthcheck_success;
    int wan_failure_count;
    int wan_l3_reconnect_success;
} cellm_mgr_t;

bool cellm_init_mgr(struct ev_loop *loop);
cellm_mgr_t *cellm_get_mgr(void);
void cellm_event_init(void);
void cellm_set_state(enum cellm_state lte_state);
void cellm_set_wan_state(enum cellm_wan_state wan_state);
char *cellm_get_state_name(enum cellm_state state);
char *cellm_get_wan_state_name(enum cellm_wan_state state);
void cellm_event_init(void);
int cellm_ovsdb_init(void);
bool cellm_init_lte(void);
int cellm_set_route_metric(cellm_mgr_t *mgr);
int celln_force_lte(cellm_mgr_t *mgr);
int cellm_restore_wan(cellm_mgr_t *mgr);
int cellm_create_route_table(cellm_mgr_t *mgr);
void cellm_create_client_table(cellm_mgr_t *mgr);
void cellm_client_table_update(cellm_mgr_t *mgr, struct schema_DHCP_leased_IP *dhcp_lease);
void cellm_client_table_delete(cellm_mgr_t *mgr, struct schema_DHCP_leased_IP *dhcp_lease);
void cellm_update_wan_route(cellm_mgr_t *mgr, struct schema_Wifi_Route_Config *route_config);
void cellm_update_route(cellm_mgr_t *mgr, char *if_name, char *lte_subnet, char *lte_gw, char *netmask);
int cellm_add_lte_client_routes(cellm_mgr_t *mgr);
int cellm_restore_default_client_routes(cellm_mgr_t *mgr);
int cellm_check_dns(const char *server, char *hostname);
const char *cellm_get_next_dns_server(void);
int cellm_update_resolv_conf(cellm_mgr_t *mgr);
int cellm_restore_resolv_conf(cellm_mgr_t *mgr);
int cellm_ovsdb_cmu_insert_lte(cellm_mgr_t *mgr);
int cellm_ovsdb_cmu_update_lte(cellm_mgr_t *mgr);
int cellm_ovsdb_cmu_disable_lte(cellm_mgr_t *mgr);
uint32_t cellm_ovsdb_cmu_get_wan_priority(cellm_mgr_t *mgr);
uint32_t cellm_ovsdb_cmu_get_lte_priority(cellm_mgr_t *mgr);
int cellm_ovsdb_wifi_inet_create_config(cellm_mgr_t *mgr);
int cellm_ovsdb_lte_create_config(cellm_mgr_t *mgr);
void cellm_ovsdb_update_awlan_node(struct schema_AWLAN_Node *new);
int cellm_ovsdb_update_wifi_route_config_metric(cellm_mgr_t *mgr, char *if_name, uint32_t metric);
void cellm_reset_modem(void);
int cellm_build_mqtt_report(time_t now);
int cellm_set_mqtt_topic(void);
void cellm_dump_modem_info(void);
int cellm_ovsdb_update_lte_state(cellm_mgr_t *mgr);
int cellm_serialize_report(void);
void cellm_mqtt_cleanup(void);
int cellm_ovsdb_cmu_update_lte_priority(cellm_mgr_t *mgr, uint32_t priority);
int cellm_update_esim(cellm_mgr_t *mgr);
bool cellm_init_modem(void);
void cellm_fini_modem(void);
char *cellm_ovsdb_get_if_type(char *if_name);
int cellm_ovsdb_check_lte_l3_state(cellm_mgr_t *mgr);
int cellm_dns_connect_check(char *if_name);
void cellm_ovsdb_config_lte(cellm_mgr_t *mgr);
void cellm_flush_flows(cellm_mgr_t *mgr);
void cellm_ovsdb_set_v6_failover(cellm_mgr_t *mgr);
void cellm_ovsdb_revert_v6_failover(cellm_mgr_t *mgr);
void cellm_set_failover(cellm_mgr_t *mgr);
int cellm_wan_healthcheck(cellm_mgr_t *mgr);
int cellm_set_route_preferred(cellm_mgr_t *mgr);
int cellm_set_wan_route_preferred(cellm_mgr_t *mgr);

void cellm_info_event_init(void);
int cellm_info_build_mqtt_report(time_t now);

#endif /* CELLM_MGR_H_INCLUDED */
