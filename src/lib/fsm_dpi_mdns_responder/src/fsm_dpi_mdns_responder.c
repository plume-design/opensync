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
    [BEGIN_MDNS] = "begin",
    [MDNS_QNAME] = "mdns.query.question.qname",
    [MDNS_RESP_TYPE] = "mdns.query.question.qtype",
    [MDNS_QUNICAST] = "mdns.query.question.qunicast",
    [END_MDNS] = "end",
};

const char *mdns_attr_value = "mdns.query.question";

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

    GET_MDNS_STATE(attribute, MDNS_QNAME);
    GET_MDNS_STATE(attribute, MDNS_RESP_TYPE);
    GET_MDNS_STATE(attribute, MDNS_QUNICAST);
    GET_MDNS_STATE(attribute, BEGIN_MDNS);
    GET_MDNS_STATE(attribute, END_MDNS);

    return UNDEFINED;
#undef GET_MDNS_STATE
}

/**
  * @brief Frees a mdns session
  *
  * @param n_session the mdns session to free
  */
void
fsm_dpi_mdns_free_session(struct mdns_resp_session *n_session)
{
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
    struct mdns_resp_session *n_session;
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
    struct mdns_resp_session *n_session;
    struct dpi_mdns_resp_client *mgr;
    struct mdns_record *rec;
    ds_tree_t *sessions;

    mgr = fsm_dpi_mdns_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session == NULL) return;

    rec = &mgr->curr_mdns_rec_processed;
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
mdns_resp_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}


#define FSM_MDNS_DEFAULT_TTL (36*60*60)

bool
fsm_dpi_mdns_set_srcip(struct mdns_resp_session *md_session)
{
    struct dpi_mdns_resp_client *mgr = fsm_dpi_mdns_get_mgr();
    struct fsm_session  *session = NULL;
    char   *srcip = NULL;

    if (!md_session || !mgr) return false;

    session = md_session->session;
    if (session->ops.get_config != NULL)
    {
        srcip = session->ops.get_config(session, "mdns_src_ip");
    }

    if (!srcip) return false;

    if (mgr->srcip && !strcmp(mgr->srcip, srcip))
    {
        LOGT("mdns_daemon: No change in the sip.");
        return false;
    }
    mgr->srcip = srcip;
    return true;
}

bool
fsm_dpi_mdns_set_intf(struct mdns_resp_session *md_session)
{
    struct dpi_mdns_resp_client *mgr = fsm_dpi_mdns_get_mgr();
    struct fsm_session  *session = NULL;
    bool   tx_change = true;
    char   *tx_if;

    if (!md_session || !mgr) return false;

    session = md_session->session;

    if (mgr->txintf && !strcmp(mgr->txintf, session->tx_intf))
    {
        LOGT("mdns_daemon: No change in the tx interface.");
        tx_change = false;
    }
    else
    {
        tx_if = STRDUP(session->tx_intf);
        mgr->txintf = tx_if;
    }

    return tx_change;
}


/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm url session to update
 */
void
fsm_dpi_mdns_update(struct fsm_session *session)
{
    struct mdns_resp_session *md_session;
    struct dpi_mdns_resp_client *mgr;
    ds_tree_t *sessions;
    bool ip_changed = false;
    bool tx_intf_changed = false;

    mgr = fsm_dpi_mdns_get_mgr();
    if (!mgr) return;

    sessions = &mgr->fsm_sessions;

    md_session = ds_tree_find(sessions, session);
    if (md_session == NULL) return;

    /* Generic config first */
    fsm_dpi_client_update(session);

    LOGD("%s: Updating mDNS responder config", __func__);

    // Get the latest mdns sip.
    ip_changed = fsm_dpi_mdns_set_srcip(md_session);
    if (ip_changed) LOGT("%s: mdns_src_ip changed to '%s'", __func__, mgr->srcip);

    // Get the latest mdns tx.
    tx_intf_changed = fsm_dpi_mdns_set_intf(md_session);
    if (tx_intf_changed) LOGT("%s: mdns tx intf changed to '%s'", __func__, mgr->txintf);

    mgr->mcast_fd = fsm_dpi_mdns_create_mcastv4_socket();
}


