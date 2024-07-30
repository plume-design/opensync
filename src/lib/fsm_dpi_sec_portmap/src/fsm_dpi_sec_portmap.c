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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "fsm.h"
#include "fsm_dpi_client_plugin.h"
#include "fsm_dpi_sec_portmap.h"
#include "log.h"
#include "memutil.h"
#include "network_metadata_report.h"
#include "os.h"
#include "os_time.h"
#include "upnp_portmap.h"
#include "upnp_portmap_pb.h"
#include "upnp_portmap.pb-c.h"
#include "upnp_report_aggregator.h"
#include "util.h"
#include "fsm_fn_trace.h"

const char * const upnp_state_str[] =
{
    [UNDEFINED] = "undefined",
    [BEGIN_UPNP] = "begin",
    [UPNP_ACTION] = "upnp.action",
    [UPNP_PROTOCOL] = "upnp.protocol",
    [UPNP_EXT_PORT] = "upnp.external_port",
    [UPNP_INT_PORT] = "upnp.internal_port",
    [UPNP_INT_CLIENT] = "upnp.internal_client",
    [UPNP_DURATION] = "upnp.duration",
    [UPNP_DESC] = "upnp.description",
    [END_UPNP] = "end",
};

const char *upnp_attr_value = "upnp";

static enum upnp_state
get_upnp_state(const char *attribute)
{
#define GET_UPNP_STATE(attr, x)                \
    do                                         \
    {                                          \
        int cmp;                               \
        cmp = strcmp(attr, upnp_state_str[x]); \
        if (cmp == 0) return x;                \
    }                                          \
    while (0)

    GET_UPNP_STATE(attribute, UPNP_ACTION);
    GET_UPNP_STATE(attribute, UPNP_PROTOCOL);
    GET_UPNP_STATE(attribute, UPNP_EXT_PORT);
    GET_UPNP_STATE(attribute, UPNP_INT_PORT);
    GET_UPNP_STATE(attribute, UPNP_INT_CLIENT);
    GET_UPNP_STATE(attribute, UPNP_DURATION);
    GET_UPNP_STATE(attribute, UPNP_DESC);
    GET_UPNP_STATE(attribute, BEGIN_UPNP);
    GET_UPNP_STATE(attribute, END_UPNP);

    return UNDEFINED;
#undef GET_UPNP_STATE
}

void
fsm_dpi_sec_portmap_reset_state(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *upnp_session;

    if (session == NULL) return;

    upnp_session = fsm_dpi_sec_portmap_get_session(session);
    MEMZERO(upnp_session->curr_rec_processed);
}

bool
fsm_dpi_sec_portmap_check_record(struct fsm_upnp_record *rec)
{
    bool is_delete_port = false;
    bool is_add_port = false;

    if (rec == NULL)
    {
        LOGD("%s: NULL record", __func__);
        return false;
    }

    if (rec->action[0] == '\0')
    {
        LOGD("%s: ACTION is empty", __func__);
        return false;
    }

    if (strcmp(rec->action, "AddPortMapping") == 0)
    {
        is_add_port = true;
    }
    else if (strcmp(rec->action, "DeletePortMapping") == 0)
    {
        is_delete_port = true;
    }
    else
    {
        LOGD("%s: action not supported %s", __func__, rec->action);
        return false;
    }

    if (rec->protocol[0] == '\0')
    {
        LOGD("%s: PROTOCOL is empty", __func__);
        return false;
    }

    if (rec->ext_port[0] == '\0')
    {
        LOGD("%s: EXT_PORT is empty", __func__);
        return false;
    }

    /* All other attributes are irrelevant */

    if (is_delete_port) return true;

    if (rec->int_port[0] == '\0' && is_add_port)
    {
        LOGD("%s: INT_PORT is empty", __func__);
        return false;
    }

    if (rec->int_client[0] == '\0' && is_add_port)
    {
        LOGD("%s: INT_CLIENT is empty", __func__);
        return false;
    }

    if (rec->duration[0] == '\0' && is_add_port)
    {
        LOGD("%s: DURATION is empty", __func__);
        return false;
    }

    /* This is the only valid case of an empty field */
    if (rec->description[0] == '\0' && is_add_port)
    {
        LOGT("%s: DESC is empty", __func__);
        return true;
    }

    return true;
}

