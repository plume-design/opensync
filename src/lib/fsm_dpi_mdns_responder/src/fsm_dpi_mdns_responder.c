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

#include "fsm_dpi_mdns_responder.h"

static struct dpi_mdns_resp_client main_data =
{
    .initialized = false,
    .curr_mdns_rec_processed.next_state =  BEGIN_MDNS,
};

struct dpi_mdns_resp_client *
fsm_dpi_mdns_get_mgr(void)
{
    return &main_data;
}

const char * const mdns_state_str[] =
{
    [UNDEFINED] = "undefined",
    [BEGIN] = "begin",
    [END] = "end",
};

const char *mdns_attr_value = "mdns";

static enum mdns_state
get_mdns_state(const char *attribute)
{
#define GET_MDNS_STATE(attr, x)                \
    do                                         \
    {                                          \
        int cmp;                               \
        cmp = strcmp(attr, mdns_state_str[x]); \
        if (cmp == 0) return x;                \
    }                                          \
    while (0)

    GET_MDNS_STATE(attribute, MDSN_QNAME);
    GET_MDNS_STATE(attribute, MDNS_UNICAST);
    GET_MDNS_STATE(attribute, END);

    return UNDEFINED;
#undef GET_MDNS_STATE
}

/**
  * @brief Frees a mdns session
  *
  * @param n_session the mdns session to free
  */
void
fsm_dpi_mdns_free_session(struct mdns_session *n_session)
{
    FREE(n_session->entry.ipaddr);
    FREE(n_session);
}


/**
  * @brief deletes a session
  *
  * @param session the fsm session keying the mdns session to delete
  */
void
fsm_dpi_mdns_delete_session(struct fsm_session *session)
{
    struct mdns_session *n_session;
    struct dpi_mdns_resp_client *mgr;
    ds_tree_t *sessions;

    mgr = fsm_dpi_mdns_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, n_session);
    fsm_dpi_mdns_free_session(n_session);

    return;
}

void
fsm_dpi_mdns_reset_state(struct fsm_session *session)
{
    struct mdns_session *n_session;
    struct neighbour_entry *entry;
    struct sockaddr_storage *ip;
    struct dpi_mdns_resp_client *mgr;
    struct ndp_record *rec;
    ds_tree_t *sessions;

    mgr = fsm_dpi_mdns_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return;

    entry = &n_session->entry;
    ip = entry->ipaddr;
    MEMZERO(*entry);
    entry->ipaddr = ip;

    rec = &mgr->curr_ndp_rec_processed;
    MEMZERO(*rec);

    rec->next_state = BEGIN_MDNS;
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
mdns_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}


#define FSM_MDNS_DEFAULT_TTL (36*60*60)
/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm url session to update
 */
void
fsm_dpi_mdns_update(struct fsm_session *session)
{
    struct mdns_session *n_session;
    struct dpi_mdns_resp_client *mgr;
    ds_tree_t *sessions;
    unsigned long ttl;
    char *str_ttl;

    /* Generic config first */
    fsm_dpi_client_update(session);

    mgr = fsm_dpi_mdns_get_mgr();
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
fsm_dpi_mdns_periodic(struct fsm_session *session)
{
    struct mdns_session *n_session;
    struct dpi_mdns_resp_client *mgr;
    ds_tree_t *sessions;
    bool need_periodic;

    mgr = fsm_dpi_mdns_get_mgr();
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
fsm_dpi_mdns_process_record(struct fsm_session *session,
                            struct net_md_stats_accumulator *acc,
                            struct net_header_parser *net_parser)
{
    struct mdns_session *n_session;
    struct neighbour_entry *entry;
    struct dpi_mdns_resp_client *mgr;
    ds_tree_t *sessions;
    struct ndp_record *rec;

    mgr = fsm_dpi_mdns_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return false;

    rec = &mgr->curr_ndp_rec_processed;

    entry = &n_session->entry;

    populate_neigh_table_with_rec(entry, rec->ip_addr, &rec->mac, AF_INET6);

    entry->cache_valid_ts = n_session->timestamp;
    entry->source = FSM_NDP;
    neigh_table_add(entry);

    return true;
}


static int
fsm_dpi_mdns_process_attr(struct fsm_session *session, const char *attr,
                          uint8_t type, uint16_t length, const void *value,
                          struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct net_header_parser *net_parser;
    struct net_md_stats_accumulator *acc;
    struct dpi_mdns_resp_client *mgr;
    struct ndp_record *rec;
    struct ip6_hdr *ip6hdr;
    unsigned int curr_state;
    uint8_t *ip = NULL;
    int cmp;
    int ret = -1;

    mgr = fsm_dpi_mdns_get_mgr();

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
        case BEGIN_MDNS:

        case END_MDNS:
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
            ret = fsm_dpi_mdns_process_icmpv6_record(session, acc, net_parser);
            fsm_dpi_mdns_reset_state(session);
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
    fsm_dpi_mdns_reset_state(session);
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
fsm_dpi_mdns_init(struct fsm_session *session)
{
    struct fsm_dpi_plugin_client_ops *client_ops;
    struct mdns_session *mdns_session;
    struct dpi_mdns_resp_client *mgr;
    int ret;

    if (session == NULL) return -1;

    mgr = fsm_dpi_mdns_get_mgr();

    if (mgr->initialized) return 1;

    ds_tree_init(&mgr->fsm_sessions, mdns_session_cmp,
                 struct mdns_session, next);
    mgr->initialized = true;

    /* Look up the mdns session */
    mdns_session = fsm_dpi_mdns_get_session(session);
    if (mdns_session == NULL)
    {
        LOGE("%s: could not allocate dns parser", __func__);
        return -1;
    }

    if (mdns_session->initialized) return 0;

    mdns_session->entry.ipaddr = CALLOC(1, sizeof(*mdns_session->entry.ipaddr));
    if (mdns_session->entry.ipaddr == NULL)
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
    session->ops.update = fsm_dpi_mdns_update;
    session->ops.periodic = fsm_dpi_mdns_periodic;
    session->ops.exit = fsm_dpi_mdns_exit;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    client_ops->process_attr = fsm_dpi_mdns_process_attr;

    /* Fetch the specific updates for this client plugin */
    session->ops.update(session);

    mdns_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    return 0;
error:
    fsm_dpi_mdns_delete_session(session);
    return -1;
}


/*
 * Provided for compatibility
 */
int
dpi_mdns_responder_plugin_init(struct fsm_session *session)
{
    return fsm_dpi_mdns_init(session);
}


void
fsm_dpi_mdns_exit(struct fsm_session *session)
{
    struct dpi_mdns_resp_client *mgr;

    mgr = fsm_dpi_mdns_get_mgr();
    if (!mgr->initialized) return;

    /* Free the generic client */
    fsm_dpi_client_exit(session);
    fsm_dpi_mdns_delete_session(session);
    return;
}


/**
 * @brief get a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct mdns_session *
fsm_dpi_mdns_get_session(struct fsm_session *session)
{
    struct dpi_mdns_resp_client *mgr;
    struct mdns_session *n_session;
    ds_tree_t *sessions;

    mgr = fsm_dpi_mdns_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session != NULL) return n_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    n_session = CALLOC(1, sizeof(struct mdns_session));
    if (n_session == NULL) return NULL;

    n_session->initialized = false;
    ds_tree_insert(sessions, n_session, session);

    return n_session;
}
