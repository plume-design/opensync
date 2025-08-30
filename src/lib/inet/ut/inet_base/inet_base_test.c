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

#include "inet.h"
#include "inet_base.h"

#include "../tests_common.c"
#include "unit_test_utils.h"

char *test_name = "inet_base_test";
/*
 * ===========================================================================
 *  INET BASE
 * ===========================================================================
 */

/**
 * Subclass inet_base
 */
typedef struct __inet_test
{
    union
    {
        inet_t      inet;
        inet_base_t base;
    };

    bool enabled_services[INET_BASE_MAX];
}
inet_test_t;

static bool test_create_fail = false;
static bool inet_test_service_commit(inet_base_t *super, enum inet_base_services srv, bool enable);

bool inet_test_init(inet_test_t *self)
{
    memset(self, 0, sizeof(*self));

    if (test_create_fail) return false;

    if (!inet_base_init(&self->base, "eth0.100"))
    {
        return false;
    }

    self->base.in_service_commit_fn = inet_test_service_commit;

    return true;
}

bool inet_test_interface_start(inet_test_t *self, bool enable)
{
    (void)self;

    int status;

    if (enable)
    {
        PR("Creating interface\n");
        /* Create the test interface */
        status  = execpr(sh_create_vlan, "eth0", "100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error creating test interface (eth0.100).");
            return false;
        }
    }
    else
    {
        PR("Destroying interface\n");
        /* Create the test interface */
        status = execpr(sh_delete_vlan, "eth0", "100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error deleting test interface (eth0.100).");
            return false;
        }
    }

    return true;
}

bool inet_test_network_start(inet_test_t *self, bool enable)
{
    (void)self;

    int status;

    if (enable)
    {
        PR("Enabling network...");
        status = execpr(_S(ifconfig "$1" up), "eth0.100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error enabling network.");
        }
    }
    else
    {
        PR("Disabling network...");
        status = execpr(_S(ifconfig "$1" down), "eth0.100");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_test: Error enabling network.");
        }
    }
    return true;
}

