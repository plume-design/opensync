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
 *  Inet Base class implementation -- implements some boilerplate code
 *  that should be more or less common to the majority of inet class
 *  implementations. For example, this class takes care of applying
 *  the config only once, DHCP client/server settings, firewall and UPnP 
 *  settings ...
 * ===========================================================================
 */
#include <stdlib.h>
#include <stdarg.h>

#include "log.h"
#include "util.h"
#include "target.h"
#include "version.h"

#include "inet.h"
#include "inet_base.h"

static bool inet_base_firewall_commit(inet_base_t *self, bool start);
static bool inet_base_upnp_commit(inet_base_t *self, bool start);
static bool inet_base_dhcp_client_commit(inet_base_t *self, bool start);
static bool inet_base_dhcp_server_commit(inet_base_t *self, bool start);
static bool inet_base_dns_commit(inet_base_t *self, bool start);
static bool inet_base_dhsnif_commit(inet_base_t *self, bool start);
static void inet_base_upnp_start_stop(inet_base_t *self);


/*
 * ===========================================================================
 *  Constructor and destructor methods
 * ===========================================================================
 */

/**
 * Statically initialize a new instance of inet_base_t
 */
bool inet_base_init(inet_base_t *self, const char *ifname)
{
    TRACE("%p %s", self, ifname);
    memset(self, 0, sizeof(*self));

    if (STRSCPY(self->inet.in_ifname, ifname) < 0)
    {
        LOG(ERR, "inet_base: %s: Interface name is too long, cannot instantiate class.", ifname);
        return false;
    }

    /*
     * Override default methods
     */
    self->inet.in_dtor_fn                   = inet_base_dtor;
    self->inet.in_interface_enable_fn       = inet_base_interface_enable;
    self->inet.in_network_enable_fn         = inet_base_network_enable;
    self->inet.in_mtu_set_fn                = inet_base_mtu_set;
    self->inet.in_nat_enable_fn             = inet_base_nat_enable;
    self->inet.in_upnp_mode_set_fn          = inet_base_upnp_mode_set;
    self->inet.in_assign_scheme_set_fn      = inet_base_assign_scheme_set;
    self->inet.in_ipaddr_static_set_fn      = inet_base_ipaddr_static_set;
    self->inet.in_gateway_set_fn            = inet_base_gateway_set;
    self->inet.in_dhcpc_option_request_fn   = inet_base_dhcpc_option_request;
    self->inet.in_dhcpc_option_set_fn       = inet_base_dhcpc_option_set;
    self->inet.in_dhcps_enable_fn           = inet_base_dhcps_enable;
    self->inet.in_dhcps_lease_set_fn        = inet_base_dhcps_lease_set;
    self->inet.in_dhcps_range_set_fn        = inet_base_dhcps_range_set;
    self->inet.in_dhcps_option_set_fn       = inet_base_dhcps_option_set;
    self->inet.in_dhcps_lease_register_fn   = inet_base_dhcps_lease_register;
    self->inet.in_dhcps_rip_set_fn          = inet_base_dhcps_rip_set;
    self->inet.in_dhcps_rip_del_fn          = inet_base_dhcps_rip_del;
    self->inet.in_dns_set_fn                = inet_base_dns_set;
    self->inet.in_dhsnif_lease_register_fn  = inet_base_dhsnif_lease_register;
    self->inet.in_commit_fn                 = inet_base_commit;
    self->inet.in_state_get_fn              = inet_base_state_get;

    /*
     * inet_base methods
     */
    self->in_service_commit_fn = inet_base_service_commit;

    self->in_dhcps = inet_dhcps_new(self->inet.in_ifname);
    if (self->in_dhcps == NULL)
    {
        LOG(ERR, "inet_base: %s: Error creating DHCPS instance.", self->inet.in_ifname);
        goto error;
    }

    self->in_fw = inet_fw_new(self->inet.in_ifname);
    if (self->in_fw == NULL)
    {
        LOG(ERR, "inet_base: %s: Error creating FW instance.", self->inet.in_ifname);
        goto error;
    }

    self->in_upnp = inet_upnp_new(self->inet.in_ifname);
    if (self->in_upnp == NULL)
    {
        LOG(WARN, "inet_base: %s: Error initializing UPnP instance.", self->inet.in_ifname);
        goto error;
    }

    self->in_dhcpc = inet_dhcpc_new(self->inet.in_ifname);
    if (self->in_dhcpc == NULL)
    {
        LOG(ERR, "inet_base: %s: Error creating DHCPC instance.", self->inet.in_ifname);
        goto error;
    }

    self->in_dhsnif = inet_dhsnif_new(self->inet.in_ifname);
    if (self->in_dhsnif == NULL)
    {
        LOG(ERR, "inet_base: %s: Error creating DHCP sniffing instance.", self->inet.in_ifname);
        goto error;
    }

    self->in_dns = inet_dns_new(self->inet.in_ifname);
    if (self->in_dns == NULL)
    {
        LOG(ERR, "inet_base: %s: Error creating DNS instance.", self->inet.in_ifname);
        goto error;
    }

    /*
     * Define the unit dependency tree structure
     */
    self->in_units =
            inet_unit_s(INET_BASE_INTERFACE,
                    inet_unit(INET_BASE_FIREWALL,
                        inet_unit(INET_BASE_UPNP, NULL),
                        NULL),
                    inet_unit(INET_BASE_MTU, NULL),
                    inet_unit_s(INET_BASE_NETWORK,
                            inet_unit_s(INET_BASE_SCHEME_NONE, NULL),
                            inet_unit(INET_BASE_SCHEME_STATIC,
                                    inet_unit(INET_BASE_DHCP_SERVER, NULL),
                                    inet_unit(INET_BASE_DNS, NULL),
                                    NULL),
                            inet_unit(INET_BASE_SCHEME_DHCP, NULL),
                            inet_unit(INET_BASE_DHCPSNIFF, NULL),
                            NULL),
                    NULL);
    if (self->in_units == NULL)
    {
        LOG(ERR, "inet_base: %s: Error initializing units structure.", self->inet.in_ifname);
        goto error;
    }

    return true;

error:
    inet_fini(&self->inet);
    return false;
}

