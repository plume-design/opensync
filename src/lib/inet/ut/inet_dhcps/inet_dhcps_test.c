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
#include "unit_test_utils.h"

#define WAIT_DEBOUNCE 1.0

char *test_name = "test_dnsmasq";

void test_dnsmasq(void)
{
    osn_dhcp_server_t * volatile dhs = NULL;

    int status;

    if (TEST_PROTECT())
    {
        procfs_entry_t *pe;

        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(
                execpr(sh_create_vlan, "eth0", "100") == 0,
                "Error creating eth0.100");

        status = execpr(_S(ifconfig "$1" up), "eth0.100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error enabling network.");
        }

        /* Start the DHCP server on the interface, use procsfs to get its status */
        dhs = osn_dhcp_server_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                dhs != NULL,
                "Error creating DHCP server instance");

        /* Set the required arguments */
        osn_ip_addr_t start;
        osn_ip_addr_t stop;

        osn_ip_addr_from_str(&start, "192.168.1.1");
        osn_ip_addr_from_str(&stop, "192.168.1.50");

        TEST_ASSERT_TRUE_MESSAGE(
            osn_dhcp_server_range_add(dhs,
                    start,
                    stop),
            "Unable to set the range start/end");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_apply(dhs),
                "Error applying DHCP server configuration");

        ev_wait(NULL, WAIT_DEBOUNCE);

        /* Use procfs to verify that the dhcp client is running */
        pe = procfs_find_dnsmasq();
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "Unable to find DNSMASQ in the process list");

       TEST_ASSERT_TRUE_MESSAGE(
                pe->pe_ppid == getpid(),
                "The DNSMASQ PID of the client is not us!");

        TEST_ASSERT_TRUE_MESSAGE(
                pe->pe_state[0] == 'S' || pe->pe_state[0] == 'R',
                "DNSMASQ is not in running state.");

        /* Check some of the more important command line arguments */
        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "--keep-in-foreground",  NULL),
                "DNSMASQ is not running in foreground");

        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "-x", "/var/run/dnsmasq/dnsmasq.pid", NULL),
                "DNSMASQ without PID file");

        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "-C", "/var/etc/dnsmasq.conf", NULL),
                "udhcpc without plume script");

        /* Check if the config file has the correct parameters */
        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/var/etc/dnsmasq.conf", "dhcp-range=eth0.100,192.168.1.1,192.168.1.50"),
                "Config file is missing DHCP pool configuration for eth0.100");

        status = execpr(_S(ifconfig "$1" down), "eth0.100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error disabling network.");
        }
    }

    if (dhs != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_del(dhs),
                "Error double stopping DNSMASQ.");
    }

    /* Kill the dummy VLAN interface */
    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

void test_dnsmasq_lease(void)
{
    osn_dhcp_server_t * volatile dhs = NULL;

    int status;

    if (TEST_PROTECT())
    {
        procfs_entry_t *pe;

        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(
                execpr(sh_create_vlan, "eth0", "100") == 0,
                "Error creating eth0.100");

        status = execpr(_S(ifconfig "$1" up), "eth0.100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error enabling network.");
        }

        /* Start the DHCP server on the interface, use procsfs to get its status */
        dhs = osn_dhcp_server_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                dhs != NULL,
                "Error creating DHCP server instance");

        /* Set the required arguments */
        osn_ip_addr_t start;
        osn_ip_addr_t stop;

        osn_ip_addr_from_str(&start, "192.168.1.1");
        osn_ip_addr_from_str(&stop, "192.168.1.50");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_range_add(dhs,
                        start,
                        stop),
                "Unable to set the range start/end");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_apply(dhs),
                "Error applying DHCP server configuration");

        ev_wait(NULL, WAIT_DEBOUNCE);

        /* Use procfs to verify that the dhcp client is running */
        pe = procfs_find_dnsmasq();
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "Unable to find DNSMASQ in the process list");

        /* Check if the config file has the correct parameters */
        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/var/etc/dnsmasq.conf", "dhcp-range=eth0.100,192.168.1.1,192.168.1.50"),
                "Config file is missing DHCP pool configuration for eth0.100");

        status = execpr(_S(ifconfig "$1" down), "eth0.100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error disabling network.");
        }
    }

    if (dhs != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_del(dhs),
                "Error double stopping DNSMASQ.");
    }

    /* Kill the dummy VLAN interface */
    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

