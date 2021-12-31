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

#include "inet_fw.h"

#define EXIT_OK(status) (WIFEXITED(status) && WEXITSTATUS(status) == 0)

void setUp() {}
void tearDown() {}

#if 0
procfs_entry_t *procfs_find_upnp(void)
{
    procfs_t pr;
    procfs_entry_t *pe;

    procfs_entry_t *retval = NULL;

    if (!procfs_open(&pr)) return NULL;

    while ((pe = procfs_read(&pr)) != NULL)
    {
        if (strstr(pe->pe_name, "miniupnpd") == NULL) continue;
        if (pe->pe_cmdline == NULL) continue;

        if (!procfs_entry_has_args(pe, "-f", "/tmp/miniupnpd/miniupnpd.conf", NULL)) continue;

        break;
    }

    if (pe != NULL)
    {
        /* Found it */
        retval = procfs_entry_getpid(pe->pe_pid);
    }

    procfs_close(&pr);

    return retval;
}
#endif

void test_fw_nat()
{
    inet_fw_t *volatile fw;

    if (TEST_PROTECT())
    {
        int status;

        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(
                execpr(sh_create_vlan, "eth0", "100") == 0,
                "Error creating eth0.100");

        /* Plant a fake rule -- inet_fw_new() shall delete it upon startup */
        status = execpr("iptables -t nat -A NM_NAT -o eth0.100 -j MASQUERADE", NULL);
        TEST_ASSERT_TRUE_MESSAGE(
                EXIT_OK(status),
                "Error creating fake itpables rule");

        fw = inet_fw_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                fw != NULL,
                "Error creating inet_fw instance for interface eth0.100");

        /* Test if the rule is still there after startup */
        status = execpr("iptables -t nat -C NM_NAT -o eth0.100 -j MASQUERADE");
        TEST_ASSERT_FALSE_MESSAGE(
                EXIT_OK(status),
                "Fake rule still active after inet_fw instantiation.");


        /* Enable NAT */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_nat_set(fw, true),
                "Error enabling NAT");

        /* Start firewall */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_start(fw),
                "Error starting firewall");

        /* Check if the correct rules were installed */
        status = execpr("iptables -t nat -C NM_NAT -o eth0.100 -j MASQUERADE");
        TEST_ASSERT_TRUE_MESSAGE(
                EXIT_OK(status),
                "NAT rules were not successfully set.");

        /* Check if the correct rules were installed */
        status = execpr("iptables -t nat -C NM_PORT_FORWARD -i eth0.100 -j MINIUPNPD");
        TEST_ASSERT_TRUE_MESSAGE(
                EXIT_OK(status),
                "MINIUPNPD rules were not successfully set.");

        /* Check that LAN rules are not active */
        status = execpr("iptables -t filter -C NM_INPUT -i eth0.100 -j ACCEPT");
        TEST_ASSERT_FALSE_MESSAGE(
                EXIT_OK(status),
                "LAN rules are active on interface, but they shouldn't be.");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_stop(fw),
                "Error stopping firewall");

        status = execpr("iptables -t nat -C NM_NAT -o eth0.100 -j MASQUERADE");
        TEST_ASSERT_FALSE_MESSAGE(
                EXIT_OK(status),
                "Firewall disabled, NAT rules should not be enabled.");

        status = execpr("iptables -t nat -C NM_PORT_FORWARD -i eth0.100 -j MINIUPNPD");
        TEST_ASSERT_FALSE_MESSAGE(
                EXIT_OK(status),
                "Firewall disabled, MINIUPNPD rules should not be enabled.");

        status = execpr("iptables -t filter -C NM_INPUT -i eth0.100 -j ACCEPT");
        TEST_ASSERT_FALSE_MESSAGE(
                EXIT_OK(status),
                "Firewall disabled, LAN rules should not be enabled.");
   }

    if (fw != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
            inet_fw_del(fw),
            "Error deleting firewall instance.");
    }

    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

