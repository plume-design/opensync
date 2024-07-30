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

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>

#include "ds_tree.h"
#include "fsm.h"
#include "fsm_dpi_client_plugin.h"
#include "sockaddr_storage.h"
#include "neigh_table.h"
#include "fsm_dpi_utils.h"
#include "kconfig.h"
#include "log.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "sockaddr_storage.h"
#include "os.h"
#include "util.h"
#include "fsm_fn_trace.h"

#include "fsm_dpi_ndp.h"

static struct dpi_ndp_client main_data =
{
    .initialized = false,
    .curr_arp_rec_processed.next_state =  BEGIN_ARP,
    .curr_ndp_rec_processed.next_state =  BEGIN_NDP,
};

struct dpi_ndp_client *
fsm_dpi_ndp_get_mgr(void)
{
    return &main_data;
}

const char * const arp_state_str[] =
{
    [UNDEFINED] = "undefined",
    [BEGIN_ARP] = "begin",
    [ARP_REQ_SIP] = "arp.request.sender_ip",
    [ARP_REQ_SMAC] = "arp.request.sender_mac",
    [ARP_REQ_TIP] = "arp.request.target_ip",
    [ARP_REQ_TMAC] = "arp.request.target_mac",
    [ARP_RES_SIP] = "arp.response.sender_ip",
    [ARP_RES_SMAC] = "arp.response.sender_mac",
    [ARP_RES_TIP] = "arp.response.target_ip",
    [ARP_RES_TMAC] = "arp.response.target_mac",
    [END_ARP] = "end",
};

const char * const ndp_state_str[] =
{
    [UNDEFINED] = "undefined",
    [BEGIN_NDP] = "begin",
    [ICMPv6_NA_TIP] = "icmpv6.ndp.neighbor_advertisement.target_addr",
    [ICMPv6_NA_TMAC] = "icmpv6.ndp.neighbor_advertisement.target_link_addr",
    [ICMPv6_NS_SMAC] = "icmpv6.ndp.neighbor_solicitation.src_link_addr",
    [ICMPv6_NS_SIP] = "icmpv6.ndp.neighbor_solicitation.target_addr",
    [END_NDP] = "end",
};

const char *arp_attr_value = "arp";
const char *ndp_attr_value = "ndp";

static enum arp_state
get_arp_state(const char *attribute)
{
#define GET_ARP_STATE(attr, x)                \
    do                                        \
    {                                         \
        int cmp;                              \
        cmp = strcmp(attr, arp_state_str[x]); \
        if (cmp == 0) return x;               \
    }                                         \
    while (0)

    GET_ARP_STATE(attribute, ARP_REQ_SIP);
    GET_ARP_STATE(attribute, ARP_REQ_SMAC);
    GET_ARP_STATE(attribute, ARP_REQ_TIP);
    GET_ARP_STATE(attribute, ARP_REQ_TMAC);
    GET_ARP_STATE(attribute, ARP_RES_SIP);
    GET_ARP_STATE(attribute, ARP_RES_SMAC);
    GET_ARP_STATE(attribute, ARP_RES_TIP);
    GET_ARP_STATE(attribute, ARP_RES_TMAC);
    GET_ARP_STATE(attribute, BEGIN_ARP);
    GET_ARP_STATE(attribute, END_ARP);

    return UNDEFINED;
#undef GET_ARP_STATE
}

static enum ndp_state
get_ndp_state(const char *attribute)
{
#define GET_NDP_STATE(attr, x)                \
    do                                        \
    {                                         \
        int cmp;                              \
        cmp = strcmp(attr, ndp_state_str[x]); \
        if (cmp == 0) return x;               \
    }                                         \
    while (0)

    GET_NDP_STATE(attribute, ICMPv6_NS_SIP);
    GET_NDP_STATE(attribute, ICMPv6_NS_SMAC);
    GET_NDP_STATE(attribute, ICMPv6_NA_TIP);
    GET_NDP_STATE(attribute, ICMPv6_NA_TMAC);
    GET_NDP_STATE(attribute, BEGIN_NDP);
    GET_NDP_STATE(attribute, END_NDP);

    return ndp_state_UNDEFINED;
#undef GET_NDP_STATE
}