/* Assign an IP of 1.1.1.1 for assign_scheme none so it's easier to verify */
bool inet_test_scheme_none_start(inet_test_t *self, bool enable)
{
    (void)self;

    int status;

    PR("Assign scheme NONE -> %d\n", enable);

    if (enable)
    {
        status = execpr(_S(ifconfig "$1" 1.1.1.1 netmask 255.255.255.255), "eth0.100");
    }
    else
    {
        status = execpr(_S(ifconfig "$1" 0.0.0.0), "eth0.100");
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Assign an IP of 1.1.1.1 for assign_scheme none so it's easier to verify */
bool inet_test_scheme_static_start(inet_test_t *self, bool enable)
{
    int status;

    bool retval = true;

    PR("Assign scheme STATIC -> %d\n", enable);

    if (enable)
    {
        if (osn_ip_addr_cmp(&self->base.in_static_addr, &OSN_IP_ADDR_INIT) == 0)
        {
            LOG(ERR, "static scheme selected but ipaddr is empty");
            return false;
        }

        if (osn_ip_addr_cmp(&self->base.in_static_netmask, &OSN_IP_ADDR_INIT) == 0)
        {
            LOG(ERR, "static scheme selected but netmask is empty");
            return false;
        }

        if (osn_ip_addr_cmp(&self->base.in_static_bcast, &OSN_IP_ADDR_INIT) == 0)
        {
            LOG(ERR, "static scheme selected but bcast is empty");
            return false;
        }

        char saddr[C_IP4ADDR_LEN];
        char snetmask[C_IP4ADDR_LEN];
        char sbcast[C_IP4ADDR_LEN];
        char sgwaddr[C_IP4ADDR_LEN];

        snprintf(saddr, sizeof(saddr), PRI_osn_ip_addr, FMT_osn_ip_addr(self->base.in_static_addr));
        snprintf(snetmask, sizeof(snetmask), PRI_osn_ip_addr, FMT_osn_ip_addr(self->base.in_static_netmask));
        snprintf(sbcast, sizeof(sbcast), PRI_osn_ip_addr, FMT_osn_ip_addr(self->base.in_static_bcast));

        status = execpr(_S(ifconfig "$1" "$2" netmask "$3" broadcast "$4"), "eth0.100", saddr, snetmask, sbcast);
        if (!WIFEXITED(status) && WEXITSTATUS(status) != 0)
        {
            PR("Errors setting static IP settings.");
            retval = false;
        }

        if (osn_ip_addr_cmp(&self->base.in_static_gwaddr, &OSN_IP_ADDR_INIT) != 0)
        {
            PR("Adding default gateway "PRI_osn_ip_addr"... ", FMT_osn_ip_addr(self->base.in_static_gwaddr));
            snprintf(sgwaddr, sizeof(sgwaddr), PRI_osn_ip_addr, FMT_osn_ip_addr(self->base.in_static_gwaddr));
            status = execpr(_S(ip route add default via "$2" dev "$1" metric 200), "eth0.100", sgwaddr);
            if (!WIFEXITED(status) && WEXITSTATUS(status) != 0)
            {
                retval = false;
            }
        }
    }
    else
    {
        /* Remove all static routes -- ignore errors */
        (void)execpr(_S(ip route del default dev "$1" || true), "eth0.100");

        status = execpr(_S(ifconfig "$1" 0.0.0.0), "eth0.100");
        if (!WIFEXITED(status) && WEXITSTATUS(status) != 0)
        {
            PR("Error unsetting static IP settings.");
            retval = false;
        }
    }

    return retval;
}

bool inet_test_service_commit(inet_base_t *super, enum inet_base_services srv, bool enable)
{
    inet_test_t *self = (inet_test_t *)super;

    self->enabled_services[srv] = enable;

    switch (srv)
    {
        case INET_BASE_IF_CREATE:
            return inet_test_interface_start(self, enable);

        case INET_BASE_NETWORK:
            return inet_test_network_start(self, enable);

        case INET_BASE_SCHEME_NONE:
            return inet_test_scheme_none_start(self, enable);

        case INET_BASE_SCHEME_STATIC:
            return inet_test_scheme_static_start(self, enable);

        default:
            PR("Delegating ...");
            /* Delegate everything else to inet_base() */
            return inet_base_service_commit(super, srv, enable);
    }

    return true;
}

void test_inet_base(void)
{
    inet_test_t it;
    int ii;
    int status;

    if (TEST_PROTECT())
    {
        TEST_ASSERT_TRUE_MESSAGE(
                inet_test_init(&it),
                "Failed to initialize inet_test() instance");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "This commit failed.");

        PR("Enabling interface ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_interface_enable(&it.inet, true),
                "Error enabling interface");

        PR("Enabling NAT ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_nat_enable(&it.inet, true),
                "Error enabling NAT");

        for (ii = 0; ii < INET_BASE_MAX; ii++)
        {
            TEST_ASSERT_FALSE_MESSAGE(
                    it.enabled_services[ii],
                    "Services were enabled before commit.");
        }

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed..");

        TEST_ASSERT_TRUE_MESSAGE(
                it.enabled_services[INET_BASE_FIREWALL],
                "Firewall is not enabled");

        TEST_ASSERT_TRUE_MESSAGE(
                it.enabled_services[INET_BASE_IF_CREATE],
                "Interface is not enabled");

        PR("Checking if eth0.100 exists ...");
        status = execpr(_S([ -d /sys/class/net/eth0.100 ]));
        TEST_ASSERT_TRUE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Test interface does not exist.");

        PR("Disabling interface ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_interface_enable((inet_t *)&it, false),
                "Error enabling interface");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit((inet_t *)&it),
                "Commit failed..");

        /* Interface is a prerequisite for all other services, check if it disabled firewall as well */
        TEST_ASSERT_FALSE_MESSAGE(
                it.enabled_services[INET_BASE_FIREWALL],
                "Firewall is enabled");

        TEST_ASSERT_FALSE_MESSAGE(
                it.enabled_services[INET_BASE_IF_CREATE],
                "Interface is enabled");

        PR("Checking if eth0.100 exists ...");
        status = execpr(_S([ -d /sys/class/net/eth0.100 ]));
        TEST_ASSERT_FALSE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Test interface does not exist.");

        PR("Re-enabling interface.");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_interface_enable(&it.inet, true),
                "Error re-enabling interface");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Error on commit");

        PR("Checking if eth0.100 exists ...");
        status = execpr(_S([ -d /sys/class/net/eth0.100 ]));
        TEST_ASSERT_TRUE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Test interface does not exist.");
    }

    TEST_ASSERT_TRUE_MESSAGE(
        inet_fini(&it.inet),
        "Destructor failed.");

    /* Check if the destructor properly cleaned up */
    PR("Checking if eth0.100 exists ...");
    status = execpr(_S([ -d /sys/class/net/eth0.100 ]));
    TEST_ASSERT_FALSE_MESSAGE(
            WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "Test interface does not exist.");
}