bool inet_base_dtor(inet_t *super)
{
    bool retval = true;

    inet_base_t *self = (inet_base_t *)super;

    if (self->in_units != NULL)
    {
        /* Stop the top service (INET_BASE_INTERACE) -- this will effectively shutdown ALL services */
        inet_unit_stop(self->in_units, INET_BASE_INTERFACE);
        if (!inet_commit(&self->inet))
        {
            LOG(WARN, "inet_base: %s: Error shutting down services.", self->inet.in_ifname);
            retval = false;
        }
    }

    if (self->in_dhcps != NULL && !inet_dhcps_del(self->in_dhcps))
    {
        LOG(WARN, "inet_base: %s: Error freeing DHCP server object.", self->inet.in_ifname);
        retval = false;
    }

    if (self->in_dhcpc != NULL && !inet_dhcpc_del(self->in_dhcpc))
    {
        LOG(WARN, "inet_base: %s: Error freeing DHCP client objects.", self->inet.in_ifname);
        retval = false;
    }

    if (self->in_fw != NULL && !inet_fw_del(self->in_fw))
    {
        LOG(WARN, "inet_base: %s: Error freeing Firewall/UPnP client objects.", self->inet.in_ifname);
        retval = false;
    }

    if (self->in_dhsnif != NULL && !inet_dhsnif_del(self->in_dhsnif))
    {
        LOG(WARN, "inet_base: %s: Error freeing DHCP sniffing object.", self->inet.in_ifname);
        retval = false;
    }

    if (self->in_units != NULL) inet_unit_free(self->in_units);

    memset(self, 0, sizeof(*self));

    return retval;
}

/**
 * Create a new instance of inet_base_t and return it casted to inet_t*
 */
inet_base_t *inet_base_new(const char *ifname)
{
    inet_base_t *self = malloc(sizeof(inet_base_t));

    if (!inet_base_init(self, ifname))
    {
        free(self);
        return NULL;
    }

    return self;
}

/*
 * ===========================================================================
 *  Interface enable
 * ===========================================================================
 */

/**
 * Interface enable switch
 */
