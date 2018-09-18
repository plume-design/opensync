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

#ifndef TARGET_COMMON_H_INCLUDED
#define TARGET_COMMON_H_INCLUDED

#include "dppline.h"
#include "ds_dlist.h"

#include "schema.h"

/**
 * @file target_common.h
 * @brief Additional target api header
 *
 * The declarations in this header depend on the platform specific declaration from header TARGET_H,
 * that is why it is separated from base target.h
 * \addtogroup libtarget
 * @{
 */

typedef struct {
    struct schema_Wifi_Radio_Config rconf;
    ds_dlist_t                      vifs_cfg;
    ds_dlist_node_t                 dsl_node;
} target_radio_cfg_t;

typedef struct {
    struct schema_Wifi_VIF_Config   vconf;
    ds_dlist_node_t                 dsl_node;
} target_vif_cfg_t;

typedef struct {
    struct schema_Wifi_Inet_Config  iconfig;
    ds_dlist_node_t                 dsl_node;
} target_inet_config_init_t;

typedef struct {
    struct schema_Wifi_Inet_State   istate;
    ds_dlist_node_t                 dsl_node;
} target_inet_state_init_t;

typedef struct {
    struct schema_Wifi_Master_State mstate;
    ds_dlist_node_t                 dsl_node;
} target_master_state_init_t;


/* Target capabilities types */
#define TARGET_GW_TYPE       (1 << 0)
#define TARGET_EXTENDER_TYPE (1 << 1)

typedef struct {
    bool link_state;
    bool router_state;
    bool internet_state;
    bool ntp_state;
} target_connectivity_check_t;

/**
 * @op_vconf target calls this whenever middelware (if exists) wants to
 *	update vif configuration
 * @op_rconf target calls this whenever middleware (if exists) wants to
 *	update radio configuration
 * @op_vstate target calls this whenever system vif state has changed,
 *	e.g. channel changed, target_vif_config_set2() was called
 * @op_rstate target calls this whenever system vif state has changed,
 *	e.g. channel changed, target_radio_config_set2() was called
 * @op_client target calls this whenever a client connects or
 *	disconnects
 * @op_clients target calls this whenever it wants to re-sync all clients due
 *	to, e.g. internal event buffer overrun.
 * @op_flush_clients target calls this whenever it wants to clear out
 *	all clients on a given vif; intended to use when target wants to
 *	fully re-sync connects clients (i.e. the call will be followed
 *	by op_client() calls) or when a vif is deconfigured abruptly
 */
struct target_radio_ops {
    void (*op_vconf)(const struct schema_Wifi_VIF_Config *vconf,
                     const char *phy);
    void (*op_rconf)(const struct schema_Wifi_Radio_Config *rconf);
    void (*op_vstate)(const struct schema_Wifi_VIF_State *vstate);
    void (*op_rstate)(const struct schema_Wifi_Radio_State *rstate);
    void (*op_client)(const struct schema_Wifi_Associated_Clients *client,
                      const char *vif,
                      bool associated);
    void (*op_clients)(const struct schema_Wifi_Associated_Clients *clients,
                       int num,
                       const char *vif);
    void (*op_flush_clients)(const char *vif);
};

/******************************************************************************
 *  RADIO definitions
 *****************************************************************************/

/**
 * @brief Hands over WM callbacks so target can notify about vif/radio statuses
 *
 * Target implementation is expected to notify WM about things like channel
 * changes, configuration being applied, clients connecting and disconnecting,
 * etc. via provided callbacks.
 *
 * Target implementation is free to perform early bookkeeping initialization,
 * e.g. open up sockets to middleware HAL API it talks to, etc.
 *
 * @return true if target is okay. False if it could not initialize. False
 * results in WM using old target API currently. In the future WM will refuse
 * to start if False is returned.
 */
bool target_radio_init(const struct target_radio_ops *ops);

/**
 * @brief Initialize radio interfaces config
 *
 * Initialize the target library radio configuration layer and return a list
 * of currently configured radio interfaces. init_cfg is a double linked list
 * of target_radio_cfg_t structures. This list is used to pre-populate
 * the Wifi_Radio_Config table.
 *
 * @note
 * The init_cfg linked list is dynamically allocated, it must be freed by the caller.
 *
 * @param init_cfg linked list of radio interfaces config (target_radio_cfg_t)
 * @return true on success
 */
