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

#ifndef INET_BASE_H_INCLUDED
#define INET_BASE_H_INCLUDED

#include "inet.h"
#include "inet_unit.h"
#include "inet_dhcpc.h"
#include "inet_dhcps.h"
#include "inet_fw.h"
#include "inet_upnp.h"
#include "inet_dns.h"
#include "inet_dhsnif.h"

#define INET_BASE_SERVICE_LIST(M)   \
    M(INET_BASE_INTERFACE)          \
    M(INET_BASE_FIREWALL)           \
    M(INET_BASE_UPNP)               \
    M(INET_BASE_NETWORK)            \
    M(INET_BASE_MTU)                \
    M(INET_BASE_SCHEME_NONE)        \
    M(INET_BASE_SCHEME_STATIC)      \
    M(INET_BASE_SCHEME_DHCP)        \
    M(INET_BASE_DHCP_SERVER)        \
    M(INET_BASE_DNS)                \
    M(INET_BASE_DHCPSNIFF)          \
    M(INET_BASE_MAX)

/*
 * This enum is used mainly to apply partial configurations through the
 * inet_unit subsystem
 */
enum inet_base_services
{
    #define _E(x) x,
    INET_BASE_SERVICE_LIST(_E)
};

typedef struct __inet_base inet_base_t;

/*
 * Base class definition.
 */
struct __inet_base
{
    union
    {
        inet_t                  inet;
    };

    /* Start/stop services */
    bool                  (*in_service_commit_fn)(
                                inet_base_t *self,
                                enum inet_base_services srv,
                                bool start);

    /* Service units dependency tree */
    inet_unit_t            *in_units;

    /* DHCP server class */
    inet_dhcps_t           *in_dhcps;

    /* DHCP client class */
    inet_dhcpc_t           *in_dhcpc;

    /* Firewall class */
    inet_fw_t              *in_fw;

    /* UPnP class */
    inet_upnp_t            *in_upnp;

    /* DNS server settings */
    inet_dns_t             *in_dns;

    /* DHCP sniffing class */
    inet_dhsnif_t          *in_dhsnif;

    int                     in_mtu;

    /* DHCP sniffing callback */

    /* The following variables below are mainly used as cache */
    enum inet_assign_scheme in_assign_scheme;

    inet_ip4addr_t          in_static_addr;
    inet_ip4addr_t          in_static_netmask;
    inet_ip4addr_t          in_static_bcast;
    inet_ip4addr_t          in_static_gwaddr;

    bool                    in_interface_enabled;
    bool                    in_network_enabled;

    bool                    in_nat_enabled;
    enum inet_upnp_mode     in_upnp_mode;

    bool                    in_dhcps_enabled;
    int                     in_dhcps_lease_time_s;
    inet_ip4addr_t          in_dhcps_lease_start;
    inet_ip4addr_t          in_dhcps_lease_stop;

    inet_ip4addr_t          in_dns_primary;
    inet_ip4addr_t          in_dns_secondary;

    inet_dhcp_lease_fn_t   *in_dhsnif_lease_fn;
};

/**
 * Extend the superclass with the inet_service_commit method
 */
static inline bool inet_service_commit(
        inet_base_t *self,
        enum inet_base_services srv,
        bool start)
{
    if (self->in_service_commit_fn == NULL) return true;

    return self->in_service_commit_fn(self, srv, start);
}

/*
 * ===========================================================================
 *  Constructors and destructor implementations
 * ===========================================================================
 */
extern bool inet_base_init(inet_base_t *self, const char *ifname);
extern bool inet_base_fini(inet_base_t *self);
extern bool inet_base_dtor(inet_t *super);
extern inet_base_t *inet_base_new(const char *ifname);

/* Interface enable method implementation */
extern bool inet_base_interface_enable(inet_t *super, bool enable);

/*
 * ===========================================================================
 *  Network/Assignment scheme method implementation
 * ===========================================================================
 */
extern bool inet_base_network_enable(inet_t *super, bool enable);
extern bool inet_base_mtu_set(inet_t *super, int mtu);

extern bool inet_base_assign_scheme_set(inet_t *super, enum inet_assign_scheme scheme);
extern bool inet_base_ipaddr_static_set(
                        inet_t *self,
                        inet_ip4addr_t addr,
                        inet_ip4addr_t netmask,
                        inet_ip4addr_t bcast);

extern bool inet_base_gateway_set(inet_t *super, inet_ip4addr_t gwaddr);

/*
 * ===========================================================================
 *  Firewall method implementations
 * ===========================================================================
 */
extern bool inet_base_nat_enable(inet_t *super, bool enable);
extern bool inet_base_upnp_mode_set(inet_t *super, enum inet_upnp_mode mode);

/*
 * ===========================================================================
 *  DHCP Client method implementation
 * ===========================================================================
 */
extern bool inet_base_dhcpc_option_request(inet_t *super, enum inet_dhcp_option opt, bool req);
extern bool inet_base_dhcpc_option_set(inet_t *super, enum inet_dhcp_option opt, const char *value);

/*
 * ===========================================================================
 *  DHCP Server method implementation
 * ===========================================================================
 */
extern bool inet_base_dhcps_enable(inet_t *super, bool enable);
extern bool inet_base_dhcps_lease_set(inet_t *super, int lease_time_s);
extern bool inet_base_dhcps_range_set(inet_t *super, inet_ip4addr_t start, inet_ip4addr_t stop);
extern bool inet_base_dhcps_option_set(inet_t *super, enum inet_dhcp_option opt, const char *value);
extern bool inet_base_dhcps_lease_register(inet_t *super, inet_dhcp_lease_fn_t *fn);
extern bool inet_base_dhcps_rip_set(inet_t *super, inet_macaddr_t macaddr,
                                    inet_ip4addr_t ip4addr, const char *hostname);
extern bool inet_base_dhcps_rip_del(inet_t *super, inet_macaddr_t macaddr);

/*
 * ===========================================================================
 *  DNS settings
 * ===========================================================================
 */
extern bool inet_base_dns_set(inet_t *super, inet_ip4addr_t primary, inet_ip4addr_t secondary);

/*
 * ===========================================================================
 *  DHCP Sniffig functions
 * ===========================================================================
 */
extern bool inet_base_dhsnif_lease_register(inet_t *super, inet_dhcp_lease_fn_t *func);

/*
 * ===========================================================================
 *  Commit & Service start/stop method implementation
 * ===========================================================================
 */
extern bool inet_base_service_commit(
        inet_base_t *self,
        enum inet_base_services srv,
        bool start);

extern bool inet_base_commit(inet_t *super);

/*
 * ===========================================================================
 *  Interface status reporting
 * ===========================================================================
 */
extern bool inet_base_state_get(inet_t *super, inet_state_t *out);

/*
 * ===========================================================================
 *  Misc
 * ===========================================================================
 */

extern const char *inet_base_service_str(enum inet_base_services srv);

#endif /* INET_BASE_H_INCLUDED */
