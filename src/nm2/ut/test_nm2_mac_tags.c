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

#include "os_types.h"
#include "target.h"
#include "unity.h"
#include "log.h"

// Include the file to be unit tested
#include "nm2_mac_tags.c"

#define LINE_BUFF_LEN        (128)
#define MAC_ADDR1 "00:e0:4c:19:01:84"
#define MAC_ADDR2 "00:e0:4c:19:01:85"
#define CREATE_ETH_DEV_OFTAG "ovsh i Openflow_Tag name:=eth_devices"
#define DELETE_ETH_DEV_OFTAG "ovsh d Openflow_Tag -w name==eth_devices"
#define MAC_LEARN_TBL_IN_CMD "ovsh i OVS_MAC_Learning brname:="CONFIG_TARGET_LAN_BRIDGE_NAME" ifname:="CONFIG_TARGET_LAN_BRIDGE_NAME" hwaddr:="
#define MAC_LEARN_TBL_DEL_CMD "ovsh d OVS_MAC_Learning -w brname=="CONFIG_TARGET_LAN_BRIDGE_NAME" ifname=="CONFIG_TARGET_LAN_BRIDGE_NAME" hwaddr:="
#define ADD_MAC_LEARN_TBL(A) MAC_LEARN_TBL_IN_CMD A
#define DEL_MAC_LEARN_TBL(A) MAC_LEARN_TBL_DEL_CMD A

void setUp() {}
void tearDown() {}

static void unit_test_init(void)
{
    ovsdb_init("NM_MAC_TAGS");
}

int is_created_oftag(char *tag)
{
    FILE *fp = NULL;
    char *cmd = "ovsh s Openflow_Tag -w name==eth_devices -c name | grep name";
    char buf[LINE_BUFF_LEN] = {0,};
    int ret = -1;

    if ((fp = popen(cmd, "r")) == NULL)
    {
        LOGE("popen error");
        return -1;
    }

    if (fgets(buf, LINE_BUFF_LEN, fp) != NULL)
    {
        if (strstr(buf, tag) != NULL)
        {
            LOGD("mac: %s found in oftag: %s\n", tag, buf);
            ret = 0;
        }
        else
        {
            LOGD("mac: %s not found in oftag: %s\n", tag, buf);
            ret = -1;
        }
    }

    pclose(fp);
    return ret;
}

int is_present_oftag_table(char *mac)
{
    FILE *fp = NULL;
    char *cmd = "ovsh s Openflow_Tag -w name==eth_devices -c device_value "\
                "| grep device_value";
    char buf[LINE_BUFF_LEN] = {0,};
    int ret = -1;

    if ((fp = popen(cmd, "r")) == NULL)
    {
        LOGE("popen error");
        return -1;
    }

    if (fgets(buf, LINE_BUFF_LEN, fp) != NULL)
    {
        if (strstr(buf, mac) != NULL)
        {
            LOGD("mac: %s found in oftag: %s\n", mac, buf);
            ret = 0;
        }
        else
        {
            LOGD("mac: %s not found in oftag: %s\n", mac, buf);
            ret = -1;
        }
    }

    pclose(fp);
    return ret;
}

static void test_set_eth_devices(void)
{
    lan_clients_oftag_add_mac(MAC_ADDR1);
    TEST_ASSERT_EQUAL_INT(0, is_present_oftag_table(MAC_ADDR1));
    lan_clients_oftag_add_mac(MAC_ADDR2);
    TEST_ASSERT_EQUAL_INT(0, is_present_oftag_table(MAC_ADDR2));
}

static void test_unset_eth_devices(void)
{
    lan_clients_oftag_remove_mac(MAC_ADDR1);
    TEST_ASSERT_EQUAL_INT(-1, is_present_oftag_table(MAC_ADDR1));
    lan_clients_oftag_remove_mac(MAC_ADDR2);
    TEST_ASSERT_EQUAL_INT(-1, is_present_oftag_table(MAC_ADDR2));
}

static void test_create_oftag(void)
{
    nm2_mac_tags_ovsdb_init();
    TEST_ASSERT_EQUAL_INT(0, is_created_oftag("eth_devices"));
}


int main(void)
{
    //struct ev_loop *loop = EV_DEFAULT;

    target_log_open("TEST", 0);
    log_severity_set(LOG_SEVERITY_DEBUG);
    UnityBegin("nm2_mac_tags_tests");
    unit_test_init();
    RUN_TEST(test_create_oftag);
    RUN_TEST(test_set_eth_devices);
    RUN_TEST(test_unset_eth_devices);
    return UNITY_END();
}