/**
  * @brief Frees a ndp session
  *
  * @param n_session the ndp session to free
  */
void
fsm_dpi_ndp_free_session(struct ndp_session *n_session)
{
    FREE(n_session->entry.ipaddr);
    FREE(n_session);
}


/**
  * @brief deletes a session
  *
  * @param session the fsm session keying the ndp session to delete
  */
void
fsm_dpi_ndp_delete_session(struct fsm_session *session)
{
    struct ndp_session *n_session;
    struct dpi_ndp_client *mgr;
    ds_tree_t *sessions;

    mgr = fsm_dpi_ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, n_session);
    fsm_dpi_ndp_free_session(n_session);

    return;
}


static bool
mac_is_empty_or_bcast(os_macaddr_t *mac)
{
    os_macaddr_t zmac = { 0 };
    os_macaddr_t fmac = { {0xff, 0xff, 0xff, 0xff, 0xff, 0xff} };
    int cmp = 0;

    cmp = memcmp(mac, &zmac, sizeof(zmac));
    if (cmp == 0) return true;

    cmp = memcmp(mac, &fmac, sizeof(fmac));
    if (cmp == 0) return true;

    return false;
}

static void
populate_neigh_table_with_rec(struct neighbour_entry *entry, void *ipaddr, os_macaddr_t *mac, int af)
{
    struct sockaddr_storage *dst;
    struct sockaddr_in6 *in6;
    struct sockaddr_in *in;

    if (!entry) return;

    dst = entry->ipaddr;

    if (af == AF_INET)
    {
        in = (struct sockaddr_in *)dst;
        memset(in, 0, sizeof(struct sockaddr_storage));
        in->sin_family = af;
        memcpy(&in->sin_addr, ipaddr, sizeof(in->sin_addr));
        entry->af_family = af;
    }
    else if (af == AF_INET6)
    {
        in6 = (struct sockaddr_in6 *)dst;
        memset(in6, 0, sizeof(struct sockaddr_storage));
        in6->sin6_family = af;
        memcpy(&in6->sin6_addr, ipaddr, sizeof(in6->sin6_addr));
        entry->af_family = af;
    }

    entry->mac = mac;
    entry->ifname = "dpi_ndp";
    return;
}

void
fsm_dpi_arp_reset_state(struct fsm_session *session)
{
    struct ndp_session *n_session;
    struct neighbour_entry *entry;
    struct sockaddr_storage *ip;
    struct dpi_ndp_client *mgr;
    struct arp_record *rec;
    ds_tree_t *sessions;

    mgr = fsm_dpi_ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return;

    entry = &n_session->entry;
    ip = entry->ipaddr;
    MEMZERO(*entry);
    entry->ipaddr = ip;

    rec = &mgr->curr_arp_rec_processed;
    MEMZERO(*rec);

    rec->next_state = BEGIN_ARP;
    return;
}

void
fsm_dpi_ndp_reset_state(struct fsm_session *session)
{
    struct ndp_session *n_session;
    struct neighbour_entry *entry;
    struct sockaddr_storage *ip;
    struct dpi_ndp_client *mgr;
    struct ndp_record *rec;
    ds_tree_t *sessions;

    mgr = fsm_dpi_ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return;

    entry = &n_session->entry;
    ip = entry->ipaddr;
    MEMZERO(*entry);
    entry->ipaddr = ip;

    rec = &mgr->curr_ndp_rec_processed;
    MEMZERO(*rec);

    rec->next_state = BEGIN_NDP;
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
ndp_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}


#define FSM_NDP_DEFAULT_TTL (36*60*60)
/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm url session to update
 */
void
fsm_dpi_ndp_update(struct fsm_session *session)
{
    struct ndp_session *n_session;
    struct dpi_ndp_client *mgr;
    ds_tree_t *sessions;
    unsigned long ttl;
    char *str_ttl;

    /* Generic config first */
    fsm_dpi_client_update(session);

    mgr = fsm_dpi_ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return;

    /* set the default timer */
    n_session->timestamp = time(NULL);
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
    LOGD("%s: %s: setting neighbor entry ttl to %" PRIu64 " secs", __func__,
         session->name, n_session->ttl);
}


