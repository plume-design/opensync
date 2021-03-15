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

#ifndef CM2_H_INCLUDED
#define CM2_H_INCLUDED

#include "schema.h"
#include "ds_list.h"
#include "ev.h"
#ifdef BUILD_HAVE_LIBCARES
#include "evx.h"
#endif
#include "target.h"

#define IFNAME_SIZE 128 + 1
#define IFTYPE_SIZE 128 + 1

#define VIF_TYPE_NAME    "vif"
#define ETH_TYPE_NAME    "eth"
#define VLAN_TYPE_NAME   "vlan"
#define PPPOE_TYPE_NAME  "pppoe"
#define GRE_TYPE_NAME    "gre"
#define BRIDGE_TYPE_NAME "bridge"

#ifdef CONFIG_TARGET_USE_WAN_BRIDGE
#ifdef CONFIG_TARGET_WAN_BRIDGE_NAME
#define CM2_WAN_BRIDGE_NAME CONFIG_TARGET_WAN_BRIDGE_NAME
#else
#define CM2_WAN_BRIDGE_NAME SCHEMA_CONSTS_BR_NAME_WAN
#endif /* CONFIG_TARGET_WAN_BRIDGE_NAME */
#else
#define CM2_WAN_BRIDGE_NAME ""
#endif /* CONFIG_TARGET_USE_WAN_BRIDGE */

typedef enum
{
    CM2_STATE_INIT,
    CM2_STATE_LINK_SEL,       // EXTENDER only
    CM2_STATE_WAN_IP,         // EXTENDER only
    CM2_STATE_NTP_CHECK,      // EXTENDER only
    CM2_STATE_OVS_INIT,
    CM2_STATE_TRY_RESOLVE,
    CM2_STATE_RE_CONNECT,
    CM2_STATE_TRY_CONNECT,
    CM2_STATE_FAST_RECONNECT, // EXTENDER only
    CM2_STATE_CONNECTED,
    CM2_STATE_INTERNET,
    CM2_STATE_QUIESCE_OVS,
    CM2_STATE_NUM,
} cm2_state_e;

extern char *cm2_state_name[];

// update reason
typedef enum
{
    CM2_REASON_TIMER,
    CM2_REASON_AWLAN,
    CM2_REASON_MANAGER,
    CM2_REASON_CHANGE,
    CM2_REASON_LINK_USED,
    CM2_REASON_LINK_NOT_USED,
    CM2_REASON_SET_NEW_VTAG,
    CM2_REASON_BLOCK_VTAG,
    CM2_REASON_OVS_INIT,
    CM2_REASON_NUM,
} cm2_reason_e;

extern char *cm2_reason_name[];

typedef enum
{
    CM2_DEST_REDIR,
    CM2_DEST_MANAGER,
} cm2_dest_e;

#define CM2_RESOURCE_MAX 512
#define CM2_HOSTNAME_MAX 256
#define CM2_PROTO_MAX 6

#ifdef BUILD_HAVE_LIBCARES
typedef enum
{
    CM2_ARES_R_NOT_STARTED,
    CM2_ARES_R_IN_PROGRESS,
    CM2_ARES_R_FINISHED,
    CM2_ARES_R_RESOLVED,
} cm2_resolve_state;

typedef struct
{
    /* h_addr_list comes from hostent structure which
     * is defined in <netdb.h>
     * An array of pointers to network addresses for the host (in
     * network byte order), terminated by a null pointer.
     */
    char **h_addr_list;
    int h_length;
    int h_addrtype;
    int h_cur_idx;
    cm2_resolve_state state;
    int req_addr_type;
} cm2_addr_list;
#endif

typedef struct
{
    bool updated;
    bool valid;
    bool resolved;
    char resource[CM2_RESOURCE_MAX];
    char proto[CM2_PROTO_MAX];
    char hostname[CM2_HOSTNAME_MAX];
    int  port;
#ifndef BUILD_HAVE_LIBCARES
    struct addrinfo *ai_list;
    struct addrinfo *ai_curr;
#else
    cm2_addr_list ipv6_addr_list;
    cm2_addr_list ipv4_addr_list;
    bool          ipv6_cur;
#endif
} cm2_addr_t;

