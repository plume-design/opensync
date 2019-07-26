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

/*
 * ===========================================================================
 *  Glue layer between INET and TARGET libraries
 * ===========================================================================
 */

#include <stdbool.h>

#include <ev.h>

#include "ds_dlist.h"
#include "ds_tree.h"
#include "const.h"
#include "osa_assert.h"
#include "target.h"
#include "version.h"
#include "evx.h"
#include "ovsdb_update.h"

#include "inet.h"
#include "inet_base.h"
#include "inet_eth.h"
#include "inet_vif.h"
#include "inet_gre.h"
#include "inet_target.h"

/* For platforms not using KConfig yet, default to polling */

#if !defined(CONFIG_USE_KCONFIG)
#define CONFIG_INET_STATUS_POLL                 1
#define CONFIG_INET_STATUS_POLL_INTERVAL_MS     200
#endif

#define IF_DELAY_COMMIT             0.3     /* Amount of time for delaying commits */

typedef void state_callback_t(struct schema_Wifi_Inet_State *istate, schema_filter_t *filter);
typedef void master_callback_t(struct schema_Wifi_Master_State *mstate, schema_filter_t *filter);

#define IF_TYPE(M)      \
    M(IF_TYPE_NONE)     \
    M(IF_TYPE_ETH)      \
    M(IF_TYPE_VIF)      \
    M(IF_TYPE_VLAN)     \
    M(IF_TYPE_BRIDGE)   \
    M(IF_TYPE_GRE)      \
    M(IF_TYPE_TAP)      \
    M(IF_TYPE_MAX)


#define IS_PORT_VALID(port)  ((port) > 0 && (port) <= 65535)

enum if_type
{
    #define _ENUM(x)    x,
    IF_TYPE(_ENUM)
    #undef _ENUM
};

const char *__if_type_str[IF_TYPE_MAX + 1] =
{
    #define _STR(x)   [x] = #x,
    IF_TYPE(_STR)
    #undef _STR
};

#define INET_CONFIG_SZ(field)       sizeof(((struct schema_Wifi_Inet_Config *)NULL)->field)
#define INET_CONFIG_COPY(a, b)      do { C_STATIC_ASSERT(sizeof(a) == sizeof(b), #a " not equal in size to " #b); memcpy(&a, &b, sizeof(a)); } while (0)

struct if_entry
{
    char                if_name[C_IFNAME_LEN];      /* Interface name */
    enum if_type        if_type;                    /* Interface type */
    bool                if_commit;                  /* Commit pending */
    inet_t              *if_inet;                   /* Inet structure */
    ds_tree_node_t      if_tnode;                   /* ds_tree node -- for device lookup by name */
    ds_dlist_node_t     if_poll_dnode;              /* ds_poll_dnode -- for status polling */
    state_callback_t    *if_istate_cb;              /* status change callback */
    master_callback_t   *if_mstate_cb;              /* master state change callback */

    inet_state_t        if_state;                   /* Remembered last interface status */
    bool                if_state_notify;            /* New status must be reported -- typically set by a configuration change */

    /* Fields that should be copied from Wifi_Inet_Config to Wifi_Inet_State */
    struct
    {
        uint8_t         if_type[INET_CONFIG_SZ(if_type)];
        uint8_t         if_uuid[INET_CONFIG_SZ(if_uuid)];
        uint8_t         _uuid[INET_CONFIG_SZ(_uuid)];
        uint8_t         dns[INET_CONFIG_SZ(dns)];
        uint8_t         dns_keys[INET_CONFIG_SZ(dns_keys)];
        int             dns_len;
        uint8_t         dhcpd[INET_CONFIG_SZ(dhcpd)];
        uint8_t         dhcpd_keys[INET_CONFIG_SZ(dhcpd_keys)];
        int             dhcpd_len;
        bool            gre_ifname_exists;
        uint8_t         gre_ifname[INET_CONFIG_SZ(gre_ifname)];
        bool            gre_remote_inet_addr_exists;
        uint8_t         gre_remote_inet_addr[INET_CONFIG_SZ(gre_remote_inet_addr)];
        bool            gre_local_inet_addr_exists;
        uint8_t         gre_local_inet_addr[INET_CONFIG_SZ(gre_local_inet_addr)];
    }
    if_cache;
};

static int if_cmp(void *_a, void *_b);

static const char *if_type_str(enum if_type type);

static struct if_entry *if_entry_get(const char *ifname, enum if_type type);

static bool if_state_get(
        const char *ifname,
        enum if_type type,
        struct schema_Wifi_Inet_State *pstate);

static bool if_master_get(
        const char *ifname,
        enum if_type type,
        struct schema_Wifi_Master_State *pstate);

static bool if_config_set(
        const char *ifname,
        enum if_type type,
        const struct schema_Wifi_Inet_Config *pconfig);

static void if_status_poll(void);

static inet_dhcp_lease_fn_t if_dhcp_lease_notify;
static inet_route_notify_fn_t if_route_state_notify;

/**
 * Global variables
 */

ds_tree_t   if_list = DS_TREE_INIT(if_cmp, struct if_entry, if_tnode);
ds_dlist_t  if_poll_list = DS_DLIST_INIT(struct if_entry, if_poll_dnode);

target_dhcp_leased_ip_cb_t *if_dhcp_lease_callback = NULL;
target_route_state_cb_t    *if_route_state_callback = NULL;

/**
 * ===========================================================================
 *  Initialization
 * ===========================================================================
 */

/**
 * Return the pre-populated entries for Wifi_Inet_Config
 */
bool inet_target_inet_config_init(ds_dlist_t *inet_ovs)
{
    ds_dlist_init(inet_ovs, target_inet_config_init_t, dsl_node);

    return true;
}

/**
 * Return the pre-populated entries for Wifi_Inet_State
 */
bool inet_target_inet_state_init(ds_dlist_t *inet_ovs)
{
    ds_dlist_init(inet_ovs, target_inet_state_init_t, dsl_node);

    return true;
}

/**
 * Return the pre-populated entries for Wifi_Master_State
 */
bool inet_target_master_state_init(ds_dlist_t *inet_ovs)
{
    ds_dlist_init(inet_ovs, target_master_state_init_t, dsl_node);

    return true;
}

/*
 * ===========================================================================
 *  Status reporting
 * ===========================================================================
 */

#if defined(CONFIG_INET_STATUS_POLL)
/*
 * Timer-based interface status polling
 */
void __if_status_poll_fn(EV_P_ ev_timer *w, int revent)
{
    if_status_poll();
}

void if_status_poll_init(void)
{
    /*
     * Interface polling using a timer
     */
    static bool poll_init = false;
    static ev_timer if_poll_timer;

    if (poll_init) return;

    MEMZERO(if_poll_timer);

    /* Create a repeating timer */
    ev_timer_init(
            &if_poll_timer,
            __if_status_poll_fn,
            0.0,
            CONFIG_INET_STATUS_POLL_INTERVAL_MS / 1000.0);

    ev_timer_start(EV_DEFAULT, &if_poll_timer);

    poll_init = true;
}

#elif defined(CONFIG_INET_STATUS_NETLINK_POLL)
/*
 * Netlink based interface status polling
 *
 * Note: Netlink sockets are rather poorly documented. Instead of trying to implement the
 * whole protocol, it is much simpler to just use a NETLINK event as a trigger for the actual
 * interface polling code. This is not optimal, but still much faster than timer-based polling.
 */