void test_dnsmasq_options(void)
{
    osn_dhcp_server_t * volatile dhs = NULL;

    int status;

    if (TEST_PROTECT())
    {
        procfs_entry_t *pe;

        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(
                execpr(sh_create_vlan, "eth0", "100") == 0,
                "Error creating eth0.100");

        status = execpr(_S(ifconfig "$1" up), "eth0.100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error enabling network.");
        }

        /* Start the DHCP server on the interface, use procsfs to get its status */
        dhs = osn_dhcp_server_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                dhs != NULL,
                "Error creating DHCP server instance");

        /* Set the required arguments */
        osn_ip_addr_t start;
        osn_ip_addr_t stop;

        osn_ip_addr_from_str(&start, "192.168.40.1");
        osn_ip_addr_from_str(&stop, "192.168.40.50");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_range_add(dhs,
                        start,
                        stop),
                "Unable to set the range start/end");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_option_set(dhs, DHCP_OPTION_ROUTER, "192.168.40.1"),
                "Unable to set DHCP_OPTION_ROUTER");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_option_set(dhs, DHCP_OPTION_DNS_SERVERS, "192.168.40.2"),
                "Unable to set DHCP_OPTION_DNS_SERVERS");

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_apply(dhs),
                "Error applying DHCP server configuration");

        ev_wait(NULL, WAIT_DEBOUNCE);

        /* Use procfs to verify that the dhcp client is running */
        pe = procfs_find_dnsmasq();
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "Unable to find DNSMASQ in the process list");

        /* Check if the config file has the correct parameters */
        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/var/etc/dnsmasq.conf", "dhcp-option=eth0.100,3,192.168.40.1"),
                "Config file is missing DHCP_OPTION_ROUTER options");

        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/var/etc/dnsmasq.conf", "dhcp-option=eth0.100,6,192.168.40.2"),
                "Config file is missing DHCP_OPTION_DNS_SERVERS options");

        status = execpr(_S(ifconfig "$1" down), "eth0.100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error disabling network.");
        }
    }

    if (dhs != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_del(dhs),
                "Error double stopping DNSMASQ.");
    }

    /* Kill the dummy VLAN interface */
    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}


void test_dnsmasq_neg(void)
{
    osn_dhcp_server_t * volatile dhs = NULL;

    int status;

    if (TEST_PROTECT())
    {
        /* Start the DHCP server on the interface, use procsfs to get its status */
        dhs = osn_dhcp_server_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                dhs != NULL,
                "Error creating DHCP server instance");

        status = execpr(_S(ifconfig "$1" up), "eth0.100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error enabling network.");
        }

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_apply(dhs),
                "Error applying DHCP server configuration");

        status = execpr(_S(ifconfig "$1" down), "eth0.100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error disabling network.");
        }
    }

    if (dhs != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_del(dhs),
                "Error double stopping DNSMASQ.");
    }

    /* Kill the dummy VLAN interface */
    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

#define MULTI_NUM 7

