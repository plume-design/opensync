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

#include <asm/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/netfilter/nfnetlink_log.h>

#include <errno.h>
#include <unistd.h>
#include <endian.h>
#include <ev.h>

#include "log.h"
#include "memutil.h"
#include "osn_nflog.h"
#include "osn_types.h"
#include "util.h"

struct osn_nflog
{
    int             nf_nflog_group;             /* Nflog group */
    int             nf_sock;                    /* Netlink socket */
    ev_io           nf_sock_ev;                 /* Netlink socket watcher */
    osn_nflog_fn_t *nf_fn;                      /* Nflog callback */
};

/*
 * Structure for easier handling of netlink messages.
 */
struct nlbuf
{
    void       *nb_buf;             /* Data buffer */
    size_t      nb_bufsz;           /* Total number of bytes available in buffer */
    void       *nb_cur;             /* Current buffer position */
};

/* Initializer for a struct nlbuf */
#define NLBUF_INIT(buf, sz) (struct nlbuf) { .nb_buf = (buf), .nb_bufsz = (sz), .nb_cur = (buf) }

static bool osn_nflog_open(osn_nflog_t *self);
static bool osn_nflog_close(osn_nflog_t *self);
static bool osn_nflog_subscribe(osn_nflog_t *self);
static bool osn_nflog_subscribe_send(int sock, int group);
static int osn_nflog_err_recv(int sock);
static int osn_nflog_packet_recv(int sock, struct osn_nflog_packet *np);
static void osn_nflog_packet_fini(struct osn_nflog_packet *np);
static void osn_nflog_sock_fn(struct ev_loop *loop, ev_io *w, int revent);
static void *nlbuf_get(struct nlbuf *nlb, size_t sz);
static size_t nlbuf_sz(struct nlbuf *nlb);

/*
 * ===========================================================================
 *  Public API implementation
 * ===========================================================================
 */
osn_nflog_t *osn_nflog_new(int nl_group, osn_nflog_fn_t *fn)
{
    osn_nflog_t *self = calloc(1, sizeof(*self));

    self->nf_sock = -1;
    self->nf_nflog_group = nl_group;
    self->nf_fn = fn;

    return self;
}

/*
 * Start processing NFLOG messages:
 *  - create the netlink socket
 *  - registser to the NFLOG events
 *  - start libev watchers on the socket file descriptor
 */
bool osn_nflog_start(osn_nflog_t *self)
{
    if (self->nf_sock>= 0) return true;

    if (!osn_nflog_open(self))
    {
        return false;
    }

    if (!osn_nflog_subscribe(self))
    {
        osn_nflog_close(self);
        return false;
    }

    ev_io_init(&self->nf_sock_ev, osn_nflog_sock_fn, self->nf_sock, EV_READ);
    ev_io_start(EV_DEFAULT, &self->nf_sock_ev);

    LOG(NOTICE, "osn_nflog: Starting NFLOG monitoring for group %d.", self->nf_nflog_group);

    return true;
}

void osn_nflog_stop(osn_nflog_t *self)
{
    if (self->nf_sock < 0) return;

    osn_nflog_close(self);

    LOG(NOTICE, "osn_nflog: NFLOG monitoring for group %d stopped.", self->nf_nflog_group);
}

void osn_nflog_del(osn_nflog_t *self)
{
    osn_nflog_stop(self);
    FREE(self);
}

/*
 * ===========================================================================
 *  NFLOG netlink protocol and related functions
 * ===========================================================================
 */

/*
 * Open the netlink socket and register to NFLOG events
 */
bool osn_nflog_open(osn_nflog_t *self)
{
    struct sockaddr_nl nladdr;

    if (self->nf_sock > 0) return true;

    /*
     * Open a socket in the netfilter domain and register to NFLOG events
     */
    self->nf_sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER);
    if (self->nf_sock < 0)
    {
        LOG(ERR, "osn_nflog: Error opening NETLINK_NETFILTER socket: %s",
                strerror(errno));
        goto error;
    }

    /* Bind the socket */
    memset(&nladdr, 0, sizeof(nladdr));
    nladdr.nl_family = AF_NETLINK;
    nladdr.nl_pid = 0;
    nladdr.nl_groups = 0;
    if (bind(self->nf_sock, (struct sockaddr *)&nladdr, sizeof(nladdr)) != 0)
    {
        LOG(ERR, "osn_nflog: Error binding NETLINK_NETFILTER socket: %s",
                strerror(errno));
        goto error;
    }

    return true;

