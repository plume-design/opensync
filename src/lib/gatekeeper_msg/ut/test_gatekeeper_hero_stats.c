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
#include <ctype.h>

#include "log.h"
#include "unity.h"
#include "memutil.h"
#include "sockaddr_storage.h"

#include "gatekeeper_msg.h"
#include "gatekeeper_cache.h"
#include "gatekeeper_hero_stats.h"

#include "test_gatekeeper_msg.h"
#include "unit_test_utils.h"

extern bool (*send_report)(qm_compress_t compress, char *topic,
                           void *data, int data_size, qm_response_t *res);


struct fsm_session g_session = {
    .node_id = "NODE_ID",
    .location_id = "LOCATION_ID",
};

static char *
mock_get_config(struct fsm_session *session, char *key)
{
    (void)session;
    if (strcmp(key, "wc_hero_stats_max_report_size") == 0) return "500";
    return NULL;
}

struct fsm_session_ops g_ops =
{
    .get_config = mock_get_config,
};

struct gk_attr_cache_interface **entry;
size_t num_attr_entries = 8;
struct gkc_ip_flow_interface **flow_entry;
size_t num_flow_entries = 4;

/**
 * @brief converts the protobuf from os_macaddr into the
 *        representing string.
 *
 * @param serialized_mac the protobuf byte array
 * @param str_mac ptr to the ALLOCATED string to receive
 *        the textual MAC
 */
void
os_mac2str(ProtobufCBinaryData *serialized_mac, char* str_mac)
{
    const char *hex = "0123456789ABCDEF";
    uint8_t *pin = serialized_mac->data;
    size_t i;

    for (i = 0; i < serialized_mac->len - 1; i++)
    {
        *str_mac++ = hex[(*pin >> 4) & 0xF];
        *str_mac++ = hex[(*pin++) & 0xF];
        *str_mac++ = ':';
    }
    *str_mac++ = hex[(*pin >> 4) & 0xF];
    *str_mac++ = hex[(*pin) & 0xF];
    *str_mac = 0;
}

/**
 * @brief Unit test replacement for the actual sending over MQTT.
 *        The serialized protobuf in data in saved to /tmp/output<X>.pb
 *        and we perform some basic checks on the protobuf itself.
 *
 * @params: prototype is identical as the one of @see qm_conn_send_direct
 */
bool
ut_qm_conn_send_direct(qm_compress_t compress, char *topic,
                       void *data, int data_size, qm_response_t *res)
{
    static size_t i;
    size_t j;
    FILE *fd;
    char filename[100];

    Gatekeeper__HeroStats__HeroReport *report;
    Gatekeeper__HeroStats__HeroStats *hs;
    char str_mac[21];

    SPRINTF(filename, "/tmp/%s_%zu.pb", topic, i++);

    LOGI("Sending report to file %s", filename);

    fd = fopen(filename, "wb");
    fwrite((char *)data, data_size, 1, fd);
    fclose(fd);

    /* perform some minimal check on the protobuf */
    report = gatekeeper__hero_stats__hero_report__unpack(NULL, data_size, data);
    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(report);
    TEST_ASSERT_NOT_NULL(report->observation_point);

    TEST_ASSERT_NOT_NULL(report->observation_point->location_id);
    TEST_ASSERT_EQUAL_STRING(g_session.location_id, report->observation_point->location_id);

    TEST_ASSERT_NOT_NULL(report->observation_point->node_id);
    TEST_ASSERT_EQUAL_STRING(g_session.node_id, report->observation_point->node_id);

    TEST_ASSERT_NOT_NULL(report->observation_window);
    TEST_ASSERT_EQUAL_INT(1, report->n_observation_window);

    TEST_ASSERT_NOT_NULL(report->observation_window[0]->hero_stats);
    for (j = 0; j < report->observation_window[0]->n_hero_stats; j++)
    {
        hs = report->observation_window[0]->hero_stats[j];
        TEST_ASSERT_NOT_NULL(hs);

        os_mac2str(&hs->device_id, str_mac);
        LOGI("From protobuf: window[0] network_id=%s", hs->network_zone);
        LOGI("From protobuf: window[0], stats[%zu], action: %d", j, hs->action);
        LOGI("From protobuf: window[0], stats[%zu], device_id=%s", j, str_mac);
    }

    gatekeeper__hero_stats__hero_report__free_unpacked(report, NULL);

    return true;
}

