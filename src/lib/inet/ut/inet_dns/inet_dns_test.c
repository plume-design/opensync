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

#include "inet_dns.h"
#include "unit_test_utils.h"

char *test_name = "test_dns";

void test_dns()
{
    inet_dns_t *volatile dns1;
    inet_dns_t *volatile dns2;

    if (TEST_PROTECT())
    {
        PR("Starting dns1");
        dns1 = inet_dns_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                dns1 != NULL,
                "Error creating inet_dns instance");

        osn_ip_addr_t srv1;
        osn_ip_addr_t srv2;

        osn_ip_addr_from_str(&srv1, "1.2.3.4");
        osn_ip_addr_from_str(&srv2, "4.3.2.1");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_dns_server_set(dns1, srv1, srv2),
                "Error setting DNS settings");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_dns_start(dns1),
                "Error starting DNS service");

        PR("Starting dns2");
        dns2 = inet_dns_new("eth0.200");
        TEST_ASSERT_TRUE_MESSAGE(
                dns1 != NULL,
                "Error creating inet_dns instance");

        /* Apply just the secondary server */
        osn_ip_addr_from_str(&srv2, "200.3.2.1");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_dns_server_set(dns2, OSN_IP_ADDR_INIT, srv2),
                "Error setting DNS settings");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_dns_start(dns1),
                "Error starting DNS service");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_dns_start(dns2),
                "Error starting DNS service");

        /* Wait for the debounce to happen */
        ev_wait(NULL, 1.0);

        execpr("ls -l /tmp/dns/; cat /tmp/resolv.conf");

        /* Check if files exists */
        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "Interface: eth0.100"),
                "Unable to find entry for interface eth0.100 in /tmp/resolv.conf");

        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "nameserver 1.2.3.4"),
                "Unable to find primary dns server in /resolv.conf");

        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "nameserver 4.3.2.1"),
                "Unable to find secondary dns server in /resolv.conf");

        /* Check if files exists */
        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "Interface: eth0.200"),
                "Unable to find entry for interface eth0.100 in /tmp/resolv.conf");

        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "nameserver 200.3.2.1"),
                "Unable to find secondary dns server in /resolv.conf");

        /* Stop DNS1 now see if the entries for dns1 disappear, check if entries for dns2 are still there */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_dns_stop(dns1),
                "Error stopping DNS service");

        PR("Stopping dns1");
        /* Wait for the debounce to happen */
        ev_wait(NULL, 1.0);

        execpr("ls -l /tmp/dns/; cat /tmp/resolv.conf");

        /* Check if files exists */
        TEST_ASSERT_FALSE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "Interface: eth0.100"),
                "Interface eth0.100 is present in /tmp/resolv.conf after stop");

        TEST_ASSERT_FALSE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "nameserver 1.2.3.4"),
                "Primary dns server for eth0.100 is present in /tmp/resolv.conf after stop");

        TEST_ASSERT_FALSE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "nameserver 4.3.2.1"),
                "Secondary dns server for eth0.100 is present in /tmp/resolv.conf after stop");

        /* Check if files exists */
        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "Interface: eth0.200"),
                "Unable to find entry for interface eth0.100 in /tmp/resolv.conf");

        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "nameserver 200.3.2.1"),
                "Unable to find secondary dns server in /resolv.conf");

        /* Test deletion without stop -- dns2 */
    }

    if (dns1 != NULL)
    {
        PR("Deleting dns1");
        TEST_ASSERT_TRUE_MESSAGE(
            inet_dns_del(dns1),
            "Error deleting inet_dns1 instance.");
    }

    if (dns2 != NULL)
    {
        PR("Deleting dns2");
        TEST_ASSERT_TRUE_MESSAGE(
            inet_dns_del(dns2),
            "Error deleting inet_dns2 instance.");
    }

    ev_wait(NULL, 1.0);

    /* Final check */
    TEST_ASSERT_FALSE_MESSAGE(
            find_in_file("/tmp/resolv.conf", "Interface: eth0.100"),
            "Interface eth0.100 is present in /tmp/resolv.conf after deletion");

    TEST_ASSERT_FALSE_MESSAGE(
            find_in_file("/tmp/resolv.conf", "nameserver 1.2.3.4"),
            "Primary dns server for eth0.100 is present in /tmp/resolv.conf after deletion");

    TEST_ASSERT_FALSE_MESSAGE(
            find_in_file("/tmp/resolv.conf", "nameserver 4.3.2.1"),
            "Secondary dns server for eth0.100 is present in /tmp/resolv.conf after deletion");
}

void test_external()
{
    inet_dns_t *volatile dns1;

    if (TEST_PROTECT())
    {
        PR("Starting dns1");
        dns1 = inet_dns_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                dns1 != NULL,
                "Error creating inet_dns instance");

        osn_ip_addr_t srv1;
        osn_ip_addr_t srv2;

        osn_ip_addr_from_str(&srv1, "1.2.3.4");
        osn_ip_addr_from_str(&srv2, "4.3.2.1");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_dns_server_set(dns1, srv1, srv2),
                "Error setting DNS settings");

        /* Create an external file and see if it'll be concatenated into the global DNS file */
        FILE *f = fopen("/tmp/dns/test.resolv", "w");
        TEST_ASSERT_TRUE_MESSAGE(
                f != NULL,
                "Unable to create test.resolv");

        fprintf(f, "CONCAT TEST\n");

        fclose(f);

        TEST_ASSERT_TRUE_MESSAGE(
                inet_dns_start(dns1),
                "Error starting DNS service");

        /* Wait for the debounce to happen */
        ev_wait(NULL, 1.0);

        /* Check if we have the string "CONCAT TEST" in /tmp/resolv.conf */
        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "CONCAT TEST"),
                "External file not merged to /tmp/resolv.conf");

        /* Stop DNS1 now see if the entries for dns1 disappear, check if entries for dns2 are still there */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_dns_stop(dns1),
                "Error stopping DNS service");
    }

    if (dns1 != NULL)
    {
        PR("Deleting dns1");
        TEST_ASSERT_TRUE_MESSAGE(
            inet_dns_del(dns1),
            "Error deleting inet_dns1 instance.");
    }

    /* Give the test some time to run the cleanup */
    ev_wait(NULL, 1.0);
}

void run_test_udhcpc(void)
{
    RUN_TEST(test_dns);
    RUN_TEST(test_external);
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
        log_open("DNS_TEST", LOG_OPEN_STDOUT);

    run_test_udhcpc();

    return ut_fini();
}
