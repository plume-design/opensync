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

#include <errno.h>
#include <netinet/icmp6.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>

#include <netinet/if_ether.h>
#include <net/if_arp.h>

#include <sys/socket.h>
#include "netdb.h"

#include "const.h"
#include "ds_tree.h"
#include "log.h"
#include "memutil.h"
#include "neigh_table.h"
#include "ndp_parse.h"
#include "assert.h"
#include "json_util.h"
#include "ovsdb.h"
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "net_header_parse.h"

static struct ndp_cache
cache_mgr =
{
    .initialized = false,
};


struct ndp_cache *
ndp_get_mgr(void)
{
    return &cache_mgr;
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
ndp_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}


/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the packet parsing handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
ndp_plugin_init(struct fsm_session *session)
{
    struct fsm_parser_ops *parser_ops;
    struct ndp_session *ndp_session;
    struct ndp_parser *parser;
    struct ndp_cache *mgr;

    if (session == NULL) return -1;

    mgr = ndp_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(&mgr->fsm_sessions, ndp_session_cmp,
                     struct ndp_session, session_node);
        mgr->initialized = true;
    }

    /* Look up the ndp session */
    ndp_session = ndp_lookup_session(session);
    if (ndp_session == NULL)
    {
        LOGE("%s: could not allocate the ndp parser", __func__);

        return -1;
    }

    /* Bail if the session is already initialized */
    if (ndp_session->initialized) return 0;

    parser = &ndp_session->parser;
    parser->sender.ipaddr = CALLOC(1, sizeof(*parser->sender.ipaddr));
    if (parser->sender.ipaddr == NULL)
    {
        LOGE("%s: could not allocate sender ip storage", __func__);
        goto exit_on_error;
    }

    parser->target.ipaddr = CALLOC(1, sizeof(*parser->target.ipaddr));
    if (parser->target.ipaddr == NULL)
    {
        LOGE("%s: could not allocate sender ip storage", __func__);
        goto exit_on_error;
    }

    parser->entry.ipaddr = CALLOC(1, sizeof(*parser->entry.ipaddr));
    if (parser->entry.ipaddr == NULL)
    {
        LOGE("%s: could not allocate sender ip storage", __func__);
        goto exit_on_error;
    }

    /* Set the fsm session */
    session->ops.update = ndp_plugin_update;
    session->ops.periodic = ndp_plugin_periodic;
    session->ops.exit = ndp_plugin_exit;
    session->handler_ctxt = ndp_session;

    /* Set the plugin specific ops */
    parser_ops = &session->p_ops->parser_ops;
    parser_ops->handler = ndp_plugin_handler;

    /* Wrap up the session initialization */
    ndp_session->session = session;
    ndp_session->timestamp = time(NULL);
    ndp_plugin_update(session);

    ndp_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    return 0;

exit_on_error:
    ndp_delete_session(session);
    return -1;
}


/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
ndp_plugin_exit(struct fsm_session *session)
{
    struct ndp_cache *mgr;

    mgr = ndp_get_mgr();
    if (!mgr->initialized) return;

    ndp_delete_session(session);
    return;
}


/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 * @param args the fsm session
 * @param net_parser the packet container
 */
void
ndp_plugin_handler(struct fsm_session *session,
                   struct net_header_parser *net_parser)
{
    struct ndp_session *n_session;
    struct ndp_parser *parser;
    size_t len;

    n_session = (struct ndp_session *)session->handler_ctxt;
    parser = &n_session->parser;
    parser->net_parser = net_parser;

    len = ndp_parse_message(parser);
    if (len == 0) return;

    if (parser->type == NEIGH_ICMPv6)
    {
        icmpv6_process_message(n_session);
    }
    else if (parser->type == NEIGH_ARP)
    {
        arp_process_message(n_session);
        parser->arp.s_eth = NULL;
        parser->arp.t_eth = NULL;
        parser->gratuitous = false;
    }

    return;
}


/**
 * @brief populate neigh entries in parser
 *
 * @param session neigh_session
 */