bool target_radio_config_init(ds_dlist_t *init_cfg);

/**
 * @brief Initialize radio interface configuration
 *
 * This is called during WM initialization only if
 * target_radio_config_need_reset() is true.
 *
 * This is expected to call op_rconf and op_vconf with initial radio/vif
 * configuration parameters.
 *
 * This is intended to handle residential gateways / systems with middleware
 * HAL that can take control over ovsdb.
 *
 * @return true on success.
 */
bool target_radio_config_init2(void);

/**
 * @brief Target tells if it requires full re-sync with Config/State
 *
 * If target implementation talks with a middleware HAL that can sometimes take
 * control over Plume cloud then this function should return true whenever
 * middleware is supposed to be in charge of the wireless configuration.
 *
 * When true target is expected to call op_vconf and op_rconf during
 * target_radio_config_init2().
 *
 * @return true if middleware exists and target wants
 * target_radio_config_init2() to be called.
 */
bool target_radio_config_need_reset(void);

/**
 * @brief Apply the configuration for the radio interface
 *
 * The interface ifname must already exist on the system.
 *
 * @param ifname interface name
 * @param rconf radio interface config
 * @return true on success
 */
bool target_radio_config_set (char *ifname, struct schema_Wifi_Radio_Config *rconf);

/**
 * @brief Apply the configuration for the radio interface
 *
 * This is API v2. Will be called only if target_radio_init() returned
 * true during init.
 *
 * @param rconf complete desired radio config
 * @param changed list of fields from rconf that are out of sync with
 * regard to rstate
 * @return true on success, false means the call will be retried later
 */
bool target_radio_config_set2(const struct schema_Wifi_Radio_Config *rconf,
                              const struct schema_Wifi_Radio_Config_flags *changed);

/**
 * @brief Get state of radio interface
 *
 * This function is used to retrieve the current state of a radio interface
 *
 * @note
 * Depending on the implementation, some of the returned values in rstate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param rstate output; radio interface state
 * @return true on success
 */
bool target_radio_state_get(char *ifname, struct schema_Wifi_Radio_State *rstate);

/**
 * @brief Subscribe to radio interface state change events.
 *
 * @note
 * The interface state is typically polled
 *
 * @param ifname interface name
 * @param radio_state_cb a callback function
 * @return true on success
 */
typedef void target_radio_state_cb_t(struct schema_Wifi_Radio_State *rstate, schema_filter_t *filter);
bool target_radio_state_register(char *ifname, target_radio_state_cb_t *radio_state_cb);

/**
 * @brief Subscribe to radio interface config change events.
 *
 * @note
 * The interface state is typically polled
 *
 * @param ifname interface name
 * @param radio_config_cb a callback function
 * @return true on success
 */
typedef void target_radio_config_cb_t(struct schema_Wifi_Radio_Config *rconf, schema_filter_t *filter);
bool target_radio_config_register(char *ifname, target_radio_config_cb_t *radio_config_cb);

/******************************************************************************
 *  VIF definitions
 *****************************************************************************/

/**
 * @brief Apply the configuration for the vif interface
 *
 * @param ifname interface name
 * @param vconf vif interface config
 * @return true on success
 */
bool target_vif_config_set (char *ifname, struct schema_Wifi_VIF_Config *vconf);

/**
 * @brief Apply the configuration for the vif interface
 *
 * @param vconf complete desired vif config
 * @param rconf complete desired radio config
 * @param cconfs complete desired vif credential config, used for
 * extender mode to provide multiple network for sta vif
 * @param changed list of fields from vconf that are out of sync with
 * state
 * @param num_cconfs number of cconfs entries
 * @return true on success, false means the call will be retried later
 */
bool target_vif_config_set2(const struct schema_Wifi_VIF_Config *vconf,
                            const struct schema_Wifi_Radio_Config *rconf,
                            const struct schema_Wifi_Credential_Config *cconfs,
                            const struct schema_Wifi_VIF_Config_flags *changed,
                            int num_cconfs);

/**
 * @brief Get state of vif interface
 *
 * This function is used to retrieve the current state of a vif interface
 *
 * @note
 * Depending on the implementation, some of the returned values in vstate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param vstate output; vif interface state
 * @return true on success
 */
bool target_vif_state_get(char *ifname, struct schema_Wifi_VIF_State *vstate);