static void
create_default_attr_entries(void)
{
    entry = CALLOC(num_attr_entries, sizeof(*entry));

    entry[0] = CALLOC(1, sizeof(*entry[0]));
    entry[0]->action = 1;
    entry[0]->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry[0]->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry[0]->cache_ttl = 1000;
    entry[0]->action = FSM_BLOCK;
    entry[0]->attr_name = STRDUP("www.test.com");
    entry[0]->direction = GKC_FLOW_DIRECTION_INBOUND;

    entry[1] = CALLOC(1, sizeof(*entry[1]));
    entry[1]->action = 1;
    entry[1]->device_mac = str2os_mac("AA:AA:AA:AA:AA:02");
    entry[1]->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry[1]->cache_ttl = 1000;
    entry[1]->action = FSM_ALLOW;
    entry[1]->attr_name = STRDUP("www.entr2.com");
    entry[1]->direction = GKC_FLOW_DIRECTION_OUTBOUND;
    entry[1]->network_id = "home--1";

    entry[2] = CALLOC(1, sizeof(*entry[2]));
    entry[2]->action = 1;
    entry[2]->device_mac = str2os_mac("AA:AA:AA:AA:AA:03");
    entry[2]->attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry[2]->cache_ttl = 1000;
    entry[2]->action = FSM_REDIRECT;
    entry[2]->ip_addr = sockaddr_storage_create(AF_INET, "1.2.3.4");
    entry[2]->network_id = "home--2";

    entry[3] = CALLOC(1, sizeof(*entry[3]));
    entry[3]->action = 1;
    entry[3]->device_mac = str2os_mac("AA:AA:AA:AA:AA:04");
    entry[3]->attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    entry[3]->cache_ttl = 1000;
    entry[3]->action = FSM_BLOCK;
    entry[3]->ip_addr = sockaddr_storage_create(AF_INET6, "0:0:0:0:0:FFFF:204.152.189.116");
    entry[3]->direction = GKC_FLOW_DIRECTION_INBOUND;

    entry[4] = CALLOC(1, sizeof(*entry[4]));
    entry[4]->action = 1;
    entry[4]->device_mac = str2os_mac("AA:AA:AA:AA:AA:04");
    entry[4]->attribute_type = GK_CACHE_REQ_TYPE_APP;
    entry[4]->cache_ttl = 1000;
    entry[4]->action = FSM_BLOCK;
    entry[4]->attr_name = STRDUP("testapp");
    entry[4]->gk_policy = "GK_POLICY";

    entry[5] = CALLOC(1, sizeof(*entry[5]));
    entry[5]->action = 1;
    entry[5]->device_mac = str2os_mac("AA:AA:AA:AA:AA:03");
    entry[5]->attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry[5]->cache_ttl = 1000;
    entry[5]->action = FSM_FORWARD;
    entry[5]->attr_name = STRDUP("10.1.2.3");
    entry[5]->ip_addr = sockaddr_storage_create(AF_INET, "10.1.2.3");

    entry[6] = CALLOC(1, sizeof(*entry[6]));
    entry[6]->device_mac = str2os_mac("AA:AA:AA:AA:AA:06");
    entry[6]->attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry[6]->cache_ttl = 1000;
    entry[6]->action_by_name = FSM_REDIRECT;
    entry[6]->ip_addr = sockaddr_storage_create(AF_INET, "1.2.3.6");
    entry[6]->direction = GKC_FLOW_DIRECTION_OUTBOUND;

    entry[7] = CALLOC(1, sizeof(*entry[7]));
    entry[7]->device_mac = str2os_mac("AA:AA:AA:AA:AA:07");
    entry[7]->attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    entry[7]->cache_ttl = 1000;
    entry[7]->action_by_name = FSM_BLOCK;
    entry[7]->ip_addr = sockaddr_storage_create(AF_INET6, "0:0:0:0:0:FFFF:204.152.189.117");
    entry[7]->direction = GKC_FLOW_DIRECTION_INBOUND;
}

