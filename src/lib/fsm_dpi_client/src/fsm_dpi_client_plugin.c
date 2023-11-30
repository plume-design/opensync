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

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ds_tree.h"
#include "fsm.h"
#include "fsm_dpi_client_plugin.h"
#include "log.h"
#include "memutil.h"
#include "network_metadata_report.h"
#include "os.h"
#include "os_time.h"
#include "os_types.h"
#include "policy_tags.h"
#include "util.h"

/* Forward declaration */
static bool find_mac_in_tag(os_macaddr_t *mac, char *tag);

static struct fsm_dpi_client_cache cache_mgr =
{
    .initialized = false,
};

time_t FSM_DPI_CLIENT_CHECK_TTL = (2 * 60);


struct fsm_dpi_client_cache *
fsm_dpi_client_get_mgr(void)
{
    return &cache_mgr;
}


static bool is_mac_any_cast(os_macaddr_t *mac)
{
    os_macaddr_t *dmac = mac;

    if (!mac) return false;
    if ((dmac->addr[0] & 0x01) == 0x01) return true;
    return false;
}

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
int
fsm_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
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
fsm_dpi_client_init(struct fsm_session *session)
{
    struct fsm_dpi_client_session *client_session;
    struct fsm_dpi_plugin_client_ops *client_ops;
    struct fsm_dpi_client_cache *mgr;

    if (session == NULL) return -1;
    if (session->node_id == NULL) return -1;
    if (session->location_id == NULL) return -1;
    if (session->p_ops == NULL) return -1;

    mgr = fsm_dpi_client_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(&mgr->fsm_sessions, fsm_session_cmp,
                     struct fsm_dpi_client_session, session_node);
        mgr->initialized = true;
    }

    /* Look up the fsm_dpi_client session */
    client_session = fsm_dpi_client_lookup_session(session);
    if (client_session == NULL)
    {
        LOGE("%s: could not allocate the fsm url session %s",
             __func__, session->name);
        return -1;
    }

    /* Bail if the session is already initialized */
    if (client_session->initialized) return 1;

    /* Set the fsm session */
    session->ops.update = fsm_dpi_client_update;     /* Potentially overridden */
    session->ops.periodic = fsm_dpi_client_periodic; /* Potentially overridden */
    session->ops.exit = fsm_dpi_client_exit;         /* Potentially overridden */
    session->handler_ctxt = client_session;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    client_ops->process_attr = fsm_dpi_client_process_attr; /* Potentially overridden */

    /* Wrap up the session initialization */
    client_session->session = session;

    /* Fetch the generic configuration */
    session->ops.update(session);

    client_session->initialized = true;

    LOGD("%s: Added session '%s'", __func__, session->name);

    return 0;
}

/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
fsm_dpi_client_exit(struct fsm_session *session)
{
    struct fsm_dpi_client_cache *mgr;

    mgr = fsm_dpi_client_get_mgr();
    if (!mgr->initialized) return;

    fsm_dpi_client_delete_session(session);
}

/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct fsm_dpi_client_session *
fsm_dpi_client_lookup_session(struct fsm_session *session)
{
    struct fsm_dpi_client_session *u_session;
    struct fsm_dpi_client_cache *mgr;
    ds_tree_t *sessions;

    mgr = fsm_dpi_client_get_mgr();
    sessions = &mgr->fsm_sessions;

    u_session = ds_tree_find(sessions, session);
    if (u_session != NULL) return u_session;

    LOGD("%s: Adding new session '%s'", __func__, session->name);
    u_session = CALLOC(1, sizeof(*u_session));
    if (u_session == NULL) return NULL;
    u_session->ttl = FSM_DPI_CLIENT_CHECK_TTL;

    ds_tree_insert(sessions, u_session, session);

    return u_session;
}

/**
 * @brief Frees a fsm url session
 *
 * @param u_session the fsm url session to free
 */
void
fsm_dpi_client_free_session(struct fsm_dpi_client_session *u_session)
{
    FREE(u_session);
}

/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the fsm session session to delete
 */
void
fsm_dpi_client_delete_session(struct fsm_session *session)
{
    struct fsm_dpi_client_session *u_session;
    struct fsm_dpi_client_cache *mgr;
    ds_tree_t *sessions;

    mgr = fsm_dpi_client_get_mgr();
    sessions = &mgr->fsm_sessions;

    u_session = ds_tree_find(sessions, session);
    if (u_session == NULL) return;

    LOGD("%s: removing session '%s'", __func__, session->name);
    ds_tree_remove(sessions, u_session);
    fsm_dpi_client_free_session(u_session);
}

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm url session to update
 */
void
fsm_dpi_client_update(struct fsm_session *session)
{
    struct fsm_dpi_client_session *u_session;

    LOGD("%s: Updating config for %s", __func__, session->name);
    u_session = session->handler_ctxt;
    if (u_session == NULL) return;

    u_session->included_devices = session->ops.get_config(session, "included_devices");
    u_session->excluded_devices = session->ops.get_config(session, "excluded_devices");
}

/**
 * @brief make the TTL configurable
 */
void
fsm_dpi_client_set_ttl(struct fsm_session *session, time_t t)
{
    struct fsm_dpi_client_session *u_session;

    u_session = session->handler_ctxt;
    if (u_session) u_session->ttl = t;
}

