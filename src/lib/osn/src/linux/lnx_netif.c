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

#include <stdlib.h>
#include <errno.h>

#include "const.h"
#include "log.h"
#include "util.h"

#include "lnx_netif.h"

static lnx_netlink_fn_t lnx_netif_nl_fn;
static void lnx_netif_status_poll(lnx_netif_t *self);
static int lnx_netif_socket(void);
static bool lnx_netif_ioctl(const char *ifname, int cmd, struct ifreq *req);

/*
 * ===========================================================================
 *  Public API
 * ===========================================================================
 */

/*
 * Initialize the Ethernet interface object
 */
bool lnx_netif_init(lnx_netif_t *self, const char *ifname)
{
    if (STRSCPY(self->ni_ifname, ifname) < 0)
    {
        LOG(ERR, "netif: %s: Interface name too long", ifname);
        return false;
    }

    self->ni_state = -1;
    self->ni_mtu = -1;
    self->ni_hwaddr = OSN_MAC_ADDR_INIT;

    /* Initialize the netlink socket */
    if (!lnx_netlink_init(&self->ni_netlink, lnx_netif_nl_fn))
    {
        LOG(ERR, "netif: %s: Error initializing netlink object.", ifname);
        return false;
    }

    /* Remember the interface index */
    self->ni_index = if_nametoindex(ifname);

    /* Set NL filters -- interface and events  */
    lnx_netlink_set_ifname(&self->ni_netlink, self->ni_ifname);
    lnx_netlink_set_events(&self->ni_netlink, LNX_NETLINK_LINK);

    return true;
}

/*
 * Destroy the instance and clean up any resources allocated during its lifetime
 */
bool lnx_netif_fini(lnx_netif_t *self)
{
    bool retval = true;

    /* Deinitialize netlink sockets */
    if (!lnx_netlink_fini(&self->ni_netlink))
    {
        LOG(ERR, "netif: %s: Error destroying netlink object.", self->ni_ifname);
        retval = false;
    }

    memset(self, 0, sizeof(*self));

    return retval;
}

/*
 * Set interface state
 */
bool lnx_netif_state_set(lnx_netif_t *self, bool up)
{
    self->ni_state = up ? 1 : 0;

    return true;
}

/*
 * Set MTU
 */
bool lnx_netif_mtu_set(lnx_netif_t *self, int mtu)
{
    self->ni_mtu = mtu;

    return true;
}

bool lnx_netif_hwaddr_set(lnx_netif_t *self, osn_mac_addr_t hwaddr)
{
    self->ni_hwaddr = hwaddr;

    return true;
}

/*
 * Apply configuration to running system
 */
bool lnx_netif_apply(lnx_netif_t *self)
{
    struct ifreq ifr;

    bool retval = true;

    /* First of all, check if the interface exists. If it does not, just return */
    if (!lnx_netif_ioctl(self->ni_ifname, SIOCGIFINDEX, &ifr))
    {
        LOG(NOTICE, "netif: Interface %s does not exists.", self->ni_ifname);
        return false;
    }

    /* Set MTU */
    if (self->ni_mtu >= 0)
    {
        ifr.ifr_mtu = self->ni_mtu;
        if (!lnx_netif_ioctl(self->ni_ifname, SIOCSIFMTU, &ifr))
        {
            LOG(NOTICE, "netif: %s: Unable to set interface MTU.", self->ni_ifname);
            retval = false;
        }
    }

    /* Set interface MAC address */
    if (osn_mac_addr_cmp(&self->ni_hwaddr, &OSN_MAC_ADDR_INIT) != 0)
    {
        /* Copy the mac address */
        memcpy(&ifr.ifr_addr.sa_data, self->ni_hwaddr.ma_addr, sizeof(self->ni_hwaddr.ma_addr));
        if (!lnx_netif_ioctl(self->ni_ifname, SIOCSIFHWADDR, &ifr))
        {
            LOG(NOTICE, "netif: %s: Unable to set MAC address "PRI_osn_mac_addr,
                        self->ni_ifname, FMT_osn_mac_addr(self->ni_hwaddr));
            retval = false;
        }
    }

    /* Set interface state */
    if (self->ni_state >= 0)
    {
        if (lnx_netif_ioctl(self->ni_ifname, SIOCGIFFLAGS, &ifr))
        {
            if (self->ni_state > 0)
            {
                ifr.ifr_flags |= IFF_UP;
            }
            else
            {
                ifr.ifr_flags &= ~IFF_UP;
            }

            if (!lnx_netif_ioctl(self->ni_ifname, SIOCSIFFLAGS, &ifr))
            {
                LOG(NOTICE, "netif: %s: Unable to set interface flags.", self->ni_ifname);
                retval = false;
            }
        }
        else
        {
            LOG(NOTICE, "netif: %s: Unable to retrieve interface flags.", self->ni_ifname);
            retval = false;
        }
    }

    return retval;
}