static void
create_default_flow_entries(void)
{
    flow_entry = CALLOC(num_flow_entries, sizeof(*flow_entry));

    flow_entry[0] = CALLOC(1, sizeof(*flow_entry[0]));
    flow_entry[0]->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    flow_entry[0]->direction = GKC_FLOW_DIRECTION_INBOUND;
    flow_entry[0]->src_port = htons(80);
    flow_entry[0]->dst_port = htons(8002);
    flow_entry[0]->ip_version = 4;
    flow_entry[0]->protocol = 16;
    flow_entry[0]->cache_ttl = 1000;
    flow_entry[0]->action = FSM_BLOCK;
    flow_entry[0]->src_ip_addr = CALLOC(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "1.2.3.4", flow_entry[0]->src_ip_addr);
    flow_entry[0]->dst_ip_addr = CALLOC(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "10.12.14.13", flow_entry[0]->dst_ip_addr);
    flow_entry[0]->gk_policy = "GK_flow_policy";

    flow_entry[1] = CALLOC(1, sizeof(*flow_entry[1]));
    flow_entry[1]->device_mac = str2os_mac("AA:AA:AA:AA:AA:02");
    flow_entry[1]->direction = GKC_FLOW_DIRECTION_INBOUND;
    flow_entry[1]->src_port = htons(443);
    flow_entry[1]->dst_port = htons(8888);
    flow_entry[1]->ip_version = 4;
    flow_entry[1]->protocol = 16;
    flow_entry[1]->cache_ttl = 1000;
    flow_entry[1]->action = FSM_BLOCK;
    flow_entry[1]->src_ip_addr = CALLOC(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "2.3.4.5", flow_entry[1]->src_ip_addr);
    flow_entry[1]->dst_ip_addr = CALLOC(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "10.2.2.2", flow_entry[1]->dst_ip_addr);
    flow_entry[1]->gk_policy = "GK_flow_policy";

    flow_entry[2] = CALLOC(sizeof(struct gkc_ip_flow_interface), 1);
    flow_entry[2]->device_mac = str2os_mac("AA:AA:AA:AA:AA:02");
    flow_entry[2]->direction = GKC_FLOW_DIRECTION_INBOUND;
    flow_entry[2]->src_port = htons(22);
    flow_entry[2]->dst_port = htons(3333);
    flow_entry[2]->ip_version = 6;
    flow_entry[2]->protocol = 16;
    flow_entry[2]->cache_ttl = 1000;
    flow_entry[2]->action = FSM_ALLOW;
    flow_entry[2]->src_ip_addr = CALLOC(1, sizeof(struct in6_addr));
    inet_pton(AF_INET6, "0:0:0:0:0:FFFF:204.152.189.116", flow_entry[2]->src_ip_addr);
    flow_entry[2]->dst_ip_addr = CALLOC(1, sizeof(struct in6_addr));
    inet_pton(AF_INET6, "1:0:0:0:0:0:0:8", flow_entry[2]->dst_ip_addr);
    flow_entry[2]->network_id = "home--1";

    flow_entry[3] = CALLOC(1, sizeof(*flow_entry[3]));
    flow_entry[3]->device_mac = str2os_mac("AA:AA:AA:AA:AA:03");
    flow_entry[3]->direction = GKC_FLOW_DIRECTION_OUTBOUND;
    flow_entry[3]->src_port = htons(16);
    flow_entry[3]->dst_port = htons(444);
    flow_entry[3]->ip_version = 4;
    /* flow_entry[3]->action = FSM_ALLOW;  Without this, we exercise the default in get_protobuf_action_value() */
    flow_entry[3]->src_ip_addr = CALLOC(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "1.2.3.4", flow_entry[3]->src_ip_addr);
    flow_entry[3]->dst_ip_addr = CALLOC(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "10.12.14.13", flow_entry[3]->dst_ip_addr);
    flow_entry[3]->network_id = "home--2";
}

void
hero_stats_setUp(void)
{
    create_default_attr_entries();
    create_default_flow_entries();
}

void
hero_stats_tearDown(void)
{
    size_t idx;

    for (idx = 0; idx < num_attr_entries; idx++)
    {
        FREE(entry[idx]->ip_addr);
        FREE(entry[idx]->device_mac);
        FREE(entry[idx]->attr_name);
        FREE(entry[idx]);
    }
    FREE(entry);

    for (idx = 0; idx < num_flow_entries; idx++)
    {
        FREE(flow_entry[idx]->device_mac);
        FREE(flow_entry[idx]->src_ip_addr);
        FREE(flow_entry[idx]->dst_ip_addr);
        FREE(flow_entry[idx]);
    }
    FREE(flow_entry);
}