void
fsm_dpi_mdns_periodic(struct fsm_session *session)
{
    struct mdns_resp_session *n_session;
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
        /* Nothing specific to be done here.*/
    }
}

bool
fsm_dpi_mdns_process_record(struct fsm_session *session,
                            struct net_md_stats_accumulator *acc,
                            struct net_header_parser *net_parser)
{
    struct fsm_dpi_mdns_service *service;
    struct dpi_mdns_resp_client *mgr;
    struct mdns_record *rec;
    uint16_t ethertype;
    char *qname;
    char *name;
    bool rc = false;

    mgr = fsm_dpi_mdns_get_mgr();
    if (!mgr->initialized) return false;

    ethertype = net_header_get_ethertype(net_parser);
    if (ethertype == ETH_P_IPV6)
    {
        LOGT("%s: MDNS responder does not support IPv6", __func__);
        return rc;
    }

    ds_tree_t *services = fsm_dpi_mdns_get_services();

    rec = &mgr->curr_mdns_rec_processed;

    if (strlen(rec->qname) == 0) return rc;

    qname = STRDUP(rec->qname);
    LOGT("%s: Processing dpi mDNS request for %s expecting %scast response", __func__,
         rec->qname, rec->unicast ? "uni": "multi");

    name = strtok(qname, ".");
    if (name == NULL)
    {
        LOGT("%s: dot delimiter not found in %s", __func__, qname);
        goto err_qname;
    }

    service = ds_tree_find(services, name);
    if(!service)
    {
        LOGT("%s: Couldn't find record for qname[%s]",__func__,rec->qname);
        goto err_qname;
    }

    rc  = fsm_dpi_mdns_send_response(service, rec->unicast ? true : false, net_parser);
    if (!rc)
    {
        LOGE("%s: Couldn't send mdns response for qname[%s]", __func__, rec->qname);
        goto err_qname;
    }

err_qname:
    FREE(qname);

    return rc;
}


