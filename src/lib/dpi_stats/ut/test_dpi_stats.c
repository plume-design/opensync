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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "dpi_stats.h"
#include "memutil.h"
#include "log.h"
#include "unity.h"
#include "target.h"
#include "qm_conn.h"

const char *test_name = "test_dpi_stats";

struct dpi_stats_packed_buffer *g_serialized = NULL;
char *g_location_id = "59f39f5acbb22513f0ae5e17";
char *g_node_id = "4C718002B3";
char *g_deployment = "dog1";
char g_mqtt_topic[256];

static void
test_dpi_stats_send_report(char *topic, struct dpi_stats_packed_buffer *pb)
{
#ifndef ARCH_X86
    qm_response_t res;
    bool ret = false;
#endif

    TEST_ASSERT_NOT_NULL(topic);
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    LOGD("%s: msg len: %zu, topic: %s",
         __func__, pb->len, topic);

#ifndef ARCH_X86
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, topic,
                              pb->buf, pb->len, &res);
    if (!ret) LOGE("error sending mqtt with topic %s", topic);
#endif

    return;
}

/**
 * @brief called by the Unity framework before every single test
 */
void
setUp(void)
{
    return;
}


/**
 * @brief called by the Unity framework after every single test
 */
void
tearDown(void)
{
    return;
}


void
test_dpi_stats_serialize_report(void)
{
    struct dpi_stats_counters *counters;
    struct dpi_stats_report report;

    memset(&report, 0, sizeof(report));
    report.location_id = g_location_id;
    report.node_id = g_node_id;
    report.plugin = "ut_dpi_plugin";

    /* Set a few counters */
    counters = &report.counters;
    counters->curr_alloc = 10;
    counters->err_scan = 3;

    g_serialized = dpi_stats_serialize_report(&report);
    TEST_ASSERT_NOT_NULL(g_serialized);

    test_dpi_stats_send_report(g_mqtt_topic, g_serialized);
    dpi_stats_free_packed_buffer(g_serialized);
}


/**
 * @brief create mqtt topic
 */
void
test_lte_set_mqtt_topic(void)
{
    int rc;

    rc = snprintf(g_mqtt_topic, sizeof(g_mqtt_topic), "dev-test/DpiStats/%s/%s/%s",
                  g_deployment, g_node_id,
                  g_location_id);
    TEST_ASSERT_NOT_EQUAL_INT(0, rc);
}


int
main(int argc, char *argv[])
{
    /* Set the logs to stdout */
    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    RUN_TEST(test_lte_set_mqtt_topic);
    RUN_TEST(test_dpi_stats_serialize_report);

    return UNITY_END();
}