bool inet_base_interface_enable(inet_t *super, bool enabled)
{
    inet_base_t *self = (inet_base_t *)super;

    if (self->in_interface_enabled == enabled) return true;

    self->in_interface_enabled = enabled;

    LOG(INFO, "inet_base: %s: %s interface.", self->inet.in_ifname, enabled ? "Starting" : "Stopping");

    return inet_unit_enable(self->in_units, INET_BASE_INTERFACE, enabled);
}

/*
 * ===========================================================================
 *  Firewall methods
 * ===========================================================================
 */

/**
 * Enable NAT on interface
 */
bool inet_base_nat_enable(inet_t *super, bool enabled)
{
    inet_base_t *self = (inet_base_t *)super;

    if (self->in_nat_enabled == enabled && inet_unit_is_enabled(self->in_units, INET_BASE_FIREWALL))
        return true;

    if (!inet_fw_nat_set(self->in_fw, enabled))
    {
        LOG(ERR, "inet_base: %s: Error setting NAT on interface (%d -> %d)",
                self->inet.in_ifname,
                self->in_nat_enabled,
                enabled);
        return false;
    }

    /* Restart the firewall service */
    self->in_nat_enabled = enabled;

    if (!inet_unit_restart(self->in_units, INET_BASE_FIREWALL, true))
    {
        LOG(ERR, "inet_base: %s: Error restarting INET_BASE_FIREWALL (NAT)",
                self->inet.in_ifname);
        return false;
    }

    /* NAT settings also affect UPnP, so handle it here */
    inet_base_upnp_start_stop(self);

    return true;
}

/**
 * UPnP mode selection; if mode is UPNP_MODE_NONE then the INET_BASE_UPNP service is stopped
 */
bool inet_base_upnp_mode_set(inet_t *super, enum inet_upnp_mode mode)
{
    inet_base_t *self = (inet_base_t *)super;

    if (self->in_upnp_mode != mode)
    {
        if (!inet_upnp_set(self->in_upnp, mode))
        {
            LOG(ERR, "inet_base: %s: Error setting UPnP mode on interface.",
                    self->inet.in_ifname);
            return false;
        }

        self->in_upnp_mode = mode;

        /* Stop the service for now, __inet_base_upnp_start_stop(self) will trigger a restart if needed */
        inet_unit_stop(self->in_units, INET_BASE_UPNP);
    }

    inet_base_upnp_start_stop(self);

    return true;
}

void inet_base_upnp_start_stop(inet_base_t *self)
{
    switch (self->in_upnp_mode)
    {
        case UPNP_MODE_NONE:
            /* Stop the UPnP service */
            inet_unit_stop(self->in_units, INET_BASE_UPNP);
            break;

        case UPNP_MODE_INTERNAL:
            /* Start the UPnP service only if we're not in NAT mode */
            inet_unit_enable(self->in_units, INET_BASE_UPNP, !self->in_nat_enabled);
            break;

        case UPNP_MODE_EXTERNAL:
            /* Star the UPnP service only if we're in NAT mode */
            inet_unit_enable(self->in_units, INET_BASE_UPNP, self->in_nat_enabled);
            break;
    }
}

/*
 * ===========================================================================
 *  Network and IP addressing
 * ===========================================================================
 */

/**
 * Interface network enable switch
 */
bool inet_base_network_enable(inet_t *super, bool enable)
{
    inet_base_t *self = (inet_base_t *)super;

    if (self->in_network_enabled == enable) return true;

    self->in_network_enabled = enable;

    return inet_unit_enable(self->in_units, INET_BASE_NETWORK, self->in_network_enabled);
}

/**
 * MTU of the interface
 */
bool inet_base_mtu_set(inet_t *super, int mtu)
{
    inet_base_t *self = (inet_base_t *)super;

    if (mtu == self->in_mtu) return true;

    self->in_mtu = mtu;

    if (self->in_mtu <= 0)
    {
        /* Stop the MTU service */
        if (!inet_unit_stop(self->in_units, INET_BASE_MTU))
        {
            LOG(ERR, "inet_base: %s: Error stopping INET_BASE_MTU.",
                    self->inet.in_ifname);
            return false;
        }
    }
    else
    {
        /* Start the MTU service */
        if (!inet_unit_restart(self->in_units, INET_BASE_MTU, true))
        {
            LOG(ERR, "inet_base: %s: Error restarting INET_BASE_MTU",
                    self->inet.in_ifname);
            return false;
        }
    }

    return true;
}


