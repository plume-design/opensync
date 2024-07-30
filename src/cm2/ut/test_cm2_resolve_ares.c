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

#include "unity.h"
#include "cm2.h"
#include "test_cm2.h"

extern cm2_state_t g_state;

void test_connect_address_order(void)
{
    // IPv6 1
    TEST_ASSERT_TRUE(cm2_write_current_target_addr());
    TEST_ASSERT_TRUE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 0);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 0);

    // IPv4 1
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_FALSE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 1);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 0);

    // IPv6 2
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_TRUE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 1);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 1);

    // IPv4 2
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_FALSE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 2);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 1);

    // IPv6 3
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_TRUE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 2);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 2);

    // IPv4 3
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_FALSE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 3);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 2);

    // IPv6 4
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_TRUE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 3);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 3);

    // IPv4 4
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_FALSE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 4);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 3);

    // IPv4 5
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_FALSE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 4);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 4);

    // IPv4 6
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_FALSE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 4);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 5);

    // IPv4 7
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_FALSE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 4);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 6);

    // IPv4 8
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_FALSE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 4);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 7);

    // re-connect current
    TEST_ASSERT_TRUE(cm2_write_current_target_addr());
    TEST_ASSERT_FALSE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv6_addr_list.h_cur_idx, 4);
    TEST_ASSERT_EQUAL_INT32(cm2_curr_addr()->ipv4_addr_list.h_cur_idx, 7);
}

void test_connect_redirector_manager(void)
{
    // redirector IPv6 1
    TEST_ASSERT_TRUE(cm2_write_current_target_addr());
    TEST_ASSERT_TRUE(cm2_curr_addr()->ipv6_pref);

    // redirector IPv4 1
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_FALSE(cm2_curr_addr()->ipv6_pref);

    g_state.addr_redirector.ipv6_addr_list.h_cur_idx = 0;
    g_state.addr_redirector.ipv4_addr_list.h_cur_idx = 0;
    g_state.addr_manager.ipv6_addr_list.h_cur_idx = 0;
    g_state.addr_manager.ipv4_addr_list.h_cur_idx = 0;
    g_state.dest = CM2_DEST_MANAGER;
    cm2_set_ipv6_pref(CM2_DEST_MANAGER);

    // manager IPv4 1
    TEST_ASSERT_TRUE(cm2_write_current_target_addr());
    TEST_ASSERT_FALSE(cm2_curr_addr()->ipv6_pref);
    TEST_ASSERT_FALSE(g_state.addr_redirector.ipv6_pref);

    // manager IPv6 1
    TEST_ASSERT_TRUE(cm2_write_next_target_addr());
    TEST_ASSERT_TRUE(cm2_curr_addr()->ipv6_pref);
}
