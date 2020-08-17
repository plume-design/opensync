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
 *  This module implements interface status polling via NETLINK sockets.
 *
 *  This is an private module and is not part of the OpenSync Networking API.
 * ===========================================================================
 */

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>

#include "log.h"
#include "evx.h"
#include "kconfig.h"
#include "util.h"

#include "lnx_netlink.h"

#if !defined(CONFIG_OSN_NETLINK_DEBOUNCE_MS)
#define CONFIG_OSN_NETLINK_DEBOUNCE_MS 300
#endif

/* List of active netlink listeners */
static ds_dlist_t lnx_netlink_list = DS_DLIST_INIT(lnx_netlink_t, nl_tnode);

/* Netlink socket */
static int lnx_netlink_sock = -1;
/* nl_sock watcher */
static ev_io lnx_netlink_sock_ev;
/* Delayed dispatching of events */
static ev_debounce lnx_netlink_dispatch_ev;

static bool lnx_netlink_global_init(void);
static bool lnx_netlink_sock_open(void);
static void lnx_netlink_sock_close(void);
static void lnx_netlink_sock_fn(struct ev_loop *loop, ev_io *ev, int revent);

/* Schedule an event to be dispatched */
static bool lnx_netlink_dispatch(uint64_t nl_event, const char *ifname);
/* Handler of the debounce timer -- this will actually dispatch pending events */
static void lnx_netlink_dispatch_fn(struct ev_loop *loop, ev_debounce *ev, int revent);
/* Filter out unwanted netlink messages as they cause too many  updates */
static bool lnx_netlink_weed_out(struct nlmsghdr *nh);

bool lnx_netlink_init(lnx_netlink_t *self, lnx_netlink_fn_t *fn)
{
    memset(self, 0, sizeof(*self));
    self->nl_fn = fn;

    return true;
}

bool lnx_netlink_fini(lnx_netlink_t *self)
{
    return lnx_netlink_stop(self);
}

bool lnx_netlink_start(lnx_netlink_t *self)
{
    if (self->nl_active) return true;

    /* Global initialization */
    if (!lnx_netlink_global_init())
    {
        LOG(ERR, "netlink: Error initializing global instance.");
        goto exit;
    }

    /* Insert listener to the global list */
    self->nl_active = true;
    ds_dlist_insert_tail(&lnx_netlink_list, self);

    /*
     * Immediately dispatch an event -- this ensures that at least 1 event is
     * received by the listener and that it is received sooner rather than later
     */
    lnx_netlink_dispatch(self->nl_events, self->nl_ifname);

exit:
    return self->nl_active;
}

bool lnx_netlink_stop(lnx_netlink_t *self)
{
    if (!self->nl_active) return true;

    ds_dlist_remove(&lnx_netlink_list, self);
    self->nl_active = false;

    return true;
}

void lnx_netlink_set_events(lnx_netlink_t *self, uint64_t events)
{
    self->nl_events = events;
}

void lnx_netlink_set_ifname(lnx_netlink_t *self, const char *ifname)
{
    STRSCPY(self->nl_ifname, ifname);
}

/*
 * Global initialization
 */
bool lnx_netlink_global_init(void)
{
    /* Run once guard */
    static bool once = false;

    if (once) return true;
    once = true;

    /* Initialize the debouncer */
    ev_debounce_init2(
            &lnx_netlink_dispatch_ev,
            lnx_netlink_dispatch_fn,
            (double)CONFIG_OSN_NETLINK_DEBOUNCE_MS / 1000.0,
            (double)CONFIG_OSN_NETLINK_DEBOUNCE_MS / 200.0); /* Max timeout is 5 times default */

     if (!lnx_netlink_sock_open())
     {
         lnx_netlink_dispatch(LNX_NETLINK_ALL, NULL);
         return false;
     }

     return true;
}

/*
 * Initialize netlink socket and start listening to netlink events
 *
 * Register an I/O watcher for the socket read events.
 */