void
arp_populate_neigh_entries(struct ndp_session *n_session)
{
    struct fsm_session_conf *conf;
    struct sockaddr_storage *dst;
    struct ndp_parser *parser;
    struct sockaddr_in *in;
    struct eth_arp *arp;

    parser = &n_session->parser;
    arp = &parser->arp;
    conf = n_session->session->conf;

    dst = parser->sender.ipaddr;
    in = (struct sockaddr_in *)dst;

    memset(in, 0, sizeof(struct sockaddr_in));
    in->sin_family = AF_INET;
    memcpy(&in->sin_addr, &arp->s_ip, sizeof(in->sin_addr));

    parser->sender.mac = arp->s_eth;
    parser->sender.ifname = conf->if_name;
    parser->sender.source = FSM_ARP;
    if (arp->t_eth == NULL) return;

    dst = parser->target.ipaddr;
    in = (struct sockaddr_in *)dst;

    memset(in, 0, sizeof(struct sockaddr_in));
    in->sin_family = AF_INET;
    memcpy(&in->sin_addr, &arp->t_ip, sizeof(in->sin_addr));

    parser->target.mac = arp->t_eth;
    parser->target.ifname = conf->if_name;
    parser->target.source = FSM_ARP;

    return;
}


/**
 * @brief checks if an arp packet gratuitous arp
 *
 * @param arp the arp parsed packet
 * @return true if the arp packet is a gratuitous arp, false otherwise
 */
bool
arp_parse_is_gratuitous(struct eth_arp *arp)
{
    os_macaddr_t fmac = { { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } };
    os_macaddr_t zmac = { 0 };
    os_macaddr_t *t_eth;
    int cmp;

    t_eth = arp->t_eth;
    if (t_eth == NULL) return false;

    cmp = memcmp(t_eth, &zmac, sizeof(zmac));
    if (cmp == 0) return true;

    cmp = memcmp(t_eth, &fmac, sizeof(fmac));
    if (cmp == 0) return true;

    return false;
}


/**
 * @brief parses the received message content
 *
 * @param parser the parsed data container
 * @return the size of the parsed message content, or 0 on parsing error.
 */
size_t
arp_parse_content(struct ndp_parser *parser)
{
    struct net_header_parser *net_parser;
    struct eth_arp *arp;
    struct arphdr *arph;
    uint16_t ar_hrd;
    uint16_t ar_pro;
    uint16_t ar_op;
    size_t len;

    /* basic validation */
    net_parser = parser->net_parser;
    arp = &parser->arp;

    arph = (struct arphdr *)(net_parser->data);
    len = parser->neigh_len;
    if (len < sizeof(*arph)) return 0;

    ar_hrd = ntohs(arph->ar_hrd);
    if (ar_hrd != ARPHRD_ETHER) return 0;

    ar_pro = ntohs(arph->ar_pro);
    if (ar_pro != ETH_P_IP) return 0;

    if (arph->ar_hln != ETH_ALEN) return 0;
    if (arph->ar_pln != 4) return 0;

    ar_op = ntohs(arph->ar_op);
    if ((ar_op != ARPOP_REQUEST) && (ar_op != ARPOP_REPLY)) return 0;
    parser->op = ar_op;

    /* Access HW/network mapping info */
    net_parser->data += sizeof(*arph);
    net_parser->parsed += sizeof(*arph);

    arp->s_eth = (os_macaddr_t *)(net_parser->data);
    net_parser->data += ETH_ALEN;
    net_parser->parsed += ETH_ALEN;

    arp->s_ip = *(uint32_t *)(net_parser->data);
    net_parser->data += 4;
    net_parser->parsed += 4;

    if (ar_op == ARPOP_REPLY)
    {
        arp->t_eth = (os_macaddr_t *)(net_parser->data);
        parser->gratuitous = arp_parse_is_gratuitous(arp);
        net_parser->data += ETH_ALEN;
        arp->t_ip = *(uint32_t *)(net_parser->data);
    }
    return len;
}


/**
 * @brief parses the received message
 *
 * @param parser the parsed data container
 * @return the size of the parsed message, or 0 on parsing error.
 */
