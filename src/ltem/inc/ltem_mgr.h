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

#include "ltem_lte_modem.h"

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

enum ltem_lte_sim_status
{
    LTEM_LTE_SIM_UNKNOWN,
    LTEM_LTE_SIM_REMOVED,
    LTEM_LTE_SIM_INSERTED,
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
    char apn[32];
} lte_config_info_t;

typedef struct lte_state_info_
{
    bool modem_present;
    char iccid[32];
    char imei[32];
    char imsi[32];
    enum ltem_lte_sim_status sim_status;
    char provider[32];
    uint32_t mcc;
    uint32_t mnc;
    uint32_t tac;
    enum lte_net_reg_status net_state;
    bool lte_failover_active;
    time_t lte_failover_start;
    time_t lte_failover_end;
    uint32_t lte_failover_count;
} lte_state_info_t;

typedef struct lte_route_info_
{
    char lte_subnet[C_IPV6ADDR_LEN];
    char lte_netmask[C_IPV6ADDR_LEN];
    char lte_gw[C_IPV6ADDR_LEN];
    char wan_subnet[C_IPV6ADDR_LEN];
    char wan_netmask[C_IPV6ADDR_LEN];
    char wan_if_name[C_IFNAME_LEN];
    char wan_gw[C_IPV6ADDR_LEN];
    char lte_dns1[C_IPV6ADDR_LEN];
    char lte_dns2[C_IPV6ADDR_LEN];
} lte_route_info_t;

#define LTE_ATCMD(M)     \
    M(AT_SWITCH_SLOT, "at+qdsim=1\r") \
    M(AT_QUERY_SLOT, "at+qdsim?\r") \
    M(AT_MODEM_RESET, "at+cfun=1,1\r") \
    M(AT_ATI, "ati\r")      \
    M(AT_IMEI, "at+gsn\r")      \
    M(AT_IMSI, "at+cimi\r")   \
    M(AT_ICCID, "at+qccid\r") \
    M(AT_CREG, "at+creg?\r") \
    M(AT_CSQ, "at+csq\r") \
    M(AT_QGDCNT, "at+qgdcnt?\r") \
    M(AT_COPS, "at+cops?\r") \
    M(AT_SRV_CELL, "at+qeng=\"servingcell\"\r") \
    M(AT_NEIGH_CELL, "at+qeng=\"neighbourcell\"\r") \
    M(AT_SET_KORE_APN, "AT+CGDCONT=1,\"IPV4V6\",\"data.icore.name\",\"0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0\",0,0,0,0\r") \
    M(AT_SET_QMI_MODE, "AT+QCFG=\"usbnet\",0\r") \
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


typedef struct ltem_handlers_
{
    bool (*ltem_mgr_init) (struct ev_loop *loop);
    int (*system_call) (const char *cmd);
    int (*lte_modem_open) (char *modem_path);
    ssize_t (*lte_modem_write) (int fd, const char *cmd);
    ssize_t (*lte_modem_read) (int fd, char *at_buf, ssize_t at_len);
    void (*lte_modem_close) (int fd);
    char *(*lte_run_microcom_cmd) (char *cmd);

} ltem_handlers_t;

typedef struct ltem_mgr_
{
    struct ev_loop *loop;
    ev_timer timer;              // manager's event timer
    time_t periodic_ts;          // periodic timestamp
    time_t mqtt_periodic_ts;     // periodic timestamp for MQTT reports
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
    ltem_handlers_t handlers;
    lte_modem_info_t modem_info;
    time_t mqtt_interval;
    char node_id[64];
    char location_id[64];
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
int ltem_force_lte_route(ltem_mgr_t *mgr);
int ltem_restore_default_route(ltem_mgr_t *mgr);
int ltem_get_modem_info(void);
int ltem_create_lte_route_table(ltem_mgr_t *mgr);
void ltem_create_client_table(ltem_mgr_t *mgr);
void ltem_client_table_update(ltem_mgr_t *mgr, struct schema_DHCP_leased_IP *dhcp_lease);
void ltem_client_table_delete(ltem_mgr_t *mgr, struct schema_DHCP_leased_IP *dhcp_lease);
void ltem_update_wan_route(ltem_mgr_t *mgr, char *if_name, char *wan_subnet, char *wan_gw, char *netmask);
void ltem_update_lte_route(ltem_mgr_t *mgr, char *if_name, char *lte_subnet, char *lte_gw, char *netmask);
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
void ltem_reset_modem(void);
void ltem_evt_switch_slot(void);
void ltem_set_qmi_mode(void);
void ltem_set_kore_apn(void);
void ltem_set_apn(char *apn);
int ltem_build_mqtt_report(time_t now);
int lte_set_mqtt_topic(void);
void lte_dump_modem_info(void);

#endif /* LTEM_MGR_H_INCLUDED */
