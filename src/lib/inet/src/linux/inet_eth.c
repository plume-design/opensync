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

#include "inet.h"
#include "inet_base.h"
#include "inet_eth.h"

#include "execsh.h"

/*
 * ===========================================================================
 *  Inet Ethernet implementation for Linux
 * ===========================================================================
 */
static int ifreq_socket(void);
static bool ifreq_exists(const char *ifname, bool *exists);
static bool ifreq_mtu_set(const char *ifname, int mtu);
static bool ifreq_mtu_get(const char *ifname, int *mtu);
static bool ifreq_status_set(const char *ifname, bool up);
static bool ifreq_status_get(const char *ifname, bool *up);
static bool ifreq_running_get(const char *ifname, bool *up);
static bool ifreq_ipaddr_set(const char *ifname, inet_ip4addr_t ipaddr);
static bool ifreq_ipaddr_get(const char *ifname, inet_ip4addr_t *ipaddr);
static bool ifreq_netmask_set(const char *ifname, inet_ip4addr_t netmask);
static bool ifreq_netmask_get(const char *ifname, inet_ip4addr_t *netmask);
static bool ifreq_bcaddr_set(const char *ifname, inet_ip4addr_t bcaddr);
static bool ifreq_bcaddr_get(const char *ifname, inet_ip4addr_t *bcaddr);
static bool ifreq_hwaddr_get(const char *ifname, inet_macaddr_t *macaddr);

static char eth_ip_route_add_default[] = _S(ip route add default via "$2" dev "$1" metric 100);
static char eth_ip_route_del_default[] = _S(ip route del default dev "$1" || true);

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
    inet_eth_t *self = NULL;

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
    if (!inet_base_init(&self->base, ifname))
    {
        LOG(ERR, "inet_eth: %s: Failed to instantiate class, inet_base_init() failed.", ifname);
        return false;
    }

    /* Override methods */
    self->inet.in_state_get_fn = inet_eth_state_get;
    self->base.in_service_commit_fn = inet_eth_service_commit;

    /* Verify that we can create the IFREQ socket */
    if (ifreq_socket() < 0)
    {
        LOG(ERR, "inet_eth: %s: Failed to create IFREQ socket.", ifname);
        return false;
    }

    return true;
}

/*
 * ===========================================================================
 *  Commit and service start & stop functions
 * ===========================================================================
 */

bool inet_eth_interface_start(inet_eth_t *self, bool enable)
{
    bool exists;
    bool retval;

    /*
     * Just check if the interface exists -- report an error otherwise so no
     * other services will be started.
     */
    retval = ifreq_exists(self->inet.in_ifname, &exists);
    if (!retval || !exists)
    {
        LOG(ERR, "inet_eth: %s: Interface does not exists. Stopping.",
                self->inet.in_ifname);

        return false;
    }

    return true;
}

/**
 * In case of inet_eth a "network start" roughly translates to a ifconfig up
 * Likewise, a "network stop" is basically an ifconfig down.
 */
bool inet_eth_network_start(inet_eth_t *self, bool enable)
{
    if (!ifreq_status_set(self->inet.in_ifname, enable))
    {
        LOG(ERR, "inet_eth: %s: Error %s network.",
                self->inet.in_ifname,
                enable ? "enabling" : "disabling");
        return false;
    }

    LOG(INFO, "inet_eth: %s: Network %s.",
            self->inet.in_ifname,
            enable ? "enabled" : "disabled");

    return true;
}

bool __inet_eth_scheme_none(inet_eth_t *self)
{
    int status;

    bool retval = true;

    /* Clear IP */
    if (!ifreq_ipaddr_set(self->inet.in_ifname, INET_IP4ADDR_ANY))
    {
        LOG(ERR, "inet_eth: %s: Unable to clear IP address or netmask.", self->inet.in_ifname);
        retval = false;
    }

    /* Remove default routes, if there are any */
    status = execsh_log(LOG_SEVERITY_INFO, eth_ip_route_del_default, self->inet.in_ifname);
    if (!WIFEXITED(status) && WEXITSTATUS(status) != 0)
    {
        LOG(ERR, "inet_eth: %s: Error removing default route.", self->inet.in_ifname);
        retval = false;
    }

    return retval;
}

/**
 * IP Assignment of NONE means that the interface must be UP, but should not have an address.
 * Just clear the IP to 0.0.0.0/0, it doesn't matter if its a stop or start event.
 */
bool inet_eth_scheme_none_start(inet_eth_t *self, bool enable)
{
    (void)enable;

    if (!__inet_eth_scheme_none(self))
    {
        LOG(ERR, "inet_eth: %s: Assignment scheme NONE error, unable to clear IP address.", self->inet.in_ifname);
        return false;
    }

    return true;
}