void
test_gkhc_init_aggregator(void)
{
    struct gkc_report_aggregator *aggr;
    struct fsm_session broken_session;
    bool ret;

    aggr = gkhc_get_aggregator();

    ret = gkhc_init_aggregator(aggr, NULL);
    TEST_ASSERT_FALSE(ret);

    broken_session.location_id = NULL;
    broken_session.node_id = NULL;
    ret = gkhc_init_aggregator(aggr, &broken_session);
    TEST_ASSERT_FALSE(ret);

    broken_session.location_id = "location";
    broken_session.node_id = NULL;
    ret = gkhc_init_aggregator(aggr, &broken_session);
    TEST_ASSERT_FALSE(ret);

    broken_session.location_id = NULL;
    broken_session.node_id = "node";
    ret = gkhc_init_aggregator(aggr, &broken_session);
    TEST_ASSERT_FALSE(ret);

    ret = gkhc_init_aggregator(aggr, &g_session);
    TEST_ASSERT_TRUE(ret);

    gkhc_release_aggregator(aggr);
}

void
test_gk_serialize_cache_no_mgr(void)
{
    struct gkc_report_aggregator *aggr;
    bool ret;

    aggr = gkhc_get_aggregator();

    /* Aggr is NULL */
    ret = gkhc_serialize_cache_entries(NULL);
    TEST_ASSERT_FALSE(ret);

    /* Aggr is not initialized */
    ret = gkhc_serialize_cache_entries(aggr);
    TEST_ASSERT_FALSE(ret);

    /* Try again after initializing */
    ret = gkhc_init_aggregator(aggr, &g_session);
    TEST_ASSERT_TRUE(ret);
    ret = gkhc_serialize_cache_entries(aggr);
    TEST_ASSERT_FALSE(ret);

    /* Initialize the cache and check for success */
    gk_cache_init(num_attr_entries);
    ret = gkhc_serialize_cache_entries(aggr);
    TEST_ASSERT_TRUE(ret);

    /* final cleanup */
    gk_cache_cleanup();
    gkhc_release_aggregator(aggr);
}

void
test_send_report_param(void)
{
    struct gkc_report_aggregator *aggr;
    int num_reports;
    bool ret;

    /* no aggr initialized */
    num_reports = gkhc_build_and_send_report(NULL, "valid_mqtt_topic");
    TEST_ASSERT_EQUAL_INT(-1, num_reports);

    /* get aggregator set up */
    aggr = gkhc_get_aggregator();
    ret = gkhc_init_aggregator(aggr, &g_session);
    TEST_ASSERT_TRUE(ret);

    num_reports = gkhc_build_and_send_report(aggr, NULL);
    TEST_ASSERT_EQUAL_INT(-1, num_reports);

    num_reports = gkhc_build_and_send_report(aggr, "");
    TEST_ASSERT_EQUAL_INT(-1, num_reports);

    gkhc_release_aggregator(aggr);
}

void
test_gk_serialize_cache_no_entries(void)
{
    struct gkc_report_aggregator *aggr;
    int num_reports;
    bool ret;

    aggr = gkhc_get_aggregator();

    gk_cache_init(num_attr_entries);

    /* We'll use the locally defined value in mock_get_config */
    g_session.ops = g_ops;

    ret = gkhc_init_aggregator(aggr, &g_session);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_size_t(500, aggr->report_max_size);

    gkhc_activate_window(aggr);
    TEST_ASSERT_NOT_EQUAL_INT(0, aggr->start_observation_window);
    TEST_ASSERT_EQUAL_INT(time(NULL), aggr->end_observation_window);

    ret = gkhc_serialize_cache_entries(aggr);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_INT(0, aggr->stats_idx);

    gkhc_close_window(aggr);
    TEST_ASSERT_NOT_EQUAL_INT(0, aggr->end_observation_window);

    num_reports = gkhc_build_and_send_report(aggr, "mqtt_topic");
    TEST_ASSERT_EQUAL_INT(0, num_reports);

    /* cleanup */
    gkhc_release_aggregator(aggr);
    gk_cache_cleanup();

    /* restore g_session */
    g_session.ops.get_config = NULL;
}

