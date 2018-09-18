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

#ifndef INET_H_INCLUDED
#define INET_H_INCLUDED

#include <stdbool.h>
#include <stdlib.h>

#include "os_types.h"
#include "const.h"

#include "inet_addr.h"

/*
 * ===========================================================================
 *  inet_t main interface
 * ===========================================================================
 */

/**
 * Amount of time to wait before commiting current configuration
 */
enum inet_assign_scheme
{
    INET_ASSIGN_INVALID,
    INET_ASSIGN_NONE,
    INET_ASSIGN_STATIC,
    INET_ASSIGN_DHCP
};

/**
 * UPNP MODE
 */
enum inet_upnp_mode
{
    UPNP_MODE_NONE,         /* Default, no UPnP settings */
    UPNP_MODE_INTERNAL,     /* This is the LAN facing UPnP interface */
    UPNP_MODE_EXTERNAL,     /* This is the WAN facing UPnP interface */
};


/*
 * DHCP client option list
 *
 * Feel free to complete this list using the URL below:
 * https://www.iana.org/assignments/bootp-dhcp-parameters/bootp-dhcp-parameters.xhtml
 */
enum inet_dhcp_option
{
    DHCP_OPTION_SUBNET_MASK = 1,
    DHCP_OPTION_ROUTER = 3,
    DHCP_OPTION_DNS_SERVERS = 6,
    DHCP_OPTION_HOSTNAME = 12,
    DHCP_OPTION_DOMAIN_NAME = 15,
    DHCP_OPTION_BCAST_ADDR = 28,
    DHCP_OPTION_VENDOR_SPECIFIC = 43,
    DHCP_OPTION_ADDRESS_REQUEST = 50,
    DHCP_OPTION_LEASE_TIME = 51,
    DHCP_OPTION_MSG_TYPE = 53,
    DHCP_OPTION_PARAM_LIST = 55,
    DHCP_OPTION_VENDOR_CLASS = 60,
    DHCP_OPTION_PLUME_SWVER = 225,
    DHCP_OPTION_PLUME_PROFILE = 226,
    DHCP_OPTION_PLUME_SERIAL_OPT = 227,
    DHCP_OPTION_MAX
};

/*
 * ===========================================================================
 *  Inet class definition
 * ===========================================================================
 */
typedef struct __inet_state inet_state_t;

struct __inet_state
{
    bool                    in_interface_enabled;
    bool                    in_network_enabled;
    enum inet_assign_scheme in_assign_scheme;
    int                     in_mtu;
    bool                    in_nat_enabled;
    enum inet_upnp_mode     in_upnp_mode;
    bool                    in_dhcps_enabled;
    bool                    in_dhcpc_enabled;
    inet_macaddr_t          in_macaddr;
    inet_ip4addr_t          in_ipaddr;
    inet_ip4addr_t          in_netmask;
    inet_ip4addr_t          in_bcaddr;
    inet_ip4addr_t          in_gateway;
    bool                    in_port_status;
};

typedef struct __inet inet_t;

#define INET_DL_FINGERPRINT_MAX     256
#define INET_DL_VENDORCLASS_MAX     256

struct inet_dhcp_lease_info
{
    inet_macaddr_t      dl_hwaddr;                                  /* Client hardware address */
    inet_ip4addr_t      dl_ipaddr;                                  /* Client IPv4 address */
    char                dl_hostname[C_HOSTNAME_LEN];                /* Hostname */
    char                dl_fingerprint[INET_DL_FINGERPRINT_MAX];    /* Fingerprint info */
    char                dl_vendorclass[INET_DL_VENDORCLASS_MAX];    /* Vendor class info */
    double              dl_leasetime;                               /* Lease time in seconds */
};

typedef bool inet_dhcp_lease_fn_t(
        inet_t *self,
        bool released,
        struct inet_dhcp_lease_info *dl);

struct __inet
{
    /* Destructor function */
    bool        (*in_dtor_fn)(inet_t *self);

    /* Enable/disable interface */
    bool        (*in_interface_enable_fn)(inet_t *self, bool enable);

    /* Set interface network  */
    bool        (*in_network_enable_fn)(inet_t *self, bool enable);

    /* Set MTU */
    bool        (*in_mtu_set_fn)(inet_t *self, int mtu);

    /**
     * Set IP assignment scheme:
     *   - INET_ASSIGN_NONE     - Interface does not have any IPv4 configuration
     *   - INET_ASSIGN_STATIC   - Use a static IP/Netmask/Broadcast address
     *   - INET_ASSIGN_DHCP     - Use dynamic IP address assignment
     */
    bool        (*in_assign_scheme_set_fn)(inet_t *self, enum inet_assign_scheme scheme);

