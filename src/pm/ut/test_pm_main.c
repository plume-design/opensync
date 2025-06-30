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

#include <unit_test_utils.h>
#include <unity.h>
#include <memutil.h>

#include <pm_hw_acc_load_netstats.h>

static const char *test_name = "nm2_tests";

static void test_pm_proc_net_dev_basic(void)
{
    char *text1 = STRDUP("Inter-|   Receive                                                |  Transmit\n"
                         " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop "
                         "fifo colls carrier compressed\n"
                         "    lo: 25983849   20382    0    0    0     0          0         0 25983849   20382    0    "
                         "0    0     0       0          0\n"
                         "enp0s31f6: 38353851091 75633084    0   32    0     0          0   2699697 14096452720 "
                         "54024213    0  102    0     0       0          0\n"
                         "wlp4s0: 620237755 2412713    0    0    0     0          0         0 150953917  626626    0  "
                         "104    0     0       0          0\n");
    char *text2 = STRDUP("Inter-|   Receive                                                |  Transmit\n"
                         " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop "
                         "fifo colls carrier compressed\n"
                         "    lo: 25983849   20382    0    0    0     0          0         0 25983849   20382    0    "
                         "0    0     0       0          0\n"
                         "enp0s31f6: 38353858421 75633117    0   32    0     0          0   2699709 14096456056 "
                         "54024234    0  102    0     0       0          0\n"
                         "wlp4s0: 620239586 2412718    0    0    0     0          0         0 150953917  626626    0  "
                         "104    0     0       0          0\n");

    struct pm_hw_acc_load_netstats *prev = pm_hw_acc_load_netstats_get_from_str(text1);
    struct pm_hw_acc_load_netstats *next = pm_hw_acc_load_netstats_get_from_str(text2);
    TEST_ASSERT_NOT_NULL(prev);
    TEST_ASSERT_NOT_NULL(next);

    uint64_t max_tx_bytes = 0;
    uint64_t max_rx_bytes = 0;
    uint64_t max_tx_pkts = 0;
    uint64_t max_rx_pkts = 0;
    pm_hw_acc_load_netstats_compare(prev, next, &max_tx_bytes, &max_rx_bytes, &max_tx_pkts, &max_rx_pkts);

    TEST_ASSERT_EQUAL_UINT64(3336, max_tx_bytes);
    TEST_ASSERT_EQUAL_UINT64(7330, max_rx_bytes);
    TEST_ASSERT_EQUAL_UINT64(21, max_tx_pkts);
    TEST_ASSERT_EQUAL_UINT64(33, max_rx_pkts);

    pm_hw_acc_load_netstats_drop(prev);
    pm_hw_acc_load_netstats_drop(next);
    FREE(text1);
    FREE(text2);
}

static void test_pm_proc_net_dev_missing(void)
{
    char *text1 = STRDUP("Inter-|   Receive                                                |  Transmit\n"
                         " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop "
                         "fifo colls carrier compressed\n"
                         "    lo: 25983849   20382    0    0    0     0          0         0 25983849   20382    0    "
                         "0    0     0       0          0\n"
                         "enp0s31f6: 38353851091 75633084    0   32    0     0          0   2699697 14096452720 "
                         "54024213    0  102    0     0       0          0\n"
                         "wlp4s0: 620237755 2412713    0    0    0     0          0         0 150953917  626626    0  "
                         "104    0     0       0          0\n");
    char *text2 = STRDUP("Inter-|   Receive                                                |  Transmit\n"
                         " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop "
                         "fifo colls carrier compressed\n"
                         "    lo: 25983849   20382    0    0    0     0          0         0 25983849   20382    0    "
                         "0    0     0       0          0\n"
                         "wlp4s0: 620239586 2412718    0    0    0     0          0         0 150953917  626626    0  "
                         "104    0     0       0          0\n");

    struct pm_hw_acc_load_netstats *prev = pm_hw_acc_load_netstats_get_from_str(text1);
    struct pm_hw_acc_load_netstats *next = pm_hw_acc_load_netstats_get_from_str(text2);
    TEST_ASSERT_NOT_NULL(prev);
    TEST_ASSERT_NOT_NULL(next);

    uint64_t max_tx_bytes = 0;
    uint64_t max_rx_bytes = 0;
    uint64_t max_tx_pkts = 0;
    uint64_t max_rx_pkts = 0;
    pm_hw_acc_load_netstats_compare(prev, next, &max_tx_bytes, &max_rx_bytes, &max_tx_pkts, &max_rx_pkts);

    /* should ignore enp0s31f6, and report wlp4s0 max values */
    TEST_ASSERT_EQUAL_UINT64(0, max_tx_bytes);
    TEST_ASSERT_EQUAL_UINT64(1831, max_rx_bytes);
    TEST_ASSERT_EQUAL_UINT64(0, max_tx_pkts);
    TEST_ASSERT_EQUAL_UINT64(5, max_rx_pkts);

    pm_hw_acc_load_netstats_drop(prev);
    pm_hw_acc_load_netstats_drop(next);
    FREE(text1);
    FREE(text2);
}