error:
    osn_nflog_close(self);
    return false;
}

/*
 * Close the netlink socket and stop the libev watchers
 */
bool osn_nflog_close(osn_nflog_t *self)
{
    if (self->nf_sock < 0) return true;

    ev_io_stop(EV_DEFAULT, &self->nf_sock_ev);

    close(self->nf_sock);
    self->nf_sock = -1;

    return true;
}

/*
 * Subscribe to NFLOG events
 */
bool osn_nflog_subscribe(osn_nflog_t *self)
{
    int err;

    if (!osn_nflog_subscribe_send(self->nf_sock, self->nf_nflog_group))
    {
        return false;
    }

    err = osn_nflog_err_recv(self->nf_sock);
    if (err != 0)
    {
        LOG(ERR, "osn_nflog: Subscribe response error: %s", strerror(err));
        return false;
    }

    return true;
}

/*
 * ev_io watcher callback, process NFLOG messages here
 */
void osn_nflog_sock_fn(struct ev_loop *loop, ev_io *w, int revent)
{
    (void)loop;
    (void)revent;

    int rc;

    osn_nflog_t *self = CONTAINER_OF(w, osn_nflog_t, nf_sock_ev);
    struct osn_nflog_packet np = OSN_NFLOG_PACKET_INIT;

    rc = osn_nflog_packet_recv(self->nf_sock, &np);
    if (rc > 0)
    {
        /* Packet received, call the status function */
        self->nf_fn(self, &np);
    }
    if (rc < 0)
    {
        LOG(ERR, "osn_nflog: Error receivng NFLOG packet.");
        osn_nflog_close(self);
    }

    osn_nflog_packet_fini(&np);
}

/*
 * ===========================================================================
 *  Low level NETLINK protocol functions
 * ===========================================================================
 */
/*
 * Send a command to the NETLINK_NETFILTER subsystem to subscribe to NFLOG
 * events
 *
 * The group parameter specifies the NFLOG group (the --nflog-group iptables
 * parameter).
 */