/**
 * IP assignment scheme -- this is basically a tristate. It toggles between NONE, STATIC and DHCP
 */
bool inet_base_assign_scheme_set(inet_t *super, enum inet_assign_scheme scheme)
{
    inet_base_t *self = (inet_base_t *)super;

    bool retval = true;
    bool none_enable = false;
    bool static_enable = false;
    bool dhcp_enable = false;

    if (self->in_assign_scheme == scheme) return true;

    self->in_assign_scheme = scheme;

    switch (scheme)
    {
        case INET_ASSIGN_NONE:
            none_enable = true;
            break;

        case INET_ASSIGN_STATIC:
            static_enable = true;
            break;

        case INET_ASSIGN_DHCP:
            dhcp_enable = true;
            break;

        default:
            return false;
    }

    if (!inet_unit_enable(self->in_units, INET_BASE_SCHEME_NONE, none_enable))
    {
        LOG(ERR, "inet_base: %s: Error setting enable status for SCHEME_NONE to %d.",
                self->inet.in_ifname,
                none_enable);
        retval = false;
    }

    if (!inet_unit_enable(self->in_units, INET_BASE_SCHEME_STATIC, static_enable))
    {
        LOG(ERR, "inet_base: %s: Error setting enable status for SCHEME_STATIC to %d.",
                self->inet.in_ifname,
                static_enable);
        retval = false;
    }

    if (!inet_unit_enable(self->in_units, INET_BASE_SCHEME_DHCP, dhcp_enable))
    {
        LOG(ERR, "inet_base: %s: Error setting enable status for SCHEME_DHCP to %d.",
                self->inet.in_ifname,
                dhcp_enable);
        retval = false;
    }

    return retval;
}

/**
 * Set the IPv4 address used by the STATIC assignment scheme
 */
bool inet_base_ipaddr_static_set(
        inet_t *super,
        inet_ip4addr_t addr,
        inet_ip4addr_t netmask,
        inet_ip4addr_t bcast)
{
    inet_base_t *self = (inet_base_t *)super;

    bool changed = false;

    changed |= inet_ip4addr_cmp(&self->in_static_addr, &addr) != 0;
    changed |= inet_ip4addr_cmp(&self->in_static_netmask, &netmask) != 0;
    changed |= inet_ip4addr_cmp(&self->in_static_bcast, &bcast) != 0;

    if (!changed) return true;

    self->in_static_addr = addr;
    self->in_static_netmask = netmask;
    self->in_static_bcast = bcast;

    return inet_unit_restart(self->in_units, INET_BASE_SCHEME_STATIC, false);
}

/**
 * Set the default gateway -- used by the STATIC assignment scheme
 */
bool inet_base_gateway_set(inet_t *super, inet_ip4addr_t gwaddr)
{
    inet_base_t *self = (inet_base_t *)super;

    if (inet_ip4addr_cmp(&self->in_static_gwaddr, &gwaddr) == 0) return true;

    self->in_static_gwaddr = gwaddr;

    return inet_unit_restart(self->in_units, INET_BASE_SCHEME_STATIC, false);
}

/*
 * ===========================================================================
 *  DHCP Client methods
 * ===========================================================================
 */
bool inet_base_dhcpc_option_request(inet_t *super, enum inet_dhcp_option opt, bool req)
{
    inet_base_t *self = (void *)super;
    bool _req;
    const char *_value;

    if (!inet_dhcpc_opt_get(self->in_dhcpc, opt, &_req, &_value))
    {
        LOG(ERR, "inet_base: %s: Error retrieving DHCP client options.", self->inet.in_ifname);
        return false;
    }

    /* No change required */
    if (req == _req) return true;

    if (!inet_dhcpc_opt_request(self->in_dhcpc, opt, req))
    {
        LOG(ERR, "inet_base: %s: Error requesting option: %d", self->inet.in_ifname, opt);
        return false;
    }

    /* Restart the DHCP client if necessary */
    return inet_unit_restart(self->in_units, INET_BASE_SCHEME_DHCP, false);
}

