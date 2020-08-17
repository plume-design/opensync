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

#include <sys/socket.h>
#include "netdb.h"

#include "const.h"
#include "ds_tree.h"
#include "log.h"
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
    parser->entry.ipaddr = calloc(1, sizeof(*parser->entry.ipaddr));
    if (parser->entry.ipaddr == NULL)
    {
        LOGE("%s: could not allocate ip storage", __func__);
        ndp_delete_session(session);

        return -1;
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

    ndp_process_message(n_session);

    return;
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
    int ip_protocol;
    size_t len;

    if (parser == NULL) return 0;

    /* Some basic validation */
    net_parser = parser->net_parser;
    ip_protocol = net_parser->ip_protocol;
    if (ip_protocol != IPPROTO_ICMPV6) return 0;

    /* Parse network header */
    parser->parsed = net_parser->parsed;
    parser->data = net_parser->data;

    /* Adjust payload length to remove potential ethernet padding */
    parser->icmpv6_len = net_parser->packet_len - net_parser->parsed;

    /* Parse the message content */
    len = ndp_parse_content(parser);
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
    struct nd_neighbor_solicit *ns;
    struct nd_opt_hdr *opt_hdr;
    os_macaddr_t *opt_mac;
    uint8_t opt_type;
    size_t opt_len;
    size_t len;
    bool ret;

    net_parser = parser->net_parser;
    len = parser->icmpv6_len;
    if (len < sizeof(*ns)) return 0;

    ns = (struct nd_neighbor_solicit *)(net_parser->data);

    /* Check for option */
    len -= sizeof(*ns);
    net_parser->data += sizeof(*ns);
    net_parser->parsed += sizeof(*ns);

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

        opt_mac = calloc(1, sizeof(*opt_mac));
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
    struct nd_neighbor_advert *na;
    struct sockaddr_storage *dst;
    struct nd_opt_hdr *opt_hdr;
    struct eth_header *eth_hdr;
    struct sockaddr_in6 *in6;
    os_macaddr_t *opt_mac;
    uint8_t opt_type;
    size_t opt_len;
    size_t len;

    char ipstr[INET6_ADDRSTRLEN];
    struct neighbour_entry *entry;

    net_parser = parser->net_parser;
    len = parser->icmpv6_len;
    if (len < sizeof(*na)) return 0;

    na = (struct nd_neighbor_advert *)(net_parser->data);

    /* Check for option */
    len -= sizeof(*na);
    net_parser->data += sizeof(*na);
    net_parser->parsed += sizeof(*na);

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

        opt_mac = calloc(1, sizeof(*opt_mac));
        if (opt_mac == NULL) return 0;
        memcpy(opt_mac, net_parser->data, sizeof(*opt_mac));
        parser->opt_mac = opt_mac;
    }

    /* Prepare reporting. RFC 4861, 4.4. */
    dst = parser->entry.ipaddr;
    in6 = (struct sockaddr_in6 *)dst;

    memset(in6, 0, sizeof(struct sockaddr_in6));
    in6->sin6_family = AF_INET6;
    memcpy(&in6->sin6_addr, &na->nd_na_target, sizeof(in6->sin6_addr));

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
ndp_parse_content(struct ndp_parser *parser)
{
    struct net_header_parser *net_parser;
    struct icmp6_hdr *icmphdr;
    uint8_t type;
    uint8_t code;
    size_t len;

    /* basic validation */
    net_parser = parser->net_parser;
    len = parser->icmpv6_len;
    if (len < sizeof(*icmphdr)) return 0;
    icmphdr = (struct icmp6_hdr *)(net_parser->data);
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
ndp_process_message(struct ndp_session *n_session)
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

    free(parser->opt_mac);
    parser->opt_mac = NULL;
    parser->entry.mac = NULL;

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) print_neigh_table();
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
    n_session = calloc(1, sizeof(struct ndp_session));
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
    free(parser->entry.ipaddr);
    free(n_session);
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

    neigh_table_ttl_cleanup(n_session->ttl, OVSDB_NDP);
    n_session->timestamp = now;
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
