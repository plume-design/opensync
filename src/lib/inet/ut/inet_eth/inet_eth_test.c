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

#include <string.h>

#include "inet_eth.h"

#include "../tests_common.c"
#include "unit_test_utils.h"

char *test_name = "test_inet_eth";

/*
 * ===========================================================================
 *  INET ETH
 * ===========================================================================
 */

void test_inet_eth_network(void)
{
    inet_t * volatile self = NULL;
    int status;

    if (TEST_PROTECT())
    {
        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(
                execpr(sh_create_vlan, "eth0", "100") == 0,
                "Error creating eth0.100");

        /* Create an eth instance for eth0.100 */
        self = inet_eth_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                self != NULL,
                "Failed to initialize inet_eth_new() instance");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(self),
                "Commit failed.");

        PR("Enabling interface ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_interface_enable(self, true),
                "Error enabling interface");

        PR("Enabling Network ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_network_enable(self, true),
                "Error enabling network");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(self),
                "Commit failed.");

        ev_wait(NULL, 3.0);

        PR("Check if the network is UP\n");
        /* Check if the interface is UP */
        status = execpr(_S([ $(cat /sys/class/net/$1.$2/operstate ) == "up" ]), "eth0", "100");
        TEST_ASSERT_TRUE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Interface is not UP");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_network_enable(self, false),
                "Unable to set network state");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(self),
                "Commit failed.");

        /* Check if the interface is Down */
        status = execpr(_S([ $(cat /sys/class/net/$1.$2/operstate ) == "down" ]), "eth0", "100");
        TEST_ASSERT_TRUE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Interface is not DOWN");
    }

    if (self != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
            inet_del(self),
            "Destructor failed.");
    }

    /* Kill the dummy VLAN interface */
    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

/**
 * Test various assignment schemas
 */
void test_inet_eth_assign_scheme(void)
{
    inet_t * volatile self = NULL;
    int status;

    procfs_entry_t *volatile pe = NULL;

    if (TEST_PROTECT())
    {
        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(
                execpr(sh_create_vlan, "eth0", "100") == 0,
                "Error creating eth0.100");

        self = inet_eth_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                self != NULL,
                "Failed to initialize inet_test() instance");

        PR("Enabling interface ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_interface_enable(self, true),
                "Error enabling interface");

        PR("Enabling Network ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_network_enable(self, true),
                "Error enabling network");

        PR("Setting assign_scheme -> None");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_assign_scheme_set(self, INET_ASSIGN_NONE),
                "Error setting assign scheme");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(self),
                "Commit failed.");

        PR("Setting assign_scheme -> DHCP");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_assign_scheme_set(self, INET_ASSIGN_DHCP),
                "Error setting assign scheme");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(self),
                "Commit failed.");

        ev_wait(NULL, 1.0);

        /* Check if udhcpc exists */
        pe = procfs_find_dhcpc("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "UDHCPC instance not found");

        PR("Setting assign_scheme -> STATIC");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_assign_scheme_set(self, INET_ASSIGN_STATIC),
                "Error setting assign scheme");

        osn_ip_addr_t ip;
        osn_ip_addr_t netmask;
        osn_ip_addr_t bcast;

        osn_ip_addr_from_str(&ip, "192.168.100.1");
        osn_ip_addr_from_str(&netmask, "255.255.255.0");
        osn_ip_addr_from_str(&bcast, "192.168.100.255");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_ipaddr_static_set(self,
                    ip,
                    netmask,
                    bcast),
                    "Error setting static IP");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(self),
                "Commit failed.");

        /* Check if the static address was assigned properly */
        status = execpr(_S(ip addr show dev "$1" | grep "inet 192.168.100.1/24 brd 192.168.100.255"), "eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Static IP configuration was not applied properly.");

        PR("Adding default gateway");
        osn_ip_addr_t gw;
        osn_ip_addr_from_str(&gw, "192.168.100.254");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_gateway_set(self, gw),
                "Error setting static IP");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(self),
                "Commit failed.");

        status = execpr(_S(ip route show | grep "default via 192.168.100.254 dev $1"), "eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Default route was not set.");
    }

    if (pe != NULL)
    {
        procfs_entry_del(pe);
    }

    if (self != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
            inet_del(self),
            "Destructor failed.");
    }

    /* Kill the dummy VLAN interface */
    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

/**
 * Test MTU -- this is a inet_eth specific setting
 */
void test_inet_eth_mtu(void)
{
    inet_t * volatile self = NULL;
    int status;

    if (TEST_PROTECT())
    {
        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(
                execpr(sh_create_vlan, "eth0", "100") == 0,
                "Error creating eth0.100");

        self = inet_eth_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                self != NULL,
                "Failed to initialize inet_test() instance");

        PR("Enabling interface ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_interface_enable(self, true),
                "Error enabling interface");

        PR("Enabling Network ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_network_enable(self, true),
                "Error enabling network");


        PR("Setting assign_scheme -> None");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_mtu_set(self, 1450),
                "Error setting assign scheme");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(self),
                "Commit failed.");

        ev_wait(NULL, 1.0);

        /* Assignment schema none = interface must not have an IP */
        status = execpr(_S(ip link show dev "$1" | grep "mtu 1450"), "eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "MTU was not set properly.");
    }

    if (self != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
            inet_del(self),
            "Destructor failed.");
    }

    /* Kill the dummy VLAN interface */
    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

void run_test_inet_eth(void)
{
    RUN_TEST(test_inet_eth_network);
    RUN_TEST(test_inet_eth_assign_scheme);
    RUN_TEST(test_inet_eth_mtu);
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
        log_open("INET_ETH_TEST", LOG_OPEN_STDOUT);

    run_test_inet_eth();

    return ut_fini();
}
