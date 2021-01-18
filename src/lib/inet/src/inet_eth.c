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

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <errno.h>

#include <string.h>

#include "log.h"
#include "util.h"
#include "execsh.h"

#include "osn_netif.h"

#include "inet.h"
#include "inet_base.h"
#include "inet_eth.h"

/*
 * ===========================================================================
 *  Inet Ethernet implementation
 * ===========================================================================
 */
static bool inet_eth_dtor(inet_t *super);
static osn_netif_status_fn_t inet_eth_netif_status_fn;
static osn_ip_status_fn_t inet_eth_ip4_status_fn;
static bool inet_eth_noflood_set(inet_t *self, bool enable);
static bool inet_eth_service_IF_READY(inet_eth_t *self, bool enable);
static bool inet_eth_network_start(inet_eth_t *self, bool enable);

/*
 * ===========================================================================
 *  Globals
 * ===========================================================================
 */

/*
 * Command for enabling the "no-flood" option for OVS interfaces
 */
static char inet_eth_ovs_noflood_cmd[] = _S(
        ifname="$1";
        flood="$2";
        bridge=$(ovs-vsctl port-to-br "$ifname") && ovs-ofctl mod-port "$bridge" "$ifname" "$flood");

/*
 * ===========================================================================
 *  Constructors/Destructors
 * ===========================================================================
 */

/**
 * New-type constructor
 */
inet_t *inet_eth_new(const char *ifname)
{
    inet_eth_t *self;

    self = malloc(sizeof(*self));
    if (self == NULL)
    {
        goto error;
    }

    if (!inet_eth_init(self, ifname))
    {
        LOG(ERR, "inet_eth: %s: Failed to initialize interface instance.", ifname);
        goto error;
    }

    return (inet_t *)self;

 error:
    if (self != NULL) free(self);
    return NULL;
}

bool inet_eth_init(inet_eth_t *self, const char *ifname)
{
    memset(self, 0, sizeof(inet_eth_t));

    if (!inet_base_init(&self->base, ifname))
    {
        LOG(ERR, "inet_eth: %s: Failed to instantiate class, inet_base_init() failed.", ifname);
        return false;
    }

    /* Interface existence will be detected, assume it doesn't exist for now */
    inet_unit_stop(self->base.in_units, INET_BASE_IF_READY);

    /* Override inet_t class methods */
    self->inet.in_dtor_fn = inet_eth_dtor;
    self->base.in_service_commit_fn = inet_eth_service_commit;
    self->inet.in_noflood_set_fn = inet_eth_noflood_set;

    /*
     * Initialize osn_netif_t -- L2 interface for ethernet-like interfaces
     */
    self->in_netif = osn_netif_new(ifname);
    osn_netif_data_set(self->in_netif, self);
    osn_netif_status_notify(self->in_netif, inet_eth_netif_status_fn);

    /*
     * Initialize osn_ip_t -- IPv4 interface configuration
     */
    self->in_ip = osn_ip_new(ifname);
    osn_ip_data_set(self->in_ip, self);
    osn_ip_status_notify(self->in_ip, inet_eth_ip4_status_fn);

    return true;
}

bool inet_eth_dtor(inet_t *super)
{
    inet_eth_t *self = (void *)super;

    return inet_eth_fini(self);
}

bool inet_eth_fini(inet_eth_t *self)
{
    bool retval = true;

    if (!inet_base_dtor(&self->inet))
    {
        retval = false;
    }

    /* Dispose of the osn_netif_t class */
    if (self->in_netif != NULL && !osn_netif_del(self->in_netif))
    {
        LOG(WARN, "inet_eth: %s: Error detected during deletion of osn_netif_t instance.",
                self->inet.in_ifname);
        retval = false;
    }

    if (self->in_ip != NULL && !osn_ip_del(self->in_ip))
    {
        LOG(WARN, "inet_eth: %s: Error detected during deletion of osn_ip_t instance.",
                self->inet.in_ifname);
        retval = false;
    }

    /* Call parent destructor */
    return retval;
}

