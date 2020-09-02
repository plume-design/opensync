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

/* This example is placed in the public domain. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <libmnl/libmnl.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <netdb.h>

#include "log.h"
#include "neigh_table.h"
#include "nf_utils.h"
#include "os_types.h"

static struct nf_neigh_context
{
    struct ev_loop *loop;
    int source;
    struct ev_io neigh_io_mnl;
    struct mnl_socket *neigh_mnl;
    process_nl_event_cb neigh_cb;
    int neigh_fd;
    struct ev_io link_io_mnl;
    struct mnl_socket *link_mnl;
    process_nl_event_cb link_cb;
    int link_fd;
} nf_neigh_context;


static struct nf_neigh_context *
nf_neigh_get_context(void)
{
    return &nf_neigh_context;
}


static const struct ndm_state
{
    int state;
    char *str_state;
} tbl_ndm_state[] =
{
    {
        .state = NUD_PERMANENT,
        .str_state = "NUD_PERMANENT",
    },
    {
        .state = NUD_REACHABLE,
        .str_state = "NUD_REACHABLE",
    },
    {
        .state = NUD_STALE,
        .str_state = "NUD_STALE",
    },
    {
        .state = NUD_INCOMPLETE,
        .str_state = "NUD_INCOMPLETE",
    },
    {
        .state = NUD_INCOMPLETE,
        .str_state = "NUD_INCOMPLETE",
    },
    {
        .state = NUD_DELAY,
        .str_state = "NUD_DELAY",
    },
    {
        .state = NUD_PROBE,
        .str_state = "NUD_PROBE",
    },
    {
        .state = NUD_FAILED,
        .str_state = "NUD_FAILED",
    },
    {
        .state = NUD_NOARP,
        .str_state = "NUD_NOARP",
    },
};

const char *nf_util_get_str_state(int state)
{
    const char *unknown_state = "unknown state";
    const struct ndm_state *map;
    size_t nelems;
    size_t i;

    nelems = (sizeof(tbl_ndm_state) / sizeof(tbl_ndm_state[0]));
    map = tbl_ndm_state;
    for (i = 0; i < nelems; i++)
    {
        if (state == map->state) return map->str_state;

        map++;
    }

    return unknown_state;
}


static int
util_data_attr_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);

    /* skip unsupported attribute in user-space */
    if (mnl_attr_type_valid(attr, IFA_MAX) < 0)
        return MNL_CB_OK;

    switch(type) {
    case NDA_DST:
        if (mnl_attr_validate(attr, MNL_TYPE_BINARY) < 0) {
            perror("mnl_attr_validate");
            return MNL_CB_ERROR;
        }
        break;
    case NDA_LLADDR:
        if (mnl_attr_validate(attr, MNL_TYPE_BINARY) < 0) {
            perror("mnl_attr_validate");
            return MNL_CB_ERROR;
        }
        break;
    }
    tb[type] = attr;
    return MNL_CB_OK;
}


/**
 * @brief mnl callback processing a netlink neighbor message
 */
static int
util_neigh_cb(const struct nlmsghdr *nlh, void *data)
{
    char ipstr[INET6_ADDRSTRLEN] = { 0 };
    struct nlattr *tb[IFA_MAX + 1] = {};
    struct nf_neigh_info neigh_info;
    struct nf_neigh_context *ctxt;
    struct ndmsg *ndm;
    bool add_entry;
    bool del_entry;
    int af_family;
    int ifindex;
    void *macaddr;
    void *ipaddr;
    int mnl_ret;
    int ret;

    ctxt = nf_neigh_get_context();
    mnl_ret = MNL_CB_OK;

    add_entry = false;
    del_entry = false;

    ndm = mnl_nlmsg_get_payload(nlh);
    if (ndm == NULL) return MNL_CB_OK;

    ret = mnl_attr_parse(nlh, sizeof(*ndm), util_data_attr_cb, tb);
    if (ret == MNL_CB_ERROR) return MNL_CB_ERROR;

    if (tb[NDA_DST] == NULL) return mnl_ret;

    ipaddr = mnl_attr_get_payload(tb[NDA_DST]);

    af_family = ndm->ndm_family;
    inet_ntop(af_family, ipaddr, ipstr, sizeof(ipstr));
    ifindex = ndm->ndm_ifindex;

    switch (ndm->ndm_state)
    {
        case NUD_PERMANENT:
        case NUD_REACHABLE:
        case NUD_STALE:
            add_entry = true;
            break;
        case NUD_INCOMPLETE:
        case NUD_DELAY:
        case NUD_PROBE:
        case NUD_FAILED:
        case NUD_NOARP:
            del_entry = true;
            break;
        default:
            return mnl_ret;
    }

    macaddr = NULL;
    if (tb[NDA_LLADDR] != NULL)
    {
        macaddr = mnl_attr_get_payload(tb[NDA_LLADDR]);
    }

    memset(&neigh_info, 0, sizeof(neigh_info));
    neigh_info.event = NF_UTIL_NEIGH_EVENT;
    neigh_info.af_family = af_family;
    neigh_info.ifindex = ifindex;
    neigh_info.ipaddr = ipaddr;
    neigh_info.hwaddr = (os_macaddr_t *)macaddr;
    neigh_info.source = ctxt->source;
    neigh_info.state = ndm->ndm_state;
    neigh_info.add = add_entry;
    neigh_info.delete = del_entry;
    if (ctxt->neigh_cb) ctxt->neigh_cb(&neigh_info);

    return mnl_ret;
}