size_t
ndp_parse_message(struct ndp_parser *parser)
{
    struct net_header_parser *net_parser;
    uint16_t ethertype;
    int ip_protocol;
    size_t len;

    if (parser == NULL) return 0;

    /* Some basic validation */
    net_parser = parser->net_parser;
    ip_protocol = net_parser->ip_protocol;
    ethertype = net_header_get_ethertype(net_parser);

    /* Parse network header */
    parser->parsed = net_parser->parsed;
    parser->data = net_parser->data;

    /* Adjust payload length to remove potential ethernet padding */
    parser->neigh_len = net_parser->packet_len - net_parser->parsed;

    if (ip_protocol == IPPROTO_ICMPV6)
    {
        /* Parse the icmpv6 message content */
        len = icmpv6_parse_content(parser);
        parser->type = NEIGH_ICMPv6;
    }
    else if (ethertype == ETH_P_ARP)
    {
        /* Parse the arp message content */
        len = arp_parse_content(parser);
        parser->type = NEIGH_ARP;
    }
    else return 0;

    return len;
}


bool
ndp_populate_sockaddr(struct ndp_parser *parser)
{
    struct net_header_parser *net_parser;
    struct sockaddr_storage *dst;
    struct sockaddr_in6 *in6;
    struct ip6_hdr *hdr;
    void *ip;

    net_parser = parser->net_parser;

    /* Retrieve the source IP address */
    hdr = net_header_get_ipv6_hdr(net_parser);
    if (hdr == NULL) return false;

    ip = &hdr->ip6_src;

    dst = parser->entry.ipaddr;
    in6 = (struct sockaddr_in6 *)dst;

    memset(in6, 0, sizeof(struct sockaddr_in6));
    in6->sin6_family = AF_INET6;
    memcpy(&in6->sin6_addr, ip, sizeof(in6->sin6_addr));

    return true;
}


size_t
ndp_parse_solicit(struct ndp_parser *parser)
{
    struct net_header_parser *net_parser;
    struct nd_neighbor_solicit ns;
    struct nd_opt_hdr *opt_hdr;
    struct in6_addr *addr;
    os_macaddr_t *opt_mac;
    uint8_t opt_type;
    size_t opt_len;
    size_t len;
    bool ret;

    net_parser = parser->net_parser;
    len = parser->neigh_len;
    if (len < sizeof(ns.nd_ns_target)) return 0;

    memset(&ns, 0, sizeof(struct nd_neighbor_solicit));

    /* ICMPv6 hdr */
    memcpy(&ns.nd_ns_hdr, &net_parser->ip_pld.icmp6hdr, sizeof(struct icmp6_hdr));

    /* target address */
    addr = (struct in6_addr *)(parser->data);
    memcpy(&ns.nd_ns_target, addr, sizeof(struct in6_addr));

    /* Check for option */
    len -= sizeof(struct in6_addr);
    net_parser->data += sizeof(struct in6_addr);
    net_parser->parsed += sizeof(struct in6_addr);

    opt_mac = NULL;
    if (len > 0)
    {
        if (len < sizeof(*opt_hdr)) return 0;

        opt_hdr = (struct nd_opt_hdr *)(net_parser->data);
        opt_len = (size_t)opt_hdr->nd_opt_len;
        if (len < opt_len) return 0;

        opt_type = (size_t)opt_hdr->nd_opt_type;
        if (opt_type != ND_OPT_SOURCE_LINKADDR) return 0;

        net_parser->data += 2;
        net_parser->parsed += 2;

        opt_mac = CALLOC(1, sizeof(*opt_mac));
        if (opt_mac == NULL) return 0;
        memcpy(opt_mac, net_parser->data, sizeof(*opt_mac));
        parser->opt_mac = opt_mac;
    }

    /*
     * Prepare reporting if the optional source link address was present.
     * RFC 4861, 4.3.
     */
    if (parser->opt_mac == NULL) return 0;

    ret = ndp_populate_sockaddr(parser);
    if (!ret) return 0;

    parser->entry.mac = opt_mac;

    return net_parser->parsed;
}