bool inet_base_dhcpc_option_set(inet_t *super, enum inet_dhcp_option opt, const char *value)
{
    inet_base_t *self = (void *)super;
    bool _req;
    const char *_value;

    if (!inet_dhcpc_opt_get(self->in_dhcpc, opt, &_req, &_value))
    {
        LOG(ERR, "inet_base: %s: Error retrieving DHCP client options.", self->inet.in_ifname);
        return false;
    }

    /* Both values are NULL, no change needed */
    if (value == NULL && _value == NULL)
    {
        return true;
    }
    /* One of the values is NULL while the other is not -- skip the strcmp() test */
    else if (value == NULL || _value == NULL)
    {
        /* pass */
    }
    /* Neither value is NULL, do a string compare */
    else if (strcmp(value, _value) == 0)
    {
        return true;
    }

    if (!inet_dhcpc_opt_set(self->in_dhcpc, opt, value))
    {
        LOG(ERR, "inet_base: %s: Error setting option: %d:%s", self->inet.in_ifname, opt, value);
        return false;
    }

    /* Restart the DHCP client if necessary */
    return inet_unit_restart(self->in_units, INET_BASE_SCHEME_DHCP, false);
}

/*
 * ===========================================================================
 *  DHCP Server methods
 * ===========================================================================
 */
bool inet_base_dhcps_enable(inet_t *super, bool enabled)
{
    inet_base_t *self = (inet_base_t *)super;

    if (self->in_dhcps_enabled == enabled) return true;

    self->in_dhcps_enabled = enabled;

    /* Start or stop the DHCP server service */
    if (!inet_unit_enable(self->in_units, INET_BASE_DHCP_SERVER, enabled))
    {
        LOG(ERR, "inet_base: %s: Error enabling/disabling INET_BASE_DHCP_SERVER", self->inet.in_ifname);
        return false;
    }

    return true;
}

/**
 * Set the lease time in seconds
 */
bool inet_base_dhcps_lease_set(inet_t *super, int lease_time_s)
{
    inet_base_t *self = (inet_base_t *)super;

    if (self->in_dhcps_lease_time_s == lease_time_s) return true;

    self->in_dhcps_lease_time_s = lease_time_s;

    if (!inet_dhcps_lease(self->in_dhcps, lease_time_s))
    {
        LOG(ERR, "inet_base: %s: Unable to set the lease time.", self->inet.in_ifname);
        return true;
    }

    /* Flag the DHCP server for restart */
    if (!inet_unit_restart(self->in_units, INET_BASE_DHCP_SERVER, false))
    {
        LOG(ERR, "inet_base: %s: Error restarting INET_BASE_DHCP_SERVER", self->inet.in_ifname);
        return false;
    }

    return true;
}

/**
 * Set the DHCP lease range interval (from IP to IP)
 */
bool inet_base_dhcps_range_set(inet_t *super, inet_ip4addr_t start, inet_ip4addr_t stop)
{
    inet_base_t *self = (inet_base_t *)super;

    bool changed = false;

    changed |= inet_ip4addr_cmp(&self->in_dhcps_lease_start, &start) != 0;
    changed |= inet_ip4addr_cmp(&self->in_dhcps_lease_stop, &stop) != 0;

    if (!changed) return true;

    self->in_dhcps_lease_start = start;
    self->in_dhcps_lease_stop = stop;

    if (!inet_dhcps_range(self->in_dhcps, start, stop))
    {
        LOG(ERR, "inet_base: %s: Error setting lease range.", self->inet.in_ifname);
        return false;
    }

    /* Flag the DHCP server for restart */
    if (!inet_unit_restart(self->in_units, INET_BASE_DHCP_SERVER, false))
    {
        LOG(ERR, "inet_base: %s: Error restarting INET_BASE_DHCP_SERVER", self->inet.in_ifname);
        return false;
    }

    return true;
}

/**
 * Set various options -- these will be sent back to DHCP clients
 */