void
test_release_aggregator(void)
{
    Gatekeeper__HeroStats__HeroObservationWindow **aggr_windows;
    struct gkc_report_aggregator *aggr;
    bool ret;


    /* setup the aggregator */
    aggr = gkhc_get_aggregator();

    gkhc_set_max_record_size(aggr, 200);
    gkhc_set_records_per_report(aggr, 2);
    gkhc_set_number_obs_windows(aggr, 3);
    ret = gkhc_init_aggregator(aggr, &g_session);
    TEST_ASSERT_TRUE(ret);
    aggr_windows = aggr->windows;

    /* Do it once more to make sure we are still OK */
    ret = gkhc_init_aggregator(aggr, &g_session);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_TRUE(aggr->initialized);
    TEST_ASSERT_EQUAL_STRING(g_session.location_id, aggr->location_id);
    TEST_ASSERT_EQUAL_STRING(g_session.node_id, aggr->node_id);
    TEST_ASSERT_EQUAL_PTR(aggr_windows, aggr->windows);
    TEST_ASSERT_EQUAL_size_t(200, aggr->report_max_size);
    TEST_ASSERT_EQUAL_size_t(2, aggr->stats_max);
    TEST_ASSERT_EQUAL_size_t(3, aggr->windows_max);
    TEST_ASSERT_EQUAL_INT(time(NULL), aggr->start_observation_window);
    TEST_ASSERT_EQUAL_INT(time(NULL), aggr->end_observation_window);

    /* Attempt to change internal sizes, will not happen as we are initialized */
    gkhc_set_max_record_size(aggr, 10);
    TEST_ASSERT_EQUAL_size_t(200, aggr->report_max_size);
    gkhc_set_records_per_report(aggr, 10);
    TEST_ASSERT_EQUAL_size_t(2, aggr->stats_max);
    gkhc_set_number_obs_windows(aggr, 50);
    TEST_ASSERT_EQUAL_size_t(3, aggr->windows_max);

    gkhc_release_aggregator(aggr);
    TEST_ASSERT_FALSE(aggr->initialized);

    /* Now add a few cache entries and verify _release_aggregator */
    gkhc_init_aggregator(aggr, &g_session);
    gkhc_activate_window(aggr);
    TEST_ASSERT_NOT_EQUAL_INT(0, aggr->start_observation_window);

    gk_cache_init(num_attr_entries);

    gkc_add_attribute_entry(entry[1]);
    entry[1]->attribute_type = GK_CACHE_REQ_TYPE_URL;
    gkc_add_attribute_entry(entry[1]);

    FREE(entry[1]->attr_name);
    entry[1]->attr_name = STRDUP("www.test2.com");
    gkc_add_attribute_entry(entry[1]);

    /* Dump cache for this observation window */
    gkhc_close_window(aggr);
    TEST_ASSERT_NOT_EQUAL_INT(0, aggr->end_observation_window);

    /* There is now cleanup required to release the memory */
    gkhc_release_aggregator(aggr);

    /* Don't forget to clean this up */
    gk_cache_cleanup();
}