void test_inet_base_network(void)
{
    inet_test_t it;
    int status;

    if (TEST_PROTECT())
    {
        TEST_ASSERT_TRUE_MESSAGE(
                inet_test_init(&it),
                "Failed to initialize inet_test() instance");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");

        PR("Enabling interface ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_interface_enable(&it.inet, true),
                "Error enabling interface");

        PR("Enabling Network ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_network_enable(&it.inet, true),
                "Error enabling network");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");

        ev_wait(NULL, 3.0);

        TEST_ASSERT_TRUE_MESSAGE(
                it.enabled_services[INET_BASE_NETWORK],
                "Firewall is not enabled");

        TEST_ASSERT_TRUE_MESSAGE(
                it.enabled_services[INET_BASE_IF_CREATE],
                "Interface is not enabled");

        PR("Check if the network is UP\n");
        /* Check if the interface is UP */
        status = execpr(_S([ $(cat /sys/class/net/"$1"/operstate ) == "up" ]), "eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Interface is not UP");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_network_enable(&it.inet, false),
                "Unable to set network state");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");

        /* Check if the interface is Down */
        status = execpr(_S([ $(cat /sys/class/net/"$1"/operstate ) == "down" ]), "eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Interface is not DOWN");
    }

    TEST_ASSERT_TRUE_MESSAGE(
        inet_fini(&it.inet),
        "Destructor failed.");
}

/**
 * Test various assignment schemas
 */
void test_inet_base_assign_scheme(void)
{
    inet_test_t it;
    int status;

    procfs_entry_t *volatile pe = NULL;

    if (TEST_PROTECT())
    {
        TEST_ASSERT_TRUE_MESSAGE(
                inet_test_init(&it),
                "Failed to initialize inet_test() instance");

        PR("Enabling interface ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_interface_enable(&it.inet, true),
                "Error enabling interface");

        PR("Enabling Network ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_network_enable(&it.inet, true),
                "Error enabling network");

        PR("Setting assign_scheme -> None");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_assign_scheme_set(&it.inet, INET_ASSIGN_NONE),
                "Error setting assign scheme");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");

        /* Check if we have an IP of 1.1.1.1 */
        status = execpr(_S(ip addr show "$1" | grep "1.1.1.1/32"), "eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Assignment NONE error");

        PR("Setting assign_scheme -> DHCP");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_assign_scheme_set(&it.inet, INET_ASSIGN_DHCP),
                "Error setting assign scheme");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");

        ev_wait(NULL, 1.0);

        /* Check if udhcpc exists */
        pe = procfs_find_dhcpc("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "UDHCPC instance not found");


        PR("Setting assign_scheme -> STATIC");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_assign_scheme_set(&it.inet, INET_ASSIGN_STATIC),
                "Error setting assign scheme");

        osn_ip_addr_t ip;
        osn_ip_addr_t netmask;
        osn_ip_addr_t bcast;

        osn_ip_addr_from_str(&ip, "192.168.100.1");
        osn_ip_addr_from_str(&netmask, "255.255.255.0");
        osn_ip_addr_from_str(&bcast, "192.168.100.255");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_ipaddr_static_set(&it.inet,
                    ip,
                    netmask,
                    bcast),
                    "Error setting static IP");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");

        /* Check if the static address was assigned properly */
        status = execpr(_S(ip addr show dev "$1" | grep "inet 192.168.100.1/24 brd 192.168.100.255"), "eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Static IP configuration was not applied properly.");

        osn_ip_addr_t gw;
        osn_ip_addr_from_str(&gw, "192.168.100.254");

        PR("Adding default gateway");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_gateway_set(&it.inet, gw),
                "Error setting static IP");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
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

    TEST_ASSERT_TRUE_MESSAGE(
        inet_fini(&it.inet),
        "Destructor failed.");
}