bool inet_base_dhcps_option_set(inet_t *super, enum inet_dhcp_option opt, const char *value)
{
    const char *old_value;

    inet_base_t *self = (inet_base_t *)super;

    old_value = inet_dhcps_option_get(self->in_dhcps, opt);

    /* Both options are cleared, nothing to do */
    if (old_value == NULL && value == NULL)
    {
        return true;
    }

    /* If both values are not NULL, check if they are equal */
    if (old_value != NULL && value != NULL)
    {
        if (strcmp(old_value, value) == 0) return true;
    }

    if (!inet_dhcps_option(self->in_dhcps, opt, value))
    {
        LOG(ERR, "inet_base: %s: Error setting DHCP option %d, %s.",
                self->inet.in_ifname,
                opt,
                value);
        return false;
    }

    /* Flag the DHCP server for restart */
    if (!inet_unit_restart(self->in_units, INET_BASE_DHCP_SERVER, false))
    {
        LOG(ERR, "inet_base: %s: Error restarting INET_BASE_DHCP_SERVER", self->inet.in_ifname);
        return false;
    }

    return true;
}

bool inet_base_dhcps_lease_register(inet_t *super, inet_dhcp_lease_fn_t *fn)
{
    inet_base_t *self = (inet_base_t *)super;

    inet_dhcps_lease_notify(self->in_dhcps, fn, super);

    return true;
}


bool inet_base_dhcps_rip_set(inet_t *super, inet_macaddr_t macaddr,
                             inet_ip4addr_t ip4addr, const char *hostname)
{
    inet_base_t *self = (inet_base_t *)super;


    if (!inet_dhcps_rip(self->in_dhcps, macaddr, ip4addr, hostname))
    {
        LOG(ERR, "inet_base: %s: Error setting IP reservation.", self->inet.in_ifname);
        return false;
    }

    /* Flag the DHCP server for restart */
    if (!inet_unit_restart(self->in_units, INET_BASE_DHCP_SERVER, true))
    {
        LOG(ERR, "inet_base: %s: Error restarting INET_BASE_DHCP_SERVER", self->inet.in_ifname);
        return false;
    }

    return true;
}

bool inet_base_dhcps_rip_del(inet_t *super, inet_macaddr_t macaddr)
{
    inet_base_t *self = (inet_base_t *)super;


    if (!inet_dhcps_rip_remove(self->in_dhcps, macaddr))
    {
        LOG(ERR, "inet_base: %s: Error deleting IP reservation.", self->inet.in_ifname);
        return false;
    }

    /* Flag the DHCP server for restart */
    if (!inet_unit_restart(self->in_units, INET_BASE_DHCP_SERVER, true))
    {
        LOG(ERR, "inet_base: %s: Error restarting INET_BASE_DHCP_SERVER", self->inet.in_ifname);
        return false;
    }

    return true;
}

/*
 * ===========================================================================
 *  DNS related settings
 * ===========================================================================
 */
bool inet_base_dns_set(inet_t *super, inet_ip4addr_t primary, inet_ip4addr_t secondary)
{
    inet_base_t *self = (inet_base_t *)super;

    /* No change -- return success */
    if (inet_ip4addr_cmp(&self->in_dns_primary, &primary) == 0 &&
            inet_ip4addr_cmp(&self->in_dns_secondary, &secondary) == 0)
    {
        return true;
    }

    if (!inet_dns_server_set(self->in_dns, primary, secondary))
    {
        LOG(ERR, "inet_base: %s: Error setting DNS server settings.", self->inet.in_ifname);
        return false;
    }

    /* Flag the DNS service for restart */
    if (!inet_unit_restart(self->in_units, INET_BASE_DNS, true))
    {
        LOG(ERR, "inet_base: %s: Error restarting INET_BASE_DNS", self->inet.in_ifname);
        return false;
    }

    return true;
}

/*
 * ===========================================================================
 *  DHCP sniffing
 * ===========================================================================
 */
