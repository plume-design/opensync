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

#include "../tests_common.c"

#include "osn_dhcp.h"

void setUp() {}
void tearDown() {}

void test_udhcpc(void)
{
    /*
     * XXX: The volatile below is needed :) TEST_PROTECT() is implemented with a setjmp().
     * A longjmp() to a setjmp point will restore the register state. At high optimization
     * levels, dhc is optimized away into a register, so a longjmp() will destroy the current
     * value and reset it to "NULL" which messes up the cleanup code below.
     */
    osn_dhcp_client_t *volatile dhc = NULL;

    if (TEST_PROTECT())
    {
        procfs_entry_t *pe;

        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(
                execpr(sh_create_vlan, "eth0", "100") == 0,
                "Error creating eth0.100");

        /* Start the DHCP client on the interface, use procsfs to get its status */
        dhc = osn_dhcp_client_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                dhc != NULL,
                "Error creating DHCP client instance");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_start(dhc),
                "Error starting DHCP client");

        usleep(100000);

        /* Use procfs to verify that the dhcp client is running */
        pe = procfs_find_dhcpc("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "Unable to find the DHCP client in the process list");

        TEST_ASSERT_TRUE_MESSAGE(
                pe->pe_ppid == getpid(),
                "Parent PID of the client is not us!");

        TEST_ASSERT_TRUE_MESSAGE(
                pe->pe_state[0] == 'S' || pe->pe_state[0] == 'R'|| pe->pe_state[0] == 'D',
                "DHCP client not in running state.");

        /* Check some of the more important command line arguments */
        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "-f",  NULL),
                "udhcpc not running in foreground");

        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "-p", "/var/run/udhcpc-eth0.100.pid", NULL),
                "udhcpc without PID file");

        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "-s", CONFIG_INSTALL_PREFIX"/bin/udhcpc.sh", NULL),
                "udhcpc without plume script");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_stop(dhc),
                "Error stopping DHCP client");
    }

    if (dhc != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_del(dhc),
                "Error double stopping DHCP client.");
    }

    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

bool error_indicator = false;

void test_udhcpc_neg_error_fn(osn_dhcp_client_t *self)
{
    (void)self;
    PR("DHCP client generated an error event!\n");
    error_indicator = true;
}

void __dummy(struct ev_loop *loop, ev_timer *w, int revent)
{
    (void)loop;
    (void)w;
    (void)revent;
}

void test_udhcpc_neg(void)
{
    /* XXX: See above, test_udhcpc() for an explanation of the volatile */
    osn_dhcp_client_t * volatile dhc;

    dhc = NULL;

    if (TEST_PROTECT())
    {
        procfs_entry_t *pe;

        /* Create a VLAN.100 interface */
        execpr(sh_delete_vlan, "eth0", "100");

        /* Start the DHCP client on the interface, use procsfs to get its status */
        dhc = osn_dhcp_client_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                dhc != NULL,
                "Error creating DHCP client instance");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_error_fn_set(dhc, test_udhcpc_neg_error_fn),
                "Error setting error callback");

        error_indicator = false;

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_start(dhc),
                "Error starting DHCP client");

        /*
         * No interface exists, udhcpc will be restart several times before giving up.
         * Poll the udhcpc client status until this happens.
         */
        ev_timer tmr;

        ev_timer_init(&tmr, __dummy, 30.0, 0.0);
        ev_timer_start(EV_DEFAULT, &tmr);

        while (ev_is_active(&tmr) && !error_indicator && ev_run(EV_DEFAULT, EVRUN_ONCE));

        if (ev_is_active(&tmr)) ev_timer_stop(EV_DEFAULT, &tmr);

        TEST_ASSERT_TRUE_MESSAGE(
                error_indicator,
                "Error indicator not set.");

        /* Use procfs to verify that the dhcp client is running */
        pe = procfs_find_dhcpc("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                pe == NULL,
                "udhcpc for interface is started, but should not be.");
    }

    if (dhc != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_del(dhc),
                "Error stopping DHCP client");
    }
}