/**
 * @brief dump the neighbors table for the given inet family
 *
 * @param af_family the inet family
 */
bool
nf_util_dump_neighs(int af_family)
{
    char buf[MNL_SOCKET_BUFFER_SIZE];
    struct mnl_socket *nl;
    struct nlmsghdr *nlh;
    struct rtgenmsg *rt;
    unsigned int portid;
    unsigned int seq;
    bool rc;
    int ret;

    nlh = mnl_nlmsg_put_header(buf);
    nlh->nlmsg_type = RTM_GETNEIGH;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh->nlmsg_seq = seq = time(NULL) + 1;

    rt = mnl_nlmsg_put_extra_header(nlh, sizeof(struct rtgenmsg));
    rt->rtgen_family = af_family;

    rc = false;

    nl = mnl_socket_open(NETLINK_ROUTE);
    if (nl == NULL)
    {
        LOGE("%s: mnl_socket_open failed: %s", __func__,
             strerror(errno));
        goto error;
    }

    if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0)
    {
        LOGE("%s: Failed to bind mnl socket: %s", __func__,
             strerror(errno));
        goto error;
    }

    portid = mnl_socket_get_portid(nl);

    if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0)
    {
        LOGE("%s: mnl_socket_sendto failed: %s", __func__,
             strerror(errno));
        goto error;
    }

    while ((ret = mnl_socket_recvfrom(nl, buf, sizeof(buf))) > 0)
    {
        ret = mnl_cb_run(buf, ret, seq, portid, util_neigh_cb, NULL);
        if (ret <= MNL_CB_STOP)
            break;
    }

    rc = true;

error:
    if (nl) mnl_socket_close(nl);
    return rc;
}


/**
 * @brief ev callback to neighbor events
 */
static void
read_mnl_neigh_cb(struct ev_loop *loop, struct ev_io *watcher,
                  int revents)
{
    char rcv_buf[MNL_SOCKET_BUFFER_SIZE];
    struct nf_neigh_context *ctxt;
    int portid;
    int ret;

    if (EV_ERROR & revents)
    {
        LOGE("%s: Invalid mnl socket event", __func__);
        return;
    }

    ctxt = nf_neigh_get_context();
    ret = mnl_socket_recvfrom(ctxt->neigh_mnl, rcv_buf, sizeof(rcv_buf));
    if (ret == -1)
    {
        LOGE("%s: mnl_socket_recvfrom failed: %s\n", __func__,
             strerror(errno));
        return;
    }

    portid = mnl_socket_get_portid(ctxt->neigh_mnl);
    ret = mnl_cb_run(rcv_buf, ret, 0, portid, util_neigh_cb, NULL);

    if (ret == -1) LOGE("%s: mnl_cb_run failed", __func__);
}


/**
 * @brief mnl callback processing a netlink link message
 */
static int
util_link_cb(const struct nlmsghdr *nlh, void *data)
{
    struct nf_neigh_info neigh_info;
    struct nf_neigh_context *ctxt;
    struct ifinfomsg *msg;
    unsigned change;
    char ifname[32];
    bool created;
    int ifindex;
    int msgtype;
    char *ifn;
    bool act;

    ctxt = nf_neigh_get_context();

    msg = mnl_nlmsg_get_payload(nlh);
    if (msg == NULL) return MNL_CB_OK;

    ifindex = msg->ifi_index;
    msgtype = nlh->nlmsg_type;
    ifn = if_indextoname(ifindex, ifname);

    /* Check if the event is an interface creation */
    change = msg->ifi_change;
    created = (msgtype == RTM_NEWLINK);
    created &= (change == (unsigned)(~0UL));
    if (created) return MNL_CB_STOP;

    act = false;
    /* Check if the interface went down */
    if ((msg->ifi_change & IFF_UP) && !(msg->ifi_flags & IFF_UP))
    {
        LOGI("%s: ifindex: %d, ifname: %s went down", __func__,
             ifindex, ifn ? ifn : "None");
        act = true;
    }

    /* Check if the interface was deleted */
    if (msgtype == RTM_DELLINK)
    {
        LOGI("%s: interface ifindex %d, ifname: %s deleted", __func__,
             ifindex, ifn ? ifn : "None");
        act |= true;
    }

    if (!act) return MNL_CB_STOP;

    memset(&neigh_info, 0, sizeof(neigh_info));
    neigh_info.event = NF_UTIL_LINK_EVENT;
    neigh_info.ifindex = ifindex;
    if (ctxt->link_cb) ctxt->link_cb(&neigh_info);

    return MNL_CB_STOP;
}