/**
 * @brief Subscribe to vif interface state change events.
 *
 * @note
 * The interface state is typically polled
 *
 * @param ifname interface name
 * @param vstate_cb a callback function
 * @return true on success
 */
typedef void target_vif_state_cb_t(struct schema_Wifi_VIF_State *rstate, schema_filter_t *filter);
bool target_vif_state_register(char *ifname, target_vif_state_cb_t *vstate_cb);

/**
 * @brief Subscribe to vif interface config change events.
 *
 * @note
 * The interface state is typically polled
 *
 * @param ifname interface name
 * @param vconfig_cb a callback function
 * @return true on success
 */
typedef void target_vif_config_cb_t(struct schema_Wifi_VIF_Config *vconf, schema_filter_t *filter);
bool target_vif_config_register(char *ifname, target_vif_config_cb_t *vconfig_cb);

/******************************************************************************
 *  DHCP definitions
 *****************************************************************************/
typedef bool target_dhcp_leased_ip_cb_t(struct schema_DHCP_leased_IP *dlip);
bool target_dhcp_leased_ip_get(struct schema_DHCP_leased_IP *dlip);
bool target_dhcp_leased_ip_register(target_dhcp_leased_ip_cb_t *dlip_cb);
bool target_dhcp_rip_set(const char *ifname, struct schema_DHCP_reserved_IP *schema_rip);
bool target_dhcp_rip_del(const char *ifname, struct schema_DHCP_reserved_IP *schema_rip);

/******************************************************************************
 *  CLIENTS definitions
 *****************************************************************************/

/**
 * @brief Subscribe to client change events.
 *
 * @param ifname interface name
 * @param vconfig_cb a callback function
 * @return true on success
 */
typedef bool target_clients_cb_t(struct schema_Wifi_Associated_Clients *schema, char *ifname, bool status);
bool target_clients_register(char *ifname, target_clients_cb_t *clients_cb);

/******************************************************************************
 *  INET definitions
 *****************************************************************************/
/**
 * @brief Initialize network interfaces state
 *
 * Initialize the target library network interface state layer and return a list of
 * currently configured network interfaces. inet_ovs is a double linked list of
 * target_inet_state_init_t structures. This list is used to pre-populate the
 * Wifi_Inet_State table.
 *
 * @note
 * Only interfaces that are significant for system operation must be returned
 * by this function. Interfaces such as "lo" can be safely ignored.
 * @note
 * the inet_ovs linked list is dynamically allocated, it must be freed by the caller.
 *
 * @param inets_ovs linked list of inet interfaces state (target_inet_state_init_t)
 * @return true on success
 */
bool target_inet_state_init(ds_dlist_t *inets_ovs);

/**
 * @brief Initialize master state
 *
 * Initialize the target library master state layer and return a list of
 * currently configured network interfaces. inet_ovs is a double linked list of
 * target_master_state_init_t structures. This list is used to pre-populate the
 * Wifi_Master_State table.
 *
 * @note
 * Only interfaces that are significant for system operation must be returned
 * by this function. Interfaces such as "lo" can be safely ignored.
 * @note
 * the inet_ovs linked list is dynamically allocated, it must be freed by the caller.
 *
 * @param inets_ovs linked list of inet interfaces state (target_inet_state_init_t)
 * @return true on success
 */
bool target_master_state_init(ds_dlist_t *inets_ovs);

/**
 * @brief Initialize network interfaces config
 *
 * Initialize the target library network configuration layer and return a list
 * of currently configured network interfaces. inet_ovs is a double linked list
 * of target_inet_config_init_t structures. This list is used to pre-populate
 * the Wifi_Inet_Config table.
 *
 * @note
 * Only interfaces that are significant for system operation must be returned
 * by this function. Interfaces such as "lo" can be safely ignored.
 * @note
 * The inet_ovs linked list is dynamically allocated; it must be freed by the caller.
 *
 * @param inets_ovs linked list of inet interfaces config (target_inet_config_init_t)
 * @return true on success
 */
bool target_inet_config_init(ds_dlist_t *inets_ovs);


/**
 * @brief Apply the network configuration for interface
 *
 * Apply the network configuration for interfaces of type "eth" and "vif". The
 * interface ifname must already exist on the system.
 *
 * @param ifname interface name
 * @param iconf interface config
 * @return true on success
 */
