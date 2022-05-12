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
#include <stdio.h>
#include <sys/socket.h>

#include "memutil.h"
#include "os.h"
#include "os_nif.h"
#include "os_types.h"
#include "sockaddr_storage.h"
#include "unit_test_utils.h"
#include "unity.h"
#include "upnp_portmap.h"
#include "upnp_report_aggregator.h"
void
test_upnp_report_aggregator(void)
{
    struct upnp_report_aggregator_t *aggr;
    struct fsm_session session;

    aggr = upnp_report_aggregator_alloc(NULL);
    TEST_ASSERT_NULL(aggr);

    session.node_id = "NODE_ID";
    session.location_id = "LOCATION_ID";

    aggr = upnp_report_aggregator_alloc(&session);
    TEST_ASSERT_NOT_NULL(aggr);
    TEST_ASSERT_NOT_NULL(aggr->ports);
    TEST_ASSERT_EQUAL(0, aggr->n_ports);

    /* Testing controlled dump */
    upnp_report_aggregator_dump(aggr);

    /* Cleanup */
    upnp_report_aggregator_free(aggr);
}

void
test_upnp_report_aggregator_add_record(void)
{
    struct upnp_report_aggregator_t *aggr;
    struct mapped_port_t a_portmap;
    struct fsm_session session;

    session.node_id = "NODE_ID";
    session.location_id = "LOCATION_ID";

    aggr = upnp_report_aggregator_alloc(&session);

    MEMZERO(a_portmap);
    a_portmap.captured_at_ms = 1234;
    a_portmap.protocol = UPNP_MAPPING_PROTOCOL_UDP;
    a_portmap.source = UPNP_SOURCE_PKT_INSPECTION_DEL;
    a_portmap.extPort = 666;

    upnp_report_aggregator_add_port(aggr, &a_portmap);
    TEST_ASSERT_EQUAL_INT(1, aggr->n_ports);

    /* Testing controlled dump */
    upnp_report_aggregator_dump(aggr);


    a_portmap.captured_at_ms = 5000;
    a_portmap.protocol = UPNP_MAPPING_PROTOCOL_UDP;
    a_portmap.extPort = 111;
    a_portmap.source = UPNP_SOURCE_IGD_POLL;
    a_portmap.duration = 100;
    a_portmap.intPort = 999;
    a_portmap.intClient = CALLOC(1, sizeof(*a_portmap.intClient));
    sockaddr_storage_populate(AF_INET, "127.0.0.1", a_portmap.intClient);
    a_portmap.enabled = true;
    os_nif_macaddr_from_str(&a_portmap.device_id, "00:11:22:33:44:55");
    a_portmap.desc = "random text";

    upnp_report_aggregator_add_port(aggr, &a_portmap);
    TEST_ASSERT_EQUAL_INT(2, aggr->n_ports);

    FREE(a_portmap.intClient);

    /* Testing controlled dump */
    upnp_report_aggregator_dump(aggr);

    /* Cleanup */
    upnp_report_aggregator_free(aggr);
}

void
run_test_upnp_report_aggregator(void)
{
    ut_setUp_tearDown(__func__, NULL, NULL);

    RUN_TEST(test_upnp_report_aggregator);
    RUN_TEST(test_upnp_report_aggregator_add_record);
}
