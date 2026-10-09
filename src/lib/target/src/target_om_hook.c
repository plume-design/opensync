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
 * Flow Cache utilities
 */
#define _GNU_SOURCE
#include <string.h>
#include "target.h"
#include "hw_acc.h"
#include "log.h"
#include "os_util.h"
#include "kconfig.h"

#ifdef CONFIG_TARGET_OM_HOOK_NF_CT_FLUSH
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <libnetfilter_conntrack/libnetfilter_conntrack.h>

struct nf_ct_flush_ctx
{
    struct nfct_handle *dth;
    uint8_t             l4proto;
    uint16_t            dport;
    struct in_addr      reply_src;
    int                 ndeleted;
};

/*
 * Match a dumped conntrack entry against (proto, dport, reply_src) and
 * destroy it if it matches.
 */
static int nf_ct_flush_cb(enum nf_conntrack_msg_type type, struct nf_conntrack *ct, void *data)
{
    struct nf_ct_flush_ctx *ctx = data;

    (void)type;

    if (nfct_get_attr_u8(ct, ATTR_ORIG_L4PROTO) != ctx->l4proto) return NFCT_CB_CONTINUE;
    if (nfct_get_attr_u16(ct, ATTR_ORIG_PORT_DST) != ctx->dport) return NFCT_CB_CONTINUE;
    if (nfct_get_attr_u32(ct, ATTR_REPL_IPV4_SRC) != ctx->reply_src.s_addr) return NFCT_CB_CONTINUE;

    if (nfct_query(ctx->dth, NFCT_Q_DESTROY, ct) == 0) ctx->ndeleted++;

    return NFCT_CB_CONTINUE;
}

static bool nf_ct_flush_by_port(char *proto, char *port, char *dst_ip)
{
    struct nfct_handle *cth;
    struct nfct_handle *dth;
    struct nf_ct_flush_ctx ctx = { 0 };
    int family = AF_INET;

    if (strcmp(proto, "tcp") == 0) {
        ctx.l4proto = IPPROTO_TCP;
    }
    else if (strcmp(proto, "udp") == 0) {
        ctx.l4proto = IPPROTO_UDP;
    }
    else {
        LOGW("target_om_hook: unsupported conntrack proto: %s", proto);
        return false;
    }

    ctx.dport = htons((uint16_t)atoi(port));

    if (inet_pton(AF_INET, dst_ip, &ctx.reply_src) != 1) {
        LOGW("target_om_hook: invalid conntrack reply-src: %s", dst_ip);
        return false;
    }

    LOGD("target_om_hook: flushing conntrack: proto=%s dport=%s reply-src=%s", proto, port, dst_ip);

    cth = nfct_open(CONNTRACK, 0);
    if (cth == NULL) {
        LOGW("target_om_hook: nfct_open() failed: %s", strerror(errno));
        return false;
    }

    // The destroys must go out on their own netlink socket.
    dth = nfct_open(CONNTRACK, 0);
    if (dth == NULL) {
        LOGW("target_om_hook: nfct_open() failed: %s", strerror(errno));
        nfct_close(cth);
        return false;
    }

    ctx.dth = dth;
    nfct_callback_register(cth, NFCT_T_ALL, nf_ct_flush_cb, &ctx);

    if (nfct_query(cth, NFCT_Q_DUMP, &family) != 0) {
        LOGW("target_om_hook: conntrack dump failed: %s", strerror(errno));
    }

    nfct_callback_unregister(cth);
    nfct_close(dth);
    nfct_close(cth);

    return ctx.ndeleted > 0;
}
#endif /* CONFIG_TARGET_OM_HOOK_NF_CT_FLUSH */

bool target_om_hook(target_om_hook_t hook, const char *openflow_rule)
{
    char        *flow = strdupa(openflow_rule);
    char        *ptr;
    bool        flushed_mac = false;
#ifdef CONFIG_TARGET_OM_HOOK_NF_CT_FLUSH
    char        *proto = NULL;
    char        *port = NULL;
    char        *dst_ip = NULL;
#endif

    switch (hook)
    {
        case TARGET_OM_POST_ADD:
        case TARGET_OM_POST_DEL:
        {
            // Examples of iptables/ebtables rules
            // "-m mac --mac-source e4:5f:01:81:64:ba"
            // "-s e4:5f:01:81:64:ba" or "-d e4:5f:01:81:64:ba"
            // "-i br-wan -p tcp --dport 8080 --to-destination 192.168.1.50:80"
            while ((ptr = strsep(&flow, " "))) {
                if (os_util_is_valid_mac_str(ptr)) {
                    flushed_mac = hw_acc_flush_flow_per_mac(ptr);
                }
#ifdef CONFIG_TARGET_OM_HOOK_NF_CT_FLUSH
                else if (strcmp(ptr, "-p") == 0) {
                    proto = strsep(&flow, " ");
                }
                else if (strcmp(ptr, "--dport") == 0) {
                    port = strsep(&flow, " ");
                }
                else if (strcmp(ptr, "--to-destination") == 0) {
                    // value is "<dst_ip>:<dst_port>"
                    dst_ip = strsep(&flow, " ");
                    if (dst_ip != NULL) {
                        char *colon = strchr(dst_ip, ':');
                        if (colon != NULL) {
                            *colon = '\0';
                        }
                    }
                }
#endif
            }

            if (!flushed_mac) {
                hw_acc_flush_all_flows();
            }

#ifdef CONFIG_TARGET_OM_HOOK_NF_CT_FLUSH
            if (proto != NULL && port != NULL && dst_ip != NULL) {
                nf_ct_flush_by_port(proto, port, dst_ip);
            }
#endif

            break;
        }

        case TARGET_OM_PRE_ADD:
        case TARGET_OM_PRE_DEL:
            break;

        default:
            break;
    }

    return true;
}