bool osn_nflog_subscribe_send(int sock, int group)
{
    struct sockaddr_nl nladdr;
    uint8_t msgbuf[256];
    struct nlbuf nb;
    int rc;

    /* Initialize the netlink message buffer */
    nb = NLBUF_INIT(msgbuf, sizeof(msgbuf));

    /*
     * The structure of a NFLOG subscribe message is as follows:
     *
     *      +-----------------------> nlmsghdr <---------------------+
     *      |.nlmsg_type = (NFNL_SUBSYS_ULOG << 8) | NFUNL_MSG_CONFIG|
     *      +--------------------------------------------------------+
     *      +-----------------------> nlgenmsg <---------------------+
     *      |                  .res_id = nflog_group                 |
     *      +--------------------------------------------------------+
     *      +------------------------> nlattr <----------------------+
     *      |                .nla_type = NFULA_CFG_CMD;              |
     *      +--------------------------------------------------------+
     *      +------------------> nfulnl_msg_config_cmd <-------------+
     *      |             .command = NFULNL_CFG_CMD_BIND;            |
     *      +--------------------------------------------------------+
     */

    /* Craft the netlink message header */
    struct nlmsghdr *nlh = nlbuf_get(&nb, sizeof(*nlh));
    if (nlh == NULL)
    {
        LOG(ERR, "osn_nflog: Error appending nlmsghdr.");
        return false;
    }
    /* Send the message to the ULOG (nflog) subsystem */
    nlh->nlmsg_type = (NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_CONFIG;
    nlh->nlmsg_pid = 0;
    nlh->nlmsg_seq = getpid(); /* Arbitrary number */
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;

    /* Craft the netfilter generic header, this is used to set the nflog group */
    struct nfgenmsg *ng = nlbuf_get(&nb, sizeof(*ng));
    if (ng == NULL)
    {
        LOG(ERR, "osn_nflog: Error appending nfgenmsg.");
        return false;
    }
    ng->nfgen_family = 0;
    ng->version = NFNETLINK_V0;
    ng->res_id = htons(group);

    /* Add a nlattr structure */
    struct nlattr *nla = nlbuf_get(&nb, sizeof(nla));
    if (nla == NULL)
    {
        LOG(ERR, "osn_nflog: Error appending nlattr.");
        return false;
    }
    nla->nla_len = sizeof(*nla) + sizeof(struct nfulnl_msg_config_cmd);
    nla->nla_type = NFULA_CFG_CMD;

    /* Craft the nfulnl_msg_config_cmd command message */
    struct nfulnl_msg_config_cmd *nfcmd = nlbuf_get(&nb, sizeof(*nfcmd));
    if (nfcmd == NULL)
    {
        LOG(ERR, "osn_nflog: Error appending nfulnl_msg_config_cmd.");
        return false;
    }
    nfcmd->command = NFULNL_CFG_CMD_BIND;

    /* Finally, patch the nlmsg header size */
    nlh->nlmsg_len = nlbuf_sz(&nb);

    memset(&nladdr, 0, sizeof(nladdr));
    nladdr.nl_family = AF_NETLINK;

    rc = sendto(sock, msgbuf, NLMSG_ALIGN(nlbuf_sz(&nb)), 0, (struct sockaddr *)&nladdr, sizeof(nladdr));
    if (rc <= 0)
    {
        LOG(ERR, "osn_nflog: Error sending NETLINK NFLOG subscribe message.");
        return false;
    }

    return true;
}

/*
 * Read and parse a nlmsgerr netlink message. Contrary to what the name suggest,
 * the nlmsgerr is used to report back the status and not just errors.
 *
 * This function returns the value of the .error field, which is an errno
 * interpretable code where 0 signifies success.
 *
 * @return
 * Return the error code or <0 if an unrecoverable error ocurred
 */
int osn_nflog_err_recv(int sock)
{
    uint8_t msgbuf[256];
    struct nlbuf nb;
    ssize_t nr;

    nr = recv(sock, msgbuf, sizeof(msgbuf), 0);
    if (nr <= 0)
    {
        LOG(ERR, "osn_nflog: Error reading errmsg response from NETLINK NFLOG socket.");
        return errno;
    }

    nb = NLBUF_INIT(msgbuf, nr);

    struct nlmsghdr *nlh = nlbuf_get(&nb, sizeof(*nlh));
    if (nlh == NULL)
    {
        LOG(ERR, "osn_nflog: errmsg too short (at nlmsghdr).");
        return ENOBUFS;
    }

    if (nlh->nlmsg_type != NLMSG_ERROR)
    {
        LOG(ERR, "osn_nflog: Expected NLMSG_ERROR, but received %ud instead.", nlh->nlmsg_type);
        return EINVAL;
    }

    struct nlmsgerr *nlerr = nlbuf_get(&nb, sizeof(*nlerr));
    if (nlerr == NULL)
    {
        LOG(ERR, "osn_nflog: errmsg too short (at nlmsgerr).");
        return ENOBUFS;
    }

    /* The error code in nlerr is -errno, invert it */
    return -(nlerr->error);
}

void osn_nflog_packet_fini(struct osn_nflog_packet *np)
{
    FREE(np->nfp_payload);
    FREE(np->nfp_hwheader);
    FREE(np->nfp_prefix);
}

/*
 * Receive and process a NFLOG packet
 *
 * @return
 *
 * This function returns the number of packets received (currently max 1) or -1
 * on error. A return code 0 is possible, in which case the content of np should
 * be considered invalid.
 */
int osn_nflog_packet_recv(int sock, struct osn_nflog_packet *np)
{
    uint8_t msgbuf[1024];
    struct nlattr *nla;
    struct nlbuf nb;
    ssize_t nr;

    memset(np, 0, sizeof(*np));

    nr = recv(sock, msgbuf, sizeof(msgbuf), 0);
    if (nr < 0)
    {
        LOG(WARN, "osn_nflog: Error receiving packet: %s", strerror(errno));
        if (errno == ENOBUFS)
        {
            LOG(DEBUG, "osn_flog: Out of buffers.");
            return 0;
        }

        return -1;
    }
    else if (nr == 0)
    {
        LOG(ERR, "osn_nflog: Recevied EOF on socket. Closing.");
        return -1;
    }

    nb = NLBUF_INIT(msgbuf, nr);

    struct nlmsghdr *nlh = nlbuf_get(&nb, sizeof(*nlh));
    if (nlh == NULL)
    {
        LOG(ERR, "osn_nflog: nflog message too short (at nlmsghdr).");
        return 0;
    }

    if (nlh->nlmsg_type != ((NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_PACKET))
    {
        LOG(WARN, "osn_nflog: Invalid packet type received: %d (expected NFULNL_MSG_PACKET - %d).",
                nlh->nlmsg_type, (NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_PACKET);
        return 0;
    }

    /* Use the message size for the buffer length, this is usually smaller than
     * the recv() return value and is most often correct value */
    nb.nb_bufsz = nlh->nlmsg_len;

    struct nfgenmsg *nfg = nlbuf_get(&nb, sizeof(*nfg));
    if (nfg == NULL)
    {
        LOG(ERR, "osn_nflog: nflog message too short (at nfgenmsg).");
        return 0;
    }

    np->nfp_group_id = nfg->res_id;

    /* Parse attributes */
    while ((nla = nlbuf_get(&nb, sizeof(*nla))) != NULL)
    {
        size_t datasz = nla->nla_len - NLA_ALIGN(sizeof(*nla));
        void *data = nlbuf_get(&nb, datasz);

        switch (nla->nla_type)
        {
            case NFULA_PACKET_HDR:
                {
                    struct nfulnl_msg_packet_hdr *mph = data;
                    if (datasz != sizeof(*mph))
                    {
                        LOG(WARN, "osn_nflog: NFULA_PACKET_HDR size mismatch.");
                        break;
                    }

                    np->nfp_hwproto = ntohs(mph->hw_protocol);
                    LOG(DEBUG, "osn_nflog: PACKET_HDR .hw_protocol = %d", np->nfp_hwproto);
                }
                break;

            case NFULA_MARK:
                {
                    if (datasz != sizeof(uint32_t))
                    {
                        LOG(WARN, "osn_nflog: NFULA_MARK size mismatch.");
                        break;
                    }

                    np->nfp_fwmark = ntohl(*(uint32_t *)data);
                    LOG(DEBUG, "osn_nflog: NFULA_MARK = %ux", np->nfp_fwmark);
                }
                break;

            case NFULA_TIMESTAMP:
                {
                    struct nfulnl_msg_packet_timestamp *ts = data;
                    if (datasz != sizeof(*ts))
                    {
                        LOG(WARN, "osn_nflog: NFULA_TIMESTAMP size mismatch");
                        break;
                    }

                    np->nfp_timestamp = be64toh(ts->sec) + ((double)be64toh(ts->usec) / 1000000.0);
                    LOG(DEBUG, "osn_nflog: NFULA_TIMESTAMP = %0.2f", np->nfp_timestamp);
                }
                break;

            case NFULA_IFINDEX_INDEV:
            case NFULA_IFINDEX_OUTDEV:
            case NFULA_IFINDEX_PHYSINDEV:
            case NFULA_IFINDEX_PHYSOUTDEV:
                {
                    char ifname[IF_NAMESIZE];
                    char ifindex;

                    if (datasz != sizeof(uint32_t))
                    {
                        LOG(WARN, "osn_nflog: NFULA_IFINDEX_INDEV size mismatch.");
                        break;
                    }

                    ifindex = ntohl(*(uint32_t *)data);
                    if (if_indextoname(ifindex, ifname) == 0)
                    {
                        snprintf(ifname, sizeof(ifname), "ifindex:%u\n", ifindex);
                    }

                    switch (nla->nla_type)
                    {
                        case NFULA_IFINDEX_INDEV:
                            STRSCPY(np->nfp_indev, ifname);
                            LOG(DEBUG, "osn_nflog: NFULA_IFINDEX_INDEV = %s",ifname);
                            break;

                        case NFULA_IFINDEX_OUTDEV:
                            STRSCPY(np->nfp_outdev, ifname);
                            LOG(DEBUG, "osn_nflog: NFULA_IFINDEX_OUTDEV = %s",ifname);
                            break;

                        case NFULA_IFINDEX_PHYSINDEV:
                            STRSCPY(np->nfp_physindev, ifname);
                            LOG(DEBUG, "osn_nflog: NFULA_IFINDEX_PHYSINDEV = %s",ifname);
                            break;

                        case NFULA_IFINDEX_PHYSOUTDEV:
                            STRSCPY(np->nfp_physoutdev, ifname);
                            LOG(DEBUG, "osn_nflog: NFULA_IFINDEX_PHYSOUTDEV = %s",ifname);
                            break;
                    }
                }
                break;

            case NFULA_HWADDR:
                {
                    osn_mac_addr_t hwaddr;

                    struct nfulnl_msg_packet_hw *hwa = data;
                    if (datasz != sizeof(*hwa))
                    {
                        LOG(WARN, "osn_nflog: NFULA_HWADDR size mismatch.");
                        break;
                    }

                    if (ntohs(hwa->hw_addrlen) != sizeof(hwaddr.ma_addr))
                    {
                        LOG(DEBUG, "osn_nflog: NFULA_HWADDR is not an ethernet address, size = %d",
                                ntohs(hwa->hw_addrlen));
                        break;
                    }

                    memcpy(hwaddr.ma_addr, hwa->hw_addr, sizeof(hwaddr.ma_addr));
                    snprintf(np->nfp_hwaddr, sizeof(np->nfp_hwaddr), PRI_osn_mac_addr, FMT_osn_mac_addr(hwaddr));
                    LOG(DEBUG, "NFULA_HWADDR = %s", np->nfp_hwaddr);
                }
                break;

            case NFULA_PAYLOAD:
                {
                    np->nfp_payload = MALLOC(datasz);
                    np->nfp_payload_len = datasz;
                    memcpy(np->nfp_payload, data, datasz);
                    LOG(DEBUG, "NFULA_PAYLOAD = %zd", np->nfp_payload_len);
                }
                break;

            case NFULA_PREFIX:
                {
                    np->nfp_prefix = STRDUP(data);
                    LOG(DEBUG, "NFULA_PREFIX = %s", np->nfp_prefix);
                }
                break;

            case NFULA_HWTYPE:
                {
                    np->nfp_hwtype = ntohs(*(uint16_t *)data);
                    LOG(DEBUG, "NFULA_HWTYPE = %d", np->nfp_hwtype);
                }
                break;

            case NFULA_HWLEN:
                {
                    np->nfp_hwlen = ntohs(*(uint16_t *)data);
                    LOG(DEBUG, "NFULA_HWLEN = %d", np->nfp_hwlen);
                }
                break;

            case NFULA_HWHEADER:
                {
                    np->nfp_hwheader = MALLOC(datasz);
                    np->nfp_hwheader_len = datasz;
                    memcpy(np->nfp_hwheader, data, datasz);
                    LOG(DEBUG, "NFULA_HEADER = %zd", np->nfp_hwheader_len);
                }
                break;

            case NFULA_UNSPEC:
            case NFULA_UID:
            case NFULA_SEQ:
            case NFULA_SEQ_GLOBAL:
            case NFULA_GID:
#if 0
            /* Not supported on all Linux versions */
            case NFULA_CT:
            case NFULA_CT_INFO:
            case NFULA_VLAN:
            case NFULA_L2HDR:
#endif
                /*
                 * Ignored attributes
                 */
                break;

            default:
                LOG(WARN, "osn_nflog: Unknown NFULNL_MSG_PACKET attribute type: %d, bytes %zd",
                        nla->nla_type, datasz);
                break;
        }
    }

    return 1;
}

/*
 * ===========================================================================
 *  Netlink utility functions
 * ===========================================================================
 */
void *nlbuf_get(struct nlbuf *nlb, size_t sz)
{
    void *retval = nlb->nb_cur;

    if (nlb->nb_cur + sz > nlb->nb_buf + nlb->nb_bufsz)
    {
        return NULL;
    }

    /*
     * Each "chunk" in the netlink message seems to be always aligned.
     * When requesting a new chunk, always align the buffer
     */
    nlb->nb_cur += NLMSG_ALIGN(sz);

    return retval;
}

size_t nlbuf_sz(struct nlbuf *nlb)
{
    return (nlb->nb_cur - nlb->nb_buf);
}