typedef enum {
    CM2_VTAG_NOT_USED = 0,
    CM2_VTAG_PENDING,
    CM2_VTAG_USED,
    CM2_VTAG_BLOCKED,
} cm2_vtag_state_t;

typedef struct {
    cm2_vtag_state_t state;
    int              failure;
    int              tag;
    int              blocked_tag;
} cm2_vtag_t;

typedef enum {
    CM2_IP_NOT_SET,
    CM2_IP_NONE,
    CM2_IP_STATIC,
    CM2_IPV4_DHCP,
    CM2_IPV6_DHCP,
} cm2_ip_assign_scheme;

typedef struct {
    cm2_ip_assign_scheme assign_scheme;
    bool                 is_ip;
    bool                 resolve_retry;
    bool                 blocked;
} cm2_ip;

typedef enum
{
    CM2_DEVICE_NONE,
    CM2_DEVICE_ROUTER,
    CM2_DEVICE_BRIDGE,
    CM2_DEVICE_LEAF,
} cm2_dev_type;

typedef struct
{
    int   ovs_resolve;
    int   ovs_resolve_fail;
    int   ovs_con;
    int   gw_offline;
    int   skip_restart;
} cm2_retries_cnt;

typedef struct
{
    char        if_name[IFNAME_SIZE];
    char        if_type[IFTYPE_SIZE];
    bool        is_bridge;
    char        bridge_name[IFNAME_SIZE];
    bool        is_used;
    bool        restart_pending;
    int         priority;
    cm2_ip      ipv4;
    cm2_ip      ipv6;
    bool        gretap_softwds;
    char        gateway_hwaddr[OS_MACSTR_SZ];
    cm2_vtag_t  vtag;
} cm2_main_link_t;

typedef struct
{
    cm2_state_e       state;
    cm2_reason_e      reason;
    cm2_dest_e        dest;
    bool              state_changed;
    bool              connected; /* Device connected to the Controller */
    bool              is_con_stable; /* Connection marked as stable */
    bool              ipv6_manager_con; /* Device connected to the Controller using IPv6 */
    time_t            timestamp;
    int               disconnects; /* Number of disconnects */
    cm2_addr_t        addr_redirector;
    cm2_addr_t        addr_manager;
    ev_timer          timer;
    ev_timer          wdt_timer;
    ev_timer          stability_timer;
    bool              run_stability;
    target_connectivity_check_option_t stability_opts_now;
    target_connectivity_check_option_t stability_opts_next;
    bool              stability_update_now;
    bool              stability_update_next;
    bool              stability_repeat;
    ev_child          stability_child;
    cm2_main_link_t   link;
    uint8_t           ble_status;
    bool              ntp_check;
    struct ev_loop    *loop;
#ifdef BUILD_HAVE_LIBCARES
    struct evx_ares   eares;
#endif
    bool              have_manager;
    bool              have_awlan;
    int               min_backoff;
    int               max_backoff;
    bool              fast_backoff;
    int               target_type;
    bool              fast_reconnect;
    bool              resolve_retry;
    cm2_retries_cnt   cnts;
    cm2_dev_type      dev_type;
} cm2_state_t;

extern cm2_state_t g_state;

typedef enum {
    BLE_ONBOARDING_STATUS_ETHERNET_LINK= 0, // Bit 0
    BLE_ONBOARDING_STATUS_WIFI_LINK,        // Bit 1
    BLE_ONBOARDING_STATUS_ETHERNET_BACKHAUL,// Bit 2
    BLE_ONBOARDING_STATUS_WIFI_BACKHAUL,    // Bit 3
    BLE_ONBOARDING_STATUS_ROUTER_OK,        // Bit 4
    BLE_ONBOARDING_STATUS_INTERNET_OK,      // Bit 5
    BLE_ONBOARDING_STATUS_CLOUD_OK,         // Bit 6
    BLE_ONBOARDING_STATUS_MAX
} cm2_ble_onboarding_status_t;