/*
 * Register status handler
 */
void lnx_netif_status_notify(lnx_netif_t *self, lnx_netif_status_fn_t *fn)
{
    self->ni_status_fn = fn;

    if (fn != NULL)
    {
        /* Start polling the interface status immediately */
        if (!lnx_netlink_start(&self->ni_netlink))
        {
            LOG(WARN, "netif: %s: Error stopping netlink object.", self->ni_ifname);
        }
    }
    else
    {
        /* Start polling the interface status immediately */
        if (!lnx_netlink_stop(&self->ni_netlink))
        {
            LOG(WARN, "netif: %s: Error starting netlink object.", self->ni_ifname);
        }
    }
}


/*
 * ===========================================================================
 *  Private functions
 * ===========================================================================
 */

/*
 * Poll the interface status, fill in the status structure and dispatch the
 * callback/
 */
void lnx_netif_status_poll(lnx_netif_t *self)
{
    unsigned int if_idx;
    struct ifreq ifr;

    LOG(INFO, "netif: %s: Interface status update.", self->ni_ifname);

    memset(&self->ni_status, 0, sizeof(self->ni_status));

    self->ni_status.ns_ifname = self->ni_ifname;

    if_idx = if_nametoindex(self->ni_ifname);
    if ((self->ni_index != 0) &&
            (if_idx != 0) &&
            (if_idx != self->ni_index))
    {
        LOG(NOTICE, "netif: %s: Interface fast re-creation detected.", self->ni_ifname);
        /*
         * Interface was destroyed and re-created fast enough that the netlink
         * debounce timer collapsed the two events into one. Simulate a interface
         * deletion event here.
         */
        self->ni_status.ns_exists = false;

        if (self->ni_status_fn != NULL)
            self->ni_status_fn(self, &self->ni_status);
    }

    self->ni_index = if_idx;

    /* Retrieve the interface flags */
    if (lnx_netif_ioctl(self->ni_ifname, SIOCGIFFLAGS, &ifr))
    {
        /* Interface existence */
        self->ni_status.ns_exists = true;
        /* Carrier */
        self->ni_status.ns_carrier = ifr.ifr_flags & IFF_RUNNING;
        /* Interface status */
        self->ni_status.ns_up = ifr.ifr_flags & IFF_UP;
    }

    /* Retrieve the MTU */
    if (lnx_netif_ioctl(self->ni_ifname, SIOCGIFMTU, &ifr))
    {
        self->ni_status.ns_mtu = ifr.ifr_mtu;
    }

    /* Retrieve hardware address */
    self->ni_status.ns_hwaddr = OSN_MAC_ADDR_INIT;
    if (lnx_netif_ioctl(self->ni_ifname, SIOCGIFHWADDR, &ifr))
    {
        memcpy(self->ni_status.ns_hwaddr.ma_addr,
                ifr.ifr_addr.sa_data,
                sizeof(self->ni_status.ns_hwaddr.ma_addr));
    }

    /* Execute the callback */
    if (self->ni_status_fn != NULL)
        self->ni_status_fn(self, &self->ni_status);
}

/*
 * Netlink socket callback
 */
void lnx_netif_nl_fn(lnx_netlink_t *nl, uint64_t event, const char *ifname)
{
    lnx_netif_t *self = CONTAINER_OF(nl, lnx_netif_t, ni_netlink);

    (void)event;
    (void)ifname;

    lnx_netif_status_poll(self);
}

/*
 * Open a socket of type AF_INET -- we will use this socket to issue
 * various ioctl()s
 */
int lnx_netif_socket(void)
{
    static int ifreq_socket = -1;

    if (ifreq_socket >= 0) return ifreq_socket;

    ifreq_socket = socket(AF_INET, SOCK_DGRAM, 0);

    return ifreq_socket;
}

/*
 * Issue an ioctl() on the specified interface
 */
bool lnx_netif_ioctl(const char *ifname, int cmd, struct ifreq *req)
{
    int s;

    /* Fill in the interface name */
    if (STRSCPY(req->ifr_name, ifname) < 0)
    {
        LOG(ERR, "netif: %s: ioctl() failed, interface name too long.", ifname);
        return false;
    }

    s = lnx_netif_socket();
    if (s < 0)
    {
        LOG(ERR, "netif: %s: Unable to acquire the IFREQ socket: %s",
                ifname,
                strerror(errno));
        return false;
    }

    /* Issue ioctl() */
    if (ioctl(s, cmd, (void *)req) < 0) return false;

    return true;
}