/* Include netlink specific headers */
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define RTNLGRP(x)  ((RTNLGRP_ ## x) > 0 ? 1 << ((RTNLGRP_ ## x) - 1) : 0)

static int __nlsock = -1;
static ev_debounce __nlsock_debounce_ev;

/*
 * nlsock I/O event callback
 */
void __if_status_nlsock_fn(EV_P_ ev_io *w, int revent)
{
    uint8_t buf[getpagesize()];

    if (revent & EV_ERROR)
    {
        LOG(EMERG, "target_inet: Error on netlink socket.");
        ev_io_stop(loop, w);
        return;
    }

    if (!(revent & EV_READ)) return;

    /* Read and discard the data from the socket */
    if (recv(__nlsock, buf, sizeof(buf), 0) < 0)
    {
        LOG(EMERG, "target_inet: Received EOF from netlink socket?");
        ev_io_stop(loop, w);
        return;
    }

    /*
     * Interface status changes may occur in bursts, since we're polling all status variables
     * at once, we can debounce the netlink events for a slight performance boost at the cost
     * of a small delay in status updates.
     *
     * This will trigger __if_status_debounce_fn() below when the debounce timer expires.
     */
    ev_debounce_start(EV_DEFAULT, &__nlsock_debounce_ev);
}

/*
 * nlsock debounce timer callback
 */
void __if_status_debounce_fn(EV_P_ ev_debounce *w, int revent)
{
    /* Poll interfaces */
    if_status_poll();
}

void if_status_poll_init(void)
{
     struct sockaddr_nl nladdr;

     /*
      * __nlsock serves a double purpose of holding the socket file descriptor and as a
      * global initialization flag
      */
     if (__nlsock >= 0) return;

     /* Create the netlink socket in the NETLINK_ROUTE domain */
     __nlsock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
     if (__nlsock < 0)
     {
         LOGE("target_init: Error creating NETLINK socket.");
         goto error;
     }

     /*
      * Bind the netlink socket to events related to interface status change
      */
     memset(&nladdr, 0, sizeof(nladdr));
     nladdr.nl_family = AF_NETLINK;
     nladdr.nl_pid = 0;
     nladdr.nl_groups =
             RTNLGRP(LINK) |            /* Link/interface status */
             RTNLGRP(IPV4_IFADDR) |     /* IPv4 address status */
             RTNLGRP(IPV6_IFADDR) |     /* IPv6 address status */
             RTNLGRP(IPV4_NETCONF) |    /* No idea */
             RTNLGRP(IPV6_NETCONF);     /* -=- */
     if (bind(__nlsock, (struct sockaddr *)&nladdr, sizeof(nladdr)) != 0)
     {
         LOGE("target_inet: Error binding NETLINK socket");
         goto error;
     }

     /* Initialize the debouncer */
     ev_debounce_init(
             &__nlsock_debounce_ev,
             __if_status_debounce_fn,
             CONFIG_INET_STATUS_NETLINK_DEBOUNCE_MS / 1000.0);

     /*
      * Initialize an start an I/O watcher
      */
     static ev_io nl_ev;

     ev_io_init(&nl_ev, __if_status_nlsock_fn, __nlsock, EV_READ);
     ev_io_start(EV_DEFAULT, &nl_ev);

     return;

 error:
     if (__nlsock >= 0) close(__nlsock);
     __nlsock = -1;
}

#else
#error Interface status reporting backend not supported.
#endif

bool __inet_poll_register(const char *ifname, void *state_cb, bool master)
{
    struct if_entry *pif = if_entry_get(ifname, IF_TYPE_NONE);

    if (pif == NULL)
    {
        LOG(ERR, "target_inet: %s: Error requesting state register.", ifname);
        return false;
    }

    /*
     * This entry is not yet registered for sending notifications. Add it to
     * the polling list
     */
    if (pif->if_istate_cb == NULL && pif->if_mstate_cb == NULL)
    {
        ds_dlist_insert_tail(&if_poll_list, pif);
    }

    if (master)
    {
        pif->if_mstate_cb = state_cb;
    }
    else
    {
        pif->if_istate_cb = state_cb;
    }

    if_status_poll_init();

    return true;
}

bool inet_target_inet_state_register(const char *ifname, void *istate_cb)
{
    return __inet_poll_register(ifname, istate_cb, false);
}

bool inet_target_master_state_register(const char *ifname, target_master_state_cb_t *mstate_cb)
{
    return __inet_poll_register(ifname, mstate_cb, true);
}

/*
 * ===========================================================================
 *  Ethernet interfaces
 * ===========================================================================
 */
bool inet_target_eth_inet_state_get(const char *ifname, struct schema_Wifi_Inet_State *istate)
{
    return if_state_get(ifname, IF_TYPE_ETH, istate);
}

bool inet_target_eth_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate)
{
    return if_master_get(ifname, IF_TYPE_ETH, mstate);
}

bool inet_target_eth_inet_config_set(const char *ifname, struct schema_Wifi_Inet_Config *iconf)
{
    return if_config_set(ifname, IF_TYPE_ETH, iconf);
}

/*
 * ===========================================================================
 *  Bridge interfaces
 * ===========================================================================
 */
bool inet_target_bridge_inet_state_get(const char *ifname, struct schema_Wifi_Inet_State *istate)
{
    return if_state_get(ifname, IF_TYPE_BRIDGE, istate);
}

bool inet_target_bridge_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate)
{
    return if_master_get(ifname, IF_TYPE_BRIDGE, mstate);
}

bool inet_target_bridge_inet_config_set(const char *ifname, struct schema_Wifi_Inet_Config *iconf)
{
    return if_config_set(ifname, IF_TYPE_BRIDGE, iconf);
}
/*
 * ===========================================================================
 *  VIF interfaces
 * ===========================================================================
 */
bool inet_target_vif_inet_state_get(const char *ifname, struct schema_Wifi_Inet_State *istate)
{
    return if_state_get(ifname, IF_TYPE_VIF, istate);
}

bool inet_target_vif_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate)
{
    return if_master_get(ifname, IF_TYPE_VIF, mstate);
}

bool inet_target_vif_inet_config_set(const char *ifname, struct schema_Wifi_Inet_Config *iconf)
{
    return if_config_set(ifname, IF_TYPE_VIF, iconf);
}

/*
 * ===========================================================================
 *  VLAN interfaces
 * ===========================================================================
 */
bool inet_target_vlan_inet_state_get(const char *ifname, struct schema_Wifi_Inet_State *istate)
{
    return if_state_get(ifname, IF_TYPE_VLAN, istate);
}

bool inet_target_vlan_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate)
{
    return if_master_get(ifname, IF_TYPE_VLAN, mstate);
}

bool inet_target_vlan_inet_config_set(const char *ifname, struct schema_Wifi_Inet_Config *iconf)
{
    return if_config_set(ifname, IF_TYPE_VLAN, iconf);
}

/*
 * ===========================================================================
 *  GRE interfaces
 * ===========================================================================
 */
bool inet_target_gre_inet_state_get(const char *ifname, char *remote_ip, struct schema_Wifi_Inet_State *istate)
{
    (void)remote_ip;

    return if_state_get(ifname, IF_TYPE_GRE, istate);
}

bool inet_target_gre_master_state_get(const char *ifname, const char *remote_ip, struct schema_Wifi_Master_State *mstate)
{
    (void)remote_ip;

    return if_master_get(ifname, IF_TYPE_GRE, mstate);
}

bool inet_target_gre_inet_config_set(const char *ifname, char *remote_ip, struct schema_Wifi_Inet_Config *iconf)
{
    (void)remote_ip;

    return if_config_set(ifname, IF_TYPE_GRE, iconf);
}

/*
 * ===========================================================================
 *  TAP interfaces
 * ===========================================================================
 */
bool inet_target_tap_inet_state_get(const char *ifname, struct schema_Wifi_Inet_State *istate)
{
    return if_state_get(ifname, IF_TYPE_TAP, istate);
}

bool inet_target_tap_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate)
{
    return if_master_get(ifname, IF_TYPE_TAP, mstate);
}

bool inet_target_tap_inet_config_set(const char *ifname, struct schema_Wifi_Inet_Config *iconf)
{
    return if_config_set(ifname, IF_TYPE_TAP, iconf);
}

/*
 * ===========================================================================
 *  MAC learning
 * ===========================================================================
 */
bool inet_target_mac_learning_register(void *omac_cb)
{
    return false;
}

/*
 * ===========================================================================
 *  Misc
 * ===========================================================================
 */

/**
 * Comparator for if_entry structures
 */
static int if_cmp(void *_a, void  *_b)
{
    struct if_entry *a = _a;
    struct if_entry *b = _b;

    return strcmp(a->if_name, b->if_name);
}

static const char *if_type_str(enum if_type type)
{
    if (type <= IF_TYPE_MAX)
    {
        return __if_type_str[type];
    }

    return "Unknown";
}