void test_fw_lan()
{
    inet_fw_t *volatile fw;

    if (TEST_PROTECT())
    {
        int status;

        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(
                execpr(sh_create_vlan, "eth0", "100") == 0,
                "Error creating eth0.100");

        /* Plant a fake rule -- inet_fw_new() shall delete it upon startup */
        status = execpr("iptables -t filter -A NM_INPUT -i eth0.100 -j ACCEPT");
        TEST_ASSERT_TRUE_MESSAGE(
                EXIT_OK(status),
                "LAN rules are active on interface, but they shouldn't be.");

        fw = inet_fw_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                fw != NULL,
                "Error creating inet_fw instance for interface eth0.100");

        /* Test if the rule is still there after startup */
        status = execpr("iptables -t filter -C NM_INPUT -i eth0.100 -j ACCEPT");
        TEST_ASSERT_FALSE_MESSAGE(
                EXIT_OK(status),
                "Fake rule still active after inet_fw instantiation.");


        /* Disable NAT */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_nat_set(fw, false),
                "Error enabling NAT");

        /* Start firewall */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_start(fw),
                "Error starting firewall");

        /* Check if the correct rules were installed */
        status = execpr("iptables -t nat -C NM_NAT -o eth0.100 -j MASQUERADE");
        TEST_ASSERT_FALSE_MESSAGE(
                EXIT_OK(status),
                "NAT rules are set, but should not be.");

        /* Check if the correct rules were installed */
        status = execpr("iptables -t nat -C NM_PORT_FORWARD -i eth0.100 -j MINIUPNPD");
        TEST_ASSERT_FALSE_MESSAGE(
                EXIT_OK(status),
                "MINIUPNPD rules are set but should not be.");

        /* Check that LAN rules ARE active */
        status = execpr("iptables -t filter -C NM_INPUT -i eth0.100 -j ACCEPT");
        TEST_ASSERT_TRUE_MESSAGE(
                EXIT_OK(status),
                "LAN rules are active on interface, but they shouldn't be.");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_stop(fw),
                "Error stopping firewall");

        status = execpr("iptables -t nat -C NM_NAT -o eth0.100 -j MASQUERADE");
        TEST_ASSERT_FALSE_MESSAGE(
                EXIT_OK(status),
                "Firewall disabled, NAT rules should not be enabled.");

        status = execpr("iptables -t nat -C NM_PORT_FORWARD -i eth0.100 -j MINIUPNPD");
        TEST_ASSERT_FALSE_MESSAGE(
                EXIT_OK(status),
                "Firewall disabled, MINIUPNPD rules should not be enabled.");

        status = execpr("iptables -t filter -C NM_INPUT -i eth0.100 -j ACCEPT");
        TEST_ASSERT_FALSE_MESSAGE(
                EXIT_OK(status),
                "Firewall disabled, LAN rules should not be enabled.");
   }

    if (fw != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
            inet_fw_del(fw),
            "Error deleting firewall instance.");
    }

    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}

#if 0
void test_fw_upnp()
{
    procfs_entry_t *pe;

    inet_fw_t *volatile fw_ext;
    inet_fw_t *volatile fw_int;

    if (TEST_PROTECT())
    {
        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(
                execpr(sh_create_vlan, "eth0", "100") == 0,
                "Error creating eth0.100");

        /* Create a VLAN.100 interface */
        TEST_ASSERT_TRUE_MESSAGE(
                execpr(sh_create_vlan, "eth0", "200") == 0,
                "Error creating eth0.200");

        fw_ext = inet_fw_new("eth0.100");
        TEST_ASSERT_TRUE_MESSAGE(
                fw_ext != NULL,
                "Error creating inet_fw instance for interface eth0.100");

        fw_int = inet_fw_new("eth0.200");
        TEST_ASSERT_TRUE_MESSAGE(
                fw_int != NULL,
                "Error creating inet_fw instance for interface eth0.100");

        /* Enable NAT on the external interface */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_nat_set(fw_ext, true),
                "Error enabling NAT");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_upnp_set(fw_ext, UPNP_MODE_EXTERNAL),
                "Unable to set external UPnP mode");

        /* Enable LAN on the internal interface */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_nat_set(fw_int, false),
                "Error disabling NAT");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_upnp_set(fw_int, UPNP_MODE_INTERNAL),
                "Unable to set internal UPnP mode");

        /* Start firewall */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_start(fw_ext),
                "Error starting firewall on eth0.100");

        /* Start firewall */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_start(fw_int),
                "Error starting firewall on eth0.200");

        ev_wait(NULL, 3.0);

        find_in_file("/tmp/miniupnpd/miniupnpd.conf", "ext_ifname=eth0.100");
        find_in_file("/tmp/miniupnpd/miniupnpd.conf", "listening_ip=eth0.200");
        find_in_file("/tmp/miniupnpd/miniupnpd.conf", "lease_file=/tmp/miniupnpd/upnpd.leases");

        pe = procfs_find_upnp();
        TEST_ASSERT_TRUE_MESSAGE(
                pe != NULL,
                "miniupnpd is not running.");

        PR("MiniUPnPD is running.");

        /* Stop one interface -- in this case MiniUPnP should be shutdown */
        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_stop(fw_int),
                "Error stopping firewall on eth0.100");

        ev_wait(NULL, 3.0);

        pe = procfs_find_upnp();
        TEST_ASSERT_TRUE_MESSAGE(
                pe == NULL,
                "miniupnpd is running.");

        PR("MiniUPnPD was terminated.");

        TEST_ASSERT_TRUE_MESSAGE(
                inet_fw_stop(fw_ext),
                "Error stopping firewall on eth0.200");

    }

    if (fw_int != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
            inet_fw_del(fw_int),
            "Error deleting firewall instance.");
    }

    if (fw_ext != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(
            inet_fw_del(fw_ext),
            "Error deleting firewall instance.");
    }

    if (execpr(sh_delete_vlan, "eth0", "200") != 0)
    {
        PR("Error deleting interface eth0.200");
    }

    if (execpr(sh_delete_vlan, "eth0", "100") != 0)
    {
        PR("Error deleting interface eth0.100");
    }
}
#endif

void run_test_udhcpc(void)
{
    RUN_TEST(test_fw_nat);
    RUN_TEST(test_fw_lan);
#if 0
    RUN_TEST(test_fw_upnp);
#endif
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
        log_open("FW_TEST", LOG_OPEN_STDOUT);

    UNITY_BEGIN();

    run_test_udhcpc();

    return UNITY_END();
}