size_t
ndp_parse_advert(struct ndp_parser *parser)
{
    struct net_header_parser *net_parser;
    struct nd_neighbor_advert na;
    struct sockaddr_storage *dst;
    struct nd_opt_hdr *opt_hdr;
    struct eth_header *eth_hdr;
    struct sockaddr_in6 *in6;
    struct in6_addr *addr;
    os_macaddr_t *opt_mac;
    uint8_t opt_type;
    size_t opt_len;
    size_t len;

    char ipstr[INET6_ADDRSTRLEN];
    struct neighbour_entry *entry;

    net_parser = parser->net_parser;
    len = parser->neigh_len;
    if (len < sizeof(na.nd_na_target)) return 0;

    memset(&na, 0, sizeof(struct nd_neighbor_advert));

    /* ICMPv6 hdr */
    memcpy(&na.nd_na_hdr, &net_parser->ip_pld.icmp6hdr, sizeof(struct icmp6_hdr));

    /* target address */
    addr = (struct in6_addr *)(parser->data);
    memcpy(&na.nd_na_target, addr, sizeof(struct in6_addr));

    /* Check for option */
    len -= sizeof(struct in6_addr);
    net_parser->data += sizeof(struct in6_addr);
    net_parser->parsed += sizeof(struct in6_addr);

    if (len > 0)
    {
        if (len < sizeof(*opt_hdr)) return 0;

        opt_hdr = (struct nd_opt_hdr *)(net_parser->data);
        opt_len = (size_t)opt_hdr->nd_opt_len;
        if (len < opt_len) return 0;

        opt_type = (size_t)opt_hdr->nd_opt_type;
        if (opt_type != ND_OPT_TARGET_LINKADDR) return 0;

        net_parser->data += 2;
        net_parser->parsed += 2;

        opt_mac = CALLOC(1, sizeof(*opt_mac));
        if (opt_mac == NULL) return 0;
        memcpy(opt_mac, net_parser->data, sizeof(*opt_mac));
        parser->opt_mac = opt_mac;
    }

    /* Prepare reporting. RFC 4861, 4.4. */
    dst = parser->entry.ipaddr;
    in6 = (struct sockaddr_in6 *)dst;

    memset(in6, 0, sizeof(struct sockaddr_in6));
    in6->sin6_family = AF_INET6;
    memcpy(&in6->sin6_addr, &na.nd_na_target, sizeof(in6->sin6_addr));

    if (parser->opt_mac)
    {
        parser->entry.mac = parser->opt_mac;
    }
    else
    {
        eth_hdr = net_header_get_eth(net_parser);
        parser->entry.mac = eth_hdr->srcmac;
    }

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_DEBUG))
    {
        entry = &parser->entry;
        getnameinfo((struct sockaddr *)entry->ipaddr,
                    sizeof(struct sockaddr_storage), ipstr, sizeof(ipstr),
                    0, 0, NI_NUMERICHOST);
        LOGD("%s: ip %s, mac "PRI_os_macaddr_lower_t, __func__,
             ipstr, FMT_os_macaddr_pt(entry->mac));
    }

    return net_parser->parsed;
}


/**
 * @brief parses the received message content
 *
 * @param parser the parsed data container
 * @return the size of the parsed message content, or 0 on parsing error.
 */
size_t
icmpv6_parse_content(struct ndp_parser *parser)
{
    struct net_header_parser *net_parser;
    struct icmp6_hdr *icmphdr;
    uint8_t type;
    uint8_t code;
    size_t len;

    /* basic validation */
    net_parser = parser->net_parser;
    icmphdr = net_parser->ip_pld.icmp6hdr;
    code = icmphdr->icmp6_code;
    if (code != 0) return 0;

    type = icmphdr->icmp6_type;
    if (type == ND_NEIGHBOR_SOLICIT) len = ndp_parse_solicit(parser);
    else if (type == ND_NEIGHBOR_ADVERT) len = ndp_parse_advert(parser);
    else len = 0;

    return len;
}


/**
 * @brief process the parsed message
 *
 * Prepare a key to lookup the flow stats info, and update the flow stats.
 * @param n_session the ndp session pointing to the parsed message
 */