bool inet_eth_noflood_set(inet_t *super, bool enable)
{
    inet_eth_t *self = CONTAINER_OF(super, inet_eth_t, inet);

    self->in_noflood_set = true;
    self->in_noflood = enable;

    /* Restart the MTU service to which the noflood option is tied to */
    if (!inet_unit_restart(self->base.in_units, INET_BASE_MTU, true))
    {
        LOG(ERR, "inet_base: %s: Error restarting INET_BASE_MTU (noflood)",
                self->inet.in_ifname);
        return false;
    }

    return true;
}

/*
 * ===========================================================================
 *  Commit and service start & stop functions
 * ===========================================================================
 */


bool inet_eth_service_IF_READY(inet_eth_t *self, bool enable)
{
    (void)enable;

    /*
     * By default just bring the interface down
     */
    return inet_eth_network_start(self, false);
}

/**
 * In case of inet_eth a "network start" roughly translates to a ifconfig up
 * Likewise, a "network stop" is basically an ifconfig down.
 */
bool inet_eth_network_start(inet_eth_t *self, bool enable)
{
    osn_netif_state_set(self->in_netif, enable);

    if (!osn_netif_apply(self->in_netif))
    {
        LOG(DEBUG, "inet_eth: %s: Error applying interface state.",
                self->inet.in_ifname);
    }

    LOG(INFO, "inet_eth: %s: Network %s.",
            self->inet.in_ifname,
            enable ? "enabled" : "disabled");

    return true;
}

/**
 * IP Assignment of NONE means that the interface must be UP, but should not have an address.
 * Just clear the IP to 0.0.0.0/0, it doesn't matter if its a stop or start event.
 */
bool inet_eth_scheme_none_start(inet_eth_t *self, bool enable)
{
    /* On stop, do nothing */
    if (!enable) return true;

    /*
     * Remove previous IP configuration, if any
     */
    if (self->in_ip != NULL)
    {
        osn_ip_del(self->in_ip);
        self->in_ip = NULL;
    }

    /*
     * Just apply an empty configuration
     */
    self->in_ip = osn_ip_new(self->inet.in_ifname);
    if (self->in_ip == NULL)
    {
        LOG(ERR, "inet_eth: %s: Error creating IP configuration object.", self->inet.in_ifname);
        return false;
    }

    /* Register for status reporting */
    osn_ip_data_set(self->in_ip, self);
    osn_ip_status_notify(self->in_ip, inet_eth_ip4_status_fn);

    if (!osn_ip_apply(self->in_ip))
    {
        LOG(ERR, "inet_eth: %s: Error applying IP configuration.", self->inet.in_ifname);
        return false;
    }

    return true;
}

