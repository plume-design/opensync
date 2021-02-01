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
#include <inttypes.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>

#include <netinet/if_ether.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>

#include <sys/socket.h>
#include "netdb.h"

#include "const.h"
#include "ds_tree.h"
#include "log.h"
#include "neigh_table.h"
#include "arp_parse.h"
#include "assert.h"
#include "json_util.h"
#include "ovsdb.h"
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "net_header_parse.h"

static struct arp_cache
cache_mgr =
{
    .initialized = false,
};


struct arp_cache *
arp_get_mgr(void)
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
arp_session_cmp(void *a, void *b)
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
arp_plugin_init(struct fsm_session *session)
{
    struct fsm_parser_ops *parser_ops;
    struct arp_session *arp_session;
    struct arp_parser *parser;
    struct arp_cache *mgr;

    if (session == NULL) return -1;

    mgr = arp_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(&mgr->fsm_sessions, arp_session_cmp,
                     struct arp_session, session_node);
        mgr->initialized = true;
    }

    /* Look up the arp session */
    arp_session = arp_lookup_session(session);
    if (arp_session == NULL)
    {
        LOGE("%s: could not allocate the arp parser", __func__);

        return -1;
    }

    /* Bail if the session is already initialized */
    if (arp_session->initialized) return 0;

    parser = &arp_session->parser;
    parser->sender.ipaddr = calloc(1, sizeof(*parser->sender.ipaddr));
    if (parser->sender.ipaddr == NULL)
    {
        LOGE("%s: could not allocate sender ip storage", __func__);
        goto exit_on_error;
    }

    parser->target.ipaddr = calloc(1, sizeof(*parser->sender.ipaddr));
    if (parser->target.ipaddr == NULL)
    {
        LOGE("%s: could not allocate sender ip storage", __func__);
        goto exit_on_error;
    }

    /* Set the fsm session */
    session->ops.update = arp_plugin_update;
    session->ops.periodic = arp_plugin_periodic;
    session->ops.exit = arp_plugin_exit;
    session->handler_ctxt = arp_session;

    /* Set the plugin specific ops */
    parser_ops = &session->p_ops->parser_ops;
    parser_ops->handler = arp_plugin_handler;

    /* Wrap up the session initialization */
    arp_session->session = session;
    arp_session->timestamp = time(NULL);
    arp_plugin_update(session);

    arp_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    return 0;

exit_on_error:
    arp_delete_session(session);

    return -1;
}