bool inet_eth_scheme_static_start(inet_eth_t *self, bool enable)
{
    int status;

    bool retval = true;

    if (enable)
    {
        /**
         * Check if all the necessary configuration is present (ipaddr, netmask and bcast)
         */
        if (INET_IP4ADDR_IS_ANY(self->base.in_static_addr))
        {
            LOG(ERR, "inet_eth: %s: ipaddr is missing for static IP assignment scheme.", self->inet.in_ifname);
            return false;
        }

        if (INET_IP4ADDR_IS_ANY(self->base.in_static_netmask))
        {
            LOG(ERR, "inet_eth: %s: netmask is missing for static IP assignment scheme.", self->inet.in_ifname);
            return false;
        }

        if (!ifreq_ipaddr_set(
                    self->inet.in_ifname,
                    self->base.in_static_addr))
        {
            LOG(ERR, "inet_eth: %s: Error setting IP for static assignment scheme.", self->inet.in_ifname);
            retval = false;
        }

        if (!ifreq_netmask_set(
                    self->inet.in_ifname,
                    self->base.in_static_netmask))
        {
            LOG(ERR, "inet_eth: %s: Error setting netmask for static assignment scheme.", self->inet.in_ifname);
            retval = false;
        }

        if (!INET_IP4ADDR_IS_ANY(self->base.in_static_bcast))
        {
            if (!ifreq_bcaddr_set(
                        self->inet.in_ifname,
                        self->base.in_static_bcast))
            {
                LOG(WARN, "inet_eth: %s: Error setting broadcast address.",
                        self->inet.in_ifname);
            }
        }

        /*
         * Use the "ip route" command to add the default route
         */
        if (!INET_IP4ADDR_IS_ANY(self->base.in_static_gwaddr))
        {
            char sgwaddr[C_IP4ADDR_LEN];

            snprintf(sgwaddr, sizeof(sgwaddr), PRI(inet_ip4addr_t), FMT(inet_ip4addr_t, self->base.in_static_gwaddr));

            status = execsh_log(LOG_SEVERITY_INFO, eth_ip_route_add_default, self->inet.in_ifname, sgwaddr);
            if (!WIFEXITED(status) && WEXITSTATUS(status) != 0)
            {
                retval = false;
            }
        }
    }
    else
    {
        retval = inet_eth_scheme_none_start(self, false);
    }

    return retval;
}

/**
 * MTU Settings
 */
bool inet_eth_mtu_start(inet_eth_t *self, bool enable)
{
    if (enable)
    {
        if (self->base.in_mtu <= 0)
        {
            LOG(ERR, "inet_eth: %s: A MTU setting of %d is invalid.",
                    self->inet.in_ifname,
                    self->base.in_mtu);

            return false;
        }

        if (!ifreq_mtu_set(self->inet.in_ifname, self->base.in_mtu))
        {
            LOG(ERR, "inet_eth: %s: Error setting MTU to %d.",
                    self->inet.in_ifname,
                    self->base.in_mtu);

            return false;
        }
    }
    else
    {
        /* Set the default MTU */
        if (!ifreq_mtu_set(self->inet.in_ifname, 1500))
        {
            LOG(ERR, "inet_eth: %s: Error resetting MTU to default (150).",
                    self->inet.in_ifname);

            return false;
        }
    }

    LOG(INFO, "inet_eth: %s: MTU setting was %s.",
            self->inet.in_ifname,
            enable ? "enabled" : "disabled" );

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
        case INET_BASE_INTERFACE:
            return inet_eth_interface_start(self, enable);

        case INET_BASE_NETWORK:
            return inet_eth_network_start(self, enable);

        case INET_BASE_SCHEME_NONE:
            return inet_eth_scheme_none_start(self, enable);

        case INET_BASE_SCHEME_STATIC:
            return inet_eth_scheme_static_start(self, enable);

        case INET_BASE_MTU:
            return inet_eth_mtu_start(self, enable);

        default:
            LOG(DEBUG, "inet_eth: %s: Delegating service %s %s to inet_base.",
                    self->inet.in_ifname,
                    inet_base_service_str(srv),
                    enable ? "start" : "stop");

            /* Delegate everything else to inet_base() */
            return inet_base_service_commit(super, srv, enable);
    }

    return true;
}

/*
 * ===========================================================================
 *  Status reporting
 * ===========================================================================
 */