void test_udhcpc_req_opts(void)
{
    osn_dhcp_client_t * volatile dhc = NULL;

    if (TEST_PROTECT())
    {
        procfs_entry_t *pe;

        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(execpr(sh_create_vlan, "eth0", "100")  == 0, "Error creating eth0.100");

        /* Start the DHCP client on the interface, use procsfs to get its status */
        dhc = osn_dhcp_client_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                dhc != NULL,
                "Error creating DHCP client instance");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_opt_request(dhc, DHCP_OPTION_HOSTNAME, true),
                "Error requesting option HOSTNAME");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_opt_request(dhc, DHCP_OPTION_LEASE_TIME, true),
                "Error requesting option LEASE TIME");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_start(dhc),
                "Error starting DHCP client");

        usleep(100000);

        /* Use procfs to verify that the dhcp client is running */
        pe = procfs_find_dhcpc("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "Unable to find the DHCP client in the process list");

        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "-O", "12", NULL),
                "udhcpc without hostname");

        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "-O", "51", NULL),
                "udhcpc without plume_swver");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_stop(dhc),
                "Error stopping DHCP client");
    }

    if (dhc != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(osn_dhcp_client_del(dhc), "Error double stopping DHCP client.");
    }

    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

void test_udhcpc_set_opts(void)
{
    osn_dhcp_client_t * volatile dhc = NULL;

    if (TEST_PROTECT())
    {
        procfs_entry_t *pe;

        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(execpr(sh_create_vlan, "eth0", "100", NULL)  == 0, "Error creating eth0.100");

        /* Start the DHCP client on the interface, use procsfs to get its status */
        dhc = osn_dhcp_client_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                dhc != NULL,
                "Error creating DHCP client instance");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_opt_set(dhc, DHCP_OPTION_HOSTNAME, "hello"),
                "Error setting option HOSTNAME");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_opt_set(dhc, DHCP_OPTION_OSYNC_SWVER, "1.23"),
                "Error setting option OSYNC_SWVER");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_start(dhc),
                "Error starting DHCP client");

        usleep(100000);

        /* Use procfs to verify that the dhcp client is running */
        pe = procfs_find_dhcpc("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "Unable to find the DHCP client in the process list");

        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "-x", "hostname:hello", NULL),
                "udhcpc hostname option not set");

        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "-x", "0xE1:312E3233", NULL),
                "udhcpc plume_swver not set");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_stop(dhc),
                "Error stopping DHCP client");
    }

    if (dhc != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_del(dhc),
                "Error double stopping DHCP client.");
    }

    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

void test_udhcpc_set_vendorclass(void)
{
    osn_dhcp_client_t * volatile dhc = NULL;

    if (TEST_PROTECT())
    {
        procfs_entry_t *pe;

        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(execpr(sh_create_vlan, "eth0", "100")  == 0, "Error creating eth0.100");

        /* Start the DHCP client on the interface, use procsfs to get its status */
        dhc = osn_dhcp_client_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                dhc != NULL,
                "Error creating DHCP client instance");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_opt_set(dhc, DHCP_OPTION_VENDOR_CLASS, "test vendor"),
                "Unable to set vendorclass");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_start(dhc),
                "Error starting DHCP client");

        usleep(100000);

        /* Use procfs to verify that the dhcp client is running */
        pe = procfs_find_dhcpc("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "Unable to find the DHCP client in the process list");

       TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "test vendor", NULL),
                "udhcpc vendorclass option not set");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_stop(dhc),
                "Error stopping DHCP client");
    }

    if (dhc != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_client_del(dhc),
                "Error double stopping DHCP client.");
    }

    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

void run_test_udhcpc(void)
{
    RUN_TEST(test_udhcpc);
    RUN_TEST(test_udhcpc_neg);
    RUN_TEST(test_udhcpc_req_opts);
    RUN_TEST(test_udhcpc_set_opts);
    RUN_TEST(test_udhcpc_set_vendorclass);
}

/*
 * ===========================================================================
 *  MAIN
 * ===========================================================================
 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (!parse_opts(argc, argv))
    {
        return false;
    }

    if (opt_verbose)
        log_open("UDHCPC_TEST", LOG_OPEN_STDOUT);

    UNITY_BEGIN();

    run_test_udhcpc();

    return UNITY_END();
}