inet_t *if_inet_new(const char *ifname, enum if_type type)
{
    static char serial_num[100] = { 0 };
    static char sku_num[100] = { 0 };
    static char hostname[C_HOSTNAME_LEN] = { 0 };
    static char vendor_class[INET_DL_VENDORCLASS_MAX] = { 0 };

    inet_t *nif = NULL;

    TRACE();
    switch (type)
    {
        case IF_TYPE_ETH:
        case IF_TYPE_BRIDGE:
        case IF_TYPE_TAP:
            nif = inet_eth_new(ifname);
            break;

        case IF_TYPE_VIF:
            nif = inet_vif_new(ifname);
            break;

        case IF_TYPE_GRE:
            nif = inet_gre_new(ifname);
            break;

        default:
            /* Unsupported types */
            LOG(ERR, "target_inet: %s: Unsupported interface type: %d", ifname, type);
            return NULL;
    }

    if (nif == NULL)
    {
        LOG(ERR, "target_inet: %s: Error initializing interface type: %d", ifname, type);
        return NULL;
    }

    /*
     * Common initialization for all interfaces
     */

    /* Retrieve vendor class, sku, hostname ... we need these values to populate DHCP options */
    if (vendor_class[0] == '\0' && target_model_get(vendor_class, sizeof(vendor_class)) == false)
    {
        STRSCPY(vendor_class, TARGET_NAME);
    }

    if (serial_num[0] == '\0')
    {
        target_serial_get(serial_num, sizeof(serial_num));
    }

    /* read SKU number, if empty, reset buffer */
    if (hostname[0] == '\0')
    {
        if (target_sku_get(sku_num, sizeof(sku_num)) == false)
        {
            tsnprintf(hostname, sizeof(hostname), "%s_Pod", serial_num);
        }
        else
        {
            tsnprintf(hostname, sizeof(hostname), "%s_Pod_%s", serial_num, sku_num);
        }
    }

    /* Request DHCP options */
    inet_dhcpc_option_request(nif, DHCP_OPTION_SUBNET_MASK, true);
    inet_dhcpc_option_request(nif, DHCP_OPTION_ROUTER, true);
    inet_dhcpc_option_request(nif, DHCP_OPTION_DNS_SERVERS, true);
    inet_dhcpc_option_request(nif, DHCP_OPTION_HOSTNAME, true);
    inet_dhcpc_option_request(nif, DHCP_OPTION_DOMAIN_NAME, true);
    inet_dhcpc_option_request(nif, DHCP_OPTION_BCAST_ADDR, true);
    inet_dhcpc_option_request(nif, DHCP_OPTION_VENDOR_SPECIFIC, true);

    /* Set DHCP options */
    inet_dhcpc_option_set(nif, DHCP_OPTION_VENDOR_CLASS, vendor_class);
    inet_dhcpc_option_set(nif, DHCP_OPTION_HOSTNAME, hostname);
    inet_dhcpc_option_set(nif, DHCP_OPTION_PLUME_SWVER, app_build_number_get());
    inet_dhcpc_option_set(nif, DHCP_OPTION_PLUME_PROFILE, app_build_profile_get());
    inet_dhcpc_option_set(nif, DHCP_OPTION_PLUME_SERIAL_OPT, serial_num);

    /* Register the route state function */
    inet_route_notify(nif, if_route_state_notify);

    return nif;
}
struct if_entry *if_entry_find_by_client_ip(inet_ip4addr_t client_ip4addr)
{
    inet_ip4addr_t subnet;
    struct if_entry *pif = NULL;

    memset(&subnet, 0, sizeof(inet_ip4addr_t));

    ds_tree_foreach(&if_list, pif)
    {
        subnet.raw = pif->if_state.in_ipaddr.raw & pif->if_state.in_netmask.raw;

        if (subnet.raw)
        {
            if ((client_ip4addr.raw & pif->if_state.in_netmask.raw) == subnet.raw)
            {
                return pif;
            }
        }
    }

    return NULL;
}

/**
 * Find and return the interface @p ifname; if it doesn't exists, create a new instance
 */
struct if_entry *if_entry_get(const char *_ifname, enum if_type type)
{
    TRACE();
    struct if_entry *pif;

    char *ifname = (char*)_ifname; // get rid of const
    pif = ds_tree_find(&if_list, ifname);

    /* Entry found, return it */
    if (pif != NULL) return pif;

    /* IF_TYPE_NONE basically means do a lookup but do not allocate a new entry -- report an error here */
    if (type == IF_TYPE_NONE) return NULL;

    /*
     * Allocate a new entry
     */
    pif = calloc(1, sizeof(struct if_entry));
    pif->if_type = type;

    if (strscpy(pif->if_name, ifname, sizeof(pif->if_name)) < 0)
    {
        LOG(ERR, "target_inet: %s (%s): Error creating interface, name too long.",
                ifname,
                if_type_str(type));
        free(pif);
        return NULL;
    }

    pif->if_inet = if_inet_new(ifname, type);
    if (pif->if_inet == NULL)
    {
        LOG(ERR, "target_inet: %s (%s): Error creating interface, constructor failed.",
                ifname,
                if_type_str(type));
        free(pif);
        return NULL;
    }

    ds_tree_insert(&if_list, pif, pif->if_name);

    LOG(INFO, "target_inet: %s: Created new interface (type %s).", ifname, if_type_str(type));

    return pif;
}

/* Issue a delayed commit on the interface */
void __if_delayed_commit(EV_P_ ev_debounce *w, int revent)
{
    struct if_entry *pif;

    ds_tree_foreach(&if_list, pif)
    {
        if (!pif->if_commit) continue;
        pif->if_commit = false;

        if (!inet_commit(pif->if_inet))
        {
            LOG(ERR, "target_inet: %s (%s): Error committing new configuration.",
                    pif->if_name,
                    if_type_str(pif->if_type));
        }
    }
}

void if_delayed_commit(struct if_entry *pif)
{
    static bool if_commit_init = false;
    static ev_debounce if_commit_timer;

    pif->if_commit = true;

    if (!if_commit_init)
    {
        ev_debounce_init(&if_commit_timer, __if_delayed_commit, IF_DELAY_COMMIT);
        if_commit_init = true;
    }

    ev_debounce_start(EV_DEFAULT, &if_commit_timer);
}


/**
 * Lookup a value by key in a schema map structure.
 *
 * A schema map structures is defined as follows:
 *
 * char name[NAME_SZ][LEN]      - value array
 * char name_keys[KEY_SZ][LEN]  - key array
 * char name_len;               - length of name and name_keys arrays
 */
#define SCHEMA_FIND_KEY(x, key)    __find_key((char *)x##_keys, sizeof(*(x ## _keys)), (char *)x, sizeof(*(x)), x##_len, key)
static inline const char * __find_key(char *keyv, size_t keysz, char *datav, size_t datasz, int vlen, const char *key)
{
    int ii;

    for (ii = 0; ii < vlen; ii++)
    {
        if (strcmp(keyv, key) == 0) return datav;

        keyv += keysz;
        datav += datasz;
    }

    return NULL;
}

/**
 * Configuration apply
 */

bool __if_config_ip_assign_scheme_set(struct if_entry *pif, const struct schema_Wifi_Inet_Config *pconfig)
{
    enum inet_assign_scheme assign_scheme = INET_ASSIGN_NONE;

    if (pconfig->ip_assign_scheme_exists)
    {
        if (strcmp(pconfig->ip_assign_scheme, "static") == 0)
        {
            assign_scheme = INET_ASSIGN_STATIC;
        }
        else if (strcmp(pconfig->ip_assign_scheme, "dhcp") == 0)
        {
            assign_scheme = INET_ASSIGN_DHCP;
        }
    }

    if (!inet_assign_scheme_set(pif->if_inet, assign_scheme))
    {
        LOG(WARN, "target_inet: %s (%s): Error setting the IP assignment scheme: %s",
                pif->if_name,
                if_type_str(pif->if_type),
                pconfig->ip_assign_scheme);

        return false;
    }

    return true;
}