/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
arp_plugin_exit(struct fsm_session *session)
{
    struct arp_cache *mgr;

    mgr = arp_get_mgr();
    if (!mgr->initialized) return;

    arp_delete_session(session);
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
arp_plugin_handler(struct fsm_session *session,
                   struct net_header_parser *net_parser)
{
    struct arp_session *a_session;
    struct arp_parser *parser;
    size_t len;

    a_session = (struct arp_session *)session->handler_ctxt;
    parser = &a_session->parser;
    parser->net_parser = net_parser;

    len = arp_parse_message(parser);
    if (len == 0) return;

    arp_process_message(a_session);

    parser->arp.s_eth = NULL;
    parser->arp.t_eth = NULL;
    parser->gratuitous = false;

    return;
}


/**
 * @brief parses the received message
 *
 * @param parser the parsed data container
 * @return the size of the parsed message, or 0 on parsing error.
 */
size_t
arp_parse_message(struct arp_parser *parser)
{
    struct net_header_parser *net_parser;
    size_t len;

    if (parser == NULL) return 0;

    /* Some basic validation */
    net_parser = parser->net_parser;

    /* Parse network header */
    parser->parsed = net_parser->parsed;
    parser->data = net_parser->data;

    /* Adjust payload length to remove potential ethernet padding */
    parser->arp_len = net_parser->packet_len - net_parser->parsed;

    /* Parse the message content */
    len = arp_parse_content(parser);
    return len;
}


bool
arp_populate_neigh_entries(struct arp_session *a_session)
{
    struct fsm_session_conf *conf;
    struct sockaddr_storage *dst;
    struct arp_parser *parser;
    struct sockaddr_in *in;
    struct eth_arp *arp;

    parser = &a_session->parser;
    arp = &parser->arp;
    conf = a_session->session->conf;

    dst = parser->sender.ipaddr;
    in = (struct sockaddr_in *)dst;

    memset(in, 0, sizeof(struct sockaddr_in));
    in->sin_family = AF_INET;
    memcpy(&in->sin_addr, &arp->s_ip, sizeof(in->sin_addr));

    parser->sender.mac = arp->s_eth;
    parser->sender.ifname = conf->if_name;
    parser->sender.source = FSM_ARP;

    if (arp->t_eth == NULL) return true;

    dst = parser->target.ipaddr;
    in = (struct sockaddr_in *)dst;

    memset(in, 0, sizeof(struct sockaddr_in));
    in->sin_family = AF_INET;
    memcpy(&in->sin_addr, &arp->t_ip, sizeof(in->sin_addr));

    parser->target.mac = arp->t_eth;
    parser->target.ifname = conf->if_name;
    parser->target.source = FSM_ARP;

    return true;
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
arp_parse_content(struct arp_parser *parser)
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
    len = parser->arp_len;
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
 * @brief process the parsed message
 *
 * Prepare a key to lookup the flow stats info, and update the flow stats.
 * @param a_session the arp session pointing to the parsed message
 */
void
arp_process_message(struct arp_session *a_session)
{
    struct arp_parser *parser;

    /* basic validation */
    parser = &a_session->parser;

    arp_populate_neigh_entries(a_session);
    parser->sender.cache_valid_ts = a_session->timestamp;
    neigh_table_add(&parser->sender);

    if (parser->op == ARPOP_REQUEST) return;

    if (parser->gratuitous) return;

    /* Record the target IP mac mapping if available */
    if (parser->arp.t_eth != NULL)
    {
        parser->target.cache_valid_ts = a_session->timestamp;
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
struct arp_session *
arp_lookup_session(struct fsm_session *session)
{
    struct arp_session *a_session;
    struct arp_cache *mgr;
    ds_tree_t *sessions;

    mgr = arp_get_mgr();
    sessions = &mgr->fsm_sessions;

    a_session = ds_tree_find(sessions, session);
    if (a_session != NULL) return a_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    a_session = calloc(1, sizeof(struct arp_session));
    if (a_session == NULL) return NULL;

    ds_tree_insert(sessions, a_session, session);

    return a_session;
}


/**
 * @brief Frees a arp session
 *
 * @param a_session the arp session to free
 */
void
arp_free_session(struct arp_session *a_session)
{
    struct arp_parser *parser;

    parser = &a_session->parser;
    free(parser->sender.ipaddr);
    free(parser->target.ipaddr);
    free(a_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the arp session to delete
 */
void
arp_delete_session(struct fsm_session *session)
{
    struct arp_session *a_session;
    struct arp_cache *mgr;
    ds_tree_t *sessions;

    mgr = arp_get_mgr();
    sessions = &mgr->fsm_sessions;

    a_session = ds_tree_find(sessions, session);
    if (a_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, a_session);
    arp_free_session(a_session);

    return;
}


#define FSM_ARP_CHECK_TTL (2*60)
/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the arp session to uprocess
 */
void
arp_plugin_periodic(struct fsm_session *session)
{
    struct arp_session *a_session;
    struct arp_cache *mgr;
    ds_tree_t *sessions;
    double cmp_clean;
    time_t now;

    mgr = arp_get_mgr();
    sessions = &mgr->fsm_sessions;

    a_session = ds_tree_find(sessions, session);
    if (a_session == NULL) return;

    now = time(NULL);
    cmp_clean = now - a_session->timestamp;
    if (cmp_clean < FSM_ARP_CHECK_TTL) return;

    neigh_table_ttl_cleanup(a_session->ttl, OVSDB_ARP);
    a_session->timestamp = now;
}


#define FSM_ARP_DEFAULT_TTL (36*60*60)
/**
 * @brief update routine
 *
 * @param session the fsm session keying the arp session to update
 */
void
arp_plugin_update(struct fsm_session *session)
{
    struct arp_session *a_session;
    struct arp_cache *mgr;
    ds_tree_t *sessions;
    unsigned long ttl;
    char *str_ttl;

    mgr = arp_get_mgr();
    sessions = &mgr->fsm_sessions;

    a_session = ds_tree_find(sessions, session);
    if (a_session == NULL) return;

    /* set the default timer */
    a_session->ttl = FSM_ARP_DEFAULT_TTL;
    str_ttl = session->ops.get_config(session, "ttl");
    if (str_ttl == NULL) goto log_settings;
    ttl = strtoul(str_ttl, NULL, 10);
    if (ttl == 0 || ttl == ULONG_MAX)
    {
        LOGE("%s: conversion of %s failed: %d", __func__, str_ttl, errno);
        goto log_settings;
    }
    a_session->ttl = (uint64_t)ttl;

log_settings:
    LOGI("%s: %s: setting neighbor entry ttl to %" PRIu64 " secs", __func__,
         session->name, a_session->ttl);
}
