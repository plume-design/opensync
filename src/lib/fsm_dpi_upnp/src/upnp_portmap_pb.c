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

#include <netinet/in.h>
#include <stdio.h>

#include "fsm.h"
#include "log.h"
#include "memutil.h"
#include "os_time.h"
#include "os_types.h"
#include "upnp_portmap.h"
#include "upnp_portmap_pb.h"
#include "upnp_portmap.pb-c.h"
#include "upnp_report_aggregator.h"

Upnp__Portmap__ObservationPoint *
upnp_portmap_alloc_observation_point(struct fsm_session *session)
{
    Upnp__Portmap__ObservationPoint *pb;

    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;
    upnp__portmap__observation_point__init(pb);

    /* No need to a STRDUP() */
    pb->node_id = session->node_id;
    pb->location_id = session->location_id;

    return pb;
}

void
upnp_portmap_free_observation_point(Upnp__Portmap__ObservationPoint *data)
{
    FREE(data);
}

Upnp__Portmap__Portmap *
upnp_portmap_alloc_portmap(struct mapped_port_t *port)
{
    Upnp__Portmap__Portmap *pb;
    struct sockaddr_in *sa;

    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;
    upnp__portmap__portmap__init(pb);

    pb->captured_at_ms = port->captured_at_ms;
    pb->ext_port = htons(port->extPort);
    switch (port->source)
    {
        case UPNP_SOURCE_IGD_POLL:
            pb->source = UPNP__PORTMAP__CAPTURE_SOURCE__IGD_POLL;
            break;
        case UPNP_SOURCE_PKT_INSPECTION_ADD:
            pb->source = UPNP__PORTMAP__CAPTURE_SOURCE__PKT_INSPECTION_ADD;
            break;
        case UPNP_SOURCE_PKT_INSPECTION_DEL:
            pb->source = UPNP__PORTMAP__CAPTURE_SOURCE__PKT_INSPECTION_DEL;
            break;
        case UPNP_SOURCE_OVSDB_STATIC:
            pb->source = UPNP__PORTMAP__CAPTURE_SOURCE__OVSDB_STATIC;
            break;
        case UPNP_SOURCE_CAPTURE_SOURCE_UNSPECIFIED:
        default:
            pb->source = UPNP__PORTMAP__CAPTURE_SOURCE__CAPTURE_SOURCE_UNSPECIFIED;
    }
    switch (port->protocol)
    {
        case UPNP_MAPPING_PROTOCOL_TCP:
            pb->protocol = UPNP__PORTMAP__PROTOCOLS__TCP;
            break;
        case UPNP_MAPPING_PROTOCOL_UDP:
            pb->protocol = UPNP__PORTMAP__PROTOCOLS__UDP;
            break;
        case UPNP_MAPPING_PROTOCOL_UNSPECIFIED:
        default:
            pb->protocol = UPNP__PORTMAP__PROTOCOLS__PROTOCOL_UNSPECIFIED;
            break;
    }

    pb->device_id.data = port->device_id.addr;
    pb->device_id.len = sizeof(port->device_id.addr);

    /* This is all that we'll have for a DEL, so return now */
    if (port->source == UPNP_SOURCE_PKT_INSPECTION_DEL) return pb;

    sa = (struct sockaddr_in *)(port->intClient);
    if (sa == NULL)
    {
        LOGD("%s: Target IP address is invalid", __func__);
        upnp_portmap_free_portmap(pb);
        return NULL;
    }
    pb->int_client = htonl(sa->sin_addr.s_addr);

    pb->int_port = htons(port->intPort);
    pb->description = port->desc;
    pb->enabled = (port->enabled) ? "1" : "0";

    pb->duration = port->duration;
    if (pb->duration == 0)
    {
        pb->duration = -1;  /* -1 == "forever" */
    }

    return pb;
}

void
upnp_portmap_free_portmap(Upnp__Portmap__Portmap *data)
{
    FREE(data);
}

Upnp__Portmap__Report *
upnp_portmap_alloc_report(struct fsm_session *session)
{
    struct fsm_dpi_upnp_session *upnp_session;
    struct upnp_report_aggregator_t *aggr;
    Upnp__Portmap__Portmap *new_datapoint;
    Upnp__Portmap__Report *pb;

    upnp_session = fsm_dpi_upnp_get_session(session);
    aggr = upnp_session->aggr;

    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;
    upnp__portmap__report__init(pb);

    pb->observation_point = upnp_portmap_alloc_observation_point(session);
    pb->reported_at_ms = clock_real_ms();

    /* Get the number of ports to be reported */
    pb->portmaps = CALLOC(aggr->n_ports, sizeof(*pb->portmaps));
    if (pb->portmaps == NULL) goto cleanup;
    pb->n_portmaps = 0;

    /*
     * Loop thru the aggregator and populate the protobuf array,
     * one entry at a time.
     */
    struct mapped_port_t *port;
    ds_list_foreach(aggr->ports, port)
    {
        new_datapoint = upnp_portmap_alloc_portmap(port);
        if (new_datapoint == NULL)
        {
            LOGT("%s: Skipping entry.", __func__);
            continue;
        }

        pb->portmaps[pb->n_portmaps] = new_datapoint;
        pb->n_portmaps++;
    }

    return pb;

cleanup:
    FREE(pb);

    return NULL;
}

void
upnp_portmap_free_report(Upnp__Portmap__Report *report)
{
    size_t i;

    if (report == NULL) return;

    upnp_portmap_free_observation_point(report->observation_point);

    for (i = 0; i < report->n_portmaps; i++)
    {
        upnp_portmap_free_portmap(report->portmaps[i]);
    }
    FREE(report->portmaps);

    FREE(report);
}