typedef enum {
    CM2_PAR_NOT_SET,
    CM2_PAR_TRUE,
    CM2_PAR_FALSE
} cm2_par_state_t;

// misc
bool cm2_is_extender(void);

// event
void cm2_event_init(struct ev_loop *loop);
void cm2_event_close(struct ev_loop *loop);
void cm2_update_state(cm2_reason_e reason);
void cm2_trigger_update(cm2_reason_e reason);
void cm2_ble_onboarding_set_status(bool state, cm2_ble_onboarding_status_t status);
void cm2_ble_onboarding_apply_config(void);
char* cm2_dest_name(cm2_dest_e dest);
char* cm2_curr_dest_name(void);
bool cm2_enable_gw_offline(void);

// ovsdb
int cm2_ovsdb_init(void);
bool cm2_ovsdb_set_Manager_target(char *target);
bool cm2_ovsdb_set_AWLAN_Node_manager_addr(char *addr);
bool cm2_connection_get_used_link(struct schema_Connection_Manager_Uplink *con);
bool cm2_ovsdb_connection_get_connection_by_ifname(const char *if_name,
                                                   struct schema_Connection_Manager_Uplink *con);
void cm2_ovsdb_refresh_dhcp(char *if_name);
bool cm2_ovsdb_set_Wifi_Inet_Config_network_state(bool state, char *ifname);
bool cm2_ovsdb_connection_update_L3_state(const char *if_name, cm2_par_state_t state);
bool cm2_ovsdb_connection_update_ntp_state(const char *if_name, bool state);
bool cm2_ovsdb_connection_update_unreachable_link_counter(const char *if_name, int counter);
bool cm2_ovsdb_connection_update_unreachable_router_counter(const char *if_name, int counter);
bool cm2_ovsdb_connection_update_unreachable_cloud_counter(const char *if_name, int counter);
bool cm2_ovsdb_connection_update_unreachable_internet_counter(const char *if_name, int counter);
int  cm2_ovsdb_ble_config_update(uint8_t ble_status);
int  cm2_ovsdb_ble_set_connectable(bool state);
bool cm2_ovsdb_is_port_name(char *port_name);
void cm2_ovsdb_remove_unused_gre_interfaces(void);
void cm2_ovsdb_connection_update_ble_phy_link(void);
bool cm2_ovsdb_update_Port_tag(const char *ifname, int tag, bool set);
bool cm2_ovsdb_update_Port_trunks(const char *ifname, int *trunks, int num_trunks);
bool cm2_ovsdb_connection_update_loop_state(const char *if_name, bool state);
bool cm2_ovsdb_WiFi_Inet_State_is_ip(const char *if_name);
void cm2_ovsdb_connection_clean_link_counters(char *if_name);
bool cm2_ovsdb_validate_bridge_port_conf(char *bname, char *pname);
bool cm2_ovsdb_is_ipv6_global_link(const char *if_name);
void cm2_ovsdb_set_dhcp_client(const char *if_name, bool enabled);
bool cm2_ovsdb_is_gw_offline_enabled(void);
bool cm2_ovsdb_is_gw_offline_ready(void);
bool cm2_ovsdb_is_gw_offline_active(void);
bool cm2_ovsdb_enable_gw_offline_conf(void);
bool cm2_ovsdb_disable_gw_offline_conf(void);
int  cm2_update_main_link_ip(cm2_main_link_t *link);
int  cm2_ovsdb_update_mac_reporting(char *ifname, bool state);
void cm2_ovsdb_set_default_wan_bridge(char *if_name, char *if_type);
bool cm2_ovsdb_set_Wifi_Inet_Config_interface_enabled(bool state, char *ifname);

#ifdef CONFIG_CM2_USE_EXTRA_DEBUGS
void cm2_ovsdb_dump_debug_data(void);
#else
static inline void cm2_ovsdb_dump_debug_data(void)
{
}
#endif