bool __if_config_igmp_set(struct if_entry *pif, const struct schema_Wifi_Inet_Config *pconfig)
{
    if (pif->if_type == IF_TYPE_BRIDGE && pconfig->igmp_exists && pconfig->igmp_present)
    {
        int iigmp = pconfig->igmp;
        int iage = pconfig->igmp_age_exists ? pconfig->igmp_age : 300;
        int itsize = pconfig->igmp_tsize_present ? pconfig->igmp_tsize: 1024;

        if (!inet_igmp_enable(pif->if_inet, iigmp, iage, itsize))
        {
            LOG(WARN, "target_inet: %s (%s): Error enabling IGMP (%d).",
                    pif->if_name,
                    if_type_str(pif->if_type),
                    pconfig->igmp_exists && pconfig->igmp);

            return false;
        }
    }

    return true;
}
bool __if_config_upnp_set(struct if_entry *pif, const struct schema_Wifi_Inet_Config *pconfig)
{
    enum inet_upnp_mode upnp = UPNP_MODE_NONE;

    if (pconfig->upnp_mode_exists)
    {
        if (strcmp(pconfig->upnp_mode, "disabled") == 0)
        {
            upnp = UPNP_MODE_NONE;
        }
        else if (strcmp(pconfig->upnp_mode, "internal") == 0)
        {
            upnp = UPNP_MODE_INTERNAL;
        }
        else if (strcmp(pconfig->upnp_mode, "external") == 0)
        {
            upnp = UPNP_MODE_EXTERNAL;
        }
        else
        {
            LOG(WARN, "target_inet: %s (%s): Unknown UPnP mode %s. Assuming \"disabled\".",
                    pif->if_name,
                    if_type_str(pif->if_type),
                    pconfig->upnp_mode);
        }
    }

    if (!inet_upnp_mode_set(pif->if_inet, upnp))
    {
        LOG(WARN, "target_inet: %s (%s): Error setting UPnP mode: %s",
                pif->if_name,
                if_type_str(pif->if_type),
                pconfig->upnp_mode);

        return false;
    }

    return true;
}

bool __if_config_static_set(struct if_entry *pif, const struct schema_Wifi_Inet_Config *pconfig)
{
    bool retval = true;

    inet_ip4addr_t ipaddr = INET_IP4ADDR_ANY;
    inet_ip4addr_t netmask = INET_IP4ADDR_ANY;
    inet_ip4addr_t bcaddr = INET_IP4ADDR_ANY;

    if (pconfig->inet_addr_exists && !inet_ip4addr_fromstr(&ipaddr, pconfig->inet_addr))
    {
        LOG(WARN, "target_inet: %s (%s): Invalid IP address: %s",
                pif->if_name,
                if_type_str(pif->if_type),
                pconfig->inet_addr);
    }

    if (pconfig->netmask_exists && !inet_ip4addr_fromstr(&netmask, pconfig->netmask))
    {
        LOG(WARN, "target_inet: %s (%s): Invalid netmask: %s",
                pif->if_name,
                if_type_str(pif->if_type),
                pconfig->netmask);
    }

    if (pconfig->broadcast_exists && !inet_ip4addr_fromstr(&bcaddr, pconfig->broadcast))
    {
        LOG(WARN, "target_inet: %s (%s): Invalid broadcast address: %s",
                pif->if_name,
                if_type_str(pif->if_type),
                pconfig->netmask);
    }

    if (!inet_ipaddr_static_set(pif->if_inet, ipaddr, netmask, bcaddr))
    {
        LOG(WARN, "target_inet: %s (%s): Error setting the static IP configuration,"
                " ipaddr = "PRI(inet_ip4addr_t)
                " netmask = "PRI(inet_ip4addr_t)
                " bcaddr = "PRI(inet_ip4addr_t)".",
                pif->if_name,
                if_type_str(pif->if_type),
                FMT(inet_ip4addr_t, ipaddr),
                FMT(inet_ip4addr_t, netmask),
                FMT(inet_ip4addr_t, bcaddr));

        retval = false;
    }

    inet_ip4addr_t gateway = INET_IP4ADDR_ANY;
    if (pconfig->gateway_exists && !inet_ip4addr_fromstr(&gateway, pconfig->gateway))
    {
        LOG(WARN, "target_inet: %s (%s): Invalid broadcast address: %s",
                pif->if_name,
                if_type_str(pif->if_type),
                pconfig->netmask);
    }

    if (!inet_gateway_set(pif->if_inet, gateway))
    {
        LOG(WARN, "target_inet: %s (%s): Error setting the default gateway "PRI(inet_ip4addr_t)".",
                pif->if_name,
                if_type_str(pif->if_type),
                FMT(inet_ip4addr_t, gateway));

        retval = false;
    }

    return retval;
}


bool __if_config_dns_set(struct if_entry *pif, const struct schema_Wifi_Inet_Config *pconfig)
{
    inet_ip4addr_t primary_dns = INET_IP4ADDR_ANY;
    inet_ip4addr_t secondary_dns = INET_IP4ADDR_ANY;

    const char *sprimary = SCHEMA_FIND_KEY(pconfig->dns, "primary");
    const char *ssecondary = SCHEMA_FIND_KEY(pconfig->dns, "secondary");

    if (sprimary != NULL &&
            !inet_ip4addr_fromstr(&primary_dns, sprimary))
    {
        LOG(WARN, "target_inet: %s (%s): Invalid primary DNS: %s",
                pif->if_name,
                if_type_str(pif->if_type),
                sprimary);
    }

    if (ssecondary != NULL &&
            !inet_ip4addr_fromstr(&secondary_dns, ssecondary))
    {
        LOG(WARN, "target_inet: %s (%s): Invalid secondary DNS: %s",
                pif->if_name,
                if_type_str(pif->if_type),
                ssecondary);
    }

    if (!inet_dns_set(pif->if_inet, primary_dns, secondary_dns))
    {
        LOG(WARN, "target_inet: %s (%s): Error applying DNS settings.",
                pif->if_name,
                if_type_str(pif->if_type));

        return false;
    }

    return true;
}

bool __if_config_dhcps_options_set(struct if_entry *pif, const char *opts)
{
    int ii;

    char *topts = strdup(opts);

    /* opt_track keeps track of options that were set -- we must unset all other options at the end */
    uint8_t opt_track[C_SET_LEN(DHCP_OPTION_MAX, uint8_t)];
    memset(opt_track, 0, sizeof(opt_track));

    /* First, split by semi colons */
    char *psemi = topts;
    char *csemi;

    while ((csemi = strsep(&psemi, ";")) != NULL)
    {
        char *pcomma = csemi;

        /* Split by comma */
        char *sopt = strsep(&pcomma, ",");
        char *sval = pcomma;

        int opt = strtoul(sopt, NULL, 0);
        if (opt <= 0 || opt >= DHCP_OPTION_MAX)
        {
            LOG(WARN, "target_inet: %s (%s): Invalid DHCP option specified: %s. Ignoring.",
                    pif->if_name,
                    if_type_str(pif->if_type),
                    sopt);
            continue;
        }

        C_SET_ADD(opt_track, opt);

        if (!inet_dhcps_option_set(pif->if_inet, opt, sval))
        {
            LOG(WARN, "target_inet: %s (%s): Error setting DHCP option %d.",
                    pif->if_name,
                    if_type_str(pif->if_type),
                    opt);
        }
    }

    /* Clear all other options */
    for (ii = 0; ii < DHCP_OPTION_MAX; ii++)
    {
        if (C_IS_SET(opt_track, ii)) continue;

        if (!inet_dhcps_option_set(pif->if_inet, ii, NULL))
        {
            LOG(WARN, "target_inet: %s (%s): Error clearing DHCP option %d.",
                    pif->if_name,
                    if_type_str(pif->if_type),
                    ii);
        }
    }

error:
    if (topts != NULL) free(topts);

    return true;
}