int
fsm_dpi_mdns_process_attr(struct fsm_session *session, const char *attr,
                          uint8_t type, uint16_t length, const void *value,
                          struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct net_header_parser *net_parser;
    struct net_md_stats_accumulator *acc;
    struct dpi_mdns_resp_client *mgr;
    struct mdns_record *rec;
    unsigned int curr_state;
    int cmp;
    int ret = -1;

    mgr = fsm_dpi_mdns_get_mgr();

    if (!mgr->initialized) return -1;
    if (pkt_info == NULL) return FSM_DPI_IGNORED;

    acc = pkt_info->acc;
    if (acc == NULL) return FSM_DPI_IGNORED;

    net_parser = pkt_info->parser;
    if (net_parser == NULL) return FSM_DPI_IGNORED;


    rec = &mgr->curr_mdns_rec_processed;

    curr_state = get_mdns_state(attr);

    switch (curr_state)
    {
        case BEGIN_MDNS:
        {
            if (type != RTS_TYPE_STRING)
            {
                LOGD("%s: value for %s should be a string", __func__, attr);
                goto reset_state_machine;
            }
            cmp = strncmp(value, mdns_attr_value, length);
            if (cmp)
            {
                LOGD("%s: value for %s should be %s", __func__, attr, mdns_attr_value);
                goto reset_state_machine;
           }
           if (rec->next_state != UNDEFINED && rec->next_state != curr_state) goto wrong_state;
           fsm_dpi_mdns_reset_state(session);
           rec->next_state = MDNS_QNAME;
           LOGT("%s: start new mDNS record - next is %s",
                __func__, mdns_state_str[rec->next_state]);
           break;
        }

        case MDNS_QNAME:
        {
            if (type != RTS_TYPE_STRING)
            {
                LOGD("%s: value for %s should be a string", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;

            STRSCPY_LEN(rec->qname, value, length);
            rec->next_state = MDNS_RESP_TYPE;
            LOGT("%s: copied %s = %s - next is %s",
                 __func__, mdns_state_str[curr_state], rec->qname,
                 mdns_state_str[rec->next_state]);
           break;
        }
        case MDNS_RESP_TYPE:
        {
            if (type != RTS_TYPE_NUMBER)
            {
                LOGD("%s: value for %s should be a number", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;

            rec->qtype = *(int64_t *)value;
            rec->next_state = MDNS_QUNICAST;
            LOGT("%s: copied %s = %u - next is %s",
                 __func__, mdns_state_str[curr_state], rec->qtype,
                 mdns_state_str[rec->next_state]);
            break;
        }
        case MDNS_QUNICAST:
        {
            if (type != RTS_TYPE_NUMBER)
            {
                LOGD("%s: value for %s should be a number", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;

            rec->unicast = *(bool *)value;
            rec->next_state = END_MDNS;
            LOGT("%s: copied %s = %d - next is %s",
                 __func__, mdns_state_str[curr_state], rec->unicast,
                 mdns_state_str[rec->next_state]);
            break;
        }
        case END_MDNS:
        {
            if (type != RTS_TYPE_STRING)
            {
                LOGD("%s: value for %s should be a string", __func__, attr);
                goto reset_state_machine;
            }
            cmp = strncmp(value, mdns_attr_value, length);
            if (cmp)
            {
                LOGD("%s: value for %s should be %s", __func__, attr, mdns_attr_value);
                goto reset_state_machine;
            }
            if (rec->next_state != END_MDNS) goto wrong_state;

            rec->next_state = BEGIN_MDNS;

            /* Now we can process the record */
            ret = fsm_dpi_mdns_process_record(session, acc, net_parser);
            acc->dpi_always = true;
            break;
        }

        default:
        {
            LOGD("%s: Unexpected attr '%s' (expected state '%s')",
                 __func__, attr, mdns_state_str[rec->next_state]);
            goto reset_state_machine;
        }
    }

    return ret;

wrong_state:
    LOGD("%s: Failed when processing attr '%s' (expected state '%s')",
         __func__, attr, mdns_state_str[rec->next_state]);
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
    struct mdns_resp_session *mdns_resp_session;
    struct dpi_mdns_resp_client *mgr;
    int ret;

    if (session == NULL) return -1;

    mgr = fsm_dpi_mdns_get_mgr();

    if (mgr->initialized) return 1;

    ds_tree_init(&mgr->fsm_sessions, mdns_resp_session_cmp,
                 struct mdns_resp_session, next);
    mgr->initialized = true;

    /* Look up the mdns session */
    mdns_resp_session = fsm_dpi_mdns_get_session(session);
    if (mdns_resp_session == NULL)
    {
        LOGE("%s: could not allocate dns parser", __func__);
        return -1;
    }

    if (mdns_resp_session->initialized) return 0;

    /* Initialize generic client */
    ret = fsm_dpi_client_init(session);
    if (ret != 0)
    {
        goto error;
    }

    /* And now all the mDNS specific calls */
    session->ops.update = fsm_dpi_mdns_update;
    session->ops.periodic = fsm_dpi_mdns_periodic;
    session->ops.exit = fsm_dpi_mdns_exit;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    client_ops->process_attr = fsm_dpi_mdns_process_attr;
    FSM_FN_MAP(fsm_dpi_mdns_process_attr);
    mdns_resp_session->session = session;

    /* Fetch the specific updates for this client plugin */
    session->ops.update(session);

    /* Initialize Service_Announcement tables */
    fsm_dpi_mdns_ovsdb_init();
    mdns_resp_session->initialized = true;

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

    fsm_dpi_mdns_ovsdb_exit();
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
struct mdns_resp_session *
fsm_dpi_mdns_get_session(struct fsm_session *session)
{
    struct dpi_mdns_resp_client *mgr;
    struct mdns_resp_session *n_session;
    ds_tree_t *sessions;

    mgr = fsm_dpi_mdns_get_mgr();
    sessions = &mgr->fsm_sessions;

    n_session = ds_tree_find(sessions, session);
    if (n_session != NULL) return n_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    n_session = CALLOC(1, sizeof(struct mdns_resp_session));
    if (n_session == NULL) return NULL;

    n_session->initialized = false;
    ds_tree_insert(sessions, n_session, session);

    return n_session;
}
