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

#include <libmnl/libmnl.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <netdb.h>

#include "log.h"
#include "neigh_table.h"

static int util_data_attr_cb(const struct nlattr *attr, void *data)
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

static int util_addr_compare(void *ipaddr, int ipaddr_len, struct sockaddr_storage *saddr)
{
    struct sockaddr_in      *in4;
    struct sockaddr_in6     *in6;

    if (ipaddr_len == 16)
    {
        in6 = (struct sockaddr_in6 *)saddr;
        return memcmp(ipaddr, &in6->sin6_addr, ipaddr_len);
    } else  if (ipaddr_len == 4) {
        in4 = (struct sockaddr_in *)saddr;
        return memcmp(ipaddr, &in4->sin_addr, ipaddr_len);
    }
    return -1;
}

static int util_data_cb(const struct nlmsghdr *nlh, void *data)
{
    struct nlattr          *tb[IFA_MAX + 1] = {};
    struct ndmsg           *ndm             = mnl_nlmsg_get_payload(nlh);
    void                   *ipaddr          = NULL;
    void                   *macaddr         = NULL;
    int                    ipaddr_len       = 0;
    struct neighbour_entry *req             = data;
    char                    ipstr[INET6_ADDRSTRLEN] = {0};

    if (mnl_attr_parse(nlh, sizeof(*ndm), util_data_attr_cb, tb) == MNL_CB_ERROR)
    {
        return MNL_CB_ERROR;
    }

    if (!tb[NDA_DST])
    {
        return MNL_CB_OK;
    }

    ipaddr_len = mnl_attr_get_payload_len(tb[NDA_DST]);

    ipaddr = mnl_attr_get_payload(tb[NDA_DST]);

    LOGD("%s: Kernel entry ip [%s] && len[%d]",
          __func__, inet_ntop((ipaddr_len == 4 ? AF_INET : AF_INET6),
                     ipaddr, ipstr, sizeof(ipstr)), ipaddr_len);
    memset(ipstr, 0, sizeof(ipstr));
    getnameinfo((struct sockaddr *)req->ipaddr,
                sizeof(struct sockaddr_storage),
                ipstr, sizeof(ipstr),
                0, 0, NI_NUMERICHOST);

    if (util_addr_compare(ipaddr, ipaddr_len, req->ipaddr))
    {
        return MNL_CB_OK;
    }

    mnl_attr_parse(nlh, sizeof(*ndm), util_data_attr_cb, tb);

    switch (ndm->ndm_state)
    {
        case NUD_PERMANENT:
        case NUD_REACHABLE:
        case NUD_STALE:
            break;
        case NUD_INCOMPLETE:
        case NUD_DELAY:
        case NUD_PROBE:
        case NUD_FAILED:
        case NUD_NOARP:
        default:
            return MNL_CB_OK;
    }

    if (tb[NDA_LLADDR])
    {
        macaddr = mnl_attr_get_payload(tb[NDA_LLADDR]);

        memcpy(req->mac, macaddr, sizeof(os_macaddr_t));
        LOGD("%s: Found the mac in the kernel for ip[%s].",__func__, ipstr);
    }
    return MNL_CB_STOP;
}

bool nf_util_get_macaddr(struct neighbour_entry *req)
{
    struct mnl_socket *nl;
    char buf[MNL_SOCKET_BUFFER_SIZE];
    struct nlmsghdr *nlh;
    struct rtgenmsg *rt;
    int ret;
    unsigned int seq, portid;
    bool rc = false;
    os_macaddr_t zeromac;
    struct sockaddr *ss = (struct sockaddr *)req->ipaddr;

    nlh = mnl_nlmsg_put_header(buf);
    nlh->nlmsg_type = RTM_GETNEIGH;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh->nlmsg_seq = seq = time(NULL);

    rt = mnl_nlmsg_put_extra_header(nlh, sizeof(struct rtgenmsg));
    rt->rtgen_family = ss->sa_family;


    memset(&zeromac, 0, sizeof(os_macaddr_t));
    memset(req->mac, 0, sizeof(os_macaddr_t));
    nl = mnl_socket_open(NETLINK_ROUTE);
    if (nl == NULL)
    {
        LOGE("nf_util: Failed to open mnl socket.");
        goto error;
    }

    if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0)
    {
        LOGE("nf_util: Failed to bind mnl socket.");
        goto error;
    }

    portid = mnl_socket_get_portid(nl);

    if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0)
    {
        LOGE("nf_util: Failed to send request on mnl socket.");
        goto error;
    }

    while ((ret = mnl_socket_recvfrom(nl, buf, sizeof(buf))) > 0)
    {
        ret = mnl_cb_run(buf, ret, seq, portid, util_data_cb, req);
        if (ret <= MNL_CB_STOP)
            break;
    }

    if (memcmp(req->mac, &zeromac, sizeof(os_macaddr_t)))
    {
        rc = true;
    }
error:
    if (nl) mnl_socket_close(nl);
    return rc;
}