void
icmpv6_process_message(struct ndp_session *n_session)
{
    struct fsm_session_conf *conf;
    struct ndp_parser *parser;

    parser = &n_session->parser;
    if (parser->entry.mac == NULL) return;

    conf = n_session->session->conf;

    /* Add the tap interface to the entry upsert */
    parser->entry.ifname = conf->if_name;

    parser->entry.source = FSM_NDP;
    parser->entry.cache_valid_ts = n_session->timestamp;

    /* Record the IP mac mapping */
    neigh_table_add(&parser->entry);

    FREE(parser->opt_mac);
    parser->opt_mac = NULL;
    parser->entry.mac = NULL;
}

/**
 * @brief process the parsed message
 *
 * Prepare a key to lookup the flow stats info, and update the flow stats.
 * @param a_session the arp session pointing to the parsed message
 */
void
arp_process_message(struct ndp_session *n_session)
{
    struct ndp_parser *parser;

    /* basic validation */
    parser = &n_session->parser;

    arp_populate_neigh_entries(n_session);
    parser->sender.cache_valid_ts = n_session->timestamp;
    neigh_table_add(&parser->sender);

    if (parser->op == ARPOP_REQUEST) return;

    if (parser->gratuitous) return;

    /* Record the target IP mac mapping if available */
    if (parser->arp.t_eth != NULL)
    {
        parser->target.cache_valid_ts = n_session->timestamp;
        neigh_table_add(&parser->target);
    }
}

/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct ndp_session *
ndp_lookup_session(struct fsm_session *session)
{
    struct ndp_session *n_session;
    struct ndp_cache *mgr;
    ds_tree_t *sessions;

    mgr = ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session != NULL) return n_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    n_session = CALLOC(1, sizeof(struct ndp_session));
    if (n_session == NULL) return NULL;

    ds_tree_insert(sessions, n_session, session);

    return n_session;
}


/**
 * @brief Frees a ndp session
 *
 * @param n_session the ndp session to free
 */
void
ndp_free_session(struct ndp_session *n_session)
{
    struct ndp_parser *parser;

    parser = &n_session->parser;
    FREE(parser->sender.ipaddr);
    FREE(parser->target.ipaddr);
    FREE(parser->entry.ipaddr);
    FREE(n_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the ndp session to delete
 */
void
ndp_delete_session(struct fsm_session *session)
{
    struct ndp_session *n_session;
    struct ndp_cache *mgr;
    ds_tree_t *sessions;

    mgr = ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, n_session);
    ndp_free_session(n_session);

    return;
}

#define FSM_NDP_CHECK_TTL (2*60)
/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the arp session to delete
 */
void
ndp_plugin_periodic(struct fsm_session *session)
{
    struct ndp_session *n_session;
    struct ndp_cache *mgr;
    ds_tree_t *sessions;
    double cmp_clean;
    time_t now;

    mgr = ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return;

    now = time(NULL);
    cmp_clean = now - n_session->timestamp;
    if (cmp_clean < FSM_NDP_CHECK_TTL) return;

    neigh_table_ttl_cleanup(n_session->ttl, OVSDB_ARP);
    neigh_table_ttl_cleanup(n_session->ttl, OVSDB_NDP);
    n_session->timestamp = now;

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) print_neigh_table();
}


#define FSM_NDP_DEFAULT_TTL (36*60*60)
/**
 * @brief update routine
 *
 * @param session the fsm session keying the ndp session to update
 */
void
ndp_plugin_update(struct fsm_session *session)
{
    struct ndp_session *n_session;
    struct ndp_cache *mgr;
    ds_tree_t *sessions;
    unsigned long ttl;
    char *str_ttl;

    mgr = ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return;

    /* set the default timer */
    n_session->ttl = FSM_NDP_DEFAULT_TTL;
    str_ttl = session->ops.get_config(session, "ttl");
    if (str_ttl == NULL) goto log_settings;

    ttl = strtoul(str_ttl, NULL, 10);
    if (ttl == 0 || ttl == ULONG_MAX)
    {
        LOGE("%s: conversion of %s failed: %d", __func__, str_ttl, errno);
        goto log_settings;
    }
    n_session->ttl = (uint64_t)ttl;

log_settings:
    LOGI("%s: %s: setting neighbor entry ttl to %" PRIu64 " secs", __func__,
         session->name, n_session->ttl);
}
