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

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "adv_data_typing.pb-c.h"
#include "fsm.h"
#include "fsm_dpi_adt.h"
#include "fsm_dpi_client_plugin.h"
#include "log.h"
#include "memutil.h"
#include "fsm_dpi_adt_cache_internal.h"
#include "network_metadata_report.h"
#include "os.h"
#include "os_nif.h"
#include "os_types.h"
#include "qm_conn.h"
#include "unity.h"
#include "unity_internals.h"
#include "fsm_dpi_adt_cache.h"

#define TEST_NETWORK_ID "test_network_id"

static union fsm_plugin_ops g_plugin_ops =
{
    .web_cat_ops =
    {
        .categories_check = NULL,
        .risk_level_check = NULL,
        .cat2str = NULL,
        .get_stats = NULL,
        .dns_response = NULL,
        .gatekeeper_req = NULL,
    },
};

static char *
test_fsm_get_network_id(struct fsm_session *session, os_macaddr_t *mac)
{
    return TEST_NETWORK_ID;
}

static struct fsm_session_ops g_sess_ops =
{
    .get_network_id = test_fsm_get_network_id,
};

static struct fsm_session g_session =
{
    .node_id = "NODE_ID",
    .location_id = "LOCATION_ID",
    .topic = "ADT_TOPIC",
};

static bool g_mock_enabled;

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
mock_ut_qm_conn_send_direct(qm_compress_t compress, char *topic,
                            void *data, int data_size, qm_response_t *res)
{
    char filename[100];
    char str_mac[32];
    static size_t i;
    FILE *fd;

    Interfaces__Adt__AdtReport *report;

    sprintf(filename, "/tmp/%s_%zu.pb", topic, i++);

    LOGI("Sending report to file %s", filename);

    fd = fopen(filename, "wb");
    TEST_ASSERT_NOT_NULL(fd);
    fwrite((char *)data, data_size, 1, fd);
    fclose(fd);

    /* perform some minimal check on the protobuf */
    report = interfaces__adt__adt_report__unpack(NULL, data_size, data);
    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(report);
    TEST_ASSERT_NOT_NULL(report->observation_point);

    TEST_ASSERT_NOT_NULL(report->observation_point->location_id);
    TEST_ASSERT_EQUAL_STRING(g_session.location_id, report->observation_point->location_id);

    TEST_ASSERT_NOT_NULL(report->observation_point->node_id);
    TEST_ASSERT_EQUAL_STRING(g_session.node_id, report->observation_point->node_id);

    TEST_ASSERT_NOT_NULL(report->data);
    TEST_ASSERT_EQUAL_INT(1, report->n_data);

    TEST_ASSERT_NOT_NULL(report->data[0]->device_id.data);
    os_mac2str(&report->data[0]->device_id, str_mac);
    LOGI("From protobuf: data[0], device_id=%s", str_mac);

    TEST_ASSERT_EQUAL_STRING(TEST_NETWORK_ID, report->data[0]->network_zone);
    LOGI("From protobuf: data[0], network_zone=%s", report->data[0]->network_zone);

    interfaces__adt__adt_report__free_unpacked(report, NULL);

    return true;
}

static char *
mock_get_config(struct fsm_session *session, char *key)
{
    (void)session;

    return key;
}

void
test_fsm_dpi_adt_init_cache(void)
{
    struct fsm_dpi_adt_cache *adt_cache;
    fsm_dpi_adt_init_cache();

    adt_cache = fsm_dpi_adt_get_cache_mgr();
    TEST_ASSERT_EQUAL_INT(0, adt_cache->counter);
    TEST_ASSERT_EQUAL_INT(ADT_CACHE_AGE_TIMER, adt_cache->age_time);
}

void
test_fsm_dpi_adt_add_to_cache_device(void)
{
    struct fsm_dpi_adt_device *adt_dev;
    struct fsm_dpi_adt_device *adt_test_dev;
    os_macaddr_t mac1 = { .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    os_macaddr_t mac2 = { .addr = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22}};
    os_macaddr_t mac3 = { .addr = {0x33, 0x33, 0x33, 0x33, 0x33, 0x33}};
    char *key = "http.host";
    char *value = "proxy-safebrowsing.googleapis.com";

    adt_test_dev = CALLOC(1, sizeof(*adt_test_dev));
    adt_test_dev->mac = CALLOC(1, sizeof(*adt_test_dev->mac));
    memcpy(adt_test_dev->mac, &mac1, sizeof(*adt_test_dev->mac));

    fsm_dpi_adt_init_cache();
    fsm_dpi_adt_add_to_cache(&mac1, key, value);
    fsm_dpi_adt_add_to_cache(&mac2, key, value);
    fsm_dpi_adt_add_to_cache(&mac3, key, value);
    adt_dev = fsm_dpi_adt_dev_cache_lookup(&mac1);
    TEST_ASSERT_NOT_NULL(adt_dev);
    adt_dev = fsm_dpi_adt_dev_cache_lookup(&mac2);
    TEST_ASSERT_NOT_NULL(adt_dev);
    adt_dev = fsm_dpi_adt_dev_cache_lookup(&mac3);
    TEST_ASSERT_NOT_NULL(adt_dev);

    fsm_dpi_adt_clear_cache();
    adt_dev = fsm_dpi_adt_dev_cache_lookup(&mac1);
    TEST_ASSERT_NULL(adt_dev);
    adt_dev = fsm_dpi_adt_dev_cache_lookup(&mac2);
    TEST_ASSERT_NULL(adt_dev);
    adt_dev = fsm_dpi_adt_dev_cache_lookup(&mac3);
    TEST_ASSERT_NULL(adt_dev);

    FREE(adt_test_dev->mac);
    FREE(adt_test_dev);
}