bool inet_eth_scheme_static_start(inet_eth_t *self, bool enable)
{
    /*
     * Remove previous IP configuration, if any
     */
    if (self->in_ip != NULL)
    {
        osn_ip_del(self->in_ip);
        self->in_ip = NULL;
    }

    self->in_ip = osn_ip_new(self->inet.in_ifname);
    if (self->in_ip == NULL)
    {
        LOG(ERR, "inet_eth: %s: Error creating IP configuration object.", self->inet.in_ifname);
        return false;
    }

    /* Register for status reporting */
    osn_ip_data_set(self->in_ip, self);
    osn_ip_status_notify(self->in_ip, inet_eth_ip4_status_fn);

    if (!enable) return true;

    /*
     * Check if all the necessary configuration is present (ipaddr, netmask and bcast)
     */
    if (osn_ip_addr_cmp(&self->base.in_static_addr, &OSN_IP_ADDR_INIT) == 0)
    {
        LOG(ERR, "inet_eth: %s: ipaddr is missing for static IP assignment scheme.", self->inet.in_ifname);
        return false;
    }

    if (osn_ip_addr_cmp(&self->base.in_static_netmask, &OSN_IP_ADDR_INIT) == 0)
    {
        LOG(ERR, "inet_eth: %s: netmask is missing for static IP assignment scheme.", self->inet.in_ifname);
        return false;
    }

    /* Construct full osn_ip_addr_t object from the IP address and Netmask */
    osn_ip_addr_t ip;

    ip = self->base.in_static_addr;
    /* Calculate the prefix from the netmask */
    ip.ia_prefix = osn_ip_addr_to_prefix(&self->base.in_static_netmask);

    if (!osn_ip_addr_add(self->in_ip, &ip))
    {
        LOG(ERR, "inet_eth: %s: Cannot assign IP address: "PRI_osn_ip_addr,
                self->inet.in_ifname,
                FMT_osn_ip_addr(ip));
        return false;
    }

    /*
     * Add the primary DNS server
     */
    if (osn_ip_addr_cmp(&self->base.in_dns_primary, &OSN_IP_ADDR_INIT) != 0)
    {
        LOG(TRACE, "inet_eth: %s: Adding primary DNS server: "PRI_osn_ip_addr,
                self->inet.in_ifname,
                FMT_osn_ip_addr(self->base.in_dns_primary));

        if (!osn_ip_dns_add(self->in_ip, &self->base.in_dns_primary))
        {
            LOG(WARN, "inet_eth: %s: Cannot assign primary DNS server: "PRI_osn_ip_addr,
                    self->inet.in_ifname,
                    FMT_osn_ip_addr(ip));
        }
    }

    /*
     * Add the secondary DNS server
     */
    if (osn_ip_addr_cmp(&self->base.in_dns_secondary, &OSN_IP_ADDR_INIT) != 0)
    {
        LOG(TRACE, "inet_eth: %s: Adding secondary DNS server: "PRI_osn_ip_addr,
                self->inet.in_ifname,
                FMT_osn_ip_addr(self->base.in_dns_secondary));

        if (!osn_ip_dns_add(self->in_ip, &self->base.in_dns_secondary))
        {
            LOG(WARN, "inet_eth: %s: Cannot assign secondary DNS server: "PRI_osn_ip_addr,
                    self->inet.in_ifname,
                    FMT_osn_ip_addr(ip));
        }
    }

    /*
     * Use the "ip route" command to add the default route
     */
    if (osn_ip_addr_cmp(&self->base.in_static_gwaddr, &OSN_IP_ADDR_INIT) != 0)
    {
        if (!osn_ip_route_gw_add(self->in_ip, &OSN_IP_ADDR_INIT, &self->base.in_static_gwaddr))
        {
            LOG(ERR, "inet_eth: %s: Error adding default route: "PRI_osn_ip_addr" -> "PRI_osn_ip_addr,
                    self->inet.in_ifname,
                    FMT_osn_ip_addr(OSN_IP_ADDR_INIT),
                    FMT_osn_ip_addr(self->base.in_static_gwaddr));
            return false;
        }
    }

    if (!osn_ip_apply(self->in_ip))
    {
        LOG(ERR, "inet_eth: %s: Error applying IP configuration.", self->inet.in_ifname);
        return false;
    }

    return true;
}

/**
 * MTU Settings
 */
bool inet_eth_mtu_start(inet_eth_t *self, bool enable)
{
    if (!enable) return true;

    /*
     * No flood flag: This might not exactly belong under the MTU category, but
     * creating another service just for this option seems a bit overkill.
     */
    if (self->in_noflood_set)
    {
        int rc;

        rc = execsh_log(
                LOG_SEVERITY_INFO,
                inet_eth_ovs_noflood_cmd,
                self->inet.in_ifname,
                self->in_noflood ? "no-flood" : "flood");
        if (rc != 0)
        {
            LOG(WARN, "inet_eth: %s: Error setting no-flood.",
                    self->inet.in_ifname);
        }
    }

    /* MTU not set */
    if (self->base.in_mtu <= 0) return true;

    /* IPv6 requires a MTU of 1280 */
    if (self->base.in_mtu < 1280)
    {
        LOG(ERR, "inet_eth: %s: A MTU setting of %d is invalid.",
                self->inet.in_ifname,
                self->base.in_mtu);

        return false;
    }

    LOG(INFO, "inet_eth: %s: Setting MTU to %d.",
            self->inet.in_ifname,
            self->base.in_mtu);

    osn_netif_mtu_set(self->in_netif, self->base.in_mtu);
    if (!osn_netif_apply(self->in_netif))
    {
        LOG(ERR, "inet_eth: %s: Error setting MTU to %d.",
                self->inet.in_ifname,
                self->base.in_mtu);
        return false;
    }

    return true;
}