/**
 * Test the DHCP server
 */
void test_inet_base_dhcps(void)
{
    inet_test_t it;

    procfs_entry_t *volatile pe = NULL;

    if (TEST_PROTECT())
    {
        TEST_ASSERT_TRUE_MESSAGE(
                inet_test_init(&it),
                "Failed to initialize inet_test() instance");

        PR("Enabling interface ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_interface_enable(&it.inet, true),
                "Error enabling interface");

        PR("Enabling Network ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_network_enable(&it.inet, true),
                "Error enabling network");

        /* The DHCP server can be started only when the assignment_schem is STATIC */
        PR("Setting assign_scheme -> STATIC");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_assign_scheme_set(&it.inet, INET_ASSIGN_STATIC),
                "Error setting assign scheme");

        osn_ip_addr_t ip;
        osn_ip_addr_t netmask;
        osn_ip_addr_t bcast;

        osn_ip_addr_from_str(&ip, "192.168.100.1");
        osn_ip_addr_from_str(&netmask, "255.255.255.0");
        osn_ip_addr_from_str(&bcast, "192.168.100.255");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_ipaddr_static_set(&it.inet,
                    ip,
                    netmask,
                    bcast),
                    "Error setting static IP");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_dhcps_enable(&it.inet, true),
                "Error enabling the DHCP server");

        PR("Setting DHCP range and options.");

        osn_ip_addr_t start;
        osn_ip_addr_t stop;

        osn_ip_addr_from_str(&start, "192.168.100.50");
        osn_ip_addr_from_str(&stop, "192.168.100.100");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_dhcps_range_set(&it.inet,
                        start,
                        stop),
                "Error setting the DHCP server IP pool range.");

        /* Add few options just for the sake of testing it */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_dhcps_option_set(&it.inet,
                        DHCP_OPTION_ROUTER,
                        "192.168.100.1"),
                "Error setting the DHCP option.");

        /* Now we should have the necessary DHCP server configuration, the next commit should succeed */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");

        ev_wait(NULL, 1.0);

        pe = procfs_find_dnsmasq();
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "DNSMASQ has not been started.");

        PR("Found DNSMASQ instance at pid %jd", (intmax_t)pe->pe_pid);

        procfs_entry_del(pe);
        pe = NULL;

        PR("Checking DNSMASQ config file...");
        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/var/etc/dnsmasq.conf", "dhcp-range=eth0.100,192.168.100.50,192.168.100.100"),
                "Missing configuration for eth0.100 in /var/etc/dnsmasq.conf");

        /* Now switch to a NONE assignment scheme, this should kill the DHCPS server because it requires
         * a static configuration */
        PR("Switching to NONE assignment scheme (the DHCP server should be shutdown).");

        /* The DHCP server can be started only when the assignment_schem is STATIC */
        PR("Setting assign_scheme -> NONE");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_assign_scheme_set(&it.inet, INET_ASSIGN_NONE),
                "Error setting assign scheme");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");
    }

    if (pe != NULL)
    {
        procfs_entry_del(pe);
    }

    TEST_ASSERT_TRUE_MESSAGE(
        inet_fini(&it.inet),
        "Destructor failed.");
}