bool target_vif_inet_config_set(char *ifname,
        struct schema_Wifi_Inet_Config *iconf);


/**
 * @brief Get state of vif network interface
 *
 * This function is used to retrieve the current state of network interface of
 * type "vif".
 *
 * @note
 * Depending on the implementation, some of the returned values in istate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface state
 * @return true on success
 */
bool target_vif_inet_state_get(char *ifname,
        struct schema_Wifi_Inet_State *istate);

/**
 * @brief Get state of vif network interface
 *
 * This function is used to retrieve the current master state of network interface
 * of type "vif".
 *
 * @note
 * Depending on the implementation, some of the returned values in mstate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface mater state
 * @return true on success
 */
bool target_vif_master_state_get(const char *ifname,
        struct schema_Wifi_Master_State *mstate);

/**
 * @brief Apply the network configuration for bridge interface
 *
 * Apply the network configuration for interface of type "bridge". Interface
 * is created if it does not exists.
 *
 * @param ifname interface name
 * @param iconf interface config
 * @return true on success
 */
bool target_bridge_inet_config_set(char *ifname,
        struct schema_Wifi_Inet_Config *iconf);

/**
 * @brief Get state of bridge network interface
 *
 * This function is used to retrieve the current state of network interface of
 * type "bridge".
 *
 * @note
 * Depending on the implementation, some of the returned values in istate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface state
 * @return true on success
 */
bool target_bridge_inet_state_get(char *ifname,
        struct schema_Wifi_Inet_State *istate);

/**
 * @brief Get master state of bridge network interface
 *
 * This function is used to retrieve the current master state of network
 * interface of type "bridge".
 *
 * @note
 * Depending on the implementation, some of the returned values in mstate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface state
 * @return true on success
 */
bool target_bridge_master_state_get(const char *ifname,
        struct schema_Wifi_Master_State *mstate);

/**
 * @brief Apply the network configuration for gre interface
 *
 * Apply the network configuration for interface of type "gre". Interface
 * is created if it does not exists. The gre_remote_inet_addr,
 * gre_local_inet_addr and gre_ifname are required fields.
 *
 * @param ifname interface name
 * @param remote_ip the remote IP of the tunnel
 * @param iconf interface config
 * @return true on success
 */
bool target_gre_inet_config_set(char *ifname, char *remote_ip,
        struct schema_Wifi_Inet_Config *iconf);

/**
 * @brief Get state of GRE network interface
 *
 * This function is used to retrieve the current state of network interface of
 * type "GRE".
 * @note
 * Depending on the implementation, some of the returned values in istate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param remote_ip the remote IP of the tunnel
 * @param istate output; interface state
 * @return true on success
 */
bool target_gre_inet_state_get(char *ifname,  char *remote_ip,
        struct schema_Wifi_Inet_State *istate);

/**
 * @brief Get mater state of GRE network interface
 *
 * This function is used to retrieve the current master state of network interface
 * of type "GRE".
 * @note
 * Depending on the implementation, some of the returned values in istate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param remote_ip the remote IP of the tunnel
 * @param istate output; interface state
 * @return true on success
 */
bool target_gre_master_state_get(const char *ifname, const char *remote_ip,
        struct schema_Wifi_Master_State *mstate);

/**
 * @brief Apply the network configuration for vlan interface
 *
 * Apply the network configuration for interface of type "vlan". Interface
 * is created if it does not exists. The parent_ifname and vlan_id  are
 * required fields.
 *
 * @param ifname interface name
 * @param iconf interface config
 * @return true on success
 */
bool target_vlan_inet_config_set(char *ifname,
        struct schema_Wifi_Inet_Config *iconf);

/**
 * @brief Get state of vlan network interface
 *
 * This function is used to retrieve the current state of network interface of
 * type "vlan".
 *
 * @note
 * Depending on the implementation, some of the returned values in istate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface state
 * @return true on success
 */
bool target_vlan_inet_state_get(char *ifname,
        struct schema_Wifi_Inet_State *istate);

/**
 * @brief Get master state of vlan network interface
 *
 * This function is used to retrieve the current state of network interface of
 * type "vlan".
 *
 * @note
 * Depending on the implementation, some of the returned values in mstate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface state
 * @return true on success
 */
bool target_vlan_master_state_get(const char *ifname,
        struct schema_Wifi_Master_State *mstate);