bool inet_base_dhsnif_lease_register(inet_t *super, inet_dhcp_lease_fn_t *func)
{
    inet_base_t *self = (inet_base_t *)super;

    if (self->in_dhsnif_lease_fn == func) return true;

    self->in_dhsnif_lease_fn = func;

    if (!inet_dhsnif_notify(self->in_dhsnif, func, super))
    {
        LOG(ERR, "inet_base: %s: Error setting the DHCP sniffing handler.", self->inet.in_ifname);
        return false;
    }

    /*
     * Restart or Stop the DHCPSNIFF service according to the value of func
     */
    if (func != NULL && !inet_unit_restart(self->in_units, INET_BASE_DHCPSNIFF, true))
    {
        LOG(ERR, "inet_base: %s: Error restarting INET_BASE_DHCPSNIFF", self->inet.in_ifname);
        return false;
    }

    if (func == NULL && !inet_unit_stop(self->in_units, INET_BASE_DHCPSNIFF))
    {
        LOG(ERR, "inet_base: %s: Error stopping INET_BASE_DHCPSNIFF", self->inet.in_ifname);
        return false;
    }

    return true;
}

/*
 * ===========================================================================
 *  Status reporting
 * ===========================================================================
 */
bool inet_base_state_get(inet_t *super, inet_state_t *out)
{
    inet_base_t *self = (inet_base_t *)super;

    memset(out, 0, sizeof(*out));

    out->in_mtu = self->in_mtu;

    out->in_interface_enabled = self->in_interface_enabled;
    out->in_network_enabled = self->in_network_enabled;

    out->in_assign_scheme = self->in_assign_scheme;

    out->in_dhcps_enabled = self->in_dhcps_enabled;

    out->in_nat_enabled = false;
    out->in_upnp_mode = UPNP_MODE_NONE;


    if (!inet_fw_state_get(self->in_fw, &out->in_nat_enabled))
    {
        LOG(DEBUG, "inet_base: %s: Error retrieving firewall module state.",
                self->inet.in_ifname);
    }

    if (!inet_dhcpc_state_get(self->in_dhcpc, &out->in_dhcpc_enabled))
    {
        LOG(DEBUG, "inet_base: %s: Error retrieving DHCP client state.",
                self->inet.in_ifname);
    }

    if (!inet_upnp_get(self->in_upnp, &out->in_upnp_mode))
    {
        LOG(DEBUG, "inet_base: %s: Erro retrieving UPnP mode.", self->inet.in_ifname);
    }

    /*
     * inet_base() has the currently inactive information below, the subclass
     * can fill with live data
     */
    out->in_ipaddr = self->in_static_addr;
    out->in_netmask = self->in_static_netmask;
    out->in_bcaddr = self->in_static_bcast;
    out->in_gateway = self->in_static_gwaddr;

    return true;
}

/*
 * ===========================================================================
 *  Commit / Start & Stop methods
 * ===========================================================================
 */
/**
 * Dispatcher of START/STOP events
 */
bool __inet_base_commit(void *ctx, intptr_t unitid, bool enable)
{
    inet_base_t *self = ctx;

    return inet_service_commit(self, unitid, enable);
}

bool inet_base_commit(inet_t *super)
{
    inet_base_t *self = (inet_base_t *)super;

    LOG(INFO, "inet_base: %s: Commiting new configuration.", self->inet.in_ifname);

    /* Commit all pending units */
    return inet_unit_commit(self->in_units, __inet_base_commit, self);
}

/**
 * Inet_base imeplementation of start/stop service. The service should be
 * started if @p start is true, otherwise it should be stopped.
 *
 * inet_base takes care of stopping/starting the service only once even if multiple starts or stops
 * are requested.
 *
 * New class implementations should implement the following services
 * - INET_BASE_INETFACE
 * - INET_BASE_NETWORK
 * - INET_BASE_SCHEME_DHCP
 */
bool inet_base_service_commit(
        inet_base_t *self,
        enum inet_base_services srv,
        bool start)
{
    LOG(INFO, "inet_base: %s: Service %s -> %s",
            self->inet.in_ifname,
            inet_base_service_str(srv),
            start ? "start" : "stop");

    switch (srv)
    {
        case INET_BASE_FIREWALL:
            return inet_base_firewall_commit(self, start);

        case INET_BASE_UPNP:
            return inet_base_upnp_commit(self, start);

        case INET_BASE_SCHEME_DHCP:
            return inet_base_dhcp_client_commit(self, start);

        case INET_BASE_DHCP_SERVER:
            return inet_base_dhcp_server_commit(self, start);

        case INET_BASE_DNS:
            return inet_base_dns_commit(self, start);

        case INET_BASE_DHCPSNIFF:
            return inet_base_dhsnif_commit(self, start);

        default:
            LOG(INFO, "inet_base: %s: Ignoring service start/stop request: %s -> %d",
                    self->inet.in_ifname,
                    inet_base_service_str(srv),
                    start);
            break;
    }

    return false;
}