bool __if_config_dhcps_set(struct if_entry *pif, const struct schema_Wifi_Inet_Config *pconfig)
{
    inet_ip4addr_t lease_start = INET_IP4ADDR_ANY;
    inet_ip4addr_t lease_stop = INET_IP4ADDR_ANY;
    int lease_time = -1;
    bool retval = true;

    const char *slease_time = SCHEMA_FIND_KEY(pconfig->dhcpd, "lease_time");
    const char *slease_start = SCHEMA_FIND_KEY(pconfig->dhcpd, "start");;
    const char *slease_stop = SCHEMA_FIND_KEY(pconfig->dhcpd, "stop");;
    const char *sdhcp_opts = SCHEMA_FIND_KEY(pconfig->dhcpd, "dhcp_option");

    /*
     * Parse the lease range, start/stop fields are just IPs
     */
    if (slease_start != NULL)
    {
        if (!inet_ip4addr_fromstr(&lease_start, slease_start))
        {
            LOG(WARN, "target_inet: %s (%s): Error parsing IP address lease_start: %s",
                    pif->if_name,
                    if_type_str(pif->if_type),
                    slease_start);
            retval = false;
        }
    }

    /* Lease stop options */
    if (slease_stop != NULL)
    {
        if (!inet_ip4addr_fromstr(&lease_stop, slease_stop))
        {
            LOG(WARN, "target_inet: %s (%s): Error parsing IP address lease_stop: %s",
                    pif->if_name,
                    if_type_str(pif->if_type),
                    slease_stop);
            retval = false;
        }
    }

    /*
     * Parse the lease time, the lease time may have a suffix denoting the units of time -- h for hours, m for minuts, s for seconds
     */
    if (slease_time != NULL)
    {
        char *suffix;
        /* The lease time is usually expressed with a suffix, for example 12h for hours. */
        lease_time = strtoul(slease_time, &suffix, 10);

        /* If the amount of characters converted is 0, then it's an error */
        if (suffix == slease_time)
        {
            LOG(WARN, "target_inet: %s (%s): Error parsing lease time: %s",
                    pif->if_name,
                    if_type_str(pif->if_type),
                    slease_time);
        }
        else
        {
            switch (*suffix)
            {
                case 'h':
                case 'H':
                    lease_time *= 60;
                    /* fall through */

                case 'm':
                case 'M':
                    lease_time *= 60;
                    /* fall through */

                case 's':
                case 'S':
                case '\0':
                    break;

                default:
                    LOG(WARN, "target_inet: %s (%s): Error parsing lease time -- invalid time suffix: %s",
                            pif->if_name,
                            if_type_str(pif->if_type),
                            slease_time);
                    lease_time = -1;
            }
        }
    }

    /*
     * Parse options -- options are a ';' separated list of comma separated key,value pairs
     */
    if (sdhcp_opts != NULL && !__if_config_dhcps_options_set(pif, sdhcp_opts))
    {
        LOG(ERR, "target_inet: %s (%s): Error parsing DHCP server options: %s",
                pif->if_name,
                if_type_str(pif->if_type),
                sdhcp_opts);
        retval = false;
    }

    if (!inet_dhcps_range_set(pif->if_inet, lease_start, lease_stop))
    {
        LOG(ERR, "target_inet: %s (%s): Error setting DHCP range.",
                pif->if_name,
                if_type_str(pif->if_type));
        retval = false;
    }

    if (!inet_dhcps_lease_set(pif->if_inet, lease_time))
    {
        LOG(ERR, "target_inet: %s (%s): Error setting DHCP lease time: %d",
                pif->if_name,
                if_type_str(pif->if_type),
                lease_time);
        retval = false;
    }


    bool enable = slease_start != NULL && slease_stop != NULL;

    /* Enable DHCP lease notifications */
    if (!inet_dhcps_lease_notify(pif->if_inet, if_dhcp_lease_notify))
    {
        LOG(ERR, "target_inet: %s (%s): Error registering DHCP lease event handler.",
                pif->if_name,
                if_type_str(pif->if_type));
        retval = false;
    }

    /* Start the DHCP server */
    if (!inet_dhcps_enable(pif->if_inet, enable))
    {
        LOG(ERR, "target_inet: %s (%s): Error %s DHCP server.",
                pif->if_name,
                if_type_str(pif->if_type),
                enable ? "enabling" : "disabling");
        retval = false;
    }

    return retval;
}


bool __if_config_dhcps_rip_set(struct if_entry *pif,
                               const struct schema_DHCP_reserved_IP *pconfig,
                               bool remove)
{
    inet_macaddr_t macaddr;
    inet_ip4addr_t ip4addr = INET_IP4ADDR_ANY;
    const char *hostname = NULL;

    memset(&macaddr, 0, sizeof(macaddr));

    if (!inet_macaddr_fromstr(&macaddr, pconfig->hw_addr))
        return false;
    if (!inet_ip4addr_fromstr(&ip4addr, pconfig->ip_addr))
        return false;
    if (pconfig->hostname_exists)
        hostname = pconfig->hostname;

    if (remove || INET_IP4ADDR_IS_ANY(ip4addr))
    {
        /* Remove IP reservation:  */
        if (!inet_dhcps_rip_del(pif->if_inet, macaddr))
        {
            LOG(ERR, "target_inet: %s (%s): Error removing DHCP reserved IP.",
                    pif->if_name,
                    if_type_str(pif->if_type));
            return false;
        }
    }
    else
    {
        /* Add or modify IP reservation:  */
        if (!inet_dhcps_rip_set(pif->if_inet, macaddr, ip4addr, hostname))
        {
            LOG(ERR, "target_inet: %s (%s): Error setting DHCP reserved IP.",
                    pif->if_name,
                    if_type_str(pif->if_type));
            return false;
        }
    }

    return true;
}


bool __if_config_ip4tunnel_set(struct if_entry *pif, const struct schema_Wifi_Inet_Config *pconfig)
{
    bool retval = true;
    const char *ifparent = NULL;
    inet_ip4addr_t local_addr = INET_IP4ADDR_ANY;
    inet_ip4addr_t remote_addr = INET_IP4ADDR_ANY;
    inet_macaddr_t remote_mac = INET_MACADDR_ANY;

    if (pif->if_type != IF_TYPE_GRE) return retval;

    if (pconfig->gre_ifname_exists)
    {
        if (pconfig->gre_ifname[0] == '\0')
        {
            LOG(WARN, "target_inet: %s (%s): Parent interface exists, but is empty.",
                    pif->if_name,
                    if_type_str(pif->if_type));
            retval = false;
        }

        ifparent = pconfig->gre_ifname;
    }

    if (pconfig->gre_local_inet_addr_exists)
    {
        if (!inet_ip4addr_fromstr(&local_addr, pconfig->gre_local_inet_addr))
        {
            LOG(WARN, "target_inet: %s (%s): Local IPv4 address is invalid: %s",
                    pif->if_name,
                    if_type_str(pif->if_type),
                    pconfig->gre_local_inet_addr);

            retval = false;
        }
    }

    if (pconfig->gre_remote_inet_addr_exists)
    {
        if (!inet_ip4addr_fromstr(&remote_addr, pconfig->gre_remote_inet_addr))
        {
            LOG(WARN, "target_inet: %s (%s): Remote IPv4 address is invalid: %s",
                    pif->if_name,
                    if_type_str(pif->if_type),
                    pconfig->gre_remote_inet_addr);

            retval = false;
        }
    }

    if (pconfig->gre_remote_mac_addr_exists)
    {
        if (!inet_macaddr_fromstr(&remote_mac, pconfig->gre_remote_mac_addr))
        {
            LOG(WARN, "target_inet: %s (%s): Remote MAC address is invalid: %s",
                    pif->if_name,
                    if_type_str(pif->if_type),
                    pconfig->gre_remote_mac_addr);

            retval = false;
        }
    }

    if (!inet_ip4tunnel_set(pif->if_inet, ifparent, local_addr, remote_addr, remote_mac))
    {
        LOG(ERR, "target_inet: %s (%s): Error setting IPv4 tunnel settings: parent=%s local="PRI(inet_ip4addr_t)" remote="PRI(inet_ip4addr_t)" remote_mac="PRI(inet_macaddr_t),
                pif->if_name,
                if_type_str(pif->if_type),
                ifparent,
                FMT(inet_ip4addr_t, local_addr),
                FMT(inet_ip4addr_t, remote_addr),
                FMT(inet_macaddr_t, remote_mac));

        retval = false;
    }

    return retval;
}