/**
 * @brief Apply the network configuration for tap interface
 *
 * Apply the network configuration for interface of type "tap".
 *
 * @param ifname interface name
 * @param iconf interface config
 * @return true on success
 */
bool target_tap_inet_config_set(char *ifname,
        struct schema_Wifi_Inet_Config *iconf);

/**
 * @brief Get state of vlan network interface
 *
 * This function is used to retrieve the current state of network interface of
 * type "tap".
 *
 * @note
 * Depending on the implementation, some of the returned values in istate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface state
 * @return true on success
 */
bool target_tap_inet_state_get(char *ifname,
        struct schema_Wifi_Inet_State *istate);

/**
 * @brief Get master state of vlan network interface
 *
 * This function is used to retrieve the current state of network interface of
 * type "tap".
 *
 * @note
 * Depending on the implementation, some of the returned values in mstate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface state
 * @return true on success
 */
bool target_tap_master_state_get(const char *ifname,
        struct schema_Wifi_Master_State *mstate);

/**
 * @brief Apply the network configuration for ethernet interface
 *
 * Apply the network configuration for interface of type "eth"
 *
 * @param ifname interface name
 * @param iconf interface config
 * @return true on success
 */
bool target_eth_inet_config_set(const char *ifname,
        struct schema_Wifi_Inet_Config *iconf);

/**
 * @brief Get state of eth network interface
 *
 * This function is used to retrieve the current state of network interface of
 * type "eth".
 *
 * @note
 * Depending on the implementation, some of the returned values in istate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface state
 * @return true on success
 */
bool target_eth_inet_state_get(const char *ifname,
        struct schema_Wifi_Inet_State *istate);

/**
 * @brief Get master state of eth network interface
 *
 * This function is used to retrieve the current state of network interface of
 * type "eth".
 *
 * @note
 * Depending on the implementation, some of the returned values in istate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface state
 * @return true on success
 */
bool target_eth_master_state_get(const char *ifname,
        struct schema_Wifi_Master_State *mstate);

/**
 * @brief Get state of ppp network interface
 *
 * This function is used to retrieve the current state of network interface of
 * type "ppp".
 *
 * @note
 * Depending on the implementation, some of the returned values in istate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface state
 * @return true on success
 */
bool target_ppp_inet_state_get(char *ifname,
        struct schema_Wifi_Inet_State *istate);

/**
 * @brief Get the master state of ppp network interface
 *
 * This function is used to retrieve the current state of network interface of
 * type "ppp".
 *
 * @note
 * Depending on the implementation, some of the returned values in mstate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param istate output; interface state
 * @return true on success
 */
bool target_ppp_master_state_get(const char *ifname,
        struct schema_Wifi_Master_State *mstate);

/**
 * @brief Subscribe to network state change events.
 *
 * This function is used to subscribe to network state change events.
 *
 * @note
 * The interface state is typically polled
 *
 * @param ifname interface name
 * @param istate_cb a callback function of type void callback(struct
 * schema_Wifi_Inet_State *istate, schema_filter_t *filter);
 * @return true on success
 */
bool target_inet_state_register(char *ifname, void *istate_cb);

/**
 * @brief Subscribe to network master state change events.
 *
 * This function is used to subscribe to network state change events.
 *
 * @note
 * The interface state is typically polled
 *
 * @param ifname interface name
 * @param istate_cb a callback function of type
 *   void callback(struct schema_Wifi_Master_State *mstate);
 * @return true on success
 */
typedef void target_master_state_cb_t(struct schema_Wifi_Master_State *mstate);
bool target_master_state_register(const char *ifname, target_master_state_cb_t *mstate_cb);

/******************************************************************************
 *  STATS definitions
 *****************************************************************************/

/**
 * @brief Enable radio tx stats
 * @param radio_cfg radio interface handle
 * @param status true (enable) or false (disable)
 * @return true on success
 */
bool target_radio_tx_stats_enable(
        radio_entry_t              *radio_cfg,
        bool                        status);

/**
 * @brief Enable radio fast scan
 * @param radio_cfg radio interface handle
 * @param if_name radio interface name
 * @return true on success
 */
bool target_radio_fast_scan_enable(
        radio_entry_t              *radio_cfg,
        ifname_t                    if_name);

/******************************************************************************
 *  CLIENT definitions
 *****************************************************************************/
