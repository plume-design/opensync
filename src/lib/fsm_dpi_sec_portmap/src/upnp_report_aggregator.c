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
#include <sys/socket.h>

#include "ds_list.h"
#include "ds_tree.h"
#include "fsm.h"
#include "log.h"
#include "memutil.h"
#include "qm_conn.h"
#include "sockaddr_storage.h"
#include "upnp_portmap.h"
#include "upnp_report_aggregator.h"

struct upnp_report_aggregator_t *
upnp_report_aggregator_alloc(struct fsm_session *session)
{
    struct upnp_report_aggregator_t *new_aggr;

    if (session == NULL) return NULL;

    new_aggr = CALLOC(1, sizeof(*new_aggr));
    if (new_aggr == NULL) return NULL;

    new_aggr->node_id = STRDUP(session->node_id);
    new_aggr->location_id = STRDUP(session->location_id);

    new_aggr->ports = CALLOC(1, sizeof(*new_aggr->ports));
    if (new_aggr->ports == NULL) goto cleanup_aggr;
    ds_list_init(new_aggr->ports, struct mapped_port_t, node);
    new_aggr->n_ports = 0;

    return new_aggr;

cleanup_aggr:
    FREE(new_aggr);
    return NULL;
}

void
upnp_report_aggregator_free(struct upnp_report_aggregator_t *aggr)
{
    if (aggr == NULL) return;

    upnp_report_aggregator_flush(aggr);

    FREE(aggr->ports);
    FREE(aggr->location_id);
    FREE(aggr->node_id);
    FREE(aggr);
}

bool
upnp_report_aggregator_add_port(struct upnp_report_aggregator_t *aggr,
                                struct mapped_port_t *a_port)
{
    struct mapped_port_t *new_port;

    new_port = CALLOC(1, sizeof(*new_port));
    if (new_port == NULL) return NULL;

    /* We need a deep copy since we are not owning the record
     * (snapshots are like that)
     */
    new_port->captured_at_ms = a_port->captured_at_ms;
    new_port->duration = a_port->duration;
    new_port->intPort = a_port->intPort;
    new_port->extPort = a_port->extPort;
    new_port->protocol = a_port->protocol;
    new_port->source = a_port->source;
    new_port->enabled = a_port->enabled;
    memcpy(&new_port->device_id, &a_port->device_id, sizeof(os_macaddr_t));

    if (a_port->intClient)
    {
        new_port->intClient = CALLOC(1, sizeof(*new_port->intClient));
        sockaddr_storage_copy(a_port->intClient, new_port->intClient);
    }
    if (a_port->desc)
        new_port->desc = STRDUP(a_port->desc);

    ds_list_insert_head(aggr->ports, new_port);
    aggr->n_ports++;

    return true;
}

bool
upnp_report_aggregator_add_snapshot(struct upnp_report_aggregator_t *aggr,
                                    ds_tree_t *snapshot)
{
    struct mapped_port_t *portmap;
    bool rc = false;

    /* Iterate thru the snapshot and insert one record at a time */
    ds_tree_foreach(snapshot, portmap)
    {
        rc |= upnp_report_aggregator_add_port(aggr, portmap);
    }

    return rc;
}

void
upnp_report_aggregator_flush(struct upnp_report_aggregator_t *aggr)
{
    struct mapped_port_t *entry;

    if (aggr == NULL) return;

    /* Cleanup the list first */
    while (!ds_list_is_empty(aggr->ports))
    {
        entry = ds_list_remove_head(aggr->ports);
        upnp_portmap_delete_record(entry);
    }

    aggr->n_ports = 0;
}

void
upnp_report_aggregator_dump(struct upnp_report_aggregator_t *aggr)
{
    struct mapped_port_t *portmap;
    ds_list_iter_t iter;

    LOGT("%s: ======== DUMP AGGR ========", __func__);
    LOGT("%s: Node_Id = %s", __func__, aggr->node_id);
    LOGT("%s: Location_Id = %s", __func__, aggr->location_id);
    LOGT("%s: Number records = %zu", __func__, aggr->n_ports);

    for (   portmap = ds_list_ifirst(&iter, aggr->ports);
            portmap != NULL;
            portmap = ds_list_inext(&iter) )
    {
        upnp_portmap_dump_record(portmap);
    }
    LOGT("%s: ======== END DUMP ========", __func__);
}