    /*
     * Set IP Address/Netmask/Broadcast/Gateway -- only when assign_scheme == INET_ASSIGN_STATIC
     *
     * bcast and gateway can be INET_ADDR_NONE
     */
    bool        (*in_ipaddr_static_set_fn)(
                        inet_t *self,
                        inet_ip4addr_t addr,
                        inet_ip4addr_t netmask,
                        inet_ip4addr_t bcast);

    /* Set the interface default gateway, typically set when assign_scheme == INET_ASSIGN_STATIC */
    bool        (*in_gateway_set_fn)(inet_t *self, inet_ip4addr_t  gwaddr);

    /* Enable NAT */
    bool        (*in_nat_enable_fn)(inet_t *self, bool enable);
    bool        (*in_upnp_mode_set_fn)(inet_t *self, enum inet_upnp_mode mode);

    /* Set primary/secondary DNS servers */
    bool        (*in_dns_set_fn)(inet_t *self, inet_ip4addr_t primary, inet_ip4addr_t secondary);

    /* DHCP client options */
    bool        (*in_dhcpc_option_request_fn)(inet_t *self, enum inet_dhcp_option opt, bool req);
    bool        (*in_dhcpc_option_set_fn)(inet_t *self, enum inet_dhcp_option opt, const char *value);

    /* True if DHCP server should be enabled on this interface */
    bool        (*in_dhcps_enable_fn)(inet_t *self, bool enabled);

    /* DHCP server otpions */
    bool        (*in_dhcps_lease_set_fn)(inet_t *self, int lease_time_s);
    bool        (*in_dhcps_range_set_fn)(inet_t *self, inet_ip4addr_t start, inet_ip4addr_t stop);
    bool        (*in_dhcps_option_set_fn)(inet_t *self, enum inet_dhcp_option opt, const char *value);
    bool        (*in_dhcps_lease_register_fn)(inet_t *self, inet_dhcp_lease_fn_t *fn);
    bool        (*in_dhcps_rip_set_fn)(inet_t *super, inet_macaddr_t macaddr,
                                       inet_ip4addr_t ip4addr, const char *hostname);
    bool        (*in_dhcps_rip_del_fn)(inet_t *super, inet_macaddr_t macaddr);


    /* IPv4 tunnels (GRE, softwds) */
    bool        (*in_ip4tunnel_set_fn)(inet_t *self, const char *parent, inet_ip4addr_t local, inet_ip4addr_t remote);

    /* DHCP sniffing */
    bool        (*in_dhsnif_enable)(inet_t *self, bool enable);

    /* DHCP sniffing - register callback for DHCP sniffing - if set to NULL sniffing is disabled */
    bool        (*in_dhsnif_lease_register_fn)(inet_t *self, inet_dhcp_lease_fn_t *func);

    /* Commit all pending changes */
    bool        (*in_commit_fn)(inet_t *self);

    /* State get method */
    bool        (*in_state_get_fn)(inet_t *self, inet_state_t *out);

    /* Interface name */
    char        in_ifname[C_IFNAME_LEN];
};

/*
 * ===========================================================================
 *  Inet Class Interfaces
 * ===========================================================================
 */

/**
 * Static destructor, counterpart to _init()
 */
static inline bool inet_fini(inet_t *self)
{
    if (self->in_dtor_fn == NULL) return false;

    return self->in_dtor_fn(self);
}

/**
 * Dynamic destructor, counterpart to _new()
 */
static inline bool inet_del(inet_t *self)
{
    bool retval = inet_fini(self);

    free(self);

    return retval;
}

static inline bool inet_interface_enable(inet_t *self, bool enable)
{
    if (self->in_interface_enable_fn == NULL) return false;

    return self->in_interface_enable_fn(self, enable);
}

static inline bool inet_network_enable(inet_t *self, bool enable)
{
    if (self->in_network_enable_fn == NULL) return false;

    return self->in_network_enable_fn(self, enable);
}

static inline bool inet_mtu_set(inet_t *self, int mtu)
{
    if (self->in_mtu_set_fn == NULL) return false;

    return self->in_mtu_set_fn(self, mtu);
}

static inline bool inet_assign_scheme_set(inet_t *self, enum inet_assign_scheme scheme)
{
    if (self->in_assign_scheme_set_fn == NULL) return false;

    return self->in_assign_scheme_set_fn(self, scheme);
}

static inline bool inet_ipaddr_static_set(
        inet_t *self,
        inet_ip4addr_t ipaddr,
        inet_ip4addr_t netmask,
        inet_ip4addr_t bcaddr)
{
    if (self->in_ipaddr_static_set_fn == NULL) return false;

    return self->in_ipaddr_static_set_fn(
            self,
            ipaddr,
            netmask,
            bcaddr);
}

static inline bool inet_gateway_set(inet_t *self, inet_ip4addr_t gwaddr)
{
    if (self->in_gateway_set_fn == NULL) return false;

    return self->in_gateway_set_fn(self, gwaddr);
}

static inline bool inet_nat_enable(inet_t *self, bool enable)
{
    if (self->in_nat_enable_fn == NULL) return false;

    return self->in_nat_enable_fn(self, enable);
}