target_client_record_t *target_client_record_alloc();
void target_client_record_free(target_client_record_t *record);

typedef bool target_stats_clients_cb_t (
        ds_dlist_t                 *client_list,
        void                       *ctx,
        int                         status);

/**
 * @brief Get clients stats
 *
 * The results will be provided to the callback function and can be called
 * either synchronously or asynchronously depending on platform specifics
 *
 * @param radio_cfg radio interface handle
 * @param essid SSID string
 * @param client_cb callback function
 * @param client_list output; resulting client list
 * @param client_ctx optional context for callback
 * @return true on success
 */
bool target_stats_clients_get (
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        target_stats_clients_cb_t  *client_cb,
        ds_dlist_t                 *client_list,
        void                       *client_ctx);

/**
 * @brief Calculate client stats deltas
 *
 * Calculates the deltas between new and old client list and stores the result
 * into client_record
 *
 * @param radio_cfg radio interface handle
 * @param client_list_new new values
 * @param client_list_old old values
 * @param client_record output; calculated deltas
 * @return true on success
 */
bool target_stats_clients_convert (
        radio_entry_t              *radio_cfg,
        target_client_record_t     *client_list_new,
        target_client_record_t     *client_list_old,
        dpp_client_record_t        *client_record);

/******************************************************************************
 *  SURVEY definitions
 *****************************************************************************/
target_survey_record_t *target_survey_record_alloc();
void target_survey_record_free(target_survey_record_t *record);

typedef bool target_stats_survey_cb_t (
        ds_dlist_t                 *survey_list,
        void                       *survey_ctx,
        int                         status);

/**
 * @brief Get radio channel survey stats
 *
 * The results will be provided to the callback function and can be called
 * either synchronously or asynchronously depending on platform specifics
 *
 * @param radio_cfg radio interface handle
 * @param chan_list list of channels
 * @param chan_num  number of channels in list
 * @param scan_type scan type
 * @param survey_cb callbnack function
 * @param survey_list output; survey stats
 * @param survey_ctx optional context for callback
 * @return true on success
 */
bool target_stats_survey_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        target_stats_survey_cb_t   *survey_cb,
        ds_dlist_t                 *survey_list,
        void                       *survey_ctx);

/**
 * @brief Calculate channel survey deltas
 *
 * Calculates the deltas between new and old channel survey and stores the result
 * into survey_record
 *
 * @param radio_cfg radio interface handle
 * @param scan_type scan type
 * @param data_new  new values
 * @param data_old  old values
 * @param survey_record output; calculated deltas
 * @return true on success
 */
bool target_stats_survey_convert (
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type,
        target_survey_record_t     *data_new,
        target_survey_record_t     *data_old,
        dpp_survey_record_t        *survey_record);

/******************************************************************************
 *  NEIGHBOR definitions
 *****************************************************************************/
typedef bool target_scan_cb_t(
        void                       *scan_ctx,
        int                         status);

/**
 * @brief Start neighbor scan
 *
 * The scanning will be performed in background and the callback function will
 * be called when the results are available. The actual results need to be
 * fetched with target_stats_scan_get()
 *
 * @param radio_cfg  radio interface handle
 * @param chan_list  channel list
 * @param chan_num   number of channels
 * @param scan_type  scan type
 * @param dwell_time dwell time in ms
 * @param scan_cb    callback function
 * @param scan_ctx   optional context for callback
 * @return true on success
 */
bool target_stats_scan_start (
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        int32_t                     dwell_time,
        target_scan_cb_t           *scan_cb,
        void                       *scan_ctx);

/**
 * @brief Stop neighbor scan
 *
 * @param radio_cfg  radio interface handle
 * @param scan_type  scan type
 * @return true on success
 */
bool target_stats_scan_stop (
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type);

/**
 * @brief Get neighbor stats
 *
 * @param radio_cfg  radio interface handle
 * @param chan_list  channel list
 * @param chan_num   number of channels
 * @param scan_type  scan type
 * @param scan_results output; neighbor stats
 * @return true on success
 */
bool target_stats_scan_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        dpp_neighbor_report_data_t *scan_results);

/******************************************************************************
 *  DEVICE definitions
 *****************************************************************************/

/**
 * @brief Get device stats
 *
 * Returns device load average (loadavg) and uptime
 *
 * @param device_entry output; device stats
 * @return true on success
 */