bool inet_eth_state_get(inet_t *super, inet_state_t *out)
{
    inet_eth_t *self = (inet_eth_t *)super;

    if (!inet_base_state_get(&self->inet, out))
    {
        return false;
    }

    (void)ifreq_exists(self->inet.in_ifname, &out->in_interface_enabled);
    if (out->in_interface_enabled)
    {
        (void)ifreq_mtu_get(self->inet.in_ifname, &out->in_mtu);
        (void)ifreq_status_get(self->inet.in_ifname, &out->in_network_enabled);
        (void)ifreq_ipaddr_get(self->inet.in_ifname, &out->in_ipaddr);
        (void)ifreq_netmask_get(self->inet.in_ifname, &out->in_netmask);
        (void)ifreq_bcaddr_get(self->inet.in_ifname, &out->in_bcaddr);
        (void)ifreq_hwaddr_get(self->inet.in_ifname, &out->in_macaddr);
        (void)ifreq_running_get(self->inet.in_ifname, &out->in_port_status);
    }

    return true;
}

/*
 * ===========================================================================
 *  IFREQ -- interface ioctl() and ifreq requets
 * ===========================================================================
 */

int ifreq_socket(void)
{
    static int ifreq_socket = -1;

    if (ifreq_socket >= 0) return ifreq_socket;

    ifreq_socket = socket(AF_INET, SOCK_DGRAM, 0);

    return ifreq_socket;
}

/**
 * ioctl() wrapper around ifreq
 */
bool ifreq_ioctl(const char *ifname, int cmd, struct ifreq *req)
{
    int s;

    if (strscpy(req->ifr_name, ifname, sizeof(req->ifr_name)) < 0)
    {
        LOG(ERR, "inet_eth: %s: ioctl() failed, interface name too long.", ifname);
        return false;
    }

    s = ifreq_socket();
    if (s < 0)
    {
        LOG(ERR, "inet_eth: %s: Unable to acquire the IFREQ socket: %s",
                ifname,
                strerror(errno));
        return false;
    }

    if (ioctl(s, cmd, (void *)req) < 0)
    {
        return false;
    }

    return true;
}

/**
 * Check if the interface exists
 */
bool ifreq_exists(const char *ifname, bool *exists)
{
    struct ifreq ifr;

    /* First get the current flags */
    if (!ifreq_ioctl(ifname, SIOCGIFINDEX, &ifr))
    {
        *exists = false;
    }
    else
    {
        *exists = true;
    }


    return true;
}

/**
 * Equivalent of ifconfig up/down
 */
bool ifreq_status_set(const char *ifname, bool up)
{
    struct ifreq ifr;

    /* First get the current flags */
    if (!ifreq_ioctl(ifname, SIOCGIFFLAGS, &ifr))
    {
        LOG(ERR, "inet_eth: %s: SIOCGIFFLAGS failed. Error retrieving the interface status: %s",
                ifname,
                strerror(errno));

        return false;
    }

    /* Set or clear IFF_UP depending on the action defined by @p up */
    if (up)
    {
        ifr.ifr_flags |= IFF_UP;
    }
    else
    {
        ifr.ifr_flags &= ~IFF_UP;
    }

    if (!ifreq_ioctl(ifname, SIOCSIFFLAGS, &ifr))
    {
        LOG(ERR, "inet_eth: %s: SIOCSIFFLAGS failed. Error setting the interface status: %s",
                ifname,
                strerror(errno));

        return false;
    }

    return true;
}

/**
 * Get interface status
 */
bool ifreq_status_get(const char *ifname, bool *up)
{
    struct ifreq ifr;

    /* First get the current flags */
    if (!ifreq_ioctl(ifname, SIOCGIFFLAGS, &ifr))
    {
        LOG(ERR, "inet_eth: %s: SIOCGIFFLAGS failed. Error retrieving the interface status: %s",
                ifname,
                strerror(errno));

        return false;
    }

    *up = ifr.ifr_flags & IFF_UP;

    return true;
}

/**
 * Get interface status
 */
bool ifreq_running_get(const char *ifname, bool *up)
{
    struct ifreq ifr;

    /* First get the current flags */
    if (!ifreq_ioctl(ifname, SIOCGIFFLAGS, &ifr))
    {
        LOG(ERR, "inet_eth: %s: SIOCGIFFLAGS failed. Error retrieving the interface running state: %s",
                ifname,
                strerror(errno));
        return false;
    }

    *up = ifr.ifr_flags & IFF_RUNNING;

    return true;
}

/**
 * Set the IP of an interface
 */