static inline bool inet_upnp_mode_set(inet_t *self, enum inet_upnp_mode mode)
{
    if (self->in_upnp_mode_set_fn == NULL) return false;

    return self->in_upnp_mode_set_fn(self, mode);
}

static inline bool inet_dns_set(inet_t *self, inet_ip4addr_t primary, inet_ip4addr_t secondary)
{
    if (self->in_dns_set_fn == NULL) return false;

    return self->in_dns_set_fn(self, primary, secondary);
}

static inline bool inet_dhcpc_option_request(inet_t *self, enum inet_dhcp_option opt, bool req)
{
    if (self->in_dhcpc_option_set_fn == NULL) return false;

    return self->in_dhcpc_option_request_fn(self, opt, req);
}

static inline bool inet_dhcpc_option_set(inet_t *self, enum inet_dhcp_option opt, const char *value)
{
    if (self->in_dhcpc_option_set_fn == NULL) return false;

    return self->in_dhcpc_option_set_fn(self, opt, value);
}

static inline bool inet_dhcps_enable(inet_t *self, bool enabled)
{
    if (self->in_dhcps_enable_fn == NULL) return false;

    return self->in_dhcps_enable_fn(self, enabled);
}

static inline bool inet_dhcps_lease_set(inet_t *self, int lease_time_s)
{
    if (self->in_dhcps_lease_set_fn == NULL) return false;

    return self->in_dhcps_lease_set_fn(self, lease_time_s);
}

static inline bool inet_dhcps_range_set(inet_t *self, inet_ip4addr_t start, inet_ip4addr_t stop)
{
    if (self->in_dhcps_range_set_fn == NULL) return false;

    return self->in_dhcps_range_set_fn(self, start, stop);
}

static inline bool inet_dhcps_option_set(inet_t *self, enum inet_dhcp_option opt, const char *value)
{
    if (self->in_dhcps_option_set_fn == NULL) return false;

    return self->in_dhcps_option_set_fn(self, opt, value);
}

static inline bool inet_dhcps_lease_register(inet_t *self, inet_dhcp_lease_fn_t *fn)
{
    if (self->in_dhcps_lease_register_fn == NULL) return false;

    return self->in_dhcps_lease_register_fn(self, fn);
}

static inline bool inet_dhcps_rip_set(inet_t *self, inet_macaddr_t macaddr,
                                      inet_ip4addr_t ip4addr, const char *hostname)
{
    if (self->in_dhcps_rip_set_fn == NULL) return false;

    return self->in_dhcps_rip_set_fn(self, macaddr, ip4addr, hostname);
}

static inline bool inet_dhcps_rip_del(inet_t *self, inet_macaddr_t macaddr)
{
    if (self->in_dhcps_rip_del_fn == NULL) return false;

    return self->in_dhcps_rip_del_fn(self, macaddr);
}


/**
 * Set IPv4 tunnel options
 *
 * parent   - parent interface
 * laddr    - local IP address
 * raddr    - remote IP address
 */
static inline bool inet_ip4tunnel_set(
        inet_t *self,
        const char *parent,
        inet_ip4addr_t laddr,
        inet_ip4addr_t raddr)
{
    if (self->in_ip4tunnel_set_fn == NULL) return false;

    return self->in_ip4tunnel_set_fn(self, parent, laddr, raddr);
}

/*
 * DHCP sniffing - @p func will be called each time a DHCP packet is sniffed
 * on the interface. This can happen multiple times for the same client
 * (depending on the DHCP negotiation phase, more data can be made available).
 *
 * If func is NULL, DHCP sniffing is disabled on the interface.
 */
static inline bool inet_dhsniff_lease_register(inet_t *self, inet_dhcp_lease_fn_t *func)
{
    if (self->in_dhsnif_lease_register_fn == NULL) return false;

    return self->in_dhsnif_lease_register_fn(self, func);
}

/**
 * Commit all pending changes; the purpose of this function is to figure out the order
 * in which subsystems must be brought up or tore down and call inet_apply() for each
 * one of them
 */
static inline bool inet_commit(inet_t *self)
{
    if (self->in_commit_fn == NULL) return false;

    return self->in_commit_fn(self);
}

/**
 * State retrieval -- simple polling interface
 */
static inline bool inet_state_get(inet_t *self, inet_state_t *out)
{
    if (self->in_state_get_fn == NULL) return false;

    return self->in_state_get_fn(self, out);
}

/*
 * ===========================================================================
 *  Inet Inerface for enumarting interfaces
 * ===========================================================================
 */
typedef struct __inet_iflist inet_iflist_t;

extern inet_iflist_t *inet_iflist_open(void);
extern const char *inet_iflist_read(inet_iflist_t *);
extern void inet_iflist_close(inet_iflist_t *);

#endif /* INET_H_INCLUDED */