void
test_gk_serialize_cache_add_entries(void)
{
    struct gkc_report_aggregator *aggr;
    int num_sent_reports;

    aggr = gkhc_get_aggregator();

    gk_cache_init(num_attr_entries);

    /* use a fairly small size so we have more than one record per
     * report sent and a few files for the records in the cache
     */
    gkhc_set_max_record_size(aggr, 210);
    gkhc_set_records_per_report(aggr, 2);
    gkhc_set_number_obs_windows(aggr, 3);
    gkhc_init_aggregator(aggr, &g_session);
    /* attach a "fake" sender */
    aggr->send_report = ut_qm_conn_send_direct;

    gkhc_activate_window(aggr);

    /* populate a bunch of entries in the cache */
    gkc_add_attribute_entry(entry[1]);

    entry[1]->attribute_type = GK_CACHE_REQ_TYPE_URL;
    gkc_add_attribute_entry(entry[1]);

    FREE(entry[1]->attr_name);
    entry[1]->attr_name = STRDUP("www.test2.com");
    entry[1]->gk_policy = "NEW POLICY";
    gkc_add_attribute_entry(entry[1]);
    /*
     * Note: at this point, gk_policy now contains a copy of the original !
     * it must be explicitly freed
     */

    FREE(entry[1]->attr_name);
    entry[1]->attr_name = STRDUP("www.test2.com");
    gkc_add_attribute_entry(entry[1]); /* Duplicate won't count ! */

    gkc_add_attribute_entry(entry[2]); /* Has a broken IPv4 ! Not inserted */
    gkc_add_attribute_entry(entry[3]);
    gkc_add_attribute_entry(entry[4]);

    FREE(entry[1]->attr_name);
    entry[1]->attr_name = STRDUP("www.test4.com");
    entry[1]->action = FSM_BLOCK;
    gkc_add_attribute_entry(entry[1]);

    gkc_add_attribute_entry(entry[5]);

    gkc_add_flow_entry(flow_entry[0]);

    FREE(entry[1]->attr_name);
    entry[1]->attr_name = STRDUP("www.test5.com");
    entry[1]->action = FSM_ALLOW;
    gkc_add_attribute_entry(entry[1]);

    gkc_add_flow_entry(flow_entry[2]);
    gkc_add_flow_entry(flow_entry[3]);
    /* all the entries are now in... */

    LOGI("Sleep 2 seconds to create an observation window");
    sleep(2);

    gkhc_close_window(aggr);

    num_sent_reports = gkhc_build_and_send_report(aggr, "mqtt_channel_name");
    /*
     * The number of reports depends on their size. This could end up changing over time
     * if/when we add fields or change data size.
     * Currently we have 4 files of resp size: 156, 180, 181 and 143.
     * See call to gkhc_set_max_record_size() above.
     */
    TEST_ASSERT_EQUAL_INT(4, num_sent_reports);

    /* Simulate a case where no update to any cached entry happens */
    gkhc_activate_window(aggr);
    LOGI("Sleep 2 more seconds to create an empty observation window");
    sleep(2);
    gkhc_close_window(aggr);

    num_sent_reports = gkhc_build_and_send_report(aggr, "mqtt_channel_name");
    TEST_ASSERT_EQUAL_INT(0, num_sent_reports);

    /* Test case where we don't have a properly defined mqtt_topic */
    /* Next statement should not complain about double free */
    num_sent_reports = gkhc_build_and_send_report(aggr, NULL);
    TEST_ASSERT_EQUAL_INT(-1, num_sent_reports);
    gkhc_activate_window(aggr);
    gkhc_close_window(aggr);
    num_sent_reports = gkhc_build_and_send_report(aggr, NULL);
    TEST_ASSERT_EQUAL_INT(-1, num_sent_reports);

    /* Cleanup */
    gk_cache_cleanup();
    gkhc_release_aggregator(aggr);
}

void
test_gk_serialize_cache_redirect_cname_entries(void)
{
    struct gkc_report_aggregator *aggr;
    int num_sent_reports;

    aggr = gkhc_get_aggregator();

    gk_cache_init(num_attr_entries);

    /* use a fairly small size so we have more than one record per
     * report sent and a few files for the records in the cache
     */
    gkhc_set_max_record_size(aggr, 200);
    gkhc_set_records_per_report(aggr, 2);
    gkhc_set_number_obs_windows(aggr, 3);
    gkhc_init_aggregator(aggr, &g_session);
    /* attach a "fake" sender */
    aggr->send_report = ut_qm_conn_send_direct;

    gkhc_activate_window(aggr);
    entry[1]->action = FSM_REDIRECT_ALLOW;
    entry[1]->fqdn_redirect = CALLOC(1, sizeof(struct fqdn_redirect_s));
    entry[1]->fqdn_redirect->redirect = true;
    entry[1]->fqdn_redirect->redirect_cname = "safesearch.com";

    /* populate a bunch of entries in the cache */
    gkc_add_attribute_entry(entry[1]);
    FREE(entry[1]->fqdn_redirect);

    LOGI("Sleep 2 seconds to create an observation window");
    sleep(2);

    gkhc_close_window(aggr);

    num_sent_reports = gkhc_build_and_send_report(aggr, "mqtt_channel_name");
    TEST_ASSERT_EQUAL_INT(1, num_sent_reports);

    /* Cleanup */
    gk_cache_cleanup();
    gkhc_release_aggregator(aggr);
}