/**
 * Test the DNS settings
 */
void test_inet_base_dns(void)
{
    inet_test_t it;

    if (TEST_PROTECT())
    {
        /*
         * Bring up the interface
         */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_test_init(&it),
                "Failed to initialize inet_test() instance");

        PR("Enabling interface ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_interface_enable(&it.inet, true),
                "Error enabling interface");

        PR("Enabling Network ...");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_network_enable(&it.inet, true),
                "Error enabling network");

        /*
         * For starters, assign a STATIC scheme with no DNS settings
         */
        PR("Setting assign_scheme -> STATIC");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_assign_scheme_set(&it.inet, INET_ASSIGN_STATIC),
                "Error setting assign scheme");

        osn_ip_addr_t ip;
        osn_ip_addr_t netmask;
        osn_ip_addr_t bcast;

        osn_ip_addr_from_str(&ip, "192.168.100.1");
        osn_ip_addr_from_str(&netmask, "255.255.255.0");
        osn_ip_addr_from_str(&bcast, "192.168.100.255");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_ipaddr_static_set(&it.inet,
                    ip,
                    netmask,
                    bcast),
                    "Error setting static IP");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");

        ev_wait(NULL, 1.0);

        /* Check that there are NO DNS settings */
        TEST_ASSERT_FALSE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "Interface: eth0.100"),
                "DNS settings are present, but shouldn't be");

        osn_ip_addr_t srv1;
        osn_ip_addr_t srv2;

        osn_ip_addr_from_str(&srv1, "1.2.3.4");
        osn_ip_addr_from_str(&srv2, "4.3.2.1");

        /*
         * Apply DNS settings and verify that they were applied to the system (/tmp/resolv.conf)
         */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_dns_set(&it.inet, srv1, srv2),
                "Error applying DNS settings");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");

        ev_wait(NULL, 1.0);

        /* Check that there are DNS settings */
        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "Interface: eth0.100"),
                "DNS settings were not applied to the system");

        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "nameserver 1.2.3.4"),
                "DNS settings were not applied to the system (primary).");

        TEST_ASSERT_TRUE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "nameserver 4.3.2.1"),
                "DNS settings were not applied to the system (secondary).");

        /* Now switch to a NONE assignment scheme, this should remove DNS settings */
        PR("Switching to NONE assignment scheme (DNS server settings should get removed).");

        /* The DHCP server can be started only when the assignment_schem is STATIC */
        PR("Setting assign_scheme -> NONE");
        TEST_ASSERT_TRUE_MESSAGE(
                inet_assign_scheme_set(&it.inet, INET_ASSIGN_NONE),
                "Error setting assign scheme");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_commit(&it.inet),
                "Commit failed.");

        /* Wait until DNSMASQ is stopped */
        ev_wait(NULL, 1.0);

        /* Check that there are DNS settings */
        TEST_ASSERT_FALSE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "Interface: eth0.100"),
                "DNS settings were applied to the system, but scheme is NONE");

        TEST_ASSERT_FALSE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "nameserver 1.2.3.4"),
                "DNS settings were applied to the system (primary), but scheme is NONE");

        TEST_ASSERT_FALSE_MESSAGE(
                find_in_file("/tmp/resolv.conf", "nameserver 4.3.2.1"),
                "DNS settings were applied to the system (secondary), but scheme is NONE");
    }

    TEST_ASSERT_TRUE_MESSAGE(
        inet_fini(&it.inet),
        "Destructor failed.");

    ev_wait(NULL, 1.0);
}


void run_test_inet_base(void)
{
    RUN_TEST(test_inet_base);
    RUN_TEST(test_inet_base_network);
    RUN_TEST(test_inet_base_assign_scheme);
    RUN_TEST(test_inet_base_dhcps);
    RUN_TEST(test_inet_base_dns);
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
        log_open("INET_BASE_TEST", LOG_OPEN_STDOUT);

    run_test_inet_base();

    return ut_fini();
}
