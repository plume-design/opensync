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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "memutil.h"
#include "execsh.h"
#include "const.h"
#include "util.h"
#include "log.h"

#include "schema.h"
#include "ovsdb_table.h"

#include "daemon.h"

/*
 * Alert:
 *   This code is subject to be removed in the near future when proper OpenSync and
 *   controller-driven solution for configuring relaying of IPv6 management protocols
 *   will be implemented.
 *
 *   Do not use the following APIs, as they are planned to be removed/changed. These APIs
 *   are currently (temporarily) allowed to be used only by OpenSync MAP-T/MAP-E implementation.
 *
 * Note: Since this is an ad hoc temporary implementation it is not fully following OpenSync
 * standards and patterns.
 */

#define NM2_IPV6RELAY_DAEMON_PATH        "/usr/sbin/6relayd"
#define NM2_IPV6RELAY_DAEMON_PID_FILE    "/var/run/6relayd.pid"

bool nm2_ipv6_relay_start(char *master_if_name, char *slave_if_name);
bool nm2_ipv6_relay_stop(void);

static bool nm2_ipv6_relay_fw_rule(bool enable);

static daemon_t ipv6_relayd;
static bool ipv6_relayd_inited;

/*
 * Start relaying IPv6 management protocols: RA, DHCPv6 and NDP.
 *
 * @param[in]  master_if_name   Master interface name
 * @param[in]  slave_if_name    Slave interface name
 *
 * @return     true on success
 */
bool nm2_ipv6_relay_start(char *master_if_name, char *slave_if_name)
{
    bool daemon_started;

    daemon_is_started(&ipv6_relayd, &daemon_started);
    if (daemon_started)
    {
        nm2_ipv6_relay_stop();
    }

    if (!daemon_init(&ipv6_relayd, NM2_IPV6RELAY_DAEMON_PATH, 0))
    {
        LOG(ERR, "nm2_ipv6_relay: Unable to initialize daemon object.");
        return false;
    }
    ipv6_relayd_inited = true;

    /* Set the PID file location -- necessary to kill stale instances */
    if (!daemon_pidfile_set(&ipv6_relayd, NM2_IPV6RELAY_DAEMON_PID_FILE, false))
    {
        LOG(WARN, "nm2_ipv6_relay: Error setting the PID file path.");
    }

    if (!daemon_restart_set(&ipv6_relayd, true, 3.0, 5))
    {
        LOG(WARN, "nm2_ipv6_relay: Error enabling daemon auto-restart on global instance.");
    }

    daemon_arg_add(&ipv6_relayd, "-A");
    daemon_arg_add(&ipv6_relayd, master_if_name);
    daemon_arg_add(&ipv6_relayd, slave_if_name);

    LOG(DEBUG, "nm2_ipv6_relay Daemon inited");

    if (!daemon_start(&ipv6_relayd))
    {
        LOG(ERROR, "nm2_ipv6_relay: Error starting IPv6 relay daemon");
        return false;
    }

    LOG(INFO, "nm2_ipv6_relay: IPv6 relay started: master interface: %s, slave interface: %s."
                " Mode: NDP (RA, RS, NS, NA) and DHCPv6 relay",
                    master_if_name, slave_if_name);

    nm2_ipv6_relay_fw_rule(true);

    return true;
}

/*
 * Stop relaying IPv6 management protocols.
 */
bool nm2_ipv6_relay_stop(void)
{
    LOG(DEBUG, "nm2_ipv6_relay: Stopping IPv6 relay daemon...");

    daemon_stop(&ipv6_relayd);

    if (ipv6_relayd_inited)
    {
        daemon_fini(&ipv6_relayd);
        ipv6_relayd_inited = false;
    }

    nm2_ipv6_relay_fw_rule(false);

    return true;
}

/*
 * DHCPv6 servers as well as relay agents use UDP port 547. Thus, when starting 6relayd
 * we must allow INPUT to UDP port 547 as the relay-reply message will be sent to UDP dst 547.
 *
 */
#if defined(CONFIG_OPENSYNC_LEGACY_FIREWALL)
static bool nm2_ipv6_relay_fw_rule(bool enable)
{
    const char *rule = "-p udp -m state --state NEW -m udp --dport 547";

    int rc = execsh_log(
            LOG_SEVERITY_DEBUG,
            _S(ip6tables -w -t filter "$1" INPUT -j ACCEPT $2),
            enable ? "-A" : "-D",
            (char *)rule);

    return rc == 0;
}
#else
static bool nm2_ipv6_relay_fw_rule(bool enable)
{
    const char *rule = "-p udp -m state --state NEW -m udp --dport 547";
    const char *name = "nm2.ipv6_relay.dhcpv6";
    bool rv;

    ovsdb_table_t table_Netfilter;

    OVSDB_TABLE_INIT_NO_KEY(Netfilter);

    if (enable)
    {
        struct schema_Netfilter netfilter;

        memset(&netfilter, 0, sizeof(netfilter));

        SCHEMA_SET_STR(netfilter.name, name);
        SCHEMA_SET_INT(netfilter.enable, true);
        SCHEMA_SET_STR(netfilter.table, "filter");
        SCHEMA_SET_INT(netfilter.priority, 0);
        SCHEMA_SET_STR(netfilter.protocol, "ipv6");
        SCHEMA_SET_STR(netfilter.chain, "INPUT");
        SCHEMA_SET_STR(netfilter.target, "ACCEPT");
        SCHEMA_SET_STR(netfilter.rule, rule);

        rv = ovsdb_table_upsert_simple(
                &table_Netfilter,
                SCHEMA_COLUMN(Netfilter, name),
                netfilter.name,
                &netfilter, true);
        if (!rv)
        {
            LOG(ERR, "nm2_ipv6_relay: Error inserting rule %s into the Netfilter table: %s", name, rule);
        }
    }
    else
    {
        rv = ovsdb_table_delete_simple(&table_Netfilter, SCHEMA_COLUMN(Netfilter, name), name);
        if (!rv)
        {
            LOG(DEBUG, "nm2_ipv6_relay: Error deleting rule %s from the Netfilter table: %s", name, rule);
        }
    }
    return rv;
}
#endif