void
test_fsm_dpi_adt_add_to_cache_key(void)
{
    struct fsm_dpi_adt_device *adt_dev;
    struct fsm_dpi_adt_device *adt_test_dev;

    os_macaddr_t mac1 = { .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    char *key1 = "http.host";
    char *key2 = "https.host";
    char *value = "proxy-safebrowsing.googleapis.com";

    adt_test_dev = CALLOC(1, sizeof(*adt_test_dev));
    adt_test_dev->mac = CALLOC(1, sizeof(*adt_test_dev->mac));
    memcpy(adt_test_dev->mac, &mac1, sizeof(*adt_test_dev->mac));

    fsm_dpi_adt_init_cache();
    fsm_dpi_adt_add_to_cache(&mac1, key1, value);
    fsm_dpi_adt_add_to_cache(&mac1, key2, value);

    adt_dev = fsm_dpi_adt_dev_cache_lookup(&mac1);
    TEST_ASSERT_NOT_NULL(adt_dev);
    fsm_dpi_adt_clear_cache();

    FREE(adt_test_dev->mac);
    FREE(adt_test_dev);
}

void
test_fsm_dpi_adt_add_to_cache_value(void)
{
    os_macaddr_t mac = { .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    struct fsm_dpi_adt_device *adt_dev_check;
    struct fsm_dpi_adt_key *adt_key_check;
    struct fsm_dpi_adt_value *adt_value_check;
    struct fsm_dpi_adt_device *adt_dev;
    bool ret;
    char *key = "http.host";
    char *value = "google.com";
    char *value1 = "yahoo.com";

    adt_dev = CALLOC(1, sizeof(*adt_dev));
    adt_dev->mac = CALLOC(1, sizeof(*adt_dev->mac));
    memcpy(adt_dev->mac, &mac, sizeof(*adt_dev->mac));

    fsm_dpi_adt_init_cache();
    ret = fsm_dpi_adt_add_to_cache(&mac, key, value);
    ret = fsm_dpi_adt_add_to_cache(&mac, key, value1);
    TEST_ASSERT_TRUE(ret);

    adt_dev_check = fsm_dpi_adt_dev_cache_lookup(&mac);
    TEST_ASSERT_NOT_NULL(adt_dev_check);

    adt_key_check = fsm_dpi_adt_key_cache_lookup(adt_dev_check, key);
    TEST_ASSERT_NOT_NULL(adt_key_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value1);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    fsm_adt_adt_dump_cache();
    fsm_dpi_adt_clear_cache();
    FREE(adt_dev->mac);
    FREE(adt_dev);
}

void
test_fsm_dpi_adt_check_cache_count(void)
{
    os_macaddr_t mac = { .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    struct fsm_dpi_adt_device *adt_dev_check;
    struct fsm_dpi_adt_key *adt_key_check;
    struct fsm_dpi_adt_value *adt_value_check;
    struct fsm_dpi_adt_device *adt_dev;
    uint64_t cache_count;

    bool ret;
    char *key = "http.host";
    char *value = "google.com";
    char *value1 = "yahoo.com";

    adt_dev = CALLOC(1, sizeof(*adt_dev));
    adt_dev->mac = CALLOC(1, sizeof(*adt_dev->mac));
    memcpy(adt_dev->mac, &mac, sizeof(*adt_dev->mac));

    fsm_dpi_adt_init_cache();
    ret = fsm_dpi_adt_add_to_cache(&mac, key, value);
    cache_count = fsm_dpi_adt_get_cache_count();
    TEST_ASSERT_EQUAL_INT(cache_count, 3);
    ret = fsm_dpi_adt_add_to_cache(&mac, key, value1);
    TEST_ASSERT_TRUE(ret);

    adt_dev_check = fsm_dpi_adt_dev_cache_lookup(&mac);
    TEST_ASSERT_NOT_NULL(adt_dev_check);

    adt_key_check = fsm_dpi_adt_key_cache_lookup(adt_dev_check, key);
    TEST_ASSERT_NOT_NULL(adt_key_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value1);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    fsm_dpi_adt_clear_cache();
    cache_count = fsm_dpi_adt_get_cache_count();
    TEST_ASSERT_EQUAL_INT(cache_count, 0);
    FREE(adt_dev->mac);
    FREE(adt_dev);
}

void
test_fsm_dpi_adt_remove_expired_devices(void)
{
    os_macaddr_t mac = { .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    struct fsm_dpi_adt_value *adt_value_check;
    struct fsm_dpi_adt_device *adt_dev_check;
    struct fsm_dpi_adt_key *adt_key_check;
    struct fsm_dpi_adt_cache *adt_cache;
    struct fsm_dpi_adt_device *adt_dev;
    char *key = "http.host";
    char *value = "google.com";
    char *value1 = "yahoo.com";
    uint64_t cache_count;
    bool ret;

    adt_dev = CALLOC(1, sizeof(*adt_dev));
    adt_dev->mac = CALLOC(1, sizeof(*adt_dev->mac));
    memcpy(adt_dev->mac, &mac, sizeof(*adt_dev->mac));

    fsm_dpi_adt_init_cache();
    adt_cache = fsm_dpi_adt_get_cache_mgr();
    adt_cache->age_time = 2;
    ret = fsm_dpi_adt_add_to_cache(&mac, key, value);
    cache_count = fsm_dpi_adt_get_cache_count();
    TEST_ASSERT_EQUAL_INT(3, cache_count);
    ret = fsm_dpi_adt_add_to_cache(&mac, key, value1);
    TEST_ASSERT_TRUE(ret);

    adt_dev_check = fsm_dpi_adt_dev_cache_lookup(&mac);
    TEST_ASSERT_NOT_NULL(adt_dev_check);

    adt_key_check = fsm_dpi_adt_key_cache_lookup(adt_dev_check, key);
    TEST_ASSERT_NOT_NULL(adt_key_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value1);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    fsm_adt_adt_dump_cache();
    sleep(4);
    fsm_dpi_adt_remove_expired_devices(adt_cache);
    cache_count = fsm_dpi_adt_get_cache_count();
    TEST_ASSERT_EQUAL_INT(0, cache_count);
    fsm_dpi_adt_clear_cache();
    FREE(adt_dev->mac);
    FREE(adt_dev);
}

void
test_fsm_dpi_adt_remove_expired_entries(void)
{
    os_macaddr_t mac = { .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    os_macaddr_t mac2 = { .addr = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}};
    struct fsm_dpi_adt_value *adt_value_check;
    struct fsm_dpi_adt_device *adt_dev_check;
    struct fsm_dpi_adt_key *adt_key_check;
    struct fsm_dpi_adt_cache *adt_cache;
    struct fsm_dpi_adt_device *adt_dev;
    char *key1 = "http.host";
    char *key2 = "https.host";
    char *value1 = "google.com";
    char *value2 = "yahoo.com";
    char *value3 = "aaa.com";
    int expected_entries = 0;
    bool ret;

    adt_dev = CALLOC(1, sizeof(*adt_dev));
    adt_dev->mac = CALLOC(1, sizeof(*adt_dev->mac));
    memcpy(adt_dev->mac, &mac, sizeof(*adt_dev->mac));

    fsm_dpi_adt_init_cache();
    adt_cache = fsm_dpi_adt_get_cache_mgr();
    /* set the age time to 2 secs */
    adt_cache->age_time = 2;
    /* Add the device, key1 and value1 */
    expected_entries = 3;
    ret = fsm_dpi_adt_add_to_cache(&mac, key1, value1);
    TEST_ASSERT_EQUAL_INT(expected_entries, fsm_dpi_adt_get_cache_count());
    /* sleep till age timer expires */
    sleep(3);
    /* add value2 to cache, this also updates add time on mac and key1 */
    ret = fsm_dpi_adt_add_to_cache(&mac, key1, value2);
    expected_entries = 4;
    TEST_ASSERT_TRUE(ret);
    /* cache contains dev, key1, value1 and value2 */
    TEST_ASSERT_EQUAL_INT(expected_entries, fsm_dpi_adt_get_cache_count());
    /* only value1 entry is expired, so it should be removed */
    fsm_dpi_adt_remove_expired_devices(adt_cache);
    /* cache contains dev, key1 and value2 */
    expected_entries--; // current value=3
    TEST_ASSERT_EQUAL_INT(expected_entries, fsm_dpi_adt_get_cache_count());

    adt_dev_check = fsm_dpi_adt_dev_cache_lookup(&mac);
    TEST_ASSERT_NOT_NULL(adt_dev_check);
    adt_key_check = fsm_dpi_adt_key_cache_lookup(adt_dev_check, key1);
    TEST_ASSERT_NOT_NULL(adt_key_check);
    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value2);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    sleep(3);
    ret = fsm_dpi_adt_add_to_cache(&mac, key2, value3);
    expected_entries += 2;// current value = 5 (key2 and value3 is added)
    /* cache entries: dev, key1, value2, key2, value3 */
    TEST_ASSERT_EQUAL_INT(expected_entries, fsm_dpi_adt_get_cache_count());
    /* key1, value2 entries should be removed */
    fsm_dpi_adt_remove_expired_devices(adt_cache);
    expected_entries -= 2; //current value 3
    /* cache entries: dev, key2, value3 */
    TEST_ASSERT_EQUAL_INT(expected_entries, fsm_dpi_adt_get_cache_count());

    /* cache entries: (dev, key2, value3) and (dev2, key1, value2) */
    sleep(3);
    fsm_dpi_adt_add_to_cache(&mac2, key1, value2);
    expected_entries += 3;
    TEST_ASSERT_EQUAL_INT(expected_entries, fsm_dpi_adt_get_cache_count());
    fsm_dpi_adt_remove_expired_devices(adt_cache);
    expected_entries -= 3; // current value 3 (dev, key2, value3) will be removed
    /* current cache: (dev2, key1, value2) */
    TEST_ASSERT_EQUAL_INT(expected_entries, fsm_dpi_adt_get_cache_count());

    sleep(3);
    fsm_dpi_adt_remove_expired_devices(adt_cache);
    expected_entries -=3; // current value 0
    TEST_ASSERT_EQUAL_INT(expected_entries, fsm_dpi_adt_get_cache_count());
    FREE(adt_dev->mac);
    FREE(adt_dev);
}

void
test_fsm_dpi_adt_remove_expired_keys(void)
{
    os_macaddr_t mac = { .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    struct fsm_dpi_adt_value *adt_value_check;
    struct fsm_dpi_adt_device *adt_dev_check;
    struct fsm_dpi_adt_key *adt_key_check;
    struct fsm_dpi_adt_cache *adt_cache;
    struct fsm_dpi_adt_device *adt_dev;
    char *key = "http.host";
    char *value = "google.com";
    char *value1 = "yahoo.com";
    uint64_t cache_count;
    bool ret;

    adt_dev = CALLOC(1, sizeof(*adt_dev));
    adt_dev->mac = CALLOC(1, sizeof(*adt_dev->mac));
    memcpy(adt_dev->mac, &mac, sizeof(*adt_dev->mac));

    fsm_dpi_adt_init_cache();
    adt_cache = fsm_dpi_adt_get_cache_mgr();
    adt_cache->age_time = 2;
    ret = fsm_dpi_adt_add_to_cache(&mac, key, value);

    ret = fsm_dpi_adt_add_to_cache(&mac, key, value1);
    TEST_ASSERT_TRUE(ret);
    cache_count = fsm_dpi_adt_get_cache_count();
    TEST_ASSERT_EQUAL_INT(4, cache_count);

    adt_dev_check = fsm_dpi_adt_dev_cache_lookup(&mac);
    TEST_ASSERT_NOT_NULL(adt_dev_check);

    adt_key_check = fsm_dpi_adt_key_cache_lookup(adt_dev_check, key);
    TEST_ASSERT_NOT_NULL(adt_key_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value1);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    fsm_adt_adt_dump_cache();
    sleep(4);
    fsm_dpi_adt_remove_expired_keys(adt_dev_check);
    cache_count = fsm_dpi_adt_get_cache_count();
    TEST_ASSERT_EQUAL_INT(1, cache_count);
    fsm_dpi_adt_clear_cache();
    FREE(adt_dev->mac);
    FREE(adt_dev);
}

void
test_fsm_dpi_adt_remove_expired_values(void)
{
    os_macaddr_t mac = { .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    struct fsm_dpi_adt_value *adt_value_check;
    struct fsm_dpi_adt_device *adt_dev_check;
    struct fsm_dpi_adt_key *adt_key_check;
    struct fsm_dpi_adt_cache *adt_cache;
    struct fsm_dpi_adt_device *adt_dev;
    char *key = "http.host";
    char *value = "google.com";
    char *value1 = "yahoo.com";
    uint64_t cache_count;
    bool ret;

    adt_dev = CALLOC(1, sizeof(*adt_dev));
    adt_dev->mac = CALLOC(1, sizeof(*adt_dev->mac));
    memcpy(adt_dev->mac, &mac, sizeof(*adt_dev->mac));

    fsm_dpi_adt_init_cache();
    adt_cache = fsm_dpi_adt_get_cache_mgr();
    adt_cache->age_time = 2;
    ret = fsm_dpi_adt_add_to_cache(&mac, key, value);

    ret = fsm_dpi_adt_add_to_cache(&mac, key, value1);
    TEST_ASSERT_TRUE(ret);
    cache_count = fsm_dpi_adt_get_cache_count();
    TEST_ASSERT_EQUAL_INT(4, cache_count);

    adt_dev_check = fsm_dpi_adt_dev_cache_lookup(&mac);
    TEST_ASSERT_NOT_NULL(adt_dev_check);

    adt_key_check = fsm_dpi_adt_key_cache_lookup(adt_dev_check, key);
    TEST_ASSERT_NOT_NULL(adt_key_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value1);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    fsm_adt_adt_dump_cache();
    sleep(4);
    fsm_dpi_adt_removed_expired_values(adt_key_check);
    cache_count = fsm_dpi_adt_get_cache_count();
    TEST_ASSERT_EQUAL_INT(2, cache_count);
    fsm_dpi_adt_clear_cache();
    FREE(adt_dev->mac);
    FREE(adt_dev);
}

void
test_fsm_dpi_adt_check_cache_before_reporting(void)
{
    struct fsm_dpi_plugin_client_pkt_info pkt_info;
    struct fsm_dpi_client_session *new_adt_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_adt_session *adt_session;
    struct net_md_stats_accumulator acc;
    struct fsm_session this_session;
    int ret;

    this_session = g_session;
    this_session.name = "dpi_adt";
    this_session.ops = g_sess_ops;
    this_session.p_ops = &g_plugin_ops;
    this_session.ops.get_config = mock_get_config;

    new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    this_session.handler_ctxt = new_adt_session;

    ret = fsm_dpi_adt_init(&this_session);
    TEST_ASSERT_EQUAL_INT(0, ret);

    adt_session = fsm_dpi_adt_get_session(&this_session);
    TEST_ASSERT_NOT_NULL(adt_session);
    aggr = adt_session->adt_aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    aggr->send_report = mock_ut_qm_conn_send_direct;

    MEMZERO(acc);
    pkt_info.acc = &acc;

    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.key = CALLOC(1, sizeof(*acc.key));
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;

    acc.key->smac = CALLOC(1, sizeof(*acc.key->smac));
    os_nif_macaddr_from_str(acc.key->smac, "00:11:22:33:44:55");

    acc.key->dmac = CALLOC(1, sizeof(*acc.key->dmac));
    os_nif_macaddr_from_str(acc.key->dmac, "AA:BB:CC:DD:EE:FF");

    acc.key->src_ip = CALLOC(4, sizeof(*acc.key->src_ip));
    inet_pton(AF_INET, "1.2.3.4", acc.key->src_ip);
    acc.key->sport = 401;
    acc.key->dst_ip = CALLOC(4, sizeof(*acc.key->dst_ip));
    inet_pton(AF_INET, "9.8.7.6", acc.key->dst_ip);
    acc.key->dport = 499;
    acc.key->ip_version = 4;

    /* entry is added to cache */
    ret = dpi_adt_store(&this_session, "the_key", RTS_TYPE_STRING, strlen("a_value"), "a_value", &pkt_info);
    TEST_ASSERT_TRUE(ret);

    /* entry already present in the cache, so returns false */
    ret = dpi_adt_store(&this_session, "the_key", RTS_TYPE_STRING, strlen("a_value"), "a_value", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* Clean up */
    fsm_dpi_adt_exit(&this_session);
    FREE(acc.key->dmac);
    FREE(acc.key->smac);
    FREE(acc.key->src_ip);
    FREE(acc.key->dst_ip);
    FREE(acc.key);
    FREE(new_adt_session);
}

void
test_fsm_dpi_adt_check_purge_cache(void)
{
    os_macaddr_t mac = { .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    struct fsm_dpi_adt_device *adt_dev_check;
    struct fsm_dpi_adt_key *adt_key_check;
    struct fsm_dpi_adt_value *adt_value_check;
    struct fsm_dpi_adt_cache *adt_cache;
    struct fsm_dpi_adt_device *adt_dev;
    uint64_t cache_count;
    bool ret;
    char *key = "http.host";
    char *value = "google.com";
    char *value1 = "yahoo.com";

    adt_dev = CALLOC(1, sizeof(*adt_dev));
    adt_dev->mac = CALLOC(1, sizeof(*adt_dev->mac));
    memcpy(adt_dev->mac, &mac, sizeof(*adt_dev->mac));

    fsm_dpi_adt_init_cache();
    adt_cache = fsm_dpi_adt_get_cache_mgr();
    adt_cache->age_time = 2;
    ret = fsm_dpi_adt_add_to_cache(&mac, key, value);
    cache_count = fsm_dpi_adt_get_cache_count();
    TEST_ASSERT_EQUAL_INT(3, cache_count);
    ret = fsm_dpi_adt_add_to_cache(&mac, key, value1);
    TEST_ASSERT_TRUE(ret);

    adt_dev_check = fsm_dpi_adt_dev_cache_lookup(&mac);
    TEST_ASSERT_NOT_NULL(adt_dev_check);

    adt_key_check = fsm_dpi_adt_key_cache_lookup(adt_dev_check, key);
    TEST_ASSERT_NOT_NULL(adt_key_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    adt_value_check = fsm_dpi_adt_value_cache_lookup(adt_key_check, value1);
    TEST_ASSERT_NOT_NULL(adt_value_check);

    fsm_adt_adt_dump_cache();
    sleep(4);
    fsm_dpi_adt_remove_expired_entries();
    cache_count = fsm_dpi_adt_get_cache_count();
    TEST_ASSERT_EQUAL_INT(0, cache_count);
    fsm_dpi_adt_clear_cache();
    FREE(adt_dev->mac);
    FREE(adt_dev);
}

void
test_fsm_dpi_adt_init(void)
{
    struct fsm_dpi_client_session *new_client_session;
    struct fsm_dpi_client_session *client_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_adt_session *adt_session;
    struct fsm_session working_session;
    struct fsm_session broken_session;
    int ret;

    client_session = (struct fsm_dpi_client_session *)g_session.handler_ctxt;
    TEST_ASSERT_NULL(client_session);

    fsm_dpi_adt_exit(&g_session);

    ret = fsm_dpi_adt_init(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    broken_session.location_id = NULL;
    broken_session.node_id = NULL;
    ret = fsm_dpi_adt_init(&broken_session);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    broken_session.location_id = "location";
    broken_session.node_id = NULL;
    ret = fsm_dpi_adt_init(&broken_session);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    broken_session.location_id = NULL;
    broken_session.node_id = "node";
    ret = fsm_dpi_adt_init(&broken_session);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    /* Still missing handler_context */
    working_session = g_session;
    ret = fsm_dpi_adt_init(&working_session);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    /* Don't forget to mock the required ops */
    working_session.ops.get_config = mock_get_config;

    /* add a handler_context, now we're complete */
    working_session.p_ops = &g_plugin_ops;

    new_client_session = CALLOC(1, sizeof(*new_client_session));
    working_session.handler_ctxt = new_client_session;

    ret = fsm_dpi_adt_init(&working_session);
    TEST_ASSERT_EQUAL_INT(0, ret);
    client_session = (struct fsm_dpi_client_session *)working_session.handler_ctxt;
    TEST_ASSERT_NOT_NULL(client_session);
    adt_session = (struct fsm_dpi_adt_session *)client_session->private_session;
    TEST_ASSERT_NOT_NULL(adt_session);
    aggr = adt_session->adt_aggr;
    TEST_ASSERT_TRUE(aggr->initialized);

    /* Already initialized: should return 1 */
    ret = fsm_dpi_adt_init(&working_session);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* Cleanup */
    fsm_dpi_adt_exit(&working_session);
    FREE(new_client_session);
}

void
test_fsm_dpi_adt_store(void)
{
    struct fsm_dpi_plugin_client_pkt_info pkt_info;
    struct fsm_dpi_client_session *new_adt_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_adt_session *adt_session;
    struct net_md_stats_accumulator acc;
    struct fsm_session this_session;
    int ret;

    this_session = g_session;
    this_session.name = "dpi_adt";
    this_session.ops = g_sess_ops;

    MEMZERO(acc);

    pkt_info.acc = &acc;

    /*
     * Do not check for a NULL session (first parameter), as the function assumes
     * the check is performed by the caller
     */

    /* incomplete session */
    ret = dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* Not initialized yet */
    new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    this_session.handler_ctxt = new_adt_session;
    ret = dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* Initializing aggregator */
    this_session.location_id = "1234";
    this_session.p_ops = &g_plugin_ops;
    this_session.ops.get_config = mock_get_config;

    ret = fsm_dpi_adt_init(&this_session);
    TEST_ASSERT_EQUAL_INT(0, ret);
    adt_session = fsm_dpi_adt_get_session(&this_session);
    TEST_ASSERT_NOT_NULL(adt_session);
    aggr = adt_session->adt_aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    TEST_ASSERT_TRUE(aggr->initialized);
    aggr->send_report = mock_ut_qm_conn_send_direct;

    MEMZERO(acc);

    /* Validate the parameters */
    ret = dpi_adt_store(&this_session, NULL, RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);
    ret = dpi_adt_store(&this_session, "", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);
    ret = dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, 0, NULL, &pkt_info);
    TEST_ASSERT_FALSE(ret);
    ret = dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, 0, "", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* Break on incomplete acc */
    ret = dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* Populate acc !!!!  There has to be a better way */
    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.key = CALLOC(1, sizeof(*acc.key));
    acc.key->direction = NET_MD_ACC_OUTBOUND_DIR;

    ret = dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* Clean things up */
    FREE(acc.key);
    fsm_dpi_adt_exit(&this_session);
    FREE(new_adt_session);
}

void
test_fsm_dpi_adt_store_to_proto(void)
{
    // struct fsm_dpi_client_session *new_adt_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_adt_session *adt_session;
    Interfaces__Adt__AdtReport report_pb;
    struct fsm_session this_session;
    bool ret;

    this_session = g_session;
    this_session.name = "dpi_adt";
    this_session.ops = g_sess_ops;
    this_session.p_ops = &g_plugin_ops;
    this_session.ops.get_config = mock_get_config;

    // new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    // this_session.handler_ctxt = new_adt_session;

    /* No destination memory */
    ret = dpi_adt_store2proto(NULL, NULL);
    TEST_ASSERT_FALSE(ret);

    /* No aggregator */
    ret = dpi_adt_store2proto(NULL, &report_pb);
    TEST_ASSERT_FALSE(ret);

    /* provide aggregator */
    ret = fsm_dpi_adt_init(&this_session);
    TEST_ASSERT_EQUAL_INT(0, ret);

    adt_session = fsm_dpi_adt_get_session(&this_session);
    TEST_ASSERT_NOT_NULL(adt_session);
    aggr = adt_session->adt_aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    aggr->send_report = mock_ut_qm_conn_send_direct;

    /* Nothing to report */
    ret = dpi_adt_store2proto(&this_session, &report_pb);
    TEST_ASSERT_FALSE(ret);

    /* Cleanup */
    fsm_dpi_adt_exit(&this_session);
    // FREE(new_adt_session);
}

void
test_fsm_dpi_adt_send_report_v4(void)
{
    struct fsm_dpi_plugin_client_pkt_info pkt_info;
    struct fsm_dpi_client_session *new_adt_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_adt_session *adt_session;
    struct net_md_stats_accumulator acc;
    struct fsm_session this_session;
    int ret;

    this_session = g_session;
    this_session.name = "dpi_adt";
    this_session.ops = g_sess_ops;
    this_session.p_ops = &g_plugin_ops;
    this_session.ops.get_config = mock_get_config;

    new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    this_session.handler_ctxt = new_adt_session;

    ret = fsm_dpi_adt_init(&this_session);
    TEST_ASSERT_EQUAL_INT(0, ret);

    adt_session = fsm_dpi_adt_get_session(&this_session);
    TEST_ASSERT_NOT_NULL(adt_session);
    aggr = adt_session->adt_aggr;
    TEST_ASSERT_NOT_NULL(aggr);

    /* attach a "fake" sender */
    if (g_mock_enabled) aggr->send_report = mock_ut_qm_conn_send_direct;

    /* nothing to report */
    ret = dpi_adt_send_report(&this_session);
    if (g_mock_enabled) TEST_ASSERT_TRUE(ret);

    MEMZERO(acc);
    pkt_info.acc = &acc;

    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.key = CALLOC(1, sizeof(*acc.key));
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;

    ret = dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    acc.key->smac = CALLOC(1, sizeof(*acc.key->smac));
    os_nif_macaddr_from_str(acc.key->smac, "00:11:22:33:44:55");

    acc.key->dmac = CALLOC(1, sizeof(*acc.key->dmac));
    os_nif_macaddr_from_str(acc.key->dmac, "AA:BB:CC:DD:EE:FF");

    acc.key->src_ip = CALLOC(4, sizeof(*acc.key->src_ip));
    inet_pton(AF_INET, "1.2.3.4", acc.key->src_ip);
    acc.key->sport = 401;
    acc.key->dst_ip = CALLOC(4, sizeof(*acc.key->dst_ip));
    inet_pton(AF_INET, "9.8.7.6", acc.key->dst_ip);
    acc.key->dport = 499;

    /* First with a bogus ip_version */
    acc.key->ip_version = 0;
    ret = dpi_adt_store(&this_session, "the_key", RTS_TYPE_STRING, strlen("a_value"), "a_value", &pkt_info);
    TEST_ASSERT_TRUE(ret);
    ret = dpi_adt_send_report(&this_session);
    /* Do not check when using qm_conn_send_report() as it will fail on x64 */
    if (g_mock_enabled) TEST_ASSERT_FALSE(ret);

    /* This time with the correct ip_version */
    acc.key->ip_version = 4;
    ret = dpi_adt_store(&this_session, "the_key", RTS_TYPE_STRING, strlen("b_value"), "b_value", &pkt_info);
    TEST_ASSERT_TRUE(ret);
    ret = dpi_adt_send_report(&this_session);
    /* Do not check when using qm_conn_send_report() as it will fail on x64 */
    if (g_mock_enabled) TEST_ASSERT_TRUE(ret);

    /* Clean up */
    fsm_dpi_adt_exit(&this_session);
    FREE(acc.key->dmac);
    FREE(acc.key->smac);
    FREE(acc.key->src_ip);
    FREE(acc.key->dst_ip);
    FREE(acc.key);
    FREE(new_adt_session);
}

void
test_fsm_dpi_adt_send_report_v6(void)
{
    struct fsm_dpi_plugin_client_pkt_info pkt_info;
    struct fsm_dpi_client_session *new_adt_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_adt_session *adt_session;
    struct net_md_stats_accumulator acc;
    struct fsm_session this_session;
    int ret;

    this_session = g_session;
    this_session.name = "dpi_adt";
    this_session.ops = g_sess_ops;
    this_session.p_ops = &g_plugin_ops;
    this_session.ops.get_config = mock_get_config;

    new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    this_session.handler_ctxt = new_adt_session;

    ret = fsm_dpi_adt_init(&this_session);
    TEST_ASSERT_EQUAL_INT(0, ret);

    adt_session = fsm_dpi_adt_get_session(&this_session);
    TEST_ASSERT_NOT_NULL(adt_session);
    aggr = adt_session->adt_aggr;
    TEST_ASSERT_NOT_NULL(aggr);

    /* attach a "fake" sender */
    if (g_mock_enabled) aggr->send_report = mock_ut_qm_conn_send_direct;

    MEMZERO(acc);
    pkt_info.acc = &acc;

    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.key = CALLOC(1, sizeof(*acc.key));
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;

    acc.key->ip_version = 6;

    acc.key->smac = CALLOC(1, sizeof(*acc.key->smac));
    os_nif_macaddr_from_str(acc.key->smac, "00:11:22:33:44:55");

    acc.key->dmac = CALLOC(1, sizeof(*acc.key->dmac));
    os_nif_macaddr_from_str(acc.key->dmac, "AA:BB:CC:DD:EE:FF");

    acc.key->src_ip = CALLOC(16, sizeof(*acc.key->src_ip));
    inet_pton(AF_INET6, "1:2::3", acc.key->src_ip);
    acc.key->sport = 601;
    acc.key->dst_ip = CALLOC(16, sizeof(*acc.key->dst_ip));
    acc.key->dport = 699;
    inet_pton(AF_INET6, "2:4::3", acc.key->dst_ip);

    ret = dpi_adt_store(&this_session, "the_key_16", RTS_TYPE_STRING, strlen("a_value_16"), "a_value_16", &pkt_info);
    TEST_ASSERT_TRUE(ret);
    ret = dpi_adt_send_report(&this_session);
    /* Do not check when using qm_conn_send_report() as it will fail on x64 */
    if (g_mock_enabled) TEST_ASSERT_TRUE(ret);

    /* Clean up */
    fsm_dpi_adt_exit(&this_session);
    FREE(acc.key->dmac);
    FREE(acc.key->smac);
    FREE(acc.key->src_ip);
    FREE(acc.key->dst_ip);
    FREE(acc.key);
    FREE(new_adt_session);
}

void
test_fsm_dpi_adt_from_top(void)
{
    struct fsm_dpi_client_session *new_adt_session;
    struct fsm_session this_session;
    int ret;

    this_session = g_session;
    this_session.name = "dpi_adt";
    new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    this_session.handler_ctxt = new_adt_session;
    this_session.p_ops = &g_plugin_ops;
    this_session.ops.get_config = mock_get_config;

    ret = fsm_dpi_adt_init(&this_session);
    TEST_ASSERT_EQUAL_INT(0, ret);

    fsm_dpi_adt_exit(&this_session);

    /* Cleanup */
    FREE(new_adt_session);
}

void
run_test_adt(void)
{
    RUN_TEST(test_fsm_dpi_adt_init);
    RUN_TEST(test_fsm_dpi_adt_init_cache);
    RUN_TEST(test_fsm_dpi_adt_add_to_cache_device);
    RUN_TEST(test_fsm_dpi_adt_add_to_cache_key);
    RUN_TEST(test_fsm_dpi_adt_add_to_cache_value);
    RUN_TEST(test_fsm_dpi_adt_check_cache_count);
    RUN_TEST(test_fsm_dpi_adt_check_purge_cache);
    RUN_TEST(test_fsm_dpi_adt_remove_expired_entries);
    RUN_TEST(test_fsm_dpi_adt_remove_expired_devices);
    RUN_TEST(test_fsm_dpi_adt_remove_expired_keys);
    RUN_TEST(test_fsm_dpi_adt_remove_expired_values);
    RUN_TEST(test_fsm_dpi_adt_check_cache_before_reporting);
    RUN_TEST(test_fsm_dpi_adt_store);
    RUN_TEST(test_fsm_dpi_adt_store_to_proto);

    /* first time with the mock function writing to disk */
    g_mock_enabled = true;
    RUN_TEST(test_fsm_dpi_adt_send_report_v4);
    RUN_TEST(test_fsm_dpi_adt_send_report_v6);

    /* second time will send using the actual qm_conn_send_report */
    g_mock_enabled = false;
    RUN_TEST(test_fsm_dpi_adt_send_report_v4);
    RUN_TEST(test_fsm_dpi_adt_send_report_v6);

    RUN_TEST(test_fsm_dpi_adt_from_top);
}