// addr resolve
cm2_addr_t* cm2_get_addr(cm2_dest_e dest);
cm2_addr_t* cm2_curr_addr(void);
void cm2_free_addrinfo(cm2_addr_t *addr);
void cm2_clear_addr(cm2_addr_t *addr);
bool cm2_parse_resource(cm2_addr_t *addr, cm2_dest_e dest);
bool cm2_set_addr(cm2_dest_e dest, char *resource);
bool cm2_is_addr_resolved(const cm2_addr_t *addr);
#ifndef BUILD_HAVE_LIBCARES
int  cm2_getaddrinfo(char *hostname, struct addrinfo **res, char *msg);
struct addrinfo* cm2_get_next_addrinfo(cm2_addr_t *addr);
#endif
bool cm2_resolve(cm2_dest_e dest);
void cm2_resolve_timeout(void);
bool cm2_write_current_target_addr(void);
bool cm2_write_next_target_addr(void);
void cm2_clear_manager_addr(void);
void cm2_free_addr_list(cm2_addr_t *addr);

// stability and watchdog
#ifdef CONFIG_CM2_USE_STABILITY_CHECK
bool cm2_vtag_stability_check(void);
target_connectivity_check_option_t cm2_util_add_ip_opts(target_connectivity_check_option_t opts);
bool cm2_connection_req_stability_check(target_connectivity_check_option_t opts, bool db_update);
void cm2_connection_req_stability_check_async(target_connectivity_check_option_t opts, bool db_update, bool repeat);
void cm2_stability_init(struct ev_loop *loop);
void cm2_stability_close(struct ev_loop *loop);
#ifdef CONFIG_CM2_USE_TCPDUMP
void cm2_tcpdump_start(char* ifname);
void cm2_tcpdump_stop(char* ifname);
#else
static inline void cm2_tcpdump_start(char* ifname)
{
}
static inline void cm2_tcpdump_stop(char* ifname)
{
}
#endif /* CONFIG_CM2_USE_TCPDUMP */
#else
static inline bool cm2_vtag_stability_check(void)
{
    return true;
}
static inline void cm2_stability_init(struct ev_loop *loop)
{
}
static inline void cm2_stability_close(struct ev_loop *loop)
{
}
static inline void cm2_connection_req_stability_check(target_connectivity_check_option_t opts, bool db_update)
{
}
#endif /* CONFIG_CM2_USE_STABILITY_CHECK */


#ifdef CONFIG_CM2_USE_WDT
void cm2_wdt_init(struct ev_loop *loop);
void cm2_wdt_close(struct ev_loop *loop);
#else
static inline void cm2_wdt_init(struct ev_loop *loop)
{
}
static inline void cm2_wdt_close(struct ev_loop *loop)
{
}
#endif /* CONFIG_CM2_USE_WDT */

static inline bool cm2_is_wan_bridge(void)
{
#ifdef CONFIG_TARGET_USE_WAN_BRIDGE
    return true;
#else
    return false;
#endif
}

static inline bool cm2_is_wan_link_management(void)
{
#ifdef CONFIG_CM2_USE_WAN_LINK_MANAGEMENT
    return true;
#else
    return false;
#endif
}

static inline bool cm2_is_config_via_ble_enabled(void)
{
#ifdef CONFIG_PLUME_CONFIG_VIA_BLE_ENABLED
    return true;
#else
    return false;
#endif
}

// net
void cm2_update_bridge_cfg(char *bridge, char *port, bool brop,
                           cm2_par_state_t state, bool dhcp_update);
void cm2_dhcpc_start_dryrun(char* ifname, char *iftype, int cnt);
void cm2_dhcpc_stop_dryrun(char* ifname);
bool cm2_is_eth_type(const char *if_type);
bool cm2_is_wifi_type(const char *if_type);
void cm2_delayed_eth_update(char *if_name, int timeout);
bool cm2_is_iface_in_bridge(const char *bridge, const char *port);
char* cm2_get_uplink_name(void);
void cm2_update_device_type(const char *iftype);
bool cm2_ovsdb_set_dhcpv6_client(char *ifname, bool enable);
bool cm2_osn_is_ipv6_global_link(const char *ifname, const char *ipv6_addr);
void cm2_restart_iface(char *ifname);
#endif /* CM2_H_INCLUDED */