int
fsm_dpi_sec_portmap_process_record(struct fsm_session *session,
                            struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct fsm_dpi_sec_portmap_session *u_session;
    enum upnp_capture_source information_source;
    struct net_md_stats_accumulator *acc;
    struct mapped_port_t *new_port;
    struct net_md_flow_info info;
    struct fsm_upnp_record *rec;
    bool is_record_complete;
    int action;
    bool rc;

    /* _Initially_ there should be no action to be taken on this */
    action = FSM_DPI_PASSTHRU;

    u_session = fsm_dpi_sec_portmap_get_session(session);

    rec = &u_session->curr_rec_processed;

    is_record_complete = fsm_dpi_sec_portmap_check_record(rec);
    if (!is_record_complete)
    {
        LOGD("%s: Not reporting incomplete record", __func__);
        return action;
    }

    LOGT("%s: Processing dpi UPnP action for %s", __func__, rec->action);
    if (strcmp(rec->action, "AddPortMapping") == 0)
    {
        information_source = UPNP_SOURCE_PKT_INSPECTION_ADD;
    }
    else if (strcmp(rec->action, "DeletePortMapping") == 0)
    {
        information_source = UPNP_SOURCE_PKT_INSPECTION_DEL;
    }
    else
    {
        /* This case should have been excluded in fsm_dpi_sec_portmap_check_record() above */
        LOGD("%s: unknown SOAP action (%s)", __func__, rec->action);
        return action;
    }

    new_port = upnp_portmap_create_record(
        information_source, rec->ext_port, rec->int_client,
        rec->int_port, rec->protocol, rec->description, "1", "", rec->duration);

    /*
     * We'll need to reach to the MAC address of the sender (local in the flow)
     * If we can't find it (unlikely), we'll have a ZERO MAC address, but we'll
     * still be reporting.
     */
    acc = pkt_info->acc;
    if (acc)
    {
        MEMZERO(info);
        rc = net_md_get_flow_info(acc, &info);
        if (rc)
        {
            memcpy(&new_port->device_id, info.local_mac, sizeof(os_macaddr_t));
        }
    }

    /* Store the port mapping action to the aggregator */
    upnp_report_aggregator_add_port(u_session->aggr, new_port);

    return action;
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
fsm_dpi_sec_portmap_init(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *upnp_session;
    struct fsm_dpi_plugin_client_ops *client_ops;
    int ret;

    /* Initialize generic client */
    ret = fsm_dpi_client_init(session);
    if (ret != 0) return -1;

    /* And now all the DNS specific calls */
    session->ops.update = fsm_dpi_sec_portmap_update;
    session->ops.periodic = fsm_dpi_sec_portmap_periodic;
    session->ops.exit = fsm_dpi_sec_portmap_exit;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    FSM_FN_MAP(fsm_dpi_sec_portmap_process_attr);
    client_ops->process_attr = fsm_dpi_sec_portmap_process_attr;


    /* Look up the UPnP session */
    upnp_session = fsm_dpi_sec_portmap_get_session(session);
    if (upnp_session == NULL)
    {
        LOGE("%s: Could not allocate UPnP parser", __func__);
        return -1;
    }

    upnp_portmap_init(session);

    /* Fetch the specific updates for this client plugin */
    session->ops.update(session);

    /* Pick up the snapshots */
    session->ops.periodic(session);

    LOGD("%s: UPnP client plugin initialized", __func__);
    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
    {
        LOGD("%s: mqtt_channel = %s", __func__, upnp_session->mqtt_topic);
        LOGD("%s: snapshot_interval = %ld", __func__, upnp_session->snapshot_interval);
        LOGD("%s: report_interval = %ld", __func__, upnp_session->report_interval);
        LOGD("%s: snapshot_max_entries = %zu", __func__, upnp_session->snapshot_max_entries);

        upnp_report_aggregator_dump(upnp_session->aggr);
    }

    return 0;
}

/*
 * Provided for compatibility
 */
int
dpi_sec_portmap_plugin_init(struct fsm_session *session)
{
    return fsm_dpi_sec_portmap_init(session);
}


void
fsm_dpi_sec_portmap_exit(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *upnp_session;

    if (session == NULL) goto final_exit;

    upnp_portmap_exit(session);

    upnp_session = fsm_dpi_sec_portmap_get_session(session);
    FREE(upnp_session);
    upnp_session = NULL;

    /* Free the generic client */
    fsm_dpi_client_exit(session);

final_exit:
    LOGD("%s: Exit UPnP client plugin", __func__);
}

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm url session to update
 */
void
fsm_dpi_sec_portmap_update(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *upnp_session;
    char *config;

    if (session == NULL) return;

    /* Generic config first */
    fsm_dpi_client_update(session);

    /* UPnP specific entries */
    upnp_session = fsm_dpi_sec_portmap_get_session(session);
    if (!upnp_session->initialized) return;

    upnp_session->mqtt_topic = session->ops.get_config(session, "mqtt_avs_upnp");

    upnp_session->snapshot_interval = 30;
    config = session->ops.get_config(session, "snapshot_interval");
    if (config)
        upnp_session->snapshot_interval = strtol(config, NULL, 10);

    upnp_session->snapshot_max_entries = 50;
    config = session->ops.get_config(session, "snapshot_max_entries");
    if (config)
        upnp_session->snapshot_max_entries = strtol(config, NULL, 10);

    upnp_session->report_interval = 30;
    config = session->ops.get_config(session, "report_interval");
    if (config)
        upnp_session->report_interval = strtol(config, NULL, 10);
}

void
fsm_dpi_sec_portmap_periodic(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *upnp_session;
    time_t now = time(NULL);
    double cmp;

    (void)fsm_dpi_client_periodic_check(session);  /* Retval could be useful later */

    upnp_session = fsm_dpi_sec_portmap_get_session(session);

    cmp = difftime(now, upnp_session->last_snapshot_fetch);
    if (cmp >= upnp_session->snapshot_interval)
    {
        upnp_session->last_snapshot_fetch = now;
        (void)upnp_portmap_fetch_all(session);
    }

    cmp = difftime(now, upnp_session->report_interval);
    if (cmp >= upnp_session->last_report_sent)
    {
        upnp_session->last_report_sent = now;
        upnp_portmap_send_report(session);
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
fsm_dpi_sec_portmap_process_attr(struct fsm_session *session, const char *attr,
                          uint8_t type, uint16_t length, const void *value,
                          struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct fsm_dpi_sec_portmap_session *upnp_session;
    struct fsm_upnp_record *rec;
    enum upnp_state curr_state;
    int action;
    int cmp;

    if (pkt_info == NULL) return FSM_DPI_IGNORED;

    /* Process the generic part (e.g., logging, include, exclude lists) */
    action = fsm_dpi_client_process_attr(session, attr, type, length, value, pkt_info);
    if (action == FSM_DPI_IGNORED) return action;

    /*
     * The combo (device, attribute) is to be processed, but no service provided
     * Pass through.
     */
    if (session->service == NULL) return FSM_DPI_PASSTHRU;

    action = FSM_DPI_IGNORED;

    upnp_session = fsm_dpi_sec_portmap_get_session(session);
    rec = &upnp_session->curr_rec_processed;

    /* Optimizing things: all attributes should be returning a string */
    if (type != RTS_TYPE_STRING)
    {
        LOGD("%s: Value for %s should be a string", __func__, attr);
        return action;
    }

    curr_state = get_upnp_state(attr);
    switch (curr_state)
    {
        case BEGIN_UPNP:
        {
            /* Since we can have more than one value associated with `begin` */
            cmp = strncmp(value, upnp_attr_value, length);
            if (cmp) break;

            LOGT("%s: start new UPnP record", __func__);
            break;
        }

        case UPNP_ACTION:
        {
            STRSCPY_LEN(rec->action, value, length);
            LOGT("%s: copied %s = %s", __func__, attr, rec->action);
            break;
        }

        case UPNP_PROTOCOL:
        {
            STRSCPY_LEN(rec->protocol, value, length);
            LOGT("%s: copied %s = %s", __func__, attr, rec->protocol);
            break;
        }

        case UPNP_EXT_PORT:
        {
            STRSCPY_LEN(rec->ext_port, value, length);
            LOGT("%s: copied %s = %s", __func__, attr, rec->ext_port);
            break;
        }

        case UPNP_INT_PORT:
        {
            STRSCPY_LEN(rec->int_port, value, length);
            LOGT("%s: copied %s = %s", __func__, attr, rec->int_port);
            break;
        }

        case UPNP_INT_CLIENT:
        {
            STRSCPY_LEN(rec->int_client, value, length);
            LOGT("%s: copied %s = %s", __func__, attr, rec->int_client);
            break;
        }

        case UPNP_DURATION:
        {
            STRSCPY_LEN(rec->duration, value, length);
            LOGT("%s: copied %s = %s", __func__, attr, rec->duration);
            break;
        }

        case UPNP_DESC:
        {
            STRSCPY_LEN(rec->description, value, length);
            LOGT("%s: copied %s = %s", __func__, attr, rec->description);
            break;
        }

        case END_UPNP:
        {
            /* Since we can have more than one value associated with `end` */
            cmp = strncmp(value, upnp_attr_value, length);
            if (cmp) goto reset_state_machine;

            /* Now we can process the record */
            action = fsm_dpi_sec_portmap_process_record(session, pkt_info);
            break;
        }

        default:
        {
            LOGD("%s: Unexpected attr '%s'", __func__, attr);
            goto reset_state_machine;
        }
    }

    return action;

reset_state_machine:
    fsm_dpi_sec_portmap_reset_state(session);
    return action;
}

/**
 * @brief get a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct fsm_dpi_sec_portmap_session *
fsm_dpi_sec_portmap_get_session(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *upnp_session;
    struct fsm_dpi_client_session *client_session;

    client_session = (struct fsm_dpi_client_session *)session->handler_ctxt;
    if (client_session == NULL) return NULL;

    upnp_session = (struct fsm_dpi_sec_portmap_session *)client_session->private_session;
    if (upnp_session == NULL)
    {
        upnp_session = CALLOC(1, sizeof(*upnp_session));
        if (upnp_session == NULL) return NULL;

        client_session->private_session = upnp_session;
    }
    return upnp_session;
}