bool __if_config_dhsnif_set(struct if_entry *pif, const struct schema_Wifi_Inet_Config *pconfig)
{
    if (pconfig->dhcp_sniff_exists && pconfig->dhcp_sniff)
    {
        return inet_dhsnif_lease_notify(pif->if_inet, if_dhcp_lease_notify);
    }
    else
    {
        return inet_dhsnif_lease_notify(pif->if_inet, NULL);
    }

    return false;
}

bool if_config_set(const char *ifname, enum if_type type, const struct schema_Wifi_Inet_Config *pconfig)
{
    TRACE();
    struct if_entry *pif = NULL;
    bool retval = true;

    pif = if_entry_get(ifname, type);
    if (pif == NULL)
    {
        return false;
    }

    if (!inet_interface_enable(pif->if_inet, pconfig->enabled))
    {
        LOG(WARN, "target_inet: %s (%s): Error enabling interface (%d).",
                ifname,
                if_type_str(type),
                pconfig->enabled);

        retval = false;
    }

    if (!inet_network_enable(pif->if_inet, pconfig->network))
    {
        LOG(WARN, "target_inet: %s (%s): Error enabling network (%d).",
                ifname,
                if_type_str(type),
                pconfig->network);

        retval = false;
    }

    if (pconfig->mtu_exists && !inet_mtu_set(pif->if_inet, pconfig->mtu))
    {
        LOG(WARN, "target_inet: %s (%s): Error setting MTU %d.",
                ifname,
                if_type_str(type),
                pconfig->mtu);

        retval = false;
    }

    if (!inet_nat_enable(pif->if_inet, pconfig->NAT_exists && pconfig->NAT))
    {
        LOG(WARN, "target_inet: %s (%s): Error enabling NAT (%d).",
                ifname,
                if_type_str(type),
                pconfig->NAT_exists && pconfig->NAT);

        retval = false;
    }

    retval &= __if_config_igmp_set(pif, pconfig);
    retval &= __if_config_ip_assign_scheme_set(pif, pconfig);
    retval &= __if_config_upnp_set(pif, pconfig);
    retval &= __if_config_static_set(pif, pconfig);
    retval &= __if_config_dns_set(pif, pconfig);
    retval &= __if_config_dhcps_set(pif, pconfig);
    retval &= __if_config_ip4tunnel_set(pif, pconfig);
    retval &= __if_config_dhsnif_set(pif, pconfig);

    if_delayed_commit(pif);

    /* Copy fields that should be simply copied to Wifi_Inet_State */
    INET_CONFIG_COPY(pif->if_cache.if_type, pconfig->if_type);
    INET_CONFIG_COPY(pif->if_cache._uuid, pconfig->_uuid);
    INET_CONFIG_COPY(pif->if_cache.if_uuid, pconfig->if_uuid);
    INET_CONFIG_COPY(pif->if_cache.dns, pconfig->dns);
    INET_CONFIG_COPY(pif->if_cache.dns_keys, pconfig->dns_keys);
    pif->if_cache.dns_len = pconfig->dns_len;
    INET_CONFIG_COPY(pif->if_cache.dhcpd, pconfig->dhcpd);
    INET_CONFIG_COPY(pif->if_cache.dhcpd_keys, pconfig->dhcpd_keys);
    pif->if_cache.dhcpd_len = pconfig->dhcpd_len;
    pif->if_cache.gre_ifname_exists = pconfig->gre_ifname_exists;
    INET_CONFIG_COPY(pif->if_cache.gre_ifname, pconfig->gre_ifname);
    pif->if_cache.gre_ifname_exists = pconfig->gre_ifname_exists;
    INET_CONFIG_COPY(pif->if_cache.gre_ifname, pconfig->gre_ifname);
    pif->if_cache.gre_remote_inet_addr_exists = pconfig->gre_remote_inet_addr_exists;
    INET_CONFIG_COPY(pif->if_cache.gre_remote_inet_addr, pconfig->gre_remote_inet_addr);
    pif->if_cache.gre_local_inet_addr_exists = pconfig->gre_local_inet_addr_exists;
    INET_CONFIG_COPY(pif->if_cache.gre_local_inet_addr, pconfig->gre_local_inet_addr);

    /*
     * Send notification about a configuration change regardless of the status in retval -- it might
     * have been a partially applied so a status change is warranted
     */
    if (pif->if_istate_cb != NULL || pif->if_mstate_cb != NULL) pif->if_state_notify = true;

    /* XXX: Always return true -- NM2 doesn't update state tables otherwise */
    /* return retval; */
    return true;
}

/**
 * Convert the interface status in @pif to a schema inet state structure
 */
void __if_state_get_copy(
        struct schema_Wifi_Inet_State *pstate,
        struct if_entry *pif)
{
    /**
     * Fill out the schema_Wifi_Inet_State structure
     */

    /* Clear the current state */
    memset(pstate, 0, sizeof(*pstate));

    strscpy(pstate->if_name, pif->if_name, sizeof(pstate->if_name));

    pstate->enabled = pif->if_state.in_interface_enabled;
    pstate->network = pif->if_state.in_network_enabled;

    pstate->mtu_exists = true;
    pstate->mtu = pif->if_state.in_mtu;

    pstate->NAT_exists = true;
    pstate->NAT = pif->if_state.in_nat_enabled;

    pstate->inet_addr_exists = true;
    snprintf(pstate->inet_addr, sizeof(pstate->inet_addr), PRI(inet_ip4addr_t),
            FMT(inet_ip4addr_t, pif->if_state.in_ipaddr));

    pstate->netmask_exists = true;
    snprintf(pstate->netmask, sizeof(pstate->netmask), PRI(inet_ip4addr_t),
            FMT(inet_ip4addr_t, pif->if_state.in_netmask));

    pstate->broadcast_exists = true;
    snprintf(pstate->broadcast, sizeof(pstate->broadcast), PRI(inet_ip4addr_t),
            FMT(inet_ip4addr_t, pif->if_state.in_bcaddr));

    pstate->gateway_exists = true;
    snprintf(pstate->gateway, sizeof(pstate->gateway), PRI(inet_ip4addr_t),
            FMT(inet_ip4addr_t, pif->if_state.in_gateway));

    snprintf(pstate->hwaddr, sizeof(pstate->hwaddr), PRI(inet_macaddr_t),
            FMT(inet_macaddr_t, pif->if_state.in_macaddr));

    /*
     * Copy the assignment scheme
     */
    char *scheme = NULL;

    switch (pif->if_state.in_assign_scheme)
    {
        case INET_ASSIGN_NONE:
            scheme = "none";
            break;

        case INET_ASSIGN_STATIC:
            scheme = "static";
            break;

        case INET_ASSIGN_DHCP:
            scheme = "dhcp";

        default:
            break;
    }

    if (scheme != NULL && strscpy(pstate->ip_assign_scheme, scheme, sizeof(pstate->ip_assign_scheme)) > 0)
    {
        pstate->ip_assign_scheme_exists = true;
    }
    else
    {
        pstate->ip_assign_scheme_exists = false;
    }

    /*
     * UPnP mode
     */
    char *upnp = NULL;
    switch (pif->if_state.in_upnp_mode)
    {
        case UPNP_MODE_NONE:
            upnp = "disabled";
            break;

        case UPNP_MODE_INTERNAL:
            upnp = "internal";
            break;

        case UPNP_MODE_EXTERNAL:
            upnp = "external";
            break;
    }

    pstate->upnp_mode_exists = true;
    strscpy(pstate->upnp_mode, upnp, sizeof(pstate->upnp_mode));

    /* Copy fields that should be simply copied to Wifi_Inet_State */
    INET_CONFIG_COPY(pstate->if_type, pif->if_cache.if_type);
    //INET_CONFIG_COPY(pstate->if_uuid, pif->if_cache.if_uuid);
    /* XXX: if_uuid must be populated from the _uuid field of Inet_Config -- if_uuid is not an uuid type though */
    strscpy(pstate->if_uuid, (char *)pif->if_cache._uuid, sizeof(pstate->if_uuid));

