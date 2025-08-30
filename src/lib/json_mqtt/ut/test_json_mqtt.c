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

#include "dhcp_parse.h"
#include "http_parse.h"
#include "json_mqtt.h"
#include "json_util.h"
#include "log.h"
#include "memutil.h"
#include "target.h"
#include "unit_test_utils.h"
#include "unity.h"
#include "upnp_parse.h"

extern char *version;
char *test_name = "test_json_mqtt";

/* Expose function to UT */
void
jencode_header(struct fsm_session *session, json_t *json_report);

void
test_jencode_header(void)
{
    struct fsm_session session = {
        .location_id = "LOC",
        .node_id = "NODE",
    };
    json_t *json_report = NULL;
    json_t *field;

    json_report = json_object();
    jencode_header(&session, json_report);

    field = json_object_get(json_report, "locationId");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_string(field));
    TEST_ASSERT_EQUAL_STRING("LOC", json_string_value(field));

    field = json_object_get(json_report, "nodeId");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_string(field));
    TEST_ASSERT_EQUAL_STRING("NODE", json_string_value(field));

    field = json_object_get(json_report, "version");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_string(field));
    TEST_ASSERT_EQUAL_STRING(version, json_string_value(field));

    field = json_object_get(json_report, "reportedAt");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_string(field));
    LOGN("Header: reportedAt = %s", json_string_value(field));

    json_decref(json_report);
}


void
test_jencode_user_agent(void)
{
    struct fsm_session session = { 0 };
    struct http_parse_report http_report = {
        .user_agent = "HTTP_UA",
    };
    char *out = NULL;

    /* We need to go at least once in all the checks of
     * jcheck_header_info */
    out = jencode_user_agent(NULL, NULL);
    TEST_ASSERT_NULL(out);

    out = jencode_user_agent(&session, &http_report);
    TEST_ASSERT_NULL(out);

    session.topic = "TOPIC";
    out = jencode_user_agent(&session, &http_report);
    TEST_ASSERT_NULL(out);

    session.location_id = "LOCATION";
    out = jencode_user_agent(&session, &http_report);
    TEST_ASSERT_NULL(out);

    session.node_id = "NODE";

    /* ensure we have something to report */
    out = jencode_user_agent(&session, NULL);
    TEST_ASSERT_NULL(out);

    /* our session is now complete, proceed with the UA test */
    out = jencode_user_agent(&session, &http_report);
    TEST_ASSERT_NOT_NULL(out);

    LOGD("%s(): %s", __func__, out);
    json_free(out);
}


/* Expose function to UT */
json_t *
json_url_report(struct fsm_session *session,
                struct fqdn_pending_req *to_report,
                struct fsm_policy_reply *policy_reply);

void
test_jencode_url_report(void)
{
    struct fsm_session session = {
        .location_id = "LOC",
        .node_id = "NODE",
        .topic = NULL,
    };
    struct fqdn_pending_req to_report;
    struct fsm_policy_reply policy_reply;
    json_t *field, *entry, *array_entry, *out_json;

    memset(&policy_reply, 0, sizeof(policy_reply));
    memset(&to_report, 0, sizeof(to_report));

    char *out = NULL;

    /* Sanity checks */
    out = jencode_url_report(NULL, NULL, NULL);
    TEST_ASSERT_NULL(out);

    out = jencode_url_report(&session, NULL, NULL);
    TEST_ASSERT_NULL(out);

    /* we have not provided a topic */
    out = jencode_url_report(&session, NULL, &policy_reply);
    TEST_ASSERT_NULL(out);


    /* Complete the session to execute */
    session.topic = "TOPIC";

    /* Prep the report */
    to_report.req_info = CALLOC(1, sizeof(*to_report.req_info));
    policy_reply.policy = "POLICY";
    policy_reply.rule_name = "rule";
    policy_reply.action = FSM_OBSERVED;
    to_report.req_info->reply = CALLOC(1, sizeof(*to_report.req_info->reply));

    /* FQDN query */
    policy_reply.req_type = FSM_FQDN_REQ;
    strncpy(to_report.req_info->url, "http://www.foo.com/index.html", sizeof(to_report.req_info->url));

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "dnsQueries");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_array(field));
    TEST_ASSERT_EQUAL_size_t(1, json_array_size(field));

    array_entry = json_array_get(field, 0);


    field = json_object_get(array_entry, "dnsAddress");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_STRING("http://www.foo.com/index.html", json_string_value(field));

    field = json_object_get(array_entry, "policy");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_STRING("POLICY", json_string_value(field));

    json_decref(out_json);


    /* URL query */
    policy_reply.req_type = FSM_URL_REQ;

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "httpUrlQueries");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_array(field));
    TEST_ASSERT_EQUAL_size_t(1, json_array_size(field));

    array_entry = json_array_get(field, 0);

    field = json_object_get(array_entry, "httpUrl");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_STRING("http://www.foo.com/index.html", json_string_value(field));

    json_decref(out_json);

    /* HOST query */
    policy_reply.req_type = FSM_HOST_REQ;

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "httpHostQueries");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_array(field));
    TEST_ASSERT_EQUAL_size_t(1, json_array_size(field));

    array_entry = json_array_get(field, 0);

    field = json_object_get(array_entry, "httpHost");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_STRING("http://www.foo.com/index.html", json_string_value(field));

    json_decref(out_json);

    /* SNI query */
    policy_reply.req_type = FSM_SNI_REQ;
    strncpy(to_report.req_info->url, "hostname.com", sizeof(to_report.req_info->url));

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "httpsSniQueries");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_array(field));
    TEST_ASSERT_EQUAL_size_t(1, json_array_size(field));

    array_entry = json_array_get(field, 0);

    field = json_object_get(array_entry, "httpsSni");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_STRING("hostname.com", json_string_value(field));

    json_decref(out_json);

    /* Not a good query */
    policy_reply.req_type = 1234;
    strncpy(to_report.req_info->url, "hostname.com", sizeof(to_report.req_info->url));

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "unknownTypeQueries");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_array(field));
    TEST_ASSERT_EQUAL_size_t(1, json_array_size(field));

    array_entry = json_array_get(field, 0);

    field = json_object_get(array_entry, "unknownType");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_STRING("hostname.com", json_string_value(field));

    json_decref(out_json);


    /* APP query */
    policy_reply.req_type = FSM_APP_REQ;
    strncpy(to_report.req_info->url, "my_application.exe", sizeof(to_report.req_info->url));

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "appNameQueries");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_array(field));
    TEST_ASSERT_EQUAL_size_t(1, json_array_size(field));

    array_entry = json_array_get(field, 0);

    field = json_object_get(array_entry, "classifiedBy");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_STRING("ip", json_string_value(field));
    field = json_object_get(array_entry, "appName");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_STRING("my_application.exe", json_string_value(field));

    json_decref(out_json);

    /* some IPv4 stuff */
    to_report.dns_response.ipv4_cnt = 2;
    to_report.dns_response.ipv4_addrs[0] = STRDUP("1.2.3.4");
    to_report.dns_response.ipv4_addrs[1] = STRDUP("127.0.0.1");

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "appNameQueries");
    TEST_ASSERT_NOT_NULL(field);

    array_entry = json_array_get(field, 0);

    field = json_object_get(array_entry, "resolvedIPv4");
    TEST_ASSERT_TRUE(json_is_array(field));
    TEST_ASSERT_EQUAL_size_t(2, json_array_size(field));

    array_entry = json_array_get(field, 0);
    TEST_ASSERT_EQUAL_STRING("1.2.3.4", json_string_value(array_entry));

    to_report.dns_response.ipv4_cnt = 0;
    FREE(to_report.dns_response.ipv4_addrs[1]);
    FREE(to_report.dns_response.ipv4_addrs[0]);

    json_decref(out_json);

    /* Reporting categories */
    policy_reply.req_type = FSM_FQDN_REQ;
    strncpy(to_report.req_info->url, "http://www.foo.com/index.html", sizeof(to_report.req_info->url));

    /* Using BC */
    to_report.req_info->reply->service_id = URL_BC_SVC;
    policy_reply.categorized = FSM_FQDN_CAT_SUCCESS;

    out = jencode_url_report(&session, &to_report, &policy_reply);
    LOGI("%s", out);
    json_free(out);

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "dnsQueries");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_array(field));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(field));

    array_entry = json_array_get(field, 0);
    entry = json_object_get(array_entry, "dnsCategorization");
    TEST_ASSERT_NOT_NULL(entry);

    TEST_ASSERT_EQUAL_STRING("webroot", json_string_value(json_object_get(entry, "source")));
    TEST_ASSERT_NOT_NULL(json_object_get(entry, "reputationScore"));
    TEST_ASSERT_NOT_NULL(json_object_get(entry, "categories"));

    json_decref(out_json);

    /* Using GK */
    to_report.req_info->reply->service_id = URL_GK_SVC;
    policy_reply.categorized = FSM_FQDN_CAT_SUCCESS;
    policy_reply.provider = "gatekeeper";

    out = jencode_url_report(&session, &to_report, &policy_reply);
    LOGI("%s", out);
    json_free(out);

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "dnsQueries");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_array(field));
    TEST_ASSERT_EQUAL_INT(1, json_array_size(field));

    array_entry = json_array_get(field, 0);
    entry = json_object_get(array_entry, "dnsCategorization");
    TEST_ASSERT_NOT_NULL(entry);

    TEST_ASSERT_EQUAL_STRING("gatekeeper", json_string_value(json_object_get(entry, "source")));
    TEST_ASSERT_NOT_NULL(json_object_get(entry, "confidenceLevel"));
    TEST_ASSERT_NOT_NULL(json_object_get(entry, "categoryId"));
    json_decref(out_json);

    /* Cleanup */
    FREE(to_report.req_info->reply);
    FREE(to_report.req_info);
}


void
test_jencode_url_ip_query_report(void)
{
    struct fsm_session session = {
        .location_id = "LOC",
        .node_id = "NODE",
        .topic = "TOPIC",
    };
    struct fsm_policy_reply policy_reply;
    struct fqdn_pending_req to_report;
    json_t *field, *entry, *out_json;

    /* Prep the report */
    memset(&policy_reply, 0, sizeof(policy_reply));
    memset(&to_report, 0, sizeof(to_report));

    to_report.req_info = CALLOC(1, sizeof(*to_report.req_info));
    policy_reply.policy = "POLICY";
    policy_reply.rule_name = "rule";
    policy_reply.action = FSM_OBSERVED;
    to_report.req_info->reply = CALLOC(1, sizeof(*to_report.req_info->reply));

    /* Using GK */
    to_report.req_info->reply->service_id = URL_GK_SVC;
    policy_reply.categorized = FSM_FQDN_CAT_SUCCESS;
    policy_reply.provider = "gatekeeper";

    /* IPv4 query */
    policy_reply.req_type = FSM_IPV4_REQ;
    strncpy(to_report.req_info->url, "10.0.0.1", sizeof(to_report.req_info->url));
    policy_reply.categorized = FSM_FQDN_CAT_PENDING;

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "classifiedBy");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_string(field));
    TEST_ASSERT_EQUAL_STRING("ip", json_string_value(field));

    field = json_object_get(out_json, "ipAddr");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_EQUAL_STRING("10.0.0.1", json_string_value(field));

    field = json_object_get(out_json, "ipCategorization");
    TEST_ASSERT_NOT_NULL(field);
    entry = json_object_get(field, "confidenceLevel");
    TEST_ASSERT_NOT_NULL(entry);
    entry = json_object_get(field, "source");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_TRUE(json_is_string(entry));
    TEST_ASSERT_EQUAL_STRING("gatekeeper", json_string_value(entry));

    json_decref(out_json);

    /* FSM_FQDN_CAT_FAILED */
    policy_reply.categorized = FSM_FQDN_CAT_FAILED;
    to_report.req_info->reply->error = 1;

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);
    field = json_object_get(out_json, "lookupError");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_integer(field));
    TEST_ASSERT_EQUAL_INT(1, json_integer_value(field));

    json_decref(out_json);

    /* FSM_FQDN_CAT_FAILED with some status */
    to_report.req_info->reply->lookup_status = 404;

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "httpStatus");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_integer(field));
    TEST_ASSERT_EQUAL_INT(404, json_integer_value(field));

    json_decref(out_json);

    /* With an accumulator */
    policy_reply.categorized = FSM_FQDN_CAT_PENDING;
    to_report.acc = CALLOC(1, sizeof(*to_report.acc));

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    field = json_object_get(out_json, "flow");
    TEST_ASSERT_NULL(field);

    json_decref(out_json);

    /* populating slowly */
    to_report.acc->key = CALLOC(1, sizeof(*to_report.acc->key));
    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);

    json_decref(out_json);

    to_report.acc->direction = NET_MD_ACC_INBOUND_DIR;
    to_report.acc->originator = NET_MD_ACC_ORIGINATOR_SRC;
    to_report.acc->key->sport = ntohs(555);
    to_report.acc->key->dport = ntohs(111);
    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);
    field = json_object_get(out_json, "flow");
    TEST_ASSERT_NOT_NULL(field);
    entry = json_object_get(field, "srcPort");

    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_TRUE(json_is_integer(entry));
    TEST_ASSERT_EQUAL_INT(555, json_integer_value(entry));

    json_decref(out_json);

    /* Cover BC */
    to_report.req_info->reply->service_id = URL_BC_SVC;

    out_json = json_url_report(&session, &to_report, &policy_reply);
    TEST_ASSERT_NOT_NULL(out_json);
    field = json_object_get(out_json, "flow");
    TEST_ASSERT_NOT_NULL(field);
    entry = json_object_get(field, "srcPort");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_TRUE(json_is_integer(entry));
    TEST_ASSERT_EQUAL_INT(555, json_integer_value(entry));

    json_decref(out_json);

    /* Check leakage of top function */

    /* Cleanup */
    FREE(to_report.acc->key);
    FREE(to_report.acc);
    FREE(to_report.req_info->reply);
    FREE(to_report.req_info);
}

/* Expose function to UT */
json_t *
json_upnp_report(struct fsm_session *session, struct upnp_report *to_report);

void
test_jencode_upnp_report(void)
{
    struct fsm_session session = {
        .location_id = "LOC",
        .node_id = "NODE",
        .topic = NULL,
    };
    struct upnp_report to_report;
    json_t *array_entry;
    json_t *field;
    json_t *json_out;
    char *out = NULL;

    /* Sanity checks */
    out = jencode_upnp_report(NULL, NULL);
    TEST_ASSERT_NULL(out);

    out = jencode_upnp_report(&session, NULL);
    TEST_ASSERT_NULL(out);

    /* Check broken header */
    out = jencode_upnp_report(&session, &to_report);
    TEST_ASSERT_NULL(out);

    to_report.url = CALLOC(1, sizeof(*to_report.url));
    to_report.url->udev = CALLOC(1, sizeof(*to_report.url->udev));
    to_report.nelems = 4;
    to_report.first = CALLOC(to_report.nelems, sizeof(*to_report.first));
    to_report.first[0].key = "KEY1";
    to_report.first[0].value = "VAL1";
    to_report.first[1].key = "KEY2";
    to_report.first[1].value = "VAL2";
    to_report.first[2].key = "";
    to_report.first[2].value = "EMPTY_KEY";
    to_report.first[3].key = "EMPTY_VALUE";
    to_report.first[3].value = "";
    json_out = json_upnp_report(&session, &to_report);

    /* we should only have 1 entries in the upnpInfo array */
    field = json_object_get(json_out, "upnpInfo");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_array(field));

    TEST_ASSERT_EQUAL_size_t(1, json_array_size(field));
    array_entry = json_array_get(field, 0);
    TEST_ASSERT_NOT_NULL(array_entry);
    TEST_ASSERT_NOT_NULL(json_object_get(array_entry, "KEY1"));
    TEST_ASSERT_NOT_NULL(json_object_get(array_entry, "KEY2"));
    TEST_ASSERT_NULL(json_object_get(array_entry, "EMPTY_VAL"));

    json_decref(json_out);

    /* Fix the header to check leakage of top function */
    session.topic = "TOPIC";
    out = jencode_upnp_report(&session, &to_report);
    TEST_ASSERT_NOT_NULL(out);
    LOGT("%s(): %s", __func__, out);

    /* Cleanup */
    json_free(out);

    /* Report cleanup */
    FREE(to_report.first);
    FREE(to_report.url->udev);
    FREE(to_report.url);
}

/* Expose function to UT */
json_t *
json_dhcp_report(struct fsm_session *session, struct dhcp_report *to_report);

int dhcp_local_domain_cmp(const void *a, const void *b)
{
    return memcmp(a, b, MAX_DN_LEN);
}

void
test_jencode_dhcp_report(void)
{
    struct fsm_session session = {
        .location_id = "LOC",
        .node_id = "NODE",
        .topic = NULL,
    };
    struct dhcp_report to_report;
    struct dhcp_local_domain *domain, *remove,  *domain1, *domain2;
    json_t *field;
    json_t *json_out;
    char *out = NULL;

    /* Sanity checks */
    out = jencode_dhcp_report(NULL, NULL);
    TEST_ASSERT_NULL(out);

    out = jencode_dhcp_report(&session, NULL);
    TEST_ASSERT_NULL(out);

    /* Check broken header */
    out = jencode_dhcp_report(&session, &to_report);
    TEST_ASSERT_NULL(out);

    /* Create and populate array */
    to_report.domain_list = CALLOC(1, sizeof(*to_report.domain_list));
    ds_tree_init(to_report.domain_list, dhcp_local_domain_cmp,
                 struct dhcp_local_domain, local_domain_node);

    domain1 = CALLOC(1, sizeof(*domain1));
    strncpy(domain1->name, "domain1", sizeof(domain1->name));
    ds_tree_insert(to_report.domain_list, domain1, &domain1->name);

    domain2 = CALLOC(1, sizeof(*domain2));
    strncpy(domain2->name, "domain2", sizeof(domain2->name));
    ds_tree_insert(to_report.domain_list, domain2, &domain2->name);

    /* test things now */
    json_out = json_dhcp_report(&session, &to_report);
    TEST_ASSERT_NOT_NULL(json_out);
    field = json_object_get(json_out, "dhcpInfo");
    TEST_ASSERT_NOT_NULL(field);
    TEST_ASSERT_TRUE(json_is_array(field));

    json_decref(json_out);

    /* Fix the header to check leakage of top function */
    session.topic = "TOPIC";
    out = jencode_dhcp_report(&session, &to_report);
    TEST_ASSERT_NOT_NULL(out);
    LOGD("%s(): %s", __func__, out);

    /* Cleanup */
    json_free(out);

    /* cleanup the array */
    domain = ds_tree_head(to_report.domain_list);
    while (domain)
    {
        remove = domain;
        domain = ds_tree_next(to_report.domain_list, domain);
        ds_tree_remove(to_report.domain_list, remove);
        FREE(remove);
    }
    FREE(to_report.domain_list);
}

int
main(int argc, char *argv[])
{
    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, NULL, NULL);

    RUN_TEST(test_jencode_header);
    RUN_TEST(test_jencode_user_agent);
    RUN_TEST(test_jencode_url_report);
    RUN_TEST(test_jencode_url_ip_query_report);
    RUN_TEST(test_jencode_upnp_report);
    RUN_TEST(test_jencode_dhcp_report);

    return ut_fini();
}