bool target_stats_device_get(
        dpp_device_record_t        *device_entry);

/**
 * @brief Get device temperature
 *
 * @param radio_cfg radio interface handle
 * @param device_entry output; device stats
 * @return true on success
 */
bool target_stats_device_temp_get(
        radio_entry_t              *radio_cfg,
        dpp_device_temp_t          *device_entry);

/**
 * @brief Get device txchainmask
 *
 * @param radio_cfg radio interface handle
 * @param txchainmask_entry; txchainmask of device
 * @return true on success
 */

bool target_stats_device_txchainmask_get(
        radio_entry_t              *radio_cfg,
        dpp_device_txchainmask_t   *txchainmask_entry);

/**
 * @brief Get device fan RPM  
 *
 * @param fan_rpm; RPM of the internal fan
 * @return true on success
 */

bool target_stats_device_fanrpm_get(uint32_t *fan_rpm);


/******************************************************************************
 *  CAPACITY definitions
 *****************************************************************************/

/**
 * @brief obsolete: capacity stats
 * @return true on success
 */
bool target_stats_capacity_enable(
        radio_entry_t              *radio_cfg,
        bool                        enabled);

/**
 * @brief obsolete: capacity stats
 * @return true on success
 */
bool target_stats_capacity_get (
        radio_entry_t              *radio_cfg,
        target_capacity_data_t     *capacity_new);

/**
 * @brief obsolete: capacity stats
 * @return true on success
 */
bool target_stats_capacity_convert(
        target_capacity_data_t     *capacity_new,
        target_capacity_data_t     *capacity_old,
        dpp_capacity_record_t      *capacity_entry);

/******************************************************************************
 *  SERVICE definitions
 *****************************************************************************/

/**
 * @brief Subscribe to changes of device config
 *
 * This is for changes of device config that originate from external management
 * protocols not ovsdb. The changes will then be applied to ovsdb by the callback.
 *
 * callback type: void (*update)(struct schema_AWLAN_Node *awlan,
 *   schema_filter_t *filter);
 *
 * @param awlan_cb callback function
 * @return true on success
 */
bool target_device_config_register(void *awlan_cb);

/**
 * @brief Apply device config
 *
 * This applies device config from ovsdb to external management protocols (if available).
 *
 * @param awlan_cb callback function
 * @return true on success
 */
bool target_device_config_set(struct schema_AWLAN_Node *awlan);

/**
 * @brief Execute external tools
 * @param cmd command string
 * @return true on success
 */
bool target_device_execute(const char* cmd);

/**
 * @brief Get device capabilities
 * @return device capabilities as a bitmask based on target capabilities types
 */
int target_device_capabilities_get();

/**
 * @brief Get device connectivity status
 * @param ifname - interface name
 * @param cstate - connectivity state
 * @return true when link is health
*/
bool target_device_connectivity_check(const char *ifname,
                                      target_connectivity_check_t *cstate);

/**
 *  @brief Restart plume managers
 *  @return true on success
 */
bool target_device_restart_managers();

/**
 *  @brief Ping watchdog system
 *  @return true on success
 */
bool target_device_wdt_ping();

/******************************************************************************
 *  INTERFACE NAME MAP definitions
 *****************************************************************************/
char *target_map_ifname(char *ifname);

/******************************************************************************
 *  MAC LEARNING definitions
 *****************************************************************************/
bool target_mac_learning_register(void * omac_cb);

/******************************************************************************
 *  PLATFORM SPECIFIC definitions
 *****************************************************************************/

/******************************************************************************
 *  CLIENT NICKNAME definitions
 *****************************************************************************/
typedef bool target_client_nickname_cb_t (
         struct schema_Client_Nickname_Config *cncfg,
         bool                                 status);

bool target_client_nickname_register(target_client_nickname_cb_t *nick_cb);
bool target_client_nickname_set(struct schema_Client_Nickname_Config *cncfg);

/******************************************************************************
 *  CLIENT FREEZE definitions
 *****************************************************************************/
typedef bool target_client_freeze_cb_t (
         struct schema_Client_Freeze_Config *cfcfg,
         bool                                status);

bool target_client_freeze_register(target_client_freeze_cb_t *freze_cb);
bool target_client_freeze_set(struct schema_Client_Freeze_Config *cfcfg);

/**@}*/

#endif /* TARGET_COMMON_H_INCLUDED */
