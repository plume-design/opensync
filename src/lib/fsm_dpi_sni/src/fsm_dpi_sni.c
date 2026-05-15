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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "fsm.h"
#include "fsm_dpi_client_plugin.h"
#include "fsm_dpi_sni.h"
#include "fsm_policy.h"
#include "log.h"
#include "memutil.h"
#include "network_metadata_report.h"
#include "os.h"
#include "fsm_fn_trace.h"

static struct fsm_dpi_sni_cache cache_mgr =
{
    .initialized = false,
    .app_decision_local = false,
};

struct fsm_dpi_sni_cache *
fsm_dpi_sni_get_mgr(void)
{
    return &cache_mgr;
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
fsm_dpi_sni_init(struct fsm_session *session)
{
    struct fsm_dpi_plugin_client_ops *client_ops;
    struct fsm_dpi_sni_cache *cache;
    int ret;

    /* Initialize generic client */
    ret = fsm_dpi_client_init(session);
    if (ret != 0) return ret;

    cache = fsm_dpi_sni_get_mgr();
    cache->initialized = true;
    /* Set the fsm session */
    session->ops.update = fsm_dpi_sni_update;
    session->ops.periodic = fsm_dpi_sni_periodic;
    session->ops.exit = fsm_dpi_sni_exit;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    client_ops->process_attr = fsm_dpi_sni_process_attr;
    FSM_FN_MAP(fsm_dpi_sni_process_attr);

    /* Fetch the specific updates for this client plugin */
    session->ops.update(session);

    LOGD("%s: Added session '%s'", __func__, session->name);

    return 0;
}

/*
 * Provided for compatibility
 */
int
dpi_sni_plugin_init(struct fsm_session *session)
{
    return fsm_dpi_sni_init(session);
}

/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
fsm_dpi_sni_exit(struct fsm_session *session)
{
    /* Free the generic client */
    fsm_dpi_client_exit(session);
}


static void fsm_dpi_sni_get_app_decision(struct fsm_session *session)
{
    struct fsm_session_ops *dpi_client_session_ops;
    struct fsm_dpi_sni_cache *cache;
    char *app_policy_decision = NULL;

    cache = fsm_dpi_sni_get_mgr();
    if (!cache->initialized) return;

    dpi_client_session_ops = &session->ops;

    if (dpi_client_session_ops->get_config) app_policy_decision = dpi_client_session_ops->get_config(session, "app_policy_decision");
    if (app_policy_decision != NULL)
    {
        if (strcmp(app_policy_decision, "local") == 0)
        {
            LOGT("%s: App policy decision is local.", __func__);
            cache->app_decision_local = true;
        }
        else if (strcmp(app_policy_decision, "cloud") == 0)
        {
            LOGT("%s: App policy decision is cloud.", __func__);
            cache->app_decision_local = false;
        }
    }
}


/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm url session to update
 */
void
fsm_dpi_sni_update(struct fsm_session *session)
{
    struct fsm_dpi_sni_cache *cache;

    cache = fsm_dpi_sni_get_mgr();
    if (!cache->initialized) return;

    /* Generic config first */
    fsm_dpi_client_update(session);

    /* Get app policy decision */
    fsm_dpi_sni_get_app_decision(session);
    /* SNI specific entries */
    LOGD("%s: Updating SNI config", __func__);
}

/**
 * @brief make the TTL configurable
 */
void
fsm_dpi_sni_set_ttl(struct fsm_session *session, time_t ttl)
{
    fsm_dpi_client_set_ttl(session, ttl);
}

/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm url session to uprocess
 */
void
fsm_dpi_sni_periodic(struct fsm_session *session)
{
    bool need_periodic;

    need_periodic = fsm_dpi_client_periodic_check(session);

    if (need_periodic)
    {
        /* Nothing specific to be done */
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
fsm_dpi_sni_process_attr(struct fsm_session *session, const char *attr,
                         uint8_t type, uint16_t length, const void *value,
                         struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct fsm_dpi_sni_redirect_flow_request request;
    struct net_md_stats_accumulator *acc;
    struct fsm_request_args request_args;
    struct net_md_flow_info info;
    struct fsm_dpi_sni_cache *cache;
    char val[length+1];
    int request_type;
    int action;
    bool rc;

    if (pkt_info == NULL) return FSM_DPI_IGNORED;

    cache = fsm_dpi_sni_get_mgr();
    if (!cache->initialized) return FSM_DPI_IGNORED;

    fsm_dpi_sni_get_app_decision(session);
    pkt_info->app_decision_local = cache->app_decision_local;
    acc = pkt_info->acc;
    if (acc == NULL) return FSM_DPI_IGNORED;

    request_type = dpi_sni_get_req_type(attr);
    if (pkt_info->app_decision_local && request_type == FSM_APP_REQ)
    {
        LOGT("%s: App policy decision is local and attr is tag, skipping gatekeeper check", __func__);
        return FSM_DPI_IGNORED;
    }

    if (!pkt_info->app_decision_local && request_type == FSM_TRAFFIC_CLASS_REQ)
    {
        LOGT("%s: App policy decision is cloud and attr is traffic_class, skipping gatekeeper check", __func__);
        return FSM_DPI_IGNORED;
    }
    /* Process the generic part (e.g., logging, include, exclude lists) */
    action = fsm_dpi_client_process_attr(session, attr, type, length, value, pkt_info);
    if (action == FSM_DPI_IGNORED) goto out;

    /*
     * The combo (device, attribute) is to be processed, but no service provided
     * Pass through.
     */
    if (session->service == NULL) return FSM_DPI_PASSTHRU;

    if (type != RTS_TYPE_STRING)
    {
        LOGI("%s: expecting a string value for %s", __func__, attr);
        goto out;
    }
    if (length == 0 || value == NULL) goto out;

    MEMZERO(info);
    rc = net_md_get_flow_info(acc, &info);
    if (!rc) goto out;

    if (request_type == FSM_UNKNOWN_REQ_TYPE)
    {
        LOGE("%s: unknown attribute %s", __func__, attr);
        return FSM_DPI_PASSTHRU;
    }

    STRSCPY_LEN(val, value, length);

    MEMZERO(request_args);
    request_args.session = session;
    request_args.device_id = info.local_mac;
    request_args.acc = acc;
    request_args.request_type = request_type;

    action = dpi_sni_policy_req(&request_args, val);

    if (action == FSM_DPI_PASSTHRU) goto out;

    /* Check for redirected flow. Overwrite the action for the redirected flow */
    MEMZERO(request);
    request.acc = acc;
    request.session = session;
    request.info = &info;
    request.attribute_value = val;
    request.req_type = request_type;

    rc = dpi_sni_is_redirected_attr(&request);

    if (rc == true)
    {
        LOGD("%s: redirected flow detected", __func__);
        action = FSM_DPI_PASSTHRU;
    }

out:
    return action;
}