static void test_pm_proc_net_dev_underflow(void)
{
    char *text1 = STRDUP("Inter-|   Receive                                                |  Transmit\n"
                         " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop "
                         "fifo colls carrier compressed\n"
                         "    lo: 25983849   20382    0    0    0     0          0         0 25983849   20382    0    "
                         "0    0     0       0          0\n"
                         "enp0s31f6: 38353858421 75633117    0   32    0     0          0   2699709 14096456056 "
                         "54024234    0  102    0     0       0          0\n"
                         "wlp4s0: 620237755 2412713    0    0    0     0          0         0 150953917  626626    0  "
                         "104    0     0       0          0\n");
    char *text2 = STRDUP("Inter-|   Receive                                                |  Transmit\n"
                         " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop "
                         "fifo colls carrier compressed\n"
                         "    lo: 25983849   20382    0    0    0     0          0         0 25983849   20382    0    "
                         "0    0     0       0          0\n"
                         "enp0s31f6: 38353851091 75633084    0   32    0     0          0   2699697 14096452720 "
                         "54024213    0  102    0     0       0          0\n"
                         "wlp4s0: 620239586 2412718    0    0    0     0          0         0 150953917  626626    0  "
                         "104    0     0       0          0\n");

    struct pm_hw_acc_load_netstats *prev = pm_hw_acc_load_netstats_get_from_str(text1);
    struct pm_hw_acc_load_netstats *next = pm_hw_acc_load_netstats_get_from_str(text2);
    TEST_ASSERT_NOT_NULL(prev);
    TEST_ASSERT_NOT_NULL(next);

    uint64_t max_tx_bytes = 0;
    uint64_t max_rx_bytes = 0;
    uint64_t max_tx_pkts = 0;
    uint64_t max_rx_pkts = 0;
    pm_hw_acc_load_netstats_compare(prev, next, &max_tx_bytes, &max_rx_bytes, &max_tx_pkts, &max_rx_pkts);

    /* should ignore enp0s31f6, and report wlp4s0 max values */
    TEST_ASSERT_EQUAL_UINT64(0, max_tx_bytes);
    TEST_ASSERT_EQUAL_UINT64(1831, max_rx_bytes);
    TEST_ASSERT_EQUAL_UINT64(0, max_tx_pkts);
    TEST_ASSERT_EQUAL_UINT64(5, max_rx_pkts);

    pm_hw_acc_load_netstats_drop(prev);
    pm_hw_acc_load_netstats_drop(next);
    FREE(text1);
    FREE(text2);
}

int main(int argc, const char **argv)
{
    ut_init(test_name, NULL, NULL);
    ut_setUp_tearDown(test_name, NULL, NULL);
    RUN_TEST(test_pm_proc_net_dev_basic);
    RUN_TEST(test_pm_proc_net_dev_missing);
    RUN_TEST(test_pm_proc_net_dev_underflow);
    return ut_fini();
}