void test_dnsmasq_multiple(void)
{
    int ii;

    osn_dhcp_server_t * volatile dhs[MULTI_NUM] = { NULL };

    char sbuf[256];

    int status;

    if (TEST_PROTECT())
    {
        procfs_entry_t *pe;

        for (ii = 0; ii < MULTI_NUM; ii++)
        {
            snprintf(sbuf, sizeof(sbuf), "%d", 100 + ii);

            /* Create a VLAN.X interface */
            TEST_ASSERT_TRUE_MESSAGE(
                    execpr(sh_create_vlan, "eth0", sbuf) == 0,
                    "Error creating eth0.100");

            status = execpr(_S(ifconfig "$1.$2" up), "eth0", sbuf);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            {
                LOG(ERR, "inet_test: Error enabling network.");
            }
        }

        for (ii = 0; ii < MULTI_NUM; ii++)
        {
            char sstart[OSN_IP_ADDR_LEN];
            char sstop[OSN_IP_ADDR_LEN];
            osn_ip_addr_t start;
            osn_ip_addr_t stop;

            snprintf(sbuf, sizeof(sbuf), "eth0.%d", 100 + ii);

            /* Start the DHCP server on the interface, use procsfs to get its status */
            dhs[ii] = osn_dhcp_server_new(sbuf);
            TEST_ASSERT_TRUE_MESSAGE(
                    dhs != NULL,
                    "Error creating DHCP server instance");

            snprintf(sstart, sizeof(sstart), "192.168.%d.1", ii);
            snprintf(sstop, sizeof(sstop), "192.168.%d.50", ii);
            osn_ip_addr_from_str(&start, sstart);
            osn_ip_addr_from_str(&stop, sstop);

            /* Set the required arguments */
            TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_range_add(dhs[ii],
                        start,
                        stop),
                "Unable to set the range start/end");

            TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_apply(dhs[ii]),
                "Error applying DHCP server configuration");
        }

        ev_wait(NULL, WAIT_DEBOUNCE);

        /* Use procfs to verify that the dhcp client is running */
        pe = procfs_find_dnsmasq();
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "Unable to find DNSMASQ in the process list");

        TEST_ASSERT_TRUE_MESSAGE(
                pe->pe_ppid == getpid(),
                "The DNSMASQ PID of the client is not us!");

        TEST_ASSERT_TRUE_MESSAGE(
                pe->pe_state[0] == 'S' || pe->pe_state[0] == 'R',
                "DNSMASQ is not in running state.");

        /* Check some of the more important command line arguments */
        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "--keep-in-foreground",  NULL),
                "DNSMASQ is not running in foreground");

        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "-x", "/var/run/dnsmasq/dnsmasq.pid", NULL),
                "DNSMASQ without PID file");

        TEST_ASSERT_TRUE_MESSAGE(
                procfs_entry_has_args(pe, "-C", "/var/etc/dnsmasq.conf", NULL),
                "udhcpc without plume script");

        /*
         * Check even instances
         */
        for (ii = 0; ii < MULTI_NUM; ii+=2)
        {
            snprintf(sbuf, sizeof(sbuf), "dhcp-range=eth0.%d,192.168.%d.1,192.168.%d.50", 100 + ii, ii, ii);

            /* Check if the config file has the correct parameters */
            TEST_ASSERT_TRUE_MESSAGE(
                    find_in_file("/var/etc/dnsmasq.conf", sbuf),
                    "Config file is missing DHCP pool configuration for eth0.X");
        }

        ev_wait(NULL, WAIT_DEBOUNCE);

        /*
         * Check odd instances
         */
        for (ii = 1; ii < MULTI_NUM; ii+=2)
        {
            snprintf(sbuf, sizeof(sbuf), "dhcp-range=eth0.%d,192.168.%d.1,192.168.%d.50", 100 + ii, ii, ii);

            /* Check if the config file has the correct parameters */
            TEST_ASSERT_TRUE_MESSAGE(
                    find_in_file("/var/etc/dnsmasq.conf", sbuf),
                    "Config file is missing DHCP pool configuration for eth0.X");
        }

        ev_wait(NULL, WAIT_DEBOUNCE);
    }

    for (ii = 0; ii < MULTI_NUM; ii++)
    {
        snprintf(sbuf, sizeof(sbuf), "%d", 100 + ii);
        status = execpr(_S(ifconfig "$1.$2" down), "eth0", sbuf);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error disabling network.");
        }
    }

    for (ii = 0; ii < MULTI_NUM; ii++)
    {
        if (dhs[ii] == NULL) continue;

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_del(dhs[ii]),
                "Error double stopping DNSMASQ.");
    }

    for (ii = 0; ii < MULTI_NUM; ii++)
    {
        snprintf(sbuf, sizeof(sbuf), "%d", 100+ii);
        /* Kill the dummy VLAN interface */
        if (execpr(sh_delete_vlan, "eth0", sbuf) != 0)
        {
            PR("Error deleting interface eth0.100");
        }
    }
}

#define ERR_NUM 3
int num_errors = 0;
int all_errors = 0;

void __errfn(osn_dhcp_server_t *self)
{
    (void)self;

    num_errors++;

    PR("Error function was called, num errors = %d\n", num_errors);

    if (num_errors >= ERR_NUM)
    {
        PR("All instances were notified about errors, waking up.");
        all_errors = 1;
    }
}

