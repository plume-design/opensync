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

#ifndef WANO_H_INCLUDED
#define WANO_H_INCLUDED

/*
 * ===========================================================================
 *  WANO Public API -- primarily used by WANO plugins
 * ===========================================================================
 */

#include <stdbool.h>
#include <stdint.h>

#include "const.h"
#include "ds_tree.h"
#include "osn_types.h"
#include "reflink.h"
#include "util.h"

#include "wano_ppline_stam.h"

/*
 * ===========================================================================
 *  WANO Plug-in API
 * ===========================================================================
 */

struct wano_plugin;
struct wano_plugin_handle;
struct wano_plugin_iter;

#define WANO_PLUGIN_MASK_L2         (1 << 0)
#define WANO_PLUGIN_MASK_IPV4       (1 << 1)
#define WANO_PLUGIN_MASK_IPV6       (1 << 2)
#define WANO_PLUGIN_MASK_ALL        UINT64_MAX

typedef struct wano_plugin_handle wano_plugin_handle_t;
typedef struct wano_plugin_iter wano_plugin_iter_t;
struct wano_plugin_status;

typedef void wano_plugin_status_fn_t(
        wano_plugin_handle_t *wh,
        struct wano_plugin_status *status);

typedef wano_plugin_handle_t *wano_plugin_ops_init_fn_t(
        const struct wano_plugin *wm,
        const char *ifname,
        wano_plugin_status_fn_t *status_fn);

typedef void wano_plugin_ops_run_fn_t(wano_plugin_handle_t *wh);
typedef void wano_plugin_ops_fini_fn_t(wano_plugin_handle_t *wh);

/**
 * WANO plug-ins are probed from high to low priority.
 *
 */
struct wano_plugin
{
    const char * const                  wanp_name;          /**< Plug-in name */
    const double                        wanp_priority;      /**< Plug-in priority */
    const uint64_t                      wanp_mask;          /**< Plug-in exclusion mask */
    wano_plugin_ops_init_fn_t * const   wanp_init;          /**< OPS: Init function */
    wano_plugin_ops_run_fn_t  * const   wanp_run;           /**< OPS: Run function */
    wano_plugin_ops_fini_fn_t * const   wanp_fini;          /**< OPS: Fini function */
    ds_tree_node_t                      _wanp_tnode;        /* Internal: r/b tree node structure */
};

/**
 * A WANO plug-in handle references a single plug-in instance and its resources
 */
struct wano_plugin_handle
{
    const struct wano_plugin    *wh_plugin;                 /**< Pointer to struct plugin */
    char                         wh_ifname[C_IFNAME_LEN];   /**< Interface name */
    void                        *wh_data;                   /**< Private data */
};

/**
 * WAN plug-in iterator -- function used for traversing the registered plug-ins
 * list. Plug-ins are returned in a sorted order from the highest to the lowest
 * priority
 */
struct wano_plugin_iter
{
    ds_tree_iter_t  wpi_iter;                /**< Tree iterator */
};

/**
 * Use this macro to initialize struct wano_plugin
 */
#define WANO_PLUGIN_INIT(name, priority, mask, init_fn, run_fn, fini_fn)    \
(struct wano_plugin)                                                        \
{                                                                           \
    .wanp_name = (name),                                                    \
    .wanp_priority = (priority),                                            \
    .wanp_mask = (mask),                                                    \
    .wanp_init = (init_fn),                                                 \
    .wanp_run = (run_fn),                                                   \
    .wanp_fini = (fini_fn),                                                 \
}

/**
 * Plug-in status reporting structure
 */
struct wano_plugin_status
{
    enum
    {
        WANP_OK,        /* WANO link was successfully provisioned */
        WANP_ERROR,     /* Error occurred while provisioning plug-in, skip to the next plug-in */
        WANP_SKIP,      /* Skip this plug-in */
        WANP_BUSY,      /* Plug-in is busy doing work, stop the timeout timer */
        WANP_RESTART,   /* Error occurred while provisioning plug-in, restart the pipeline */
        WANP_ABORT,     /* Abort this plug-in and terminate the current plug-in pipeline */
        WANP_DETACH     /* Detach plug-in: the plug-in stays active but for all intent and purposes is ignored by the
                           pipeline. The plug-in is terminated only when a pipeline restart or termination occurs.
                           Useful for implementing monitoring or passive type of plug-ins */
    }
    ws_type;