/**
 * Start/stop event dispatcher
 */
bool inet_eth_service_commit(inet_base_t *super, enum inet_base_services srv, bool enable)
{
    inet_eth_t *self = (inet_eth_t *)super;

    LOG(INFO, "inet_eth: %s: Service %s -> %s.",
            self->inet.in_ifname,
            inet_base_service_str(srv),
            enable ? "start" : "stop");

    switch (srv)
    {
        case INET_BASE_IF_READY:
            if (!inet_eth_service_IF_READY(self, enable))
            {
                return false;
            }
            break;

        case INET_BASE_NETWORK:
            return inet_eth_network_start(self, enable);

        case INET_BASE_SCHEME_NONE:
            return inet_eth_scheme_none_start(self, enable);

        case INET_BASE_SCHEME_STATIC:
            return inet_eth_scheme_static_start(self, enable);

        case INET_BASE_MTU:
            return inet_eth_mtu_start(self, enable);

        default:
            break;
    }

    /* Delegate the service handling to the parent class */
    return inet_base_service_commit(super, srv, enable);
}

/*
 * ===========================================================================
 *  Status reporting
 * ===========================================================================
 */
void inet_eth_netif_status_fn(osn_netif_t *netif, struct osn_netif_status *status)
{
    bool enabled;

    inet_eth_t *self = osn_netif_data_get(netif);

    self->base.in_state.in_interface_exists = status->ns_exists;
    if (status->ns_exists)
    {
        self->base.in_state.in_port_status = status->ns_carrier;
        self->base.in_state.in_mtu = status->ns_mtu;
        self->base.in_state.in_macaddr = status->ns_hwaddr;
    }
    else
    {
        self->base.in_state.in_port_status = false;
        self->base.in_state.in_mtu = 0;
        self->base.in_state.in_macaddr = OSN_MAC_ADDR_INIT;
    }

    inet_base_state_update(&self->base);

    enabled = inet_unit_is_enabled(self->base.in_units, INET_BASE_IF_READY);
    if (enabled != self->base.in_state.in_interface_exists)
    {
        if (self->base.in_state.in_interface_exists)
        {
            LOG(NOTICE, "inet_eth: %s: Interface now exists, restarting.",
                    self->inet.in_ifname);
        }
        else
        {
            LOG(NOTICE, "inet_eth: %s: Interface ceased to exist, stopping.",
                    self->inet.in_ifname);
        }

        /*
         * Stop or start the interface unit according to the interface existence
         * bit
         */
        inet_unit_enable(
                self->base.in_units,
                INET_BASE_IF_READY,
                self->base.in_state.in_interface_exists);

        inet_commit(&self->inet);
    }
}

void inet_eth_ip4_status_fn(osn_ip_t *ip, struct osn_ip_status *status)
{
    inet_eth_t *self = osn_ip_data_get(ip);

    if (status->is_addr_len > 0)
    {
        self->base.in_state.in_ipaddr = status->is_addr[0];
        /* Remove the prefix */
        self->base.in_state.in_ipaddr.ia_prefix = -1;

        self->base.in_state.in_netmask = osn_ip_addr_from_prefix(status->is_addr[0].ia_prefix);
        self->base.in_state.in_bcaddr = osn_ip_addr_to_bcast(&status->is_addr[0]);
    }
    else
    {
        self->base.in_state.in_ipaddr = OSN_IP_ADDR_INIT;
        self->base.in_state.in_netmask = OSN_IP_ADDR_INIT;
        self->base.in_state.in_bcaddr = OSN_IP_ADDR_INIT;
    }

    inet_base_state_update(&self->base);
}