    INET_CONFIG_COPY(pstate->inet_config, pif->if_cache._uuid);
    INET_CONFIG_COPY(pstate->dns, pif->if_cache.dns);
    INET_CONFIG_COPY(pstate->dns_keys, pif->if_cache.dns_keys);
    pstate->dns_len = pif->if_cache.dns_len;
    INET_CONFIG_COPY(pstate->dhcpd, pif->if_cache.dhcpd);
    INET_CONFIG_COPY(pstate->dhcpd_keys, pif->if_cache.dhcpd_keys);
    pstate->dhcpd_len = pif->if_cache.dhcpd_len;

    pstate->gre_ifname_exists = pif->if_cache.gre_ifname_exists;
    INET_CONFIG_COPY(pstate->gre_ifname, pif->if_cache.gre_ifname);
    pstate->gre_remote_inet_addr_exists = pif->if_cache.gre_remote_inet_addr_exists;
    INET_CONFIG_COPY(pstate->gre_remote_inet_addr, pif->if_cache.gre_remote_inet_addr);
    pstate->gre_local_inet_addr_exists = pif->if_cache.gre_local_inet_addr_exists;
    INET_CONFIG_COPY(pstate->gre_local_inet_addr, pif->if_cache.gre_local_inet_addr);

    /* Unsupported fields, for now */
    pstate->vlan_id_exists = false;
    pstate->parent_ifname_exists = false;

    pstate->softwds_mac_addr_exists = false;
    pstate->softwds_wrap = false;

}

bool if_state_get(const char *ifname, enum if_type type, struct schema_Wifi_Inet_State *pstate)
{
    TRACE();
    struct if_entry *pif = if_entry_get(ifname, type);

    if (pif == NULL)
    {
        LOG(ERR, "target_inet: %s (%s): Unable to retrieve interface state, there's no instance yet.",
                ifname,
                if_type_str(type));

        return false;
    }

    if (pif->if_inet == NULL)
    {
        LOG(ERR, "target_inet: %s (%s): Unable to retrieve interface state, no inet instance.",
                pif->if_name,
                if_type_str(pif->if_type));

        return false;
    }

    if (!inet_state_get(pif->if_inet, &pif->if_state))
    {
        LOG(ERR, "target_inet: %s (%s): Unable to retrieve interface state.",
                pif->if_name,
                if_type_str(pif->if_type));

        return false;
    }

    __if_state_get_copy(pstate, pif);

    return true;
}

/**
 * Convert the interface status in @pif to a schema master state structure
 */
void __if_master_get_copy(
        struct schema_Wifi_Master_State *pstate,
        struct if_entry *pif)
{
    /**
     * Fill out the schema_Wifi_Inet_State structure
     */

    /* Clear the current state */
    memset(pstate, 0, sizeof(*pstate));

    strscpy(pstate->if_name, pif->if_name, sizeof(pstate->if_name));

    pstate->port_state_exists = true;
    snprintf(pstate->port_state, sizeof(pstate->port_state), "%s",
            pif->if_state.in_port_status ? "active" : "inactive");

    pstate->network_state_exists = true;
    snprintf(pstate->network_state, sizeof(pstate->network_state), "%s",
            pif->if_state.in_network_enabled ? "up" : "down");

    pstate->inet_addr_exists = true;
    snprintf(pstate->inet_addr, sizeof(pstate->inet_addr), PRI(inet_ip4addr_t),
            FMT(inet_ip4addr_t, pif->if_state.in_ipaddr));

    pstate->netmask_exists = true;
    snprintf(pstate->netmask, sizeof(pstate->netmask), PRI(inet_ip4addr_t),
            FMT(inet_ip4addr_t, pif->if_state.in_netmask));

    INET_CONFIG_COPY(pstate->if_type, pif->if_cache.if_type);

    // Apparently this is a required field, but it's not really used in PML1.2
    strscpy(pstate->if_uuid.uuid, "00000000-0000-0000-0000-000000000000", sizeof(pstate->if_uuid.uuid));
    pstate->if_uuid_exists = true;

    /* XXX: Unsupported fields for now */
    pstate->onboard_type_exists = false;
    pstate->uplink_priority_exists = false;
    pstate->dhcpc_len = 0;

    /* Do not update the port_status for VIF interfaces */
    if (pif->if_type == IF_TYPE_VIF)
    {
        //LOG(TRACE, "target_inet: %s (%s): mark port_state not present", pif->if_name, if_type_str(pif->if_type));
        schema_Wifi_Master_State_mark_all_present(pstate);
        pstate->_partial_update = true;
        pstate->port_state_present = false;
    }
}

bool if_master_get(const char *ifname, enum if_type type, struct schema_Wifi_Master_State *pstate)
{
    struct if_entry *pif = if_entry_get(ifname, type);

    if (pif == NULL)
    {
        LOG(ERR, "target_inet: %s (%s): Unable to retrieve interface master state, there's no instance yet.",
                ifname,
                if_type_str(type));
        return false;
    }

    if (pif->if_inet == NULL)
    {
        LOG(ERR, "target_inet: %s (%s): Unable to retrieve interface master state, no inet instance.",
                pif->if_name,
                if_type_str(pif->if_type));
        return false;
    }

    if (!inet_state_get(pif->if_inet, &pif->if_state))
    {
        LOG(ERR, "target_inet: %s (%s): Unable to retrieve interface master state.",
                pif->if_name,
                if_type_str(pif->if_type));
        return false;
    }

    __if_master_get_copy(pstate, pif);

    return true;
}


/**
 * Status poll
 */
void if_status_poll(void)
{
    ds_dlist_iter_t iter;
    struct if_entry *pif;
    inet_state_t tstate;

    /**
     * Traverse the list of interface registered for status polling
     */
    for (pif = ds_dlist_ifirst(&iter, &if_poll_list);
            pif != NULL;
            pif = ds_dlist_inext(&iter))
    {
        inet_state_get(pif->if_inet, &tstate);

        /* Binary compare old and new states */
        if (memcmp(&pif->if_state, &tstate, sizeof(pif->if_state)) != 0)
        {
            memcpy(&pif->if_state, &tstate, sizeof(pif->if_state));
            pif->if_state_notify = true;
        }

        if (!pif->if_state_notify) continue;

        if (pif->if_istate_cb != NULL)
        {
            struct schema_Wifi_Inet_State pstate;
            __if_state_get_copy(&pstate, pif);
            pif->if_istate_cb(&pstate, NULL);
        }

        if (pif->if_mstate_cb != NULL)
        {
            struct schema_Wifi_Master_State pstate;
            __if_master_get_copy(&pstate, pif);
            pif->if_mstate_cb(&pstate, NULL);
        }

        pif->if_state_notify = false;
    }
}

/*
 * ===========================================================================
 *  DHCP lease/sniffing handling
 * ===========================================================================
 */

bool inet_target_dhcp_leased_ip_register(target_dhcp_leased_ip_cb_t *dlip_cb)
{
    if_dhcp_lease_callback = dlip_cb;

    return true;
}

bool __inet_target_dhcp_rip_set(
        const char *ifname,
        struct schema_DHCP_reserved_IP *schema_rip,
        bool remove)
{
    struct if_entry *pif = NULL;
    inet_ip4addr_t client_ip4addr;

    memset(&client_ip4addr, 0, sizeof(client_ip4addr));

    if (ifname)
    {
        pif = if_entry_get(ifname, IF_TYPE_NONE);
        if (!pif)
        {
            LOG(ERR, "inet_target: dhcp_rip: No interface '%s' found. Unable to process DHCP reservation: ip=%s mac=%s",
                    ifname,
                    schema_rip->ip_addr,
                    schema_rip->hw_addr);
            return false;
        }
    }
    else
    {
        /* If ifname not explicitly set (most often the case), we must do a
         * IP/subnet lookup to find the matching interface for client IP. */
        if (!inet_ip4addr_fromstr(&client_ip4addr, schema_rip->ip_addr))
        {
            LOG(ERR, "inet_target: dhcp_rip: Invalid IP address: %s", schema_rip->ip_addr);
            return false;
        }

        pif = if_entry_find_by_client_ip(client_ip4addr);
        if (!pif)
        {
            LOG(ERR, "inet_target: dhcp_rip: Unable to find interface configuration with subnet: ip=%s mac=%s",
                    schema_rip->ip_addr,
                    schema_rip->hw_addr);
            return false;
        }
    }

    /* Push new configuration */
    if (!__if_config_dhcps_rip_set(pif, schema_rip, remove))
    {
        LOG(ERR, "inet_target: dhcp_rip: Error processing DHCP reservation: ip=%s mac=%s",
                schema_rip->ip_addr,
                schema_rip->hw_addr);
        return false;
    }

    /* Commit configuration */
    if_delayed_commit(pif);

    return true;
}