    /** WAN interface, valid for WAN_OK and WANP_NEW */
    char    ws_ifname[C_IFNAME_LEN];
    /** WAN interface type, valid for WANP_OK and WANP_NEW */
    char    ws_iftype[16];
};

#define WANO_PLUGIN_STATUS(type, ...)       \
(struct wano_plugin_status)                 \
{                                           \
    .ws_type = (type),                      \
    __VA_ARGS__                             \
}

/**
 * Register a WANO plug-in
 *
 * @param[in]   wm  Pointer to a WANO plugin structure
 */
void wano_plugin_register(struct wano_plugin *wm);

/**
 * Unregister a WANO plug-in
 *
 * @param[in]   wm  Pointer to a WANO plugin structure
 */
void wano_plugin_unregister(struct wano_plugin *wm);

/**
 * Reset current plug-in iterator and return the head of the list (entry with
 * lowest priority).
 *
 * @return
 * Return a pointer to a plug-in structure (struct wano_plugin) or NULL if no
 * plug-ins are registered.
 */
struct wano_plugin *wano_plugin_first(wano_plugin_iter_t *iter);

/**
 * After wano_plugin_first() is called, return the next element in descending
 * priority * order
 */
struct wano_plugin *wano_plugin_next(wano_plugin_iter_t *iter);

/**
 * Find a WANO plug-in by name
 *
 * @param[in]   name WANO plug-in name
 *
 * @return
 * This function returns a WANO plug-in structure on success or a NULL pointer
 * if the plug-in couldn't be found
 */
struct wano_plugin *wano_plugin_find(const char *name);

/**
 * Initialize a WANO plug-in instance
 *
 * @param[in]   wm Pointer to a WANO plug-in structure
 * @param[in]   ifname Interface name
 * @param[in]   status_fn Pointer to the status update function callback
 *
 * @return
 * This function returns a pointer to a WANO plug-in handle (instance) or NULL
 * on error or if the plug-in could not be initialized for the specific
 * interface (for example, if it is not supported).
 */
wano_plugin_handle_t *wano_plugin_init(
        struct wano_plugin *wm,
        const char *ifname,
        wano_plugin_status_fn_t *status_fn);

/**
 * Start a WANO plug-in link bringup sequence
 *
 * @param[in]   wh Pointer to a WANO plug-in instance
 */
void wano_plugin_run(wano_plugin_handle_t *wh);

/**
 * Release a WANO plug-in instance
 *
 * If the WAN link bringup sequence has been started with wano_plugin_run(), this
 * function will abort it
 *
 * @param[in]   wh Pointer to a WANO plug-in instance
 *
 * @note
 * This function may trigger wan_status_fn_t callbacks
 */
bool wano_plugin_fini(wano_plugin_handle_t *wh);

/**
 * Retrieve the plugin's name
 */
static inline const char *wano_plugin_name(wano_plugin_handle_t *wh)
{
    return wh->wh_plugin->wanp_name;
}

/*
 * ===========================================================================
 *  WANO Interface State API -- used to query cached interface status from
 *  OVSDB
 *
 * Listening to events from Wifi_Inet_State is a common pattern in WANO
 * plug-ins. This plug-in implements caching mechanisms and event subscription
 * services for the Wifi_Inet_State table.
 *
 * ===========================================================================
 */

/**
 * Structure used to register to wano_inet_state events
 */
struct wano_inet_state_event;
struct wano_inet_state;

typedef struct wano_inet_state_event wano_inet_state_event_t;

typedef void wano_inet_state_event_fn_t(
        struct wano_inet_state_event *event,
        struct wano_inet_state *state);

/**
 *  Wifi_Inet_State cache structure
 */
struct wano_inet_state
{
    bool                    is_inet_state_valid;                /**< True if structure is valid (present in OVSDB) */
    bool                    is_master_state_valid;              /**< True if structure is valid (present in OVSDB) */
    char                    is_ifname[C_IFNAME_LEN];            /**< The interface name, also the primary key */
    char                    is_ip_assign_scheme[32];            /**< Cached "ip_assign_scheme" column */
    bool                    is_enabled;                         /**< Cached "enable" column */
    bool                    is_network;                         /**< Cached "network" column */
    bool                    is_nat;                             /**< Cached "NAT" column */
    osn_ip_addr_t           is_ipaddr;                          /**< Cached IPv4 address */
    osn_ip_addr_t           is_netmask;                         /**< Cached netmask */
    osn_ip_addr_t           is_gateway;                         /**< Cached gateway */
    osn_ip_addr_t           is_dns1;                            /**< Primary DNS server */
    osn_ip_addr_t           is_dns2;                            /**< Secondary DNS server */
    bool                    is_port_state;                      /**< Port state */
    ds_tree_node_t          is_tnode;                           /**< Tree node */
    reflink_t               is_reflink;                         /**< Event subscribers */
};

