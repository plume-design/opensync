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
#include <unity.h>

#include "test_cm2.h"
#include "kconfig.h"

#include "cm2.h"

const char *test_name = "cm2_tests";

char *resolved_ipv6_addr[5] = {"http://[fd00:beef::1]:1234", "http://[fd00:beef::2]:1234", "http://[fd00:beef::3]:1234", "http://[fd00:beef::4]:1234", NULL};
char *resolved_ipv4_addr[9] = {"http://192.168.1.1:1234", "http://192.168.1.2:1234", "http://192.168.1.3:1234", "http://192.168.1.4:1234"
                             , "http://192.168.1.5:1234", "http://192.168.1.6:1234", "http://192.168.1.7:1234", "http://192.168.1.8:1234"
                             , NULL};

cm2_state_t g_state =
{
    .dest = CM2_DEST_REDIR,
    .addr_redirector.ipv6_addr_list.h_addr_list = resolved_ipv6_addr,
    .addr_redirector.ipv6_addr_list.h_addrtype = AF_INET6,
    .addr_redirector.ipv4_addr_list.h_addr_list = resolved_ipv4_addr,
    .addr_redirector.ipv4_addr_list.h_addrtype = AF_INET,
    .addr_manager.ipv6_addr_list.h_addr_list = resolved_ipv6_addr,
    .addr_manager.ipv6_addr_list.h_addrtype = AF_INET6,
    .addr_manager.ipv4_addr_list.h_addr_list = resolved_ipv4_addr,
    .addr_manager.ipv4_addr_list.h_addrtype = AF_INET,
    .addr_redirector.ipv6_pref = true,
    .addr_manager.ipv6_pref = true,
    .link.ipv4.is_ip = true,
    .link.ipv6.is_ip = true,
};

/* Mock function */
cm2_addr_t* cm2_get_addr(cm2_dest_e dest)
{
    if (dest == CM2_DEST_REDIR)
    {
        return &g_state.addr_redirector;
    }
    else
    {
        return &g_state.addr_manager;
    }
}

cm2_addr_t* cm2_curr_addr(void)
{
    return cm2_get_addr(g_state.dest);
}

cm2_dest_e cm2_get_dest_type(void)
{
    return g_state.dest;
}

bool cm2_ovsdb_set_Manager_target(char *target)
{
    return true;
}

char* cm2_dest_name(cm2_dest_e dest)
{
    return (dest == CM2_DEST_REDIR) ? "redirector" : "manager";
}

char* cm2_curr_dest_name(void)
{
    return cm2_dest_name(g_state.dest);
}

void cm2_test_init(void)
{
    target_log_open("TEST_CM2", 0);
    log_severity_set(LOG_SEVERITY_TRACE);
    g_state.addr_redirector.ipv6_pref = true;
    g_state.addr_manager.ipv6_pref = true;

    return;
}

void cm2_test_setup(void)
{
    g_state.dest = CM2_DEST_REDIR,
    g_state.addr_redirector.ipv6_addr_list.h_addr_list = resolved_ipv6_addr;
    g_state.addr_redirector.ipv6_addr_list.h_addrtype = AF_INET6;
    g_state.addr_redirector.ipv4_addr_list.h_addr_list = resolved_ipv4_addr;
    g_state.addr_redirector.ipv4_addr_list.h_addrtype = AF_INET;
    g_state.addr_manager.ipv6_addr_list.h_addr_list = resolved_ipv6_addr;
    g_state.addr_manager.ipv6_addr_list.h_addrtype = AF_INET6;
    g_state.addr_manager.ipv4_addr_list.h_addr_list = resolved_ipv4_addr;
    g_state.addr_manager.ipv4_addr_list.h_addrtype = AF_INET;
    g_state.link.ipv4.is_ip = true;
    g_state.link.ipv6.is_ip = true;
    g_state.addr_redirector.ipv6_addr_list.h_cur_idx = 0;
    g_state.addr_redirector.ipv4_addr_list.h_cur_idx = 0;
    g_state.addr_manager.ipv6_addr_list.h_cur_idx = 0;
    g_state.addr_manager.ipv4_addr_list.h_cur_idx = 0;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(test_name, cm2_test_init, NULL);

    ut_setUp_tearDown(test_name, cm2_test_setup, NULL);

    //cm2_test_init();

    RUN_TEST(test_connect_address_order);

    RUN_TEST(test_connect_redirector_manager);

    return ut_fini();
}