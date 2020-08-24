#ifndef __IOTM_TEST__
#define __IOTM_TEST__

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "iotm.h"
#include "log.h"
#include "target.h"
#include "unity.h"

#define test_name "IOTM_TESTS"


void test_iot_rule_ovsdb_suite();
void test_iot_session_suite();
void test_event_suite();
void test_ev_suite();
void test_iotm_rule_suite();
void test_iot_router_suite();
void test_list_suite();
void test_tree_suite();
void test_iotm_plug_event_suite();
void test_iotm_plug_command_suite();
void test_iot_tag_suite();
void test_iot_tl_suite();
void test_data_type_suite();

#endif // __IOTM__ */
