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
#include <stdint.h>
#include <stdio.h>

#include "fsm.h"
#include "fsm_dpi_client_plugin.h"
#include "fsm_dpi_adt.h"
#include "log.h"
#include "memutil.h"
#include "network_metadata_report.h"
#include "qm_conn.h"

static size_t FSM_DPI_ADT_MAX_DATAPOINTS = 10;

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
fsm_dpi_adt_init(struct fsm_session *session)
{
    struct fsm_dpi_client_session *client_session;
    struct fsm_dpi_plugin_client_ops *client_ops;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_adt_session *adt_session;
    int ret;

    /* Initialize generic client */
    ret = fsm_dpi_client_init(session);
    if (ret != 0) return ret;

    /* And now all the ADT specific calls */
    session->ops.update = fsm_dpi_adt_update;
    session->ops.periodic = fsm_dpi_adt_periodic;
    session->ops.exit = fsm_dpi_adt_exit;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    client_ops->process_attr = fsm_dpi_adt_process_attr;

    /* Fetch the specific updates for this client plugin */
    session->ops.update(session);

    client_session = (struct fsm_dpi_client_session *)session->handler_ctxt;
    adt_session = (struct fsm_dpi_adt_session *)client_session->private_session;
    if (adt_session == NULL)
    {
        adt_session = CALLOC(1, sizeof(*adt_session));
        if (adt_session == NULL) return -1;

        adt_session->adt_aggr = CALLOC(1, sizeof(*aggr));
        if (adt_session->adt_aggr == NULL) return -1;

        client_session->private_session = adt_session;
    }

    aggr = adt_session->adt_aggr;
    if (aggr->initialized) return 0;

    aggr->node_id = STRDUP(session->node_id);
    if (aggr->node_id == NULL) goto cleanup;
    aggr->location_id = STRDUP(session->location_id);
    if (aggr->location_id == NULL) goto cleanup;

    aggr->data_max = FSM_DPI_ADT_MAX_DATAPOINTS;
    aggr->data_idx = 0;
    aggr->data_prov = aggr->data_max;
    aggr->data = CALLOC(aggr->data_prov, sizeof(*aggr->data));
    if (aggr->data == NULL) goto cleanup;

    /* Make eventual testing easier */
    aggr->send_report = qm_conn_send_direct;

    aggr->initialized = true;

    LOGD("%s: Added session %s", __func__, session->name);

    return 0;

cleanup:
    fsm_dpi_client_exit(session);

    return -1;
}


/*
 * Provided for compatibility
 */
int
dpi_adt_plugin_init(struct fsm_session *session)
{
    return fsm_dpi_adt_init(session);
}


void
fsm_dpi_adt_exit(struct fsm_session *session)
{
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_adt_session *adt_session;

    aggr = NULL;

    adt_session = fsm_dpi_adt_get_session(session);
    if (adt_session == NULL) goto clean_all;

    aggr = adt_session->adt_aggr;
    if (aggr == NULL) goto clean_all;
    if (!aggr->initialized) goto clean_all;

    dpi_adt_send_report(session);

    /* This should be empty, but make extra sure */
    dpi_adt_free_aggr_store(aggr);
    FREE(aggr->data);
    FREE(aggr->location_id);
    FREE(aggr->node_id);

    FREE(adt_session);

    aggr->initialized = false;

clean_all:
    FREE(aggr);

    /* Free the generic client */
    fsm_dpi_client_exit(session);
}

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm url session to update
 */
void
fsm_dpi_adt_update(struct fsm_session *session)
{
    /* Generic config first */
    fsm_dpi_client_update(session);

    /* ADT specific entries */
    LOGD("%s: Updating ADT config", __func__);
}

void
fsm_dpi_adt_periodic(struct fsm_session *session)
{
    bool need_periodic;

    need_periodic = fsm_dpi_client_periodic_check(session);

    if (!need_periodic) return;

    /* Since we are sending reports without buffering, this should be a no-op */
    dpi_adt_send_report(session);
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
fsm_dpi_adt_process_attr(struct fsm_session *session, const char *attr,
                         uint8_t type, uint16_t length, const void *value,
                         struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    int action = FSM_DPI_PASSTHRU;
    int rc;

    if (session == NULL) return action;

    if (attr == NULL || strlen(attr) == 0)
    {
        LOGD("%s: Invalid attribute key", __func__);
        return action;
    }
    if (value == NULL || length == 0)
    {
        LOGD("%s: Invalid value for attr %s", __func__, attr);
        return action;
    }

    /* Process the generic part (e.g., logging, include, exclude lists) */
    action = fsm_dpi_client_process_attr(session, attr, type, length, value, pkt_info);
    if (action == FSM_DPI_IGNORED)
    {
        LOGD("%s: Nothing to be reported", __func__);
        return action;
    }

    /*
     * Current implementation requires immediate reporting, so
     * we store then send immediately.
     */
    rc = dpi_adt_store(session, attr, type, length, value, pkt_info);
    if (!rc)
    {
        LOGD("%s: Failed to store ADT report", __func__);
        return action;
    }
    rc = dpi_adt_send_report(session);
    if (!rc)
    {
        LOGD("%s: Failed to send ADT report", __func__);
    }

    return action;
};

struct fsm_dpi_adt_session *
fsm_dpi_adt_get_session(struct fsm_session *session)
{
    struct fsm_dpi_client_session *client_session;
    struct fsm_dpi_adt_session *adt_session;

    client_session = (struct fsm_dpi_client_session *)session->handler_ctxt;
    if (!client_session) return NULL;
    adt_session = (struct fsm_dpi_adt_session *)client_session->private_session;
    return adt_session;
}