bool inet_target_dhcp_rip_set(
        const char *ifname,
        struct schema_DHCP_reserved_IP *schema_rip)
{
    return __inet_target_dhcp_rip_set(ifname, schema_rip, false);
}

bool inet_target_dhcp_rip_del(
        const char *ifname,
        struct schema_DHCP_reserved_IP *schema_rip)
{
    return __inet_target_dhcp_rip_set(ifname, schema_rip, true);
}

bool __inet_target_portforward(
        const char *ifname,
        struct schema_IP_Port_Forward *schema_pf,
        bool remove)
{
    struct if_entry *pif = NULL;
    struct inet_portforward portfw;
    bool rc = 0;

    memset(&portfw, 0, sizeof(portfw));

    if (!ifname)
        ifname = schema_pf->src_ifname;

    pif = if_entry_get(ifname, IF_TYPE_NONE);
    if (!pif)
        return false;


    LOG(DEBUG, "inet_target: %s (%s): %s port forwarding entry: "
               "dst_ip=%s, dst_port=%d, src_port=%d, proto=%s",
               pif->if_name, if_type_str(pif->if_type),
               remove ? "deleting" : "adding",
               schema_pf->dst_ipaddr, schema_pf->dst_port, schema_pf->src_port,
               schema_pf->protocol);


    if (!inet_ip4addr_fromstr(&portfw.pf_dst_ipaddr, schema_pf->dst_ipaddr))
        return false;
    if (!IS_PORT_VALID(schema_pf->dst_port) || !IS_PORT_VALID(schema_pf->src_port))
        return false;
    portfw.pf_dst_port = (uint16_t)schema_pf->dst_port;
    portfw.pf_src_port = (uint16_t)schema_pf->src_port;
    if (!strcmp("udp", schema_pf->protocol))
        portfw.pf_proto = INET_PROTO_UDP;
    else if (!strcmp("tcp", schema_pf->protocol))
        portfw.pf_proto = INET_PROTO_TCP;
    else
        return false;

    if (remove)
    {
        rc = inet_portforward_del(pif->if_inet, &portfw);
    }
    else
    {
        rc = inet_portforward_set(pif->if_inet, &portfw);
    }

    if (!rc)
    {
        LOG(ERR, "inet_target: %s (%s): Error %s port forwarding entry: "
                 "dst_ip=%s, dst_port=%d, src_port=%d, proto=%s",
                  pif->if_name, if_type_str(pif->if_type),
                  remove ? "deleting" : "adding",
                  schema_pf->dst_ipaddr, schema_pf->dst_port, schema_pf->src_port,
                  schema_pf->protocol);
        return false;
    }

    if_delayed_commit(pif);

    return true;
}


bool inet_target_portforward_set(
        const char *ifname,
        struct schema_IP_Port_Forward *schema_pf)
{
    return __inet_target_portforward(ifname, schema_pf, false);
}

bool inet_target_portforward_del(
        const char *ifname,
        struct schema_IP_Port_Forward *schema_pf)
{
    return __inet_target_portforward(ifname, schema_pf, true);
}

bool if_dhcp_lease_notify(
        inet_t *self,
        bool released,
        struct inet_dhcp_lease_info *dl)
{
    if (if_dhcp_lease_callback == NULL) return false;

    bool ip_is_any = INET_IP4ADDR_IS_ANY(dl->dl_ipaddr);

    LOG(INFO, "target_inet: %s DHCP lease: MAC:"PRI(inet_macaddr_t)" IP:"PRI(inet_ip4addr_t)" Hostname:%s Time:%d%s",
            released ? "Released" : "Acquired",
            FMT(inet_macaddr_t, dl->dl_hwaddr),
            FMT(inet_ip4addr_t, dl->dl_ipaddr),
            dl->dl_hostname,
            (int)dl->dl_leasetime,
            ip_is_any ? ", skipping" : "");

    if (ip_is_any) return true;

    /* Fill in the schema structure */
    struct schema_DHCP_leased_IP sdl;

    memset(&sdl, 0, sizeof(sdl));

    sdl.hwaddr_exists = true;
    snprintf(sdl.hwaddr, sizeof(sdl.hwaddr), PRI(inet_macaddr_t), FMT(inet_macaddr_t, dl->dl_hwaddr));

    sdl.inet_addr_exists = true;
    snprintf(sdl.inet_addr, sizeof(sdl.inet_addr), PRI(inet_ip4addr_t), FMT(inet_ip4addr_t, dl->dl_ipaddr));

    sdl.hostname_exists = true;
    strscpy(sdl.hostname, dl->dl_hostname, sizeof(sdl.hostname));

    sdl.fingerprint_exists = true;
    strscpy(sdl.fingerprint, dl->dl_fingerprint, sizeof(sdl.fingerprint));

    sdl.vendor_class_exists = true;
    strscpy(sdl.vendor_class, dl->dl_vendorclass, sizeof(sdl.vendor_class));

    /* A lease time of 0 indicates that this entry should be deleted */
    sdl.lease_time_exists = true;
    if (released)
    {
        sdl.lease_time = 0;
    }
    else
    {
        sdl.lease_time = (int)dl->dl_leasetime;
        /* OLPS-153: sdl.lease_time should never be 0 on first insert! */
        if (sdl.lease_time == 0) sdl.lease_time = -1;
    }

    if (!if_dhcp_lease_callback(&sdl))
    {
        LOG(WARN, "target_inet: Error processing DCHP lease entry "PRI(inet_macaddr_t)" ("PRI(inet_ip4addr_t)", %s)",
                FMT(inet_macaddr_t, dl->dl_hwaddr),
                FMT(inet_ip4addr_t, dl->dl_ipaddr),
                dl->dl_hostname);
    }

    return true;
}

/*
 * ===========================================================================
 *  Routing table status reporting
 * ===========================================================================
 */
bool if_route_state_notify(inet_t *self, struct inet_route_state *rts, bool remove)
{
    struct schema_Wifi_Route_State schema_rts;

    LOG(TRACE, "target_init: %s: Route state notify, remove = %d", self->in_ifname, remove);

    if (if_route_state_callback == NULL) return false;

    memset(&schema_rts, 0, sizeof(schema_rts));

    if (strscpy(schema_rts.if_name, self->in_ifname, sizeof(schema_rts.if_name)) < 0)
    {
        LOG(WARN, "target_inet: %s: Route state interface name too long.", self->in_ifname);
        return false;
    }

    snprintf(schema_rts.dest_addr, sizeof(schema_rts.dest_addr), PRI(inet_ip4addr_t),
            FMT(inet_ip4addr_t, rts->rts_dst_ipaddr));

    snprintf(schema_rts.dest_mask, sizeof(schema_rts.dest_mask), PRI(inet_ip4addr_t),
            FMT(inet_ip4addr_t, rts->rts_dst_mask));

    snprintf(schema_rts.gateway, sizeof(schema_rts.gateway), PRI(inet_ip4addr_t),
            FMT(inet_ip4addr_t, rts->rts_gw_ipaddr));

    snprintf(schema_rts.gateway_hwaddr, sizeof(schema_rts.gateway_hwaddr), PRI(inet_macaddr_t),
            FMT(inet_macaddr_t, rts->rts_gw_hwaddr));

    schema_rts.gateway_hwaddr_exists = inet_macaddr_cmp(&rts->rts_gw_hwaddr, &INET_MACADDR_ANY) != 0;

    schema_rts._update_type = remove ? OVSDB_UPDATE_DEL : OVSDB_UPDATE_MODIFY;

    if_route_state_callback(&schema_rts);

    return true;
}

bool inet_target_route_state_register(target_route_state_cb_t *rts_cb)
{
    if_route_state_callback = rts_cb;

    return true;
}