#define RTNLGRP(x)  ((RTNLGRP_ ## x) > 0 ? 1 << ((RTNLGRP_ ## x) - 1) : 0)

bool lnx_netlink_sock_open(void)
{
     struct sockaddr_nl nladdr;

     if (lnx_netlink_sock >= 0) return true;

     /* Create the netlink socket in the NETLINK_ROUTE domain */
     lnx_netlink_sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
     if (lnx_netlink_sock < 0)
     {
         LOGE("netlink: Error creating NETLINK socket.");
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
             RTNLGRP(NEIGH) |           /* IPv4/6 neighbor reports  */
             RTNLGRP(IPV4_IFADDR) |     /* IPv4 address status */
             RTNLGRP(IPV6_IFADDR) |     /* IPv6 address status */
             RTNLGRP(IPV4_ROUTE) |      /* IPv4 route */
             RTNLGRP(IPV6_ROUTE) |      /* IPv6 route */
             RTNLGRP(IPV4_NETCONF) |    /* Unknown? */
             RTNLGRP(IPV6_NETCONF);     /* Unknown? */

     if (bind(lnx_netlink_sock, (struct sockaddr *)&nladdr, sizeof(nladdr)) != 0)
     {
         LOGE("netlink: Error binding NETLINK socket");
         goto error;
     }

     /*
      * Initialize an start an I/O watcher
      */
     ev_io_init(&lnx_netlink_sock_ev, lnx_netlink_sock_fn, lnx_netlink_sock, EV_READ);
     ev_io_start(EV_DEFAULT, &lnx_netlink_sock_ev);

     LOG(NOTICE, "netlink: NETLINK socket successfully created.");

     return true;

 error:
     if (lnx_netlink_sock >= 0)
     {
         close(lnx_netlink_sock);
         lnx_netlink_sock = -1;
     }

     return false;
}

void lnx_netlink_sock_close(void)
{
    if (lnx_netlink_sock < 0) return;

    /* Stop listening to I/O events */
    ev_io_stop(EV_DEFAULT, &lnx_netlink_sock_ev);

    /* Close the socket */
    if (close(lnx_netlink_sock) != 0)
    {
        LOG(ERR, "netlink: Error closing NETLINK socket.");
    }

    LOG(NOTICE, "netlink: NETLINK socket closed.");

    lnx_netlink_sock = -1;
}

void lnx_netlink_sock_fn(struct ev_loop *loop, ev_io *w, int revent)
{
    char ifname[IF_NAMESIZE];
    struct nlmsghdr *nl_msg;
    char *pifname;
    size_t nl_len;
    ssize_t rc;

    /* Use VLA to allocate temporary buffer space */
    uint8_t buf[getpagesize()];

    (void)loop;
    (void)w;

    if (revent & EV_ERROR)
    {
        LOG(EMERG, "netlink: Error on netlink socket.");
        goto error;
    }

    if (!(revent & EV_READ)) return;

    /* Read the data from the socket and place it into buf */
    rc = recv(lnx_netlink_sock, buf, sizeof(buf), 0);
    if (rc < 0)
    {
        LOG(ERR, "netlink: Received error from netlink socket: %s (%d)", strerror(errno), errno);
        goto error;
    }

    /*
     * Parse the RTNETLINK message
     */
    for (nl_msg = (void *)buf, nl_len = (size_t)rc;
            NLMSG_OK(nl_msg, nl_len);
            nl_msg = NLMSG_NEXT(nl_msg, nl_len))
    {
        /* Filter certain type of netlink messages as they cause too much unnecessary updates */
        if (lnx_netlink_weed_out(nl_msg))
        {
            continue;
        }

        switch (nl_msg->nlmsg_type)
        {
            case RTM_NEWLINK:
            case RTM_DELLINK:
            {
                struct ifinfomsg *ifm = NLMSG_DATA(nl_msg);

                pifname = if_indextoname(ifm->ifi_index, ifname);
                if (pifname == NULL)
                {
                    LOG(DEBUG, "netlink: Unable to resolve interface index %d (RTM_NEWLINK or RTM_DELLINK).",
                            ifm->ifi_index);
                }

                LOG(DEBUG, "netlink: LNX_NETLINK_LINK event on interface: %s", pifname == NULL ? "(null)" : pifname);
                lnx_netlink_dispatch(LNX_NETLINK_LINK, pifname);
                break;
            }

            case RTM_NEWADDR:
            case RTM_DELADDR:
            {
                struct ifaddrmsg *ifa = NLMSG_DATA(nl_msg);

                pifname = if_indextoname(ifa->ifa_index, ifname);
                if (pifname == NULL)
                {
                    LOG(DEBUG, "netlink: Unable to resolve interface index %d (RTM_NEWADDR or RTM_DELADDR).",
                            ifa->ifa_index);
                }

                /* Extract the type */
                switch (ifa->ifa_family)
                {
                    case AF_INET:
                        LOG(DEBUG, "netlink: LNX_NETLINK_IP4ADDR event on interface: %s",
                                pifname == NULL ? "(null)" : pifname);
                        lnx_netlink_dispatch(LNX_NETLINK_IP4ADDR, pifname);
                        break;

                    case AF_INET6:
                        LOG(DEBUG, "netlink: LNX_NETLINK_IP6ADDR event on interface: %s",
                                pifname == NULL ? "(null)" : pifname);
                        lnx_netlink_dispatch(LNX_NETLINK_IP6ADDR, pifname);
                        break;


                    default:
                        LOG(DEBUG, "netlink: LNX_NETLINK_IP?ADDR (unknown family) event on interface: %s ",
                                pifname == NULL ? "(null)" : pifname);
                        /* Send event for both IPv4 and IPv6 */
                        lnx_netlink_dispatch(LNX_NETLINK_IP4ADDR | LNX_NETLINK_IP6ADDR, pifname);
                        break;
                }

                break;
            }

            case RTM_NEWNEIGH:
            case RTM_DELNEIGH:
            {
                struct ndmsg *ndm = NLMSG_DATA(nl_msg);

                pifname = if_indextoname(ndm->ndm_ifindex, ifname);
                if (pifname == NULL)
                {
                    LOG(DEBUG, "netlink: Unable to resolve interface index %d (RTM_NEWNEIGH or RTM_DELNEIGH).",
                            ndm->ndm_ifindex);
                }

                /* Extract the type */
                switch (ndm->ndm_family)
                {
                    case AF_INET:
                        LOG(DEBUG, "netlink: LNX_NETLINK_IP4NEIGH event on interface: %s",
                                pifname == NULL ? "(null)" : pifname);
                        lnx_netlink_dispatch(LNX_NETLINK_IP4NEIGH, pifname);
                        break;

                    case AF_INET6:
                        LOG(DEBUG, "netlink: LNX_NETLINK_IP6NEIGH event on interface: %s",
                                pifname == NULL ? "(null)" : pifname);
                        lnx_netlink_dispatch(LNX_NETLINK_IP6NEIGH, pifname);
                        break;


                    default:
                        LOG(DEBUG, "netlink: LNX_NETLINK_IP?NEIGH (unknown family) event on interface: %s ",
                                pifname == NULL ? "(null)" : pifname);
                        /* Send event for both IPv4 and IPv6 */
                        lnx_netlink_dispatch(LNX_NETLINK_IP4NEIGH | LNX_NETLINK_IP6NEIGH, pifname);
                        break;
                }

                break;
            }

            case RTM_NEWROUTE:
            case RTM_DELROUTE:
            {
                struct rtmsg *rtm = NLMSG_DATA(nl_msg);

                switch (rtm->rtm_family)
                {
                    case AF_INET:
                        LOG(DEBUG, "netlink: LNX_NETLINK_IP4ROUTE event (global).");
                        lnx_netlink_dispatch(LNX_NETLINK_IP4ROUTE, NULL);
                        break;

                    case AF_INET6:
                        LOG(DEBUG, "netlink: LNX_NETLINK_IP6ROUTE event (global).");
                        lnx_netlink_dispatch(LNX_NETLINK_IP6ROUTE, NULL);
                        break;


                    default:
                        LOG(DEBUG, "netlink: LXN_NETLINK_IP?ROUTE (unknown family) event (global).");
                        /* Send event for both IPv4 and IPv6 */
                        lnx_netlink_dispatch(LNX_NETLINK_IP4ROUTE | LNX_NETLINK_IP6ROUTE, NULL);
                        break;
                }

                break;
            }

            default:
                LOG(NOTICE, "netlink: Unknown message type %d.", nl_msg->nlmsg_type);
                break;
        }
    }

    return;

error:
    /* Error processing this event -- dispatch a global update */
    lnx_netlink_sock_close();
    lnx_netlink_dispatch(LNX_NETLINK_ALL, NULL);
}

bool lnx_netlink_weed_out(struct nlmsghdr *nh)
{
    struct rtmsg *rm;
    struct rtattr *rta;
    unsigned int rtalen;

    rm = NLMSG_DATA(nh);

    switch (nh->nlmsg_type)
    {
        case RTM_NEWLINK:
        case RTM_DELLINK:
        case RTM_GETLINK:
            rta = IFLA_RTA(rm);
            rtalen = RTM_PAYLOAD(nh);

            for (;RTA_OK(rta, rtalen); rta = RTA_NEXT(rta, rtalen))
            {
                /*
                 * IFLA_WIRELESS can became very noisy under certain conditions
                 * (WPS enabled) -- ignore them as they can cause too much
                 * updates.
                 */
                if (rta->rta_type == IFLA_WIRELESS)
                {
                    return true;
                }
            }
    }

    return false;
}

bool lnx_netlink_dispatch(uint64_t events, const char *ifname)
{
    lnx_netlink_t *nl;

    /* Traverse list of registered listeners and do a delayed dispatch */
    ds_dlist_foreach(&lnx_netlink_list, nl)
    {
        if ((nl->nl_events & events) == 0) continue;

        /* If ifname is NULL or empty, disregard the nl->nl_ifname filter */
        if (ifname != NULL &&
                ifname[0] != '\0' &&
                nl->nl_ifname[0] != '\0')
        {
            if (strcmp(ifname, nl->nl_ifname) != 0) continue;
        }

        nl->nl_pending |= nl->nl_events & events;
    }

    /* Start debouncing timer -- see nl_dispatch_fn()*/
    ev_debounce_start(EV_DEFAULT, &lnx_netlink_dispatch_ev);

    return true;
}

/**
 * Debounce timer callback -- actually dispatch the messages
 */
void lnx_netlink_dispatch_fn(struct ev_loop *loop, ev_debounce *ev, int revent)
{
    (void)loop;
    (void)ev;
    (void)revent;

    lnx_netlink_t *nl;

    /* Traverse list of registered listeners and do a delayed dispatch */
    ds_dlist_foreach(&lnx_netlink_list, nl)
    {
        if (nl->nl_pending == 0) continue;
        /* Invoke the callback */
        nl->nl_fn(nl, nl->nl_pending, nl->nl_ifname[0] == '\0' ? NULL : nl->nl_ifname);
        nl->nl_pending = 0;
    }

    /*
     * NETLINK sockets may fail when the system is under stress. In such cases
     * revert back to a polling method. Each polling interval we will retry
     * opening the NETLINK socket until it succeeds.
     */
    if (!lnx_netlink_sock_open())
    {
        /* If we cannot acquire a netlink socket, revert to polling */
        lnx_netlink_dispatch(LNX_NETLINK_ALL, NULL);
    }

    return;
}