void
fsm_dpi_ndp_periodic(struct fsm_session *session)
{
    struct ndp_session *n_session;
    struct dpi_ndp_client *mgr;
    ds_tree_t *sessions;
    bool need_periodic;

    mgr = fsm_dpi_ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return;


    need_periodic = fsm_dpi_client_periodic_check(session);
    if (need_periodic)
    {
        if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) print_neigh_table();
        neigh_table_ttl_cleanup(n_session->ttl, OVSDB_ARP);
        neigh_table_ttl_cleanup(n_session->ttl, OVSDB_NDP);
    }
}


bool
fsm_dpi_ndp_process_arp_record(struct fsm_session *session,
                               struct net_md_stats_accumulator *acc,
                               struct net_header_parser *net_parser)
{
    struct ndp_session *n_session;
    struct neighbour_entry *entry;
    struct dpi_ndp_client *mgr;
    struct arp_record *rec;
    ds_tree_t *sessions;
    time_t now;

    mgr = fsm_dpi_ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return false;

    rec = &mgr->curr_arp_rec_processed;

    entry = &n_session->entry;
    now = time(NULL);

    populate_neigh_table_with_rec(entry, &rec->s_ip_addr, &rec->s_mac, AF_INET);

    entry->cache_valid_ts = now;
    entry->source = FSM_ARP;
    neigh_table_add(entry);

    if (mac_is_empty_or_bcast(&rec->t_mac)) return true;

    populate_neigh_table_with_rec(entry, &rec->t_ip_addr, &rec->t_mac, AF_INET);

    entry->cache_valid_ts = now;
    entry->source = FSM_ARP;
    neigh_table_add(entry);

    return true;
}

bool
fsm_dpi_ndp_process_icmpv6_record(struct fsm_session *session,
                                  struct net_md_stats_accumulator *acc,
                                  struct net_header_parser *net_parser)
{
    struct ndp_session *n_session;
    struct neighbour_entry *entry;
    struct dpi_ndp_client *mgr;
    ds_tree_t *sessions;
    struct ndp_record *rec;

    mgr = fsm_dpi_ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return false;

    rec = &mgr->curr_ndp_rec_processed;

    entry = &n_session->entry;

    if (mac_is_empty_or_bcast(&rec->mac)) return true;

    populate_neigh_table_with_rec(entry, rec->ip_addr, &rec->mac, AF_INET6);

    entry->cache_valid_ts = time(NULL);
    entry->source = FSM_NDP;
    neigh_table_add(entry);

    return true;
}