void test_dnsmasq_errfn(void)
{
    int ii;

    osn_dhcp_server_t * volatile dhs[ERR_NUM] = { NULL };

    char sbuf[256];

    int status;

    if (TEST_PROTECT())
    {
        procfs_entry_t *pe;

        for (ii = 0; ii < ERR_NUM; ii++)
        {
            snprintf(sbuf, sizeof(sbuf), "%d", 100 + ii);

            /* Create a VLAN.X interface */
            TEST_ASSERT_TRUE_MESSAGE(
                    execpr(sh_create_vlan, "eth0", sbuf) == 0,
                    "Error creating eth0.100");

            status = execpr(_S(ifconfig "$1.$2" up), "eth0", sbuf);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            {
                LOG(ERR, "inet_test: Error enabling network.");
            }

        }

        for (ii = 0; ii < ERR_NUM; ii++)
        {
            char sstart[OSN_IP_ADDR_LEN];
            char sstop[OSN_IP_ADDR_LEN];
            osn_ip_addr_t start;
            osn_ip_addr_t stop;

            snprintf(sbuf, sizeof(sbuf), "eth0.%d", 100 + ii);

            /* Start the DHCP server on the interface, use procsfs to get its status */
            dhs[ii] = osn_dhcp_server_new(sbuf);
            TEST_ASSERT_TRUE_MESSAGE(
                    dhs != NULL,
                    "Error creating DHCP server instance");

            snprintf(sstart, sizeof(sstart), "192.168.%d.1", ii);
            snprintf(sstop, sizeof(sstop), "192.168.%d.50", ii);
            osn_ip_addr_from_str(&start, sstart);
            osn_ip_addr_from_str(&stop, sstop);

            /* Set the required arguments */
            TEST_ASSERT_TRUE_MESSAGE(
                    osn_dhcp_server_range_add(dhs[ii],
                            start,
                            stop),
                    "Unable to set the range start/end");

            /* Set the error callback */
            osn_dhcp_server_error_notify(dhs[ii], __errfn);

            if (ii == 1)
            {
                /*
                 * For one interface, set invalid options -- this should case dnsmasq to fail and options should
                 * propagate to all instances
                 */
                TEST_ASSERT_TRUE_MESSAGE(
                        osn_dhcp_server_option_set(dhs[ii], DHCP_OPTION_ROUTER, "not_an_ip"),
                        "Failed to set the DHCP_OPTION_ROUTER option");
            }

            TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_apply(dhs[ii]),
                "Error applying DHCP server configuration");
        }

        all_errors = 0;
        num_errors = 0;
        ev_wait(&all_errors, 60.0);

        TEST_ASSERT_FALSE_MESSAGE(
                num_errors < ERR_NUM,
                "Not all instances reported an error.");

        /* Use procfs to verify that the dhcp server is NOT running */
        pe = procfs_find_dnsmasq();
        TEST_ASSERT_TRUE_MESSAGE(
                pe == NULL,
                "Unable to find DNSMASQ in the process list");

        all_errors = 0;
        num_errors = 0;
        ev_wait(NULL, WAIT_DEBOUNCE);

        TEST_ASSERT_TRUE_MESSAGE(
                num_errors == 0,
                "Errors were reported although the bad instance was stopped.");
    }

    for (ii = 0; ii < ERR_NUM; ii++)
    {
        if (dhs[ii] == NULL) continue;

        TEST_ASSERT_TRUE_MESSAGE(
                osn_dhcp_server_del(dhs[ii]),
                "Error double stopping DNSMASQ.");
    }

    for (ii = 0; ii < ERR_NUM; ii++)
    {
        snprintf(sbuf, sizeof(sbuf), "%d", 100+ii);
        /* Kill the dummy VLAN interface */
        if (execpr(sh_delete_vlan, "eth0", sbuf) != 0)
        {
            PR("Error deleting interface eth0.100");
        }
    }
}

void run_test_dnsmasq(void)
{
    RUN_TEST(test_dnsmasq);
    RUN_TEST(test_dnsmasq_neg);
    RUN_TEST(test_dnsmasq_lease);
    RUN_TEST(test_dnsmasq_options);
    RUN_TEST(test_dnsmasq_multiple);
    RUN_TEST(test_dnsmasq_errfn);
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

    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, NULL, NULL);

    if (!parse_opts(argc, argv))
    {
        return false;
    }

    if (opt_verbose)
        log_open("DNSMASQ_TEST", LOG_OPEN_STDOUT);

    run_test_dnsmasq();

    return ut_fini();
}