/**
 * Start or stop the firewall service
 */
bool inet_base_firewall_commit(inet_base_t *self, bool start)
{
    /* Start service */
    if (start && !inet_fw_start(self->in_fw))
    {
        LOG(ERR, "inet_base: %s: Error starting the Firewall service.", self->inet.in_ifname);
        return false;
    }

    /* Stop service */
    if (!start && !inet_fw_stop(self->in_fw))
    {
        LOG(ERR, "inet_base: %s: Error stopping the Firewall service.", self->inet.in_ifname);
        return false;
    }

    return true;
}

bool inet_base_upnp_commit(inet_base_t *self, bool start)
{
    /* Start service */
    if (start && !inet_upnp_start(self->in_upnp))
    {
        LOG(ERR, "inet_base: %s: Error starting the UPnP service.", self->inet.in_ifname);
        return false;
    }

    /* Stop service */
    if (!start && !inet_upnp_stop(self->in_upnp))
    {
        LOG(ERR, "inet_base: %s: Error stopping the UPnP service.", self->inet.in_ifname);
         return false;
    }

    return true;
}
/**
 * Start or stop the DHCP client service
 */
bool inet_base_dhcp_client_commit(inet_base_t *self, bool start)
{
    /* Start service */
    if (start && !inet_dhcpc_start(self->in_dhcpc))
    {
        LOG(ERR, "inet_base: %s: Error starting the DHCP client service.", self->inet.in_ifname);
        return false;
    }

    /* Stop service */
    if (!start && !inet_dhcpc_stop(self->in_dhcpc))
    {
        LOG(ERR, "inet_base: %s: Error stopping the DHCP client service.", self->inet.in_ifname);
        return false;
    }

    return true;
}

/**
 * Start or stop the DHCP server service
 */
bool inet_base_dhcp_server_commit(inet_base_t *self, bool start)
{
    /* Start service */
    if (start && !inet_dhcps_start(self->in_dhcps))
    {
        LOG(ERR, "inet_base: %s: Error starting the DHCP server service.", self->inet.in_ifname);
        return false;
    }

    /* Stop service */
    if (!start && !inet_dhcps_stop(self->in_dhcps))
    {
        LOG(ERR, "inet_base: %s: Error stopping the DHCP server service.", self->inet.in_ifname);
        return false;
    }

    return true;
}

bool inet_base_dns_commit(inet_base_t *self, bool start)
{
    /* Start service */
    if (start && !inet_dns_start(self->in_dns))
    {
        LOG(ERR, "inet_base: %s: Error starting the DNS service.", self->inet.in_ifname);
        return false;
    }

    /* Stop service */
    if (!start && !inet_dns_stop(self->in_dns))
    {
        LOG(ERR, "inet_base: %s: Error stopping the DNS service.", self->inet.in_ifname);
        return false;
    }

    return true;
}

bool inet_base_dhsnif_commit(inet_base_t *self, bool start)
{
    /* Start service */
    if (start && !inet_dhsnif_start(self->in_dhsnif))
    {
        LOG(ERR, "inet_base: %s: Error starting the DHCP sniffing service.", self->inet.in_ifname);
        return false;
    }

    /* Stop service */
    if (!start && !inet_dhsnif_stop(self->in_dhsnif))
    {
        LOG(ERR, "inet_base: %s: Error stopping the DHCP sniffing service.", self->inet.in_ifname);
        return false;
    }

    return true;
}

/*
 * ===========================================================================
 *  Miscellaneous
 * ===========================================================================
 */
const char *inet_base_service_str(enum inet_base_services srv)
{
    #define _V(x) case x: return #x;

    switch (srv)
    {
        INET_BASE_SERVICE_LIST(_V)

        default:
            return "Unknown";
    }
}
