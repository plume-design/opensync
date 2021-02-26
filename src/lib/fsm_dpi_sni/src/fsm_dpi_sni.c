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

#include "assert.h"
#include "const.h"
#include "ds_tree.h"
#include "json_mqtt.h"
#include "log.h"
#include "fsm_dpi_sni.h"
#include "network_metadata_report.h"
#include "policy_tags.h"

static struct fsm_dpi_sni_cache
cache_mgr =
{
    .initialized = false,
};


struct fsm_dpi_sni_cache *
fsm_dpi_sni_get_mgr(void)
{
    return &cache_mgr;
}


static const struct fsm_req_type
{
    char *req_str_type;
    int req_type;
} req_map[] =
{
    {
        .req_str_type = "http.host",
        .req_type = FSM_HOST_REQ,
    },
    {
        .req_str_type = "tls.sni",
        .req_type = FSM_SNI_REQ,
    },
    {
        .req_str_type = "http.url",
        .req_type = FSM_URL_REQ,
    },
    {
        .req_str_type = "tag",
        .req_type = FSM_APP_REQ,
    }
};


/**
 * @brief sets the request type based on the flow attribute
 *
 * @param the ovsdb configuration
 * @return an integer representing the type of service
 */
static int
fsm_req_type(char *attr)
{
    const struct fsm_req_type *map;
    size_t nelems;
    size_t i;
    int cmp;

    /* Walk the known types */
    nelems = (sizeof(req_map) / sizeof(req_map[0]));
    map = req_map;
    for (i = 0; i < nelems; i++)
    {
        cmp = strcmp(attr, map->req_str_type);
        if (!cmp) return map->req_type;

        map++;
    }

    return FSM_UNKNOWN_REQ_TYPE;
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
fsm_session_cmp(void *a, void *b)
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
 * by fsm
 *
 * @param session pointer provided by fsm
 */
int
dpi_sni_plugin_init(struct fsm_session *session)
{
    struct fsm_dpi_plugin_client_ops *client_ops;
    struct fsm_dpi_sni_session *fsm_dpi_sni_session;
    struct fsm_dpi_sni_cache *mgr;

    if (session == NULL) return -1;

    mgr = fsm_dpi_sni_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(&mgr->fsm_sessions, fsm_session_cmp,
                     struct fsm_dpi_sni_session, session_node);
        mgr->initialized = true;
    }

    /* Look up the fsm_dpi_sni session */
    fsm_dpi_sni_session = fsm_dpi_sni_lookup_session(session);
    if (fsm_dpi_sni_session == NULL)
    {
        LOGE("%s: could not allocate the fsm url session %s", __func__,
             session->name);

        return -1;
    }

    /* Bail if the session is already initialized */
    if (fsm_dpi_sni_session->initialized) return 0;

    /* Set the fsm session */
    session->ops.update = fsm_dpi_sni_plugin_update;
    session->ops.periodic = fsm_dpi_sni_plugin_periodic;
    session->ops.exit = fsm_dpi_sni_plugin_exit;
    session->handler_ctxt = fsm_dpi_sni_session;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    client_ops->process_attr = fsm_dpi_sni_process_attr;

    /* Wrap up the session initialization */
    fsm_dpi_sni_session->session = session;
    fsm_dpi_sni_plugin_update(session);

    fsm_dpi_sni_session->initialized = true;
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
fsm_dpi_sni_plugin_exit(struct fsm_session *session)
{
    struct fsm_dpi_sni_cache *mgr;

    mgr = fsm_dpi_sni_get_mgr();
    if (!mgr->initialized) return;

    fsm_dpi_sni_delete_session(session);
    return;
}


/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct fsm_dpi_sni_session *
fsm_dpi_sni_lookup_session(struct fsm_session *session)
{
    struct fsm_dpi_sni_session *u_session;
    struct fsm_dpi_sni_cache *mgr;
    ds_tree_t *sessions;

    mgr = fsm_dpi_sni_get_mgr();
    sessions = &mgr->fsm_sessions;

    u_session = ds_tree_find(sessions, session);
    if (u_session != NULL) return u_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    u_session = calloc(1, sizeof(*u_session));
    if (u_session == NULL) return NULL;

    ds_tree_insert(sessions, u_session, session);

    return u_session;
}


/**
 * @brief Frees a fsm url session
 *
 * @param u_session the fsm url session to free
 */
void
fsm_dpi_sni_free_session(struct fsm_dpi_sni_session *u_session)
{
    free(u_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the fsm session session to delete
 */
void
fsm_dpi_sni_delete_session(struct fsm_session *session)
{
    struct fsm_dpi_sni_session *u_session;
    struct fsm_dpi_sni_cache *mgr;
    ds_tree_t *sessions;

    mgr = fsm_dpi_sni_get_mgr();
    sessions = &mgr->fsm_sessions;

    u_session = ds_tree_find(sessions, session);
    if (u_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, u_session);
    fsm_dpi_sni_free_session(u_session);

    return;
}


#define FSM_DPI_SNI_CHECK_TTL (2*60)
/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm url session to uprocess
 */
void
fsm_dpi_sni_plugin_periodic(struct fsm_session *session)
{
    struct fsm_dpi_sni_session *u_session;
    double cmp_clean;
    time_t now;

    u_session = session->handler_ctxt;
    if (u_session == NULL) return;

    now = time(NULL);
    cmp_clean = now - u_session->timestamp;
    if (cmp_clean < FSM_DPI_SNI_CHECK_TTL) return;

    u_session->timestamp = now;
}


/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm url session to update
 */
void
fsm_dpi_sni_plugin_update(struct fsm_session *session)
{
    struct fsm_dpi_sni_session *u_session;

    u_session = session->handler_ctxt;
    if (u_session == NULL) return;

    u_session->included_devices = session->ops.get_config(session,
                                                          "included_devices");
    u_session->excluded_devices = session->ops.get_config(session,
                                                          "excluded_devices");

    return;
}


static void
fsm_dpi_sni_send_report(struct fqdn_pending_req *req)
{
    struct fsm_session *session = req->fsm_context;
    char *report;

    if (req->to_report != true) return;
    if (req->num_replies > 1) return;

    report = jencode_url_report(session, req);

    session->ops.send_report(session, report);
}


/**
 * @brief request an action from the policy engine
 *
 * @param session the fsm session
 * @param mac the device mac addresss
 * @param attr the attribute flow
 * @param attr_value the attribute flow value
 * @return the action to take
 */
static int
fsm_dpi_sni_policy_req(struct fsm_session *session,
                       os_macaddr_t *mac,
                       char *attr,
                       char *attr_value)
{
    struct fsm_policy_client *policy_client;
    struct fqdn_pending_req fqdn_req;
    struct fsm_policy_req policy_req;
    int action;

    memset(&policy_req, 0, sizeof(policy_req));
    memset(&fqdn_req, 0, sizeof(fqdn_req));
    policy_client = &session->policy_client;

    fqdn_req.provider = session->service->name;
    fqdn_req.fsm_context = session;
    fqdn_req.send_report = session->ops.send_report;
    memcpy(fqdn_req.dev_id.addr, mac->addr,
           sizeof(fqdn_req.dev_id.addr));
    fqdn_req.redirect = false;
    fqdn_req.to_report = false;
    fqdn_req.fsm_checked = false;
    fqdn_req.req_type = fsm_req_type(attr);
    fqdn_req.policy_table = policy_client->table;
    fqdn_req.numq = 1;
    fqdn_req.req_info = calloc(sizeof(struct fsm_url_request), 1);
    if (fqdn_req.req_info == NULL) return FSM_DPI_PASSTHRU;

    /* Set the backend provider ops */
    fqdn_req.categories_check = session->provider_ops->categories_check;
    fqdn_req.risk_level_check = session->provider_ops->risk_level_check;
    fqdn_req.gatekeeper_req = session->provider_ops->gatekeeper_req;

    STRSCPY(fqdn_req.req_info->url, attr_value);
    memset(&policy_req, 0, sizeof(policy_req));
    policy_req.device_id = mac;
    policy_req.url = attr_value;
    policy_req.fqdn_req = &fqdn_req;

    fsm_apply_policies(session, &policy_req);
    fqdn_req.action = policy_req.reply.action;
    fqdn_req.policy = policy_req.reply.policy;
    fqdn_req.policy_idx = policy_req.reply.policy_idx;
    fqdn_req.rule_name = policy_req.reply.rule_name;
    fqdn_req.rd_ttl = policy_req.reply.rd_ttl;
    action = (policy_req.reply.action == FSM_BLOCK ?
              FSM_DPI_DROP : FSM_DPI_PASSTHRU);
    fqdn_req.to_report = true;
    /* Process reporting */
    if (policy_req.reply.log == FSM_REPORT_NONE)
    {
        fqdn_req.to_report = false;
    }

    if ((policy_req.reply.log == FSM_REPORT_BLOCKED) &&
        (policy_req.reply.action != FSM_BLOCK))
    {
        fqdn_req.to_report = false;
    }

    /* Overwrite logging and policy if categorization failed */
    if (fqdn_req.categorized == FSM_FQDN_CAT_FAILED)
    {
        fqdn_req.action = FSM_ALLOW;
        fqdn_req.to_report = true;
    }

    fsm_dpi_sni_send_report(&fqdn_req);

    free(fqdn_req.rule_name);
    free(fqdn_req.policy);
    fsm_free_url_reply(fqdn_req.req_info->reply);
    free(fqdn_req.req_info);

    return action;
}


/**
 * @brief check if a mac address belongs to a given tag or matches a value
 *
 * @param the mac address to check
 * @param val an opensync tag name or the string representation of a mac address
 * @return true if the mac matches the value, false otherwise
 */
static bool
fsm_dpi_sni_find_mac_in_val(os_macaddr_t *mac, char *val)
{
    char mac_s[32] = { 0 };
    bool rc;
    int ret;

    if (val == NULL) return false;
    if (mac == NULL) return false;

    snprintf(mac_s, sizeof(mac_s), PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(mac));

    rc = om_tag_in(mac_s, val);
    if (rc) return true;

    ret = strncmp(mac_s, val, strlen(mac_s));
    return (ret == 0);
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
fsm_dpi_sni_process_attr(struct fsm_session *session, char *attr, char *value,
                         struct net_md_stats_accumulator *acc)
{
    struct fsm_dpi_sni_session *u_session;
    struct net_md_flow_info info;
    struct net_md_flow_key *key;
    bool excluded;
    bool included;
    int action;
    bool act;
    bool ret;

    if (acc == NULL) return FSM_DPI_IGNORED;
    if (acc->originator == NET_MD_ACC_UNKNOWN_ORIGINATOR) return FSM_DPI_IGNORED;

    key = acc->key;
    if (key == NULL) return FSM_DPI_IGNORED;

    u_session = session->handler_ctxt;
    if (u_session == NULL) return FSM_DPI_IGNORED;

    LOGT("%s: %s: attribute: %s, value %s", __func__,
         session->name, attr, value);
    LOGT("%s: service provider: %s", __func__,
         session->service ? session->service->name : "None");

    excluded = (u_session->excluded_devices != NULL);
    included = (u_session->included_devices != NULL);

    act = false;
    action = FSM_DPI_IGNORED;

    memset(&info, 0, sizeof(info));
    ret = net_md_get_flow_info(acc, &info);
    if (!ret) goto out;

    if (excluded || included)
    {
        if (info.local_mac == NULL) goto out;
        if (excluded)
        {
            act = !fsm_dpi_sni_find_mac_in_val(info.local_mac,
                                               u_session->excluded_devices);
            if (!act) goto out;
        }
        if (included)
        {
            act = fsm_dpi_sni_find_mac_in_val(info.local_mac,
                                              u_session->excluded_devices);
            if (!act) goto out;
        }
    }

    if (session->service == NULL) return FSM_DPI_PASSTHRU;

    action = fsm_dpi_sni_policy_req(session, info.local_mac, attr, value);

out:
    return action;
}
