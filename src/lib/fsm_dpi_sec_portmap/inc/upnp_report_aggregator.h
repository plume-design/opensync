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

#ifndef UPNP_REPORT_AGGREGATOR_H_INCLUDED
#define UPNP_REPORT_AGGREGATOR_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>

#include "ds_list.h"
#include "ds_tree.h"
#include "fsm.h"
#include "qm_conn.h"
#include "upnp_portmap.h"

struct upnp_report_aggregator_t
{
    char *node_id;
    char *location_id;

    ds_list_t *ports;  /* This will be a list of struct mapped_port_t */
    size_t n_ports;

    bool initialized;
};

struct upnp_report_aggregator_t *upnp_report_aggregator_alloc(struct fsm_session *session);
void upnp_report_aggregator_free(struct upnp_report_aggregator_t *aggr);

bool upnp_report_aggregator_add_port(struct upnp_report_aggregator_t *aggr, struct mapped_port_t *a_port);
bool upnp_report_aggregator_add_snapshot(struct upnp_report_aggregator_t *aggr, ds_tree_t *snapshot);

void upnp_report_aggregator_flush(struct upnp_report_aggregator_t *aggr);

void upnp_report_aggregator_dump(struct upnp_report_aggregator_t *aggr);

#endif /* UPNP_REPORT_AGGREGATOR_H_INCLUDED */