bool ifreq_ipaddr_set(const char *ifname, inet_ip4addr_t ipaddr)
{
    struct ifreq ifr;

    ifr.ifr_addr.sa_family = AF_INET;
    memcpy(&((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr, &ipaddr, sizeof(ipaddr));
    if (!ifreq_ioctl(ifname, SIOCSIFADDR, &ifr))
    {
        LOG(ERR, "inet_eth: %s: SIOCSIFADDR failed. Error setting the IP address: %s",
                ifname,
                strerror(errno));

        return false;
    }

    return true;
}

/**
 * Get the IP of an interface
 */
bool ifreq_ipaddr_get(const char *ifname, inet_ip4addr_t *ipaddr)
{
    struct ifreq ifr;

    ifr.ifr_addr.sa_family = AF_INET;

    if (!ifreq_ioctl(ifname, SIOCGIFADDR, &ifr))
    {
        *ipaddr = INET_IP4ADDR_ANY;
    }
    else
    {
        memcpy(
                ipaddr,
                &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr,
                sizeof(*ipaddr));
    }

    return true;
}


/**
 * Set the Netmask of an interface
 */
bool ifreq_netmask_set(const char *ifname, inet_ip4addr_t netmask)
{
    struct ifreq ifr;

    ifr.ifr_addr.sa_family = AF_INET;
    memcpy(&((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr, &netmask, sizeof(netmask));
    if (!ifreq_ioctl(ifname, SIOCSIFNETMASK, &ifr))
    {
        LOG(ERR, "inet_eth: %s: SIOCSIFNETMASK failed. Error setting the netmask address: %s",
                ifname,
                strerror(errno));

        return false;
    }

    return true;
}

/**
 * Get the Netmask of an interface
 */
bool ifreq_netmask_get(const char *ifname, inet_ip4addr_t *netmask)
{
    struct ifreq ifr;

    ifr.ifr_addr.sa_family = AF_INET;

    if (!ifreq_ioctl(ifname, SIOCGIFNETMASK, &ifr))
    {
        *netmask = INET_IP4ADDR_ANY;
    }
    else
    {
        memcpy(
                netmask,
                &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr,
                sizeof(*netmask));
    }

    return true;
}


/**
 * Set the broadcast address of an interface
 */
bool ifreq_bcaddr_set(const char *ifname, inet_ip4addr_t bcaddr)
{
    struct ifreq ifr;

    ifr.ifr_addr.sa_family = AF_INET;
    memcpy(&((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr, &bcaddr, sizeof(bcaddr));
    if (!ifreq_ioctl(ifname, SIOCSIFBRDADDR, &ifr))
    {
        LOG(ERR, "inet_eth: %s: SIOCSIFBRDADDR failed. Error setting the boradcast address: %s",
                ifname,
                strerror(errno));

        return false;
    }

    return true;
}

/**
 * Get the broadcast address of an interface
 */
bool ifreq_bcaddr_get(const char *ifname, inet_ip4addr_t *bcaddr)
{
    struct ifreq ifr;

    ifr.ifr_addr.sa_family = AF_INET;

    if (!ifreq_ioctl(ifname, SIOCGIFBRDADDR, &ifr))
    {
        *bcaddr = INET_IP4ADDR_ANY;
    }
    else
    {
        memcpy(
                bcaddr,
                &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr,
                sizeof(*bcaddr));
    }

    return true;
}

/**
 * Set the MTU
 */
bool ifreq_mtu_set(const char *ifname, int mtu)
{
    struct ifreq ifr;

    ifr.ifr_mtu = mtu;

    if (!ifreq_ioctl(ifname, SIOCSIFMTU, &ifr))
    {
        LOG(ERR, "inet_eth: %s: SIOCSIFMTU failed. Error setting the MTU: %s",
                ifname,
                strerror(errno));

        return false;
    }

    return true;
}

/**
 * Get the MTU
 */
bool ifreq_mtu_get(const char *ifname, int *mtu)
{
    struct ifreq ifr;

    if (!ifreq_ioctl(ifname, SIOCGIFMTU, &ifr))
    {
        LOG(ERR, "inet_eth: %s: SIOCGIFMTU failed. Error retrieving the MTU: %s",
                ifname,
                strerror(errno));

        return false;
    }

    *mtu = ifr.ifr_mtu;

    return true;
}


/**
 * Get the MAC address, as string
 */
bool ifreq_hwaddr_get(const char *ifname, inet_macaddr_t *macaddr)
{
    struct ifreq ifr;

    /* Get the MAC(hardware) address */
    if (!ifreq_ioctl(ifname, SIOCGIFHWADDR, &ifr))
    {
        LOG(ERR, "inet_eth: %s: SIOCGIFHWADDR failed. Error retrieving the MAC address: %s",
                ifname,
                strerror(errno));

        return false;
    }

    /* Copy the address */
    memcpy(macaddr, ifr.ifr_addr.sa_data, sizeof(inet_macaddr_t));

    return true;

}