/**
 * @brief ev callback to link layer events
 */
static void
read_mnl_link_cb(struct ev_loop *loop, struct ev_io *watcher,
                int revents)
{
    char rcv_buf[MNL_SOCKET_BUFFER_SIZE];
    struct nf_neigh_context *ctxt;
    int portid;
    int ret;

    if (EV_ERROR & revents)
    {
        LOGE("%s: Invalid mnl socket event", __func__);
        return;
    }

    ctxt = nf_neigh_get_context();
    ret = mnl_socket_recvfrom(ctxt->link_mnl, rcv_buf, sizeof(rcv_buf));
    if (ret == -1)
    {
        LOGE("%s: mnl_socket_recvfrom failed: %s\n", __func__,
             strerror(errno));
        return;
    }

    portid = mnl_socket_get_portid(ctxt->link_mnl);
    ret = mnl_cb_run(rcv_buf, ret, 0, portid, util_link_cb, NULL);

    if (ret == -1) LOGE("%s: mnl_cb_run failed\n", __func__);
}


/**
 * @brief ev subscription to link layer events
 */
int
nf_link_event_init(void)
{
    struct nf_neigh_context *ctxt;
    struct mnl_socket *nl;
    int group;
    int ret;

    ctxt = nf_neigh_get_context();
    if (ctxt->neigh_cb == NULL) return 0;

    nl = mnl_socket_open(NETLINK_ROUTE);
    if (nl == NULL)
    {
        LOGE("%s: mnl_socket_open failed: %s", __func__,
             strerror(errno));
        return -1;
    }

    ret = mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID);
    if (ret < 0)
    {
        LOGE("%s: mnl_socket_bind failed: %s", __func__,
             strerror(errno));
        return -1;
    }

    group = RTNLGRP_LINK;
    ret = mnl_socket_setsockopt(nl, NETLINK_ADD_MEMBERSHIP, &group,
                                sizeof(int));
    if (ret < 0)
    {
        LOGE("%s: mnl_socket_setsockopt failed: %s", __func__,
             strerror(errno));
        return -1;
    }

    ctxt->link_mnl = nl;
    ctxt->link_fd = mnl_socket_get_fd(nl);
    ev_io_init(&ctxt->link_io_mnl, read_mnl_link_cb,
               ctxt->link_fd, EV_READ);
    ev_io_start(ctxt->loop, &ctxt->link_io_mnl);

    return 0;
}


int
nf_neigh_event_init(void)
{
    struct nf_neigh_context *ctxt;
    struct mnl_socket *nl;
    int group;
    int ret;

    ctxt = nf_neigh_get_context();

    if (ctxt->neigh_cb == NULL) return 0;

    nl = mnl_socket_open(NETLINK_ROUTE);
    if (nl == NULL)
    {
        LOGE("%s: mnl_socket_open failed: %s", __func__,
             strerror(errno));
        return -1;
    }

    ret = mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID);
    if (ret < 0)
    {
        LOGE("%s: mnl_socket_bind failed: %s", __func__,
             strerror(errno));
        return -1;
    }

    group = RTNLGRP_NEIGH;
    ret = mnl_socket_setsockopt(nl, NETLINK_ADD_MEMBERSHIP, &group,
                                sizeof(int));
    if (ret < 0)
    {
        LOGE("%s: mnl_socket_setsockopt failed: %s", __func__,
             strerror(errno));
        return -1;
    }

    ctxt->neigh_mnl = nl;
    ctxt->neigh_fd = mnl_socket_get_fd(nl);
    ev_io_init(&ctxt->neigh_io_mnl, read_mnl_neigh_cb,
               ctxt->neigh_fd, EV_READ);
    ev_io_start(ctxt->loop, &ctxt->neigh_io_mnl);
    LOGI("%s: nf_neigh_context initialized", __func__);

    return 0;
}


int
nf_neigh_init(struct nf_neigh_settings *neigh_settings)
{
    struct nf_neigh_context *ctxt;
    int ret;

    ctxt = nf_neigh_get_context();
    memset(ctxt, 0, sizeof(*ctxt));
    ctxt->loop = neigh_settings->loop;
    ctxt->neigh_cb = neigh_settings->neigh_cb;
    ctxt->link_cb = neigh_settings->link_cb;
    ctxt->source = neigh_settings->source;

    ret = nf_link_event_init();
    if (ret)
    {
        LOGE("%s: link event monitor init failure", __func__);
        return -1;
    }

    ret = nf_neigh_event_init();
    if (ret)
    {
        LOGE("%s: neighbor event monitor init failure", __func__);
        return -1;
    }

    nf_util_dump_neighs(AF_INET);
    nf_util_dump_neighs(AF_INET6);

    return 0;
}


int nf_neigh_exit(void)
{
    struct nf_neigh_context *ctxt;

    ctxt = nf_neigh_get_context();

    mnl_socket_close(ctxt->neigh_mnl);
    mnl_socket_close(ctxt->link_mnl);

    return 0;
}
