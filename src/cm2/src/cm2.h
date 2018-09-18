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

#define IFNAME_SIZE 128 + 1
#define IFTYPE_SIZE 128 + 1

#define VIF_TYPE_NAME    "vif"
#define ETH_TYPE_NAME    "eth"
#define GRE_TYPE_NAME    "gre"
#define BRIDGE_TYPE_NAME "bridge"
#define BR_WAN_NAME      "br-wan"

#define CM2_DEFAULT_OVS_MAX_BACKOFF   60
#define CM2_DEFAULT_OVS_MIN_BACKOFF   30
#define CM2_DEFAULT_OVS_FAST_BACKOFF  8

typedef enum
{
    CM2_LINK_NOT_DEFINED,
    CM2_LINK_ETH_BRIDGE,
    CM2_LINK_ETH_ROUTER,
    CM2_LINK_GRE,
} cm2_main_link_type;

typedef enum
{
    CM2_STATE_INIT,
    CM2_STATE_LINK_SEL,    // EXTENDER only
    CM2_STATE_WAN_IP,      // EXTENDER only
    CM2_STATE_OVS_INIT,
    CM2_STATE_TRY_RESOLVE,
    CM2_STATE_RE_CONNECT,
    CM2_STATE_TRY_CONNECT,
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

typedef struct
{
    bool updated;
    bool valid;
    bool resolved;
    char resource[CM2_RESOURCE_MAX];
    char proto[CM2_PROTO_MAX];
    char hostname[CM2_HOSTNAME_MAX];
    int  port;
    struct addrinfo *ai_list;
    struct addrinfo *ai_curr;
} cm2_addr_t;

typedef struct
{
    char if_name[IFNAME_SIZE];
    char if_type[IFTYPE_SIZE];
    bool has_L3;
    bool is_used;
    int  priority;
    bool is_ip;
    bool gretap_softwds;
} cm2_main_link_t;

typedef struct
{
    cm2_state_e       state;
    cm2_reason_e      reason;
    cm2_dest_e        dest;
    bool              state_changed;
    bool              connected;
    time_t            timestamp;
    int               disconnects;
    cm2_addr_t        addr_redirector;
    cm2_addr_t        addr_manager;
    ev_timer          timer;
    ev_timer          wdt_timer;
    ev_timer          stability_timer;
    bool              run_stability;
    cm2_main_link_t   link;
    uint8_t           ble_status;
    bool              ntp_check;

    struct ev_loop *loop;
    bool have_manager;
    bool have_awlan;
    int min_backoff;
    int max_backoff;
    bool fast_backoff;
    int target_type;
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

// misc
bool cm2_is_extender();

// event
void cm2_event_init(struct ev_loop *loop);
void cm2_event_close(struct ev_loop *loop);
void cm2_update_state(cm2_reason_e reason);
void cm2_trigger_update(cm2_reason_e reason);
void cm2_ble_onboarding_set_status(bool state, cm2_ble_onboarding_status_t status);
void cm2_ble_onboarding_apply_config();
char* cm2_dest_name(cm2_dest_e dest);
char* cm2_curr_dest_name();

// ovsdb
int cm2_ovsdb_init(void);
bool cm2_ovsdb_set_Manager_target(char *target);
bool cm2_ovsdb_set_AWLAN_Node_manager_addr(char *addr);
bool cm2_connection_get_used_link(struct schema_Connection_Manager_Uplink *con);
bool cm2_ovsdb_connection_get_connection_by_ifname(const char *if_name,
                                                   struct schema_Connection_Manager_Uplink *con);
bool cm2_ovsdb_set_dhcp(char *ifname, bool state);
bool cm2_ovsdb_set_Wifi_Inet_Config_network_state(bool state, char *ifname);
bool cm2_ovsdb_connection_update_L3_state(const char *if_name, bool state);
bool cm2_ovsdb_connection_update_ntp_state(const char *if_name, bool state);
bool cm2_ovsdb_connection_update_unreachable_link_counter(const char *if_name, int counter);
bool cm2_ovsdb_connection_update_unreachable_router_counter(const char *if_name, int counter);
bool cm2_ovsdb_connection_update_unreachable_cloud_counter(const char *if_name, int counter);
bool cm2_ovsdb_connection_update_unreachable_internet_counter(const char *if_name, int counter);
int  cm2_ovsdb_ble_config_update(uint8_t ble_status);
bool cm2_ovsdb_is_port_name(char *port_name);
void cm2_ovsdb_remove_unused_gre_interfaces();

// addr resolve
cm2_addr_t* cm2_get_addr(cm2_dest_e dest);
cm2_addr_t* cm2_curr_addr();
void cm2_free_addrinfo(cm2_addr_t *addr);
void cm2_clear_addr(cm2_addr_t *addr);
bool cm2_parse_resource(cm2_addr_t *addr, cm2_dest_e dest);
bool cm2_set_addr(cm2_dest_e dest, char *resource);
int  cm2_getaddrinfo(char *hostname, struct addrinfo **res, char *msg);
bool cm2_resolve(cm2_dest_e dest);
struct addrinfo* cm2_get_next_addrinfo(cm2_addr_t *addr);
bool cm2_write_target_addr(cm2_addr_t *addr, struct addrinfo *ai);
bool cm2_write_current_target_addr();
bool cm2_write_next_target_addr();
void cm2_clear_manager_addr();

// stability and watchdog
void cm2_connection_stability_check();
void cm2_stability_init(struct ev_loop *loop);
void cm2_stability_close(struct ev_loop *loop);
void cm2_wdt_init(struct ev_loop *loop);
void cm2_wdt_close(struct ev_loop *loop);

// net
int cm2_ovs_insert_port_into_bridge(char *bridge, char *port, int flag_add);
void cm2_dhcpc_dryrun(char* ifname, bool background);

#endif