bool
fsm_dpi_client_periodic_check(struct fsm_session *session)
{
    struct fsm_dpi_client_session *u_session;
    time_t cmp_clean;
    time_t now;

    u_session = session->handler_ctxt;
    if (u_session == NULL) return false;

    now = time(NULL);
    cmp_clean = now - u_session->timestamp;
    if (cmp_clean < u_session->ttl) return false;

    u_session->timestamp = now;
    return true;
}

/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm url session to uprocess
 */
void
fsm_dpi_client_periodic(struct fsm_session *session)
{
    time_t now;
    char time_str[TIME_STR_SZ];

    if (fsm_dpi_client_periodic_check(session))
    {
        now = time(NULL);
        if (!time_to_str(now, time_str, TIME_STR_SZ))
        {
            snprintf(time_str, TIME_STR_SZ, "TIME_ERR");
        }
        LOGD("%s: Triggers at %s", __func__, time_str);
    }
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
fsm_dpi_client_process_attr(struct fsm_session *session, const char *attr,
                            uint8_t type, uint16_t length, const void *value,
                            struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct fsm_dpi_client_session *u_session;
    struct net_md_stats_accumulator *acc;
    struct net_md_flow_info info;
    struct net_md_flow_key *key;
    struct fsm_session *service;
    bool has_excluded;
    bool has_included;
    char *value_str;
    size_t sz;
    char *val;
    bool act;
    bool rc;

    if (session == NULL) return FSM_DPI_IGNORED;

    service = session->service;
    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
    {
        LOGT("%s: service provider: %s", __func__,
             service ? service->name : "None");
        switch (type)
        {
            case RTS_TYPE_STRING:
            {
                value_str = CALLOC(1, length + 1);
                strncpy(value_str, value, length);
                LOGD("%s: %s: attribute: %s, value %s", __func__,
                     session->name, attr, value_str);
                FREE(value_str);
                break;
            }

            case RTS_TYPE_NUMBER:
            {
                LOGD("%s: %s: attribute: %s, value %" PRId64, __func__,
                     session->name, attr, *(int64_t *)value);
                break;
            }

            case RTS_TYPE_BINARY:
            {
                sz = 2 * length + 1;
                val = CALLOC(1, sz);
                if (val == NULL) return FSM_DPI_IGNORED;
                (void)bin2hex(value, length, val, sz);
                LOGD("%s: %s: attribute: %s, value %s", __func__,
                     session->name, attr, val);
                FREE(val);
                break;
            }

            default:
            {
                LOGD("%s: %s: attribute: %s has unknown type %d", __func__,
                     session->name, attr, type);
                return FSM_DPI_IGNORED;
            }
        }
    }

    u_session = session->handler_ctxt;
    if (u_session == NULL) return FSM_DPI_IGNORED;

    if (pkt_info == NULL) return FSM_DPI_IGNORED;

    acc = pkt_info->acc;
    if (acc == NULL) return FSM_DPI_IGNORED;

    key = acc->key;
    if (key == NULL)
    {
        LOGT("%s: No key for accumulator", __func__);
        return FSM_DPI_IGNORED;
    }

    // Multicast and broadcast packets always need to be inspected
    if (is_mac_any_cast(key->dmac) == true)
    {
        return FSM_DPI_INSPECT;
    }

    if (acc->originator == NET_MD_ACC_UNKNOWN_ORIGINATOR)
    {
        LOGT("%s: No originator", __func__);
        return FSM_DPI_IGNORED;
    }

    MEMZERO(info);
    rc = net_md_get_flow_info(acc, &info);
    if (!rc)
    {
        LOGT("%s: No flow information", __func__);
        return FSM_DPI_IGNORED;
    }

    has_excluded = (u_session->excluded_devices != NULL);
    has_included = (u_session->included_devices != NULL);
    if (has_excluded || has_included)
    {
        if (info.local_mac == NULL) return FSM_DPI_IGNORED;
        if (has_excluded)
        {
            act = !find_mac_in_tag(info.local_mac,
                                   u_session->excluded_devices);
            if (!act) return FSM_DPI_IGNORED;
        }
        if (has_included)
        {
            act = find_mac_in_tag(info.local_mac,
                                  u_session->included_devices);
            if (!act) return FSM_DPI_IGNORED;
        }
    }

    pkt_info->tag_flow = true;
    return FSM_DPI_INSPECT;
}

/**
 * @brief check if a MAC address belongs to a given tag or matches a value
 *
 * @param mac the MAC address to check
 * @param tag an opensync tag name or the string representation of a MAC address
 * @return true if the mac matches the value, false otherwise
 */
static bool
find_mac_in_tag(os_macaddr_t *mac, char *tag)
{
    char mac_s[32] = { 0 };
    bool rc;
    int ret;

    if (tag == NULL) return false;
    if (mac == NULL) return false;

    snprintf(mac_s, sizeof(mac_s), PRI_os_macaddr_lower_t, FMT_os_macaddr_pt(mac));

    rc = om_tag_in(mac_s, tag);
    if (rc) return true;

    ret = strncmp(mac_s, tag, strlen(mac_s));
    return (ret == 0);
}