static int
fsm_dpi_arp_process_attr(struct fsm_session *session, const char *attr,
                         uint8_t type, uint16_t length, const void *value,
                         struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct net_header_parser *net_parser;
    struct net_md_stats_accumulator *acc;
    struct dpi_ndp_client *mgr;
    struct arp_record *rec;
    unsigned int curr_state;
    int cmp;
    int ret = -1;

    mgr = fsm_dpi_ndp_get_mgr();

    if (!mgr->initialized) return -1;
    if (pkt_info == NULL) return FSM_DPI_IGNORED;

    acc = pkt_info->acc;
    if (acc == NULL) return FSM_DPI_IGNORED;

    net_parser = pkt_info->parser;
    if (net_parser == NULL) return FSM_DPI_IGNORED;

    rec = &mgr->curr_arp_rec_processed;

    curr_state = get_arp_state(attr);

    switch (curr_state)
    {
        case ARP_REQ_SMAC:
        case ARP_RES_SMAC:
        {

            if (type != RTS_TYPE_BINARY)
            {
                LOGD("%s: value for %s should be a binary array", __func__, attr);
                goto reset_state_machine;
            }

            if (rec->next_state != curr_state && curr_state != ARP_RES_SMAC) goto wrong_state;

            rec->next_state = ARP_REQ_SIP;
            memcpy(&rec->s_mac, value, length);
            LOGT("%s: copied "PRI(os_macaddr_t) " next is %s",
                 __func__,FMT(os_macaddr_t, rec->s_mac),
                 arp_state_str[rec->next_state]);
            break;
        }

        case ARP_REQ_SIP:
        case ARP_RES_SIP:
        {
            char ipv4_addr[INET_ADDRSTRLEN];
            const char *res;

            if (type != RTS_TYPE_BINARY)
            {
                LOGD("%s: value for %s should be a binary array", __func__, attr);
                goto reset_state_machine;
            }

            if (rec->next_state != curr_state && curr_state != ARP_RES_SIP) goto wrong_state;

            rec->next_state = ARP_REQ_TMAC;
            rec->s_ip_addr = *(uint32_t *)value;
            res = inet_ntop(AF_INET, value, ipv4_addr, INET_ADDRSTRLEN);
            LOGT("%s: copied %s - next is %s",
                 __func__, (res != NULL ? ipv4_addr : arp_state_str[curr_state]),
                 arp_state_str[rec->next_state]);
            break;
        }

        case ARP_REQ_TMAC:
        case ARP_RES_TMAC:
        {

            if (type != RTS_TYPE_BINARY)
            {
                LOGD("%s: value for %s should be a binary array", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state && curr_state != ARP_RES_TMAC) goto wrong_state;

            rec->next_state = ARP_REQ_TIP;
            memcpy(&rec->t_mac, value, length);
            LOGT("%s: copied "PRI(os_macaddr_t) " next is %s",
                 __func__,FMT(os_macaddr_t, rec->t_mac),
                 arp_state_str[rec->next_state]);
            break;
        }

        case ARP_RES_TIP:
        case ARP_REQ_TIP:
        {
            char ipv4_addr[INET_ADDRSTRLEN];
            const char *res;

            if (type != RTS_TYPE_BINARY)
            {
                LOGD("%s: value for %s should be a binary array", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state && curr_state != ARP_RES_TIP) goto wrong_state;

            rec->next_state = END_ARP;
            rec->t_ip_addr = *(uint32_t *)value;
            res = inet_ntop(AF_INET, value, ipv4_addr, INET_ADDRSTRLEN);
            LOGT("%s: copied %s - next is %s",
                 __func__, (res != NULL ? ipv4_addr : arp_state_str[curr_state]),
                 arp_state_str[rec->next_state]);
            break;
        }

        case END_ARP:
        {
            if (type != RTS_TYPE_STRING)
            {
                LOGD("%s: value for %s should be a string", __func__, attr);
                goto reset_state_machine;
            }

            cmp = strncmp(value, arp_attr_value, length);
            if (cmp)
            {
                LOGD("%s: value for %s should be %s", __func__, attr, arp_attr_value);
                goto reset_state_machine;
            }

            /* Now we can process the record */
            ret = fsm_dpi_ndp_process_arp_record(session, acc, net_parser);
            fsm_dpi_arp_reset_state(session);
            acc->dpi_always = true;
            break;
        }

        default:
        {
            LOGD("%s: Unexpected attr '%s' (expected state '%s')",
                 __func__, attr, arp_state_str[rec->next_state]);
            goto reset_state_machine;
        }
    }

    return ret;

wrong_state:
    LOGD("%s: Failed when processing attr '%s' (expected state '%s')",
         __func__, attr, arp_state_str[rec->next_state]);
reset_state_machine:
    fsm_dpi_arp_reset_state(session);
    return -1;
}

static int
fsm_dpi_icmpv6_process_attr(struct fsm_session *session, const char *attr,
                            uint8_t type, uint16_t length, const void *value,
                            struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct net_header_parser *net_parser;
    struct net_md_stats_accumulator *acc;
    struct dpi_ndp_client *mgr;
    struct ndp_record *rec;
    struct ip6_hdr *ip6hdr;
    unsigned int curr_state;
    uint8_t *ip = NULL;
    int cmp;
    int ret = -1;

    mgr = fsm_dpi_ndp_get_mgr();

    if (!mgr->initialized) return -1;
    if (pkt_info == NULL) return FSM_DPI_IGNORED;

    acc = pkt_info->acc;
    if (acc == NULL) return FSM_DPI_IGNORED;

    net_parser = pkt_info->parser;
    if (net_parser == NULL) return FSM_DPI_IGNORED;


    rec = &mgr->curr_ndp_rec_processed;

    curr_state = get_ndp_state(attr);

    switch (curr_state)
    {
        case ICMPv6_NS_SIP:
        {
            char ipv6_addr[INET6_ADDRSTRLEN];
            const char *res;

            if (type != RTS_TYPE_BINARY)
            {
                LOGD("%s: value for %s should be a binary array", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;

            ip6hdr = net_header_get_ipv6_hdr(net_parser);

            rec->next_state = ICMPv6_NS_SMAC;
            ip = (uint8_t *)(&ip6hdr->ip6_src.s6_addr);
            memcpy(&rec->ip_addr, ip, sizeof(ip6hdr->ip6_src.s6_addr));
            res = inet_ntop(AF_INET6,rec->ip_addr, ipv6_addr, INET6_ADDRSTRLEN);
            LOGT("%s: srcip copied %s - next is %s",
                 __func__, (res != NULL ? ipv6_addr : ndp_state_str[curr_state]),
                 ndp_state_str[rec->next_state]);
            break;
        }

        case ICMPv6_NA_TIP:
        {
            char ipv6_addr[INET6_ADDRSTRLEN];
            const char *res;

            if (type != RTS_TYPE_BINARY)
            {
                LOGD("%s: value for %s should be a binary array", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state && curr_state != ICMPv6_NA_TIP) goto wrong_state;

            rec->next_state = ICMPv6_NA_TMAC;
            memcpy(rec->ip_addr, value, length);
            res = inet_ntop(AF_INET6, value, ipv6_addr, INET6_ADDRSTRLEN);
            LOGT("%s: copied %s - next is %s",
                 __func__, (res != NULL ? ipv6_addr : ndp_state_str[curr_state]),
                 ndp_state_str[rec->next_state]);
            break;
        }

        case ICMPv6_NS_SMAC:
        case ICMPv6_NA_TMAC:
        {

            if (type != RTS_TYPE_BINARY)
            {
                LOGD("%s: value for %s should be a binary array", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;

            rec->next_state = END_NDP;
            memcpy(&rec->mac, value, length);
            LOGT("%s: copied "PRI(os_macaddr_t) " next is %s",
                 __func__,FMT(os_macaddr_t, rec->mac),
                 ndp_state_str[rec->next_state]);
            break;
        }

        case END_NDP:
        {
            if (type != RTS_TYPE_STRING)
            {
                LOGD("%s: value for %s should be a string", __func__, attr);
                goto reset_state_machine;
            }
            cmp = strncmp(value, ndp_attr_value, length);
            if (cmp)
            {
                LOGD("%s: value for %s should be %s", __func__, attr, ndp_attr_value);
                goto reset_state_machine;
            }

            /* Now we can process the record */
            ret = fsm_dpi_ndp_process_icmpv6_record(session, acc, net_parser);
            fsm_dpi_ndp_reset_state(session);
            break;
        }

        default:
        {
            LOGD("%s: Unexpected attr '%s' (expected state '%s')",
                 __func__, attr, ndp_state_str[rec->next_state]);
            goto reset_state_machine;
        }
    }

    return ret;

wrong_state:
    LOGD("%s: Failed when processing attr '%s' (expected state '%s')",
         __func__, attr, ndp_state_str[rec->next_state]);
reset_state_machine:
    fsm_dpi_ndp_reset_state(session);
    return -1;
}

static int
fsm_dpi_ndp_identify_state(const char *attr, int type, int length, const char *value)
{
    int cmp = 0;

    if (type != RTS_TYPE_STRING) return 0;

    cmp = strncmp(value, arp_attr_value, length);
    if (!cmp)
    {
        if (strcmp(attr, "begin") == 0)
        {
            return BEGIN_ARP;
        }
    }

    cmp = strncmp(value, ndp_attr_value, length);
    if (!cmp)
    {
        if (strcmp(attr, "begin") == 0)
        {
            return BEGIN_NDP;
        }
    }
    return -1;
}

/**
 * @brief process a flow attribute
 *
 * @param session the fsm session
 * @param attr the attribute flow
 * @param value the attribute flow value
 * @param acc the flow
 */
int
fsm_dpi_ndp_process_attr(struct fsm_session *session, const char *attr,
                         uint8_t type, uint16_t length, const void *value,
                         struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct dpi_ndp_client *mgr;
    struct arp_record *a_rec;
    struct ndp_record *n_rec;
    int    init_state;
    int    action;

   /* Process the generic part (e.g., logging, include, exclude lists) */
    action = fsm_dpi_client_process_attr(session, attr, type, length, value, pkt_info);
    if (action == FSM_DPI_IGNORED) return action;

    mgr = fsm_dpi_ndp_get_mgr();
    if (!mgr->initialized) return -1;


    a_rec = &mgr->curr_arp_rec_processed;
    n_rec = &mgr->curr_ndp_rec_processed;

    if (a_rec->next_state != BEGIN_ARP)
    {
        return fsm_dpi_arp_process_attr(session, attr, type, length, value, pkt_info);
    }

    if (n_rec->next_state != BEGIN_NDP)
    {
        return fsm_dpi_icmpv6_process_attr(session, attr, type, length, value, pkt_info);
    }

    init_state = fsm_dpi_ndp_identify_state(attr, type, length, value);
    if (init_state == BEGIN_ARP)
    {
        a_rec->next_state = ARP_REQ_SMAC;
        LOGT("%s: start new ARP record - next is %s",
             __func__, arp_state_str[a_rec->next_state]);
    }
    else if (init_state == BEGIN_NDP)
    {
        n_rec->next_state = ICMPv6_NS_SIP;
        LOGT("%s: start new NDP record - next is %s",
             __func__, ndp_state_str[n_rec->next_state]);
    }

    return -1;
}


/**
 * @brief session initialization entry point
 *
 * Initializes the plugin generic fields of the session,
 * like the packet parsing handler and the periodic routines called
 * by fsm
 *
 * @param session pointer provided by fsm
 */
int
fsm_dpi_ndp_init(struct fsm_session *session)
{
    struct fsm_dpi_plugin_client_ops *client_ops;
    struct ndp_session *ndp_session;
    struct dpi_ndp_client *mgr;
    int ret;

    if (session == NULL) return -1;

    mgr = fsm_dpi_ndp_get_mgr();

    if (mgr->initialized) return 1;

    ds_tree_init(&mgr->fsm_sessions, ndp_session_cmp,
                 struct ndp_session, next);
    mgr->initialized = true;

    /* Look up the ndp session */
    ndp_session = fsm_dpi_ndp_get_session(session);
    if (ndp_session == NULL)
    {
        LOGE("%s: could not allocate dns parser", __func__);
        return -1;
    }

    if (ndp_session->initialized) return 0;

    ndp_session->entry.ipaddr = CALLOC(1, sizeof(*ndp_session->entry.ipaddr));
    if (ndp_session->entry.ipaddr == NULL)
    {
        LOGE("%s: could not allocate arp ip storage", __func__);
        goto error;
    }

    /* Initialize generic client */
    ret = fsm_dpi_client_init(session);
    if (ret != 0)
    {
        goto error;
    }

    /* And now all the NDP specific calls */
    session->ops.update = fsm_dpi_ndp_update;
    session->ops.periodic = fsm_dpi_ndp_periodic;
    session->ops.exit = fsm_dpi_ndp_exit;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    client_ops->process_attr = fsm_dpi_ndp_process_attr;
    FSM_FN_MAP(fsm_dpi_ndp_process_attr);

    /* Fetch the specific updates for this client plugin */
    session->ops.update(session);

    ndp_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    return 0;
error:
    fsm_dpi_ndp_delete_session(session);
    return -1;
}


/*
 * Provided for compatibility
 */
int
dpi_ndp_plugin_init(struct fsm_session *session)
{
    return fsm_dpi_ndp_init(session);
}


void
fsm_dpi_ndp_exit(struct fsm_session *session)
{
    struct dpi_ndp_client *mgr;

    mgr = fsm_dpi_ndp_get_mgr();
    if (!mgr->initialized) return;

    /* Free the generic client */
    fsm_dpi_client_exit(session);
    fsm_dpi_ndp_delete_session(session);
    return;
}


/**
 * @brief get a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct ndp_session *
fsm_dpi_ndp_get_session(struct fsm_session *session)
{
    struct dpi_ndp_client *mgr;
    struct ndp_session *n_session;
    ds_tree_t *sessions;

    mgr = fsm_dpi_ndp_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session != NULL) return n_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    n_session = CALLOC(1, sizeof(struct ndp_session));
    if (n_session == NULL) return NULL;

    n_session->initialized = false;
    ds_tree_insert(sessions, n_session, session);

    return n_session;
}