struct wano_inet_state_event
{
    wano_inet_state_event_fn_t
                           *ise_event_fn;                       /**< Event handler function */
    reflink_t               ise_inet_state_reflink;             /**< Reflink to wano_inet_state */
    struct wano_inet_state *ise_inet_state;                     /**< Pointer to inet state */
    ev_async                ise_async;                          /**< Async watcher, for forcing update refreshes */
    bool                    ise_init;                           /**< True if the structure was properly initialized */
};

/**
 * Initialize the wano_inet_state_event_t structure and subscribe to Wifi_Inet_State updates for interface @p ifname
 */

bool wano_inet_state_event_init(
        wano_inet_state_event_t *self,
        const char *ifname,
        wano_inet_state_event_fn_t *fn);

/**
 * Force a async state refresh. The callback will be invoked asynchronously next time the libev event loop is run.
 */
void wano_inet_state_event_refresh(wano_inet_state_event_t *self);

/**
 * Deinitialize wano_inet_state_event_t and stop listening for events on interface @p ifname
 */
void wano_inet_state_event_fini(wano_inet_state_event_t *self);

/*
 * ===========================================================================
 *  WANO Inet Config API -- Wrappers for writing/updating Wifi_Inet_Config
 * ===========================================================================
 */

/**
 * Tristate type; this is used to represent bools with the additional
 * option of having a value of "none" (unset).
 */
typedef enum wano_tri
{
    WANO_TRI_NONE = 0,
    WANO_TRI_TRUE,
    WANO_TRI_FALSE,
}
wano_tri_t;

struct wano_inet_config_args
{
    const char     *if_type;            /**< Maps to Wifi_Inet_Config:if_type */
    wano_tri_t      enabled;            /**< Maps to Wifi_Inet_Config:enabled */
    wano_tri_t      network;            /**< Maps to Wifi_Inet_Config:network */
    wano_tri_t      nat;                /**< Maps to Wifi_Inet_Config:NAT */
    const char     *ip_assign_scheme;   /**< Maps to Wifi_Inet_Config:ip_assign_scheme */
    const char     *inet_addr;          /**< Maps to Wifi_Inet_Config:inet_addr */
    const char     *netmask;            /**< Maps to Wifi_Inet_Config:netmask */
    const char     *gateway;            /**< Maps to Wifi_Inet_Config:gateway */
    const char     *dns1;               /**< Maps to Wifi_Inet_config::dns */
    const char     *dns2;               /**< Maps to Wifi_Inet_config::dns */
    const char     *parent_ifname;      /**< Maps to Wifi_Inet_Config:parent_ifname */
    const int       vlan_id;            /**< Maps to Wifi_Inet_Config:vlan_id */
};

#define WANO_INET_CONFIG_UPDATE(ifname, ...) \
    wano_inet_config_update(ifname, &(struct wano_inet_config_args){ __VA_ARGS__ })

/**
 * Update the OVSDB Wifi_Inet_Config. Fields with a default value of 0 are not updated.
 *
 * bools are split into two fields, one to disable and one to enable the
 * option. For example, the "enabled" column can be set to true by setting
 * the .if_enabled field to true, and set to false by setting the .if_disabled field
 * to true.
 *
 * Example usage (via the above macro wrapper):
 *
 * WIFI_INET_CONFIG_UPDATE("eth0", .if_enable = true, ip_assign_scheme = "none", network_enable = true);
 */
bool wano_inet_config_update(const char *ifname, struct wano_inet_config_args *args);

/*
 * ===========================================================================
 *  WANO Connection_Manager_Uplink API - Wrapper for updating the
 *  Connection_Manager_Uplink table
 * ===========================================================================
 */

struct wano_connmgr_uplink;
struct wano_connmgr_uplink_event;
struct wano_connmgr_uplink_state;

typedef struct wano_connmgr_uplink_event wano_connmgr_uplink_event_t;

typedef void wano_connmgr_uplink_event_fn_t(
        wano_connmgr_uplink_event_t *event,
        struct wano_connmgr_uplink_state *state);

/*
 * Structure used for reporting Connection_Manager_Uplink status
 */