void
test_gk_serialize_cache_redirect_ip_entries(void)
{
    struct gkc_report_aggregator *aggr;
    int num_sent_reports;

    aggr = gkhc_get_aggregator();

    gk_cache_init(num_attr_entries);

    /* use a fairly small size so we have more than one record per
     * report sent and a few files for the records in the cache
     */
    gkhc_set_max_record_size(aggr, 200);
    gkhc_set_records_per_report(aggr, 2);
    gkhc_set_number_obs_windows(aggr, 3);
    gkhc_init_aggregator(aggr, &g_session);
    /* attach a "fake" sender */
    aggr->send_report = ut_qm_conn_send_direct;

    gkhc_activate_window(aggr);

    entry[0]->fqdn_redirect = CALLOC(1, sizeof(struct fqdn_redirect_s));
    entry[0]->fqdn_redirect->redirect = true;
    STRSCPY(entry[0]->fqdn_redirect->redirect_ips[0], "1.2.3.4");

    /* populate a bunch of entries in the cache */
    gkc_add_attribute_entry(entry[0]);
    FREE(entry[0]->fqdn_redirect);

    entry[1]->fqdn_redirect = CALLOC(1, sizeof(struct fqdn_redirect_s));
    entry[1]->fqdn_redirect->redirect = true;
    STRSCPY(entry[1]->fqdn_redirect->redirect_ips[1], "fe80::1");

    /* populate a bunch of entries in the cache */
    gkc_add_attribute_entry(entry[1]);
    FREE(entry[1]->fqdn_redirect);

    LOGI("Sleep 2 seconds to create an observation window");
    sleep(2);

    gkhc_close_window(aggr);

    num_sent_reports = gkhc_build_and_send_report(aggr, "mqtt_channel_name");
    TEST_ASSERT_EQUAL_INT(1, num_sent_reports);

    /* Simulate a case where no update to any cached entry happens */
    gkhc_activate_window(aggr);
    LOGI("Sleep 2 more seconds to create an empty observation window");
    sleep(2);

    entry[0]->fqdn_redirect = CALLOC(1, sizeof(struct fqdn_redirect_s));
    entry[0]->fqdn_redirect->redirect = true;
    STRSCPY(entry[0]->fqdn_redirect->redirect_ips[0], "1.2.3.4");
    STRSCPY(entry[0]->fqdn_redirect->redirect_ips[1], "fe80::1");

    /* populate a bunch of entries in the cache */
    gkc_add_attribute_entry(entry[0]);
    FREE(entry[0]->fqdn_redirect);

    LOGI("Sleep 2 seconds to create an observation window");
    sleep(2);

    gkhc_close_window(aggr);

    num_sent_reports = gkhc_build_and_send_report(aggr, "mqtt_channel_name");
    TEST_ASSERT_EQUAL_INT(1, num_sent_reports);

    /* Cleanup */
    gk_cache_cleanup();
    gkhc_release_aggregator(aggr);
}

void
test_gk_serialize_cache_action_by_name_entries(void)
{
    struct gkc_report_aggregator *aggr;
    int num_sent_reports;

    aggr = gkhc_get_aggregator();

    gk_cache_init(num_attr_entries);

    /* use a fairly small size so we have more than one record per
     * report sent and a few files for the records in the cache
     */
    gkhc_set_max_record_size(aggr, 200);
    gkhc_set_records_per_report(aggr, 2);
    gkhc_set_number_obs_windows(aggr, 3);
    gkhc_init_aggregator(aggr, &g_session);
    /* attach a "fake" sender */
    aggr->send_report = ut_qm_conn_send_direct;

    gkhc_activate_window(aggr);

    /* populate a bunch of entries in the cache */
    gkc_add_attribute_entry(entry[6]);

    /* populate a bunch of entries in the cache */
    gkc_add_attribute_entry(entry[7]);

    LOGI("Sleep 2 seconds to create an observation window");
    sleep(2);

    gkhc_close_window(aggr);

    num_sent_reports = gkhc_build_and_send_report(aggr, "mqtt_channel_name");
    TEST_ASSERT_NOT_EQUAL_INT(0, num_sent_reports);
}

void
run_test_gatekeeper_hero_stats(void)
{
    struct gkc_report_aggregator *aggr;

    ut_setUp_tearDown(__func__, hero_stats_setUp, hero_stats_tearDown);

    aggr = gkhc_get_aggregator();

    /* attach a "fake" sender */
    aggr->send_report = ut_qm_conn_send_direct;

    RUN_TEST(test_gkhc_init_aggregator);
    RUN_TEST(test_gk_serialize_cache_no_mgr);
    RUN_TEST(test_send_report_param);
    RUN_TEST(test_gk_serialize_cache_no_entries);
    RUN_TEST(test_release_aggregator);
    RUN_TEST(test_gk_serialize_cache_add_entries);
    RUN_TEST(test_gk_serialize_cache_redirect_cname_entries);
    RUN_TEST(test_gk_serialize_cache_redirect_ip_entries);
    RUN_TEST(test_gk_serialize_cache_action_by_name_entries);

    ut_setUp_tearDown(NULL, NULL, NULL);
}