struct wano_connmgr_uplink_state
{
    char        if_name[C_IFNAME_LEN];
    char        bridge[C_IFNAME_LEN];
    bool        has_L2;
    bool        has_L3;
};

/**
 * Structure for processing Connection_Manager_Uplink events
 */
struct wano_connmgr_uplink_event
{
    wano_connmgr_uplink_event_fn_t *ce_event_fn;                /* Event callback */
    ev_async                        ce_async;                   /* Wake-up object */
    bool                            ce_started;                 /* Started flag */
    struct wano_connmgr_uplink     *ce_cmu;                     /* Object pointer to wano_connmgr_uplink */
    reflink_t                       ce_cmu_reflink;             /* Reflink to wano_connmgr_uplink */
};

/**
 * Structure used for writing to Connection_Manager_Uplink
 */
struct wano_connmgr_uplink_args
{
    const char     *if_type;
    int             priority;
    wano_tri_t      has_L2;
    wano_tri_t      has_L3;
    wano_tri_t      loop;
};

/**
 * Initialize a Connection_Manager_Uplink event listening object
 */
void wano_connmgr_uplink_event_init(
        wano_connmgr_uplink_event_t *self,
        wano_connmgr_uplink_event_fn_t *fn);

/**
 * Start receiving events from the row associated with @p ifname in the
 * Connection_Manager_Uplink table
 */
bool wano_connmgr_uplink_event_start(wano_connmgr_uplink_event_t *self, const char *ifname);

/**
 * Stop receiving events from the registered interface row in the
 * Connection_Manager_Uplink table
 */
void wano_connmgr_uplink_event_stop(wano_connmgr_uplink_event_t *self);

/**
 * Flush the Connection_Manager_Uplink table
 */
bool wano_connmgr_uplink_flush(void);

#define WANO_CONNMGR_UPLINK_UPDATE(ifname, ...) \
    wano_connmgr_uplink_update(ifname, &(struct wano_connmgr_uplink_args){ __VA_ARGS__ })

/**
 * Update the Connection_Manager_Uplink table
 */
bool wano_connmgr_uplink_update(
        const char *ifname,
        struct wano_connmgr_uplink_args *args);
/**
 * Delete a single row from the Connection_Manager_Uplink table
 */
bool wano_connmgr_uplink_delete(const char *ifname);

/*
 * ===========================================================================
 *  WANO Port table API - Wrapper for monitoring the Port table
 * ===========================================================================
 */
struct wano_ovs_port;
struct wano_ovs_port_event;
struct wano_ovs_port_state;

typedef struct wano_ovs_port_event wano_ovs_port_event_t;

typedef void wano_ovs_port_event_fn_t(
        wano_ovs_port_event_t *event,
        struct wano_ovs_port_state *state);

/**
 * Structure used for reporting Port table status
 */
struct wano_ovs_port_state
{
    bool                        ps_exists;                 /**< True if a Port entry for this interface exists */
    char                        ps_name[C_IFNAME_LEN];     /**< Port/interface name */
};

/**
 * Structure for processing Port table events
 */
struct wano_ovs_port_event
{
    struct wano_ovs_port       *pe_ovs_port;                /**< Pointer to the cached object */
    reflink_t                   pe_ovs_port_reflink;        /**< Reflink to the object above */
    bool                        pe_exists;                  /**< Port for this interface exists */
    wano_ovs_port_event_fn_t   *pe_event_fn;                /**< Event callback */
    ev_async                    pe_async;                   /**< Async watcher */
    bool                        pe_started;                 /**< True wano_ovs_port_event_start() was called */
};

/**
 * Initialize a Port table event listening object
 */
void wano_ovs_port_event_init(
        wano_ovs_port_event_t *self,
        wano_ovs_port_event_fn_t *fn);

/**
 * Start receiving events from the row associated with @p ifname in the
 * Connection_Manager_Uplink table
 */
bool wano_ovs_port_event_start(wano_ovs_port_event_t *self, const char *ifname);

/**
 * Stop receiving events from the registered interface row in the
 * Connection_Manager_Uplink table
 */
void wano_ovs_port_event_stop(wano_ovs_port_event_t *self);

/*
 * ===========================================================================
 *  WANO Plugin Pipelines
 *
 *  A plug-in pipeline manages execution of plug-ins on an interface. The
 *  pipeline is responsible for scheduling plug-ins in series or in parallel
 *  depending on the plug-in and pipeline masks.
 * ===========================================================================
 */

/**
 * Object representing a plug-in pipeline
 */
typedef struct wano_ppline wano_ppline_t;

enum wano_ppline_status
{
    /** At least one plug-in was provisioned */
    WANO_PPLINE_OK,
    /**
     * No plug-in was provisioned because the plug-in list queue was exhausted or
     * the plug-in pipeline was aborted.
     */
    WANO_PPLINE_IDLE,
    /**
     * The pipeline was restarted, either carrier loss was detected or a plug-in
     * requested a restart
     */
    WANO_PPLINE_RESTART,
};

/**
 * WAN pipeline structure
 */
struct wano_ppline
{
    char                        wpl_ifname[C_IFNAME_LEN];   /**< Interface name */
    char                        wpl_iftype[32];             /**< Interface type */
    uint64_t                    wpl_plugin_emask;           /**< Exclusion mask */
    uint64_t                    wpl_plugin_rmask;           /**< Mask of currently running plug-in types */
    uint64_t                    wpl_plugin_imask;           /**< Interface reset mask */
    struct wano_plugin         *wpl_plugin_next;            /**< Next plugin in the pipeline */
    wano_plugin_iter_t          wpl_plugin_iter;            /**< Plug-in iterator */
    ds_dlist_t                  wpl_plugin_waitq;           /**< Queue of waiting plug-ins */
    ds_dlist_t                  wpl_plugin_runq;            /**< Queue of currently running plug-ins */
    wano_ppline_state_t         wpl_state;                  /**< STAM state machine */
    wano_inet_state_event_t     wpl_inet_state_event;       /**< Interface status monitoring */
    bool                        wpl_carrier_exception;      /**< Generate a PPLINE_RESTART exception on carrier loss */
    bool                        wpl_init;                   /**< True if successfully initialized */
    wano_connmgr_uplink_event_t wpl_cmu_event;              /**< Connection_Manager_Uplink watcher */
    wano_ovs_port_event_t       wpl_ovs_port_event;         /**< Port table watcher */
    int                         wpl_retries;                /**< Number of retries */
    ev_timer                    wpl_retry_timer;            /**< Retry timer */
    bool                        wpl_bridge;                 /**< True interface is in bridge */
    bool                        wpl_uplink_bridge;          /**< True if Connection_Manager_Uplink:bridge is set */
    double                      wpl_immediate_timeout;      /**< Immedate timeout */
    ds_dlist_t                  wpl_event_list;             /**< List of event callbacks */
    bool                        wpl_has_l3;                 /**< True if the has_L3 column was set */
};

typedef struct wano_ppline_event wano_ppline_event_t;

/**
 * WANO plug-in pipeline event callback
 */
typedef void wano_ppline_event_fn_t(
        wano_ppline_event_t *wpe,
        enum wano_ppline_status status);

/**
 * WANO plug-in pipeline event object
 */
struct wano_ppline_event
{
    wano_ppline_event_fn_t     *wpe_event_fn;
    wano_ppline_t              *wpe_ppline;
    ds_dlist_node_t             wpe_dnode;
};

/**
 * Initialize a plug-in pipeline on the specified interface.
 *
 * Only plug-ins matching the mask @p mask will be provisioned,  if a plug-in
 * doesn't satisfy all the bits in @p mask, it will be skipped.
 *
 * @param[in]   self    wano_ppline_t object to be initialized
 * @param[in]   ifname  interface to runt he plug-in pipeline on
 * @param[in]   emask   Plug-in exclusion mask -- prevents certain types of
 *                      plug-ins from running on this pipeline
 *
 * @return
 * In case of error, false is return and the object referenced by @p self should
 * be considered invalid.
 */
bool wano_ppline_init(
        wano_ppline_t *self,
        const char *ifname,
        const char *iftype,
        uint64_t emask);

/**
 * Terminate the plug-in pipeline and stop all currently active plug-ins.
 */
void wano_ppline_fini(wano_ppline_t *self);

/**
 * Utility function for retrieving the pipeline instance from a plug-in
 * handle
 */
wano_ppline_t *wano_ppline_from_plugin_handle(wano_plugin_handle_t *plugin);

/** Initialize a plug-in pipeline event structure */
void wano_ppline_event_init(
        wano_ppline_event_t *event,
        wano_ppline_event_fn_t *fn);

/** Start listening to pipeline events */
void wano_ppline_event_start(wano_ppline_event_t *self, wano_ppline_t *wpp);

/** Stop listening to pipeline events */
void wano_ppline_event_stop(wano_ppline_event_t *event);

#endif /* WANO_H_INCLUDED */
