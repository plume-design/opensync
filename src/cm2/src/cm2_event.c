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

/*

Basic logic:

Start by connecting to redirector_addr

In any state:
  if device type is TARGET_EXTENDER_TYPE
      start link selection process
  else
      go to init ovs state

In link selection state:
  waiting for main link that can be used
  if yes goes to br wan ip state

In wan ip state
  Waiting for IP on WAN bridge or uplink
  if yes goes to init ovs state

In init ovs state:
  if redirector_addr is updated then try to connect to redirector.
In any state except if connected to manager:
  if manager_addr is updated then try to connect to manager.

In connected state:
   - if device can work as TARGET_EXTENDER_TYPE start stability check functionality

Each connect attempt goes through all resolved IPs until connection established

If we get disconnected from manager:
  count disconnects += 1
  if disconnects > 3 go back to redirector
  wait up to 1 minute if ovsdb is able to re-connect (*Note-1)
  if re-connect is successful and connection is stable for more than 10 minutes
    then reset disconnects = 0
  if re-connect does not happen, go back to redirector

If anything else goes wrong go back to redirector

Note-1: the wait for re-connect back to same manager addr because
  while doing channel/topology change we expect to loose the connection
  for a short while, but to speed up re-connect to controller we try to
  avoid going through redirector.

*/

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <resolv.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "os.h"
#include "util.h"
#include "os_time.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "log.h"
#include "ds.h"
#include "json_util.h"
#include "cm2.h"
#include "target.h"

#define MODULE_ID LOG_MODULE_ID_EVENT

// interval and timeout in seconds
#define CM2_EVENT_INTERVAL              1
#define CM2_NO_TIMEOUT                  -1
#define CM2_DEFAULT_TIMEOUT             60  // 1 min
#define CM2_FAST_RECONNECT_TIMEOUT      20
#define CM2_ONBOARD_LINK_SEL_TIMEOUT    120
#define CM2_RESOLVE_TIMEOUT             180
#define CM2_CONNECT_TIMEOUT             30

#define CM2_MAX_DISCONNECTS             10
#define CM2_STABLE_PERIOD               300 // 5 min
#define CM2_RESOLVE_RETRY_THRESHOLD     10
#define CM2_RESOLVE_FATAL_THRESHOLD     5
#define CM2_CONNECT_FATAL_THRESHOLD     10
#define CM2_GW_OFFLINE_RETRY_THRESHOLD  3
#define CM2_GW_SKIP_RESTART_THRESHOLD   360

// state info
#define CM2_STATE_DIR  "/tmp/plume/"
#define CM2_STATE_FILE CM2_STATE_DIR"cm.state"
#define CM2_STATE_TMP  CM2_STATE_DIR"cm.state.tmp"

typedef struct cm2_state_info
{
    char *name;
    int timeout;
} cm2_state_info_t;

cm2_state_info_t cm2_state_info[CM2_STATE_NUM] =
{
    [CM2_STATE_INIT]             = { "INIT",                CM2_NO_TIMEOUT },
    [CM2_STATE_LINK_SEL]         = { "LINK_SEL",            CM2_ONBOARD_LINK_SEL_TIMEOUT },
    [CM2_STATE_WAN_IP]           = { "WAN_IP",              CM2_DEFAULT_TIMEOUT },
    [CM2_STATE_NTP_CHECK]        = { "NTP_CHECK",           CM2_DEFAULT_TIMEOUT },
    [CM2_STATE_OVS_INIT]         = { "OVS_INIT",            CM2_NO_TIMEOUT },
    [CM2_STATE_TRY_RESOLVE]      = { "TRY_RESOLVE",         CM2_RESOLVE_TIMEOUT },
    [CM2_STATE_RE_CONNECT]       = { "RE_CONNECT",          CM2_CONNECT_TIMEOUT },
    [CM2_STATE_TRY_CONNECT]      = { "TRY_CONNECT",         CM2_CONNECT_TIMEOUT },
    [CM2_STATE_FAST_RECONNECT]   = { "FAST_RECONNECT",      CM2_FAST_RECONNECT_TIMEOUT },
    [CM2_STATE_CONNECTED]        = { "CONNECTED",           CM2_NO_TIMEOUT },
    [CM2_STATE_QUIESCE_OVS]      = { "QUIESCE_OVS",         CM2_DEFAULT_TIMEOUT },
    [CM2_STATE_INTERNET]         = { "INTERNET",            CM2_DEFAULT_TIMEOUT },
};

// reason for update
char *cm2_reason_name[CM2_REASON_NUM] =
{
    "timer",
    "ovs-awlan",
    "ovs-manager",
    "state-change",
    "link-used",
    "link-not-used",
};

void cm2_ble_onboarding_set_status(bool state, cm2_ble_onboarding_status_t status)
{
    if (state)
        g_state.ble_status |= (1 << status);
    else
        g_state.ble_status &= ~(1 << status);

    LOGI("Set BT status = %x", g_state.ble_status);
}

void cm2_ble_onboarding_apply_config(void)
{
    cm2_ovsdb_ble_config_update(g_state.ble_status);
}

static void cm2_set_backhaul_update_ble_state(void) {
    bool eth_type;

    eth_type = !strcmp(g_state.link.if_type, ETH_TYPE_NAME) ||
               !strcmp(g_state.link.if_type, VLAN_TYPE_NAME);

    g_state.ble_status = 0;
    if (eth_type) {
        cm2_ble_onboarding_set_status(true,
                BLE_ONBOARDING_STATUS_ETHERNET_LINK);
        cm2_ble_onboarding_set_status(true,
                BLE_ONBOARDING_STATUS_ETHERNET_BACKHAUL);
    }  else {
        cm2_ble_onboarding_set_status(true,
                BLE_ONBOARDING_STATUS_WIFI_LINK);
        cm2_ble_onboarding_set_status(true,
                BLE_ONBOARDING_STATUS_WIFI_BACKHAUL);
    }
    cm2_ble_onboarding_apply_config();
}

static void cm2_set_ble_state(bool state, cm2_ble_onboarding_status_t status) {
    cm2_ble_onboarding_set_status(state, status);
    cm2_ble_onboarding_apply_config();
}

char* cm2_dest_name(cm2_dest_e dest)
{
    return (dest == CM2_DEST_REDIR) ? "redirector" : "manager";
}

char* cm2_curr_dest_name(void)
{
    return cm2_dest_name(g_state.dest);
}

void cm2_reset_time(void)
{
    g_state.timestamp = time_monotonic();
}

int cm2_get_time(void)
{
    return time_monotonic() - g_state.timestamp;
}

cm2_state_info_t *cm2_get_state_info(cm2_state_e state)
{
    if ((int)state < 0 || state >= CM2_STATE_NUM)
    {
        LOG(ERROR, "Invalid state: %d", state);
        static cm2_state_info_t invalid = { "INVALID", 0 };
        return &invalid;
    }
    return &cm2_state_info[state];
}

cm2_state_info_t *cm2_curr_state_info(void)
{
    return cm2_get_state_info(g_state.state);
}

char *cm2_get_state_name(cm2_state_e state)
{
    return cm2_get_state_info(state)->name;
}

char *cm2_curr_state_name(void)
{
    return cm2_curr_state_info()->name;
}

int cm2_get_timeout(void)
{
    return cm2_curr_state_info()->timeout;
}

static bool cm2_timeout(bool expected)
{
    int seconds = cm2_get_timeout();
    int delta = cm2_get_time();

    if (seconds != CM2_NO_TIMEOUT && delta >= seconds)
    {
        if (!expected)
        {
            LOG(WARNING, "State %s timeout: %d >= %d",
                    cm2_curr_state_name(), delta, seconds);
        }

        return true;
    }
    return false;
}

void cm2_set_state(bool success, cm2_state_e state)
{
    if (g_state.state == state)
    {
        LOG(DEBUG, "Same state %s %s",
                str_success(success),
                cm2_get_state_name(state));
        return;
    }
    LOG_SEVERITY(success ? LOG_SEVERITY_NOTICE : LOG_SEVERITY_WARNING,
            "State %s %s -> %s",
            cm2_curr_state_name(),
            str_success(success),
            cm2_get_state_name(state));
    g_state.state = state;
    cm2_reset_time();
    g_state.state_changed = true;
}

bool cm2_state_changed(void)
{
    bool changed = g_state.state_changed;
    g_state.state_changed = false;
    return changed;
}


bool cm2_is_connected_to(cm2_dest_e dest)
{
    return (g_state.state == CM2_STATE_CONNECTED && g_state.dest == dest);
}

void cm2_log_state(cm2_reason_e reason)
{
    LOG(DEBUG, "=== Update s: %s r: %s t: %d o: %d",
            cm2_curr_state_name(), cm2_reason_name[reason],
            cm2_get_time(), cm2_get_timeout());
    // dump current state to /tmp
    time_t t = time_real();
    struct tm *lt = localtime(&t);
    char   timestr[80];
    char   str[1024] = "";
    char   *s = str;
    size_t ss = sizeof(str);
    int    len, ret;
    strftime(timestr, sizeof(timestr), "%d %b %H:%M:%S %Z", lt);
    append_snprintf(&s, &ss, "%s\n", timestr);
    append_snprintf(&s, &ss, "s: %s to: %s\n",
            cm2_curr_state_name(),
            cm2_curr_dest_name());
    append_snprintf(&s, &ss, "r: %s t: %d o: %d dis: %d\n",
            cm2_reason_name[reason],
            cm2_get_time(),
            cm2_get_timeout(),
            g_state.disconnects);
    append_snprintf(&s, &ss, "redir:  u:%d v:%d r:%d '%s'\n",
            g_state.addr_redirector.updated,
            g_state.addr_redirector.valid,
            g_state.addr_redirector.resolved,
            g_state.addr_redirector.resource);
    append_snprintf(&s, &ss, "manager: u:%d v:%d r:%d '%s'\n",
            g_state.addr_manager.updated,
            g_state.addr_manager.valid,
            g_state.addr_manager.resolved,
            g_state.addr_manager.resource);
    int fd;
    mkdir(CM2_STATE_DIR, 0755);
    fd = open(CM2_STATE_TMP, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (!fd) return;
    len = strlen(str);
    ret = write(fd, str, len);
    close(fd);
    if (ret == len) {
        rename(CM2_STATE_TMP, CM2_STATE_FILE);
    }
}

static void cm2_compute_backoff(void)
{
    unsigned int backoff = g_state.max_backoff;

    if (g_state.fast_backoff) {
        backoff = CONFIG_CM2_OVS_SHORT_BACKOFF;
        goto set_backoff;
    }

    // Get a random value from /dev/urandom
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1) {
        LOGE("Error opening /dev/urandom");
        goto set_backoff;
    }
    int ret = read(fd, &backoff, sizeof(backoff));
    if (ret <= 0) {
        LOGE("Error reading /dev/urandom");
        goto set_backoff;
    }
    backoff = (backoff % (g_state.max_backoff - g_state.min_backoff)) +
        g_state.min_backoff;

  set_backoff:
    cm2_state_info[CM2_STATE_QUIESCE_OVS].timeout = backoff;
}

static void cm2_extender_init_state(void) {
    struct schema_Connection_Manager_Uplink con;
    memset(&g_state.link.ip, 0, sizeof(g_state.link.ip));
    memset(&g_state.link, 0, sizeof(g_state.link));

    if (cm2_connection_get_used_link(&con)) {
        LOGD("%s link %s is  already marked as used", __func__, con.if_name);
        g_state.link.is_used = true;
        STRSCPY(g_state.link.if_name, con.if_name);
        STRSCPY(g_state.link.if_type, con.if_type);
        g_state.link.is_used = true;
        g_state.link.priority = con.priority;
    } else {
        g_state.link.priority = -1;
    }
    g_state.fast_backoff = true;
    g_state.dev_type = CM2_DEVICE_NONE;
}

static void cm2_trigger_restart_managers(void) {
    bool skip_restart;
    bool r;

    skip_restart = false;

    if (cm2_is_wifi_type(g_state.link.if_type))
        goto restart;

    r = cm2_vtag_stability_check();
    if (!r) {
        LOGI("Vtag pending, skip restart managers");
        skip_restart = true;
        goto restart;
    }

    if (cm2_is_config_via_ble_enabled() &&
        g_state.dev_type == CM2_DEVICE_NONE) {
        LOGI("Enable two way mode communication, skip restart managers");
        cm2_ovsdb_ble_set_connectable(true);
        skip_restart = true;
    }

    if (g_state.dev_type == CM2_DEVICE_ROUTER) {
        LOGI("Detected device in Router mode, skip restart managers");
        skip_restart = true;
        goto restart;
    }

    if (cm2_ovsdb_is_gw_offline_enabled()) {
        if (cm2_ovsdb_is_gw_offline_active()) {
            LOGI("GW offline in active state, skip restart managers");
            goto restart;
        }

        LOGI("Waiting for applying GW offline functionality [%d,%d]",
             g_state.cnts.gw_offline, CM2_GW_OFFLINE_RETRY_THRESHOLD);

        skip_restart = true;

        if (g_state.cnts.gw_offline < CM2_GW_OFFLINE_RETRY_THRESHOLD)
            g_state.cnts.gw_offline++;

        if (g_state.cnts.gw_offline != CM2_GW_OFFLINE_RETRY_THRESHOLD)
            goto restart;

        if (cm2_ovsdb_is_gw_offline_ready()) {
            r = cm2_ovsdb_enable_gw_offline_conf();
            if (!r) {
                LOGW("Enabling GW offline configuration failed");
                g_state.cnts.gw_offline--;
            } else {
                LOGI("GW offline configuration enabled");
            }
        } else {
            LOGW("GW offline configuration not ready, restart managers");
            skip_restart = false;
        }
        goto restart;
    }

restart:
    cm2_ovsdb_dump_debug_data();
    if (skip_restart &&
        g_state.cnts.skip_restart++ < CM2_GW_SKIP_RESTART_THRESHOLD) {
        LOGI("Device type: %d, Skip restart managers [%d/%d]",
             g_state.dev_type, g_state.cnts.skip_restart,
             CM2_GW_SKIP_RESTART_THRESHOLD);
        cm2_reset_time();
        return;
    }

    WARN_ON(!target_device_wdt_ping());
    LOGW("Trigger restart managers");
    WARN_ON(target_device_restart_managers());
}

static void cm2_restart_ovs_connection(bool state) {
    if (cm2_is_extender())
    {
        if (g_state.cnts.ovs_resolve_fail < CM2_RESOLVE_FATAL_THRESHOLD &&
            g_state.cnts.ovs_con < CM2_CONNECT_FATAL_THRESHOLD)
            cm2_set_state(state, CM2_STATE_LINK_SEL);
        else
            cm2_trigger_restart_managers();
    }
    else
        cm2_set_state(state, CM2_STATE_OVS_INIT);
}

static bool cm2_set_new_vtag(void) {
    if (g_state.link.vtag.state == CM2_VTAG_BLOCKED &&
        g_state.link.vtag.tag == g_state.link.vtag.blocked_tag) {
        LOGI("vtag: Skipping set new vtag [%d] due to connectivity problem",
              g_state.link.vtag.tag);
        return false;
    }

    if (cm2_is_eth_type(g_state.link.if_type))
    {
        if (!cm2_ovsdb_update_Port_trunks(g_state.link.if_name, &g_state.link.vtag.tag, 1))
        {
            LOGW("vtag: Failed to set new trunk = %d on %s",
                g_state.link.vtag.tag, g_state.link.if_name);
            return false;
        }
    }

    if (!cm2_ovsdb_update_Port_tag(cm2_get_uplink_name(), g_state.link.vtag.tag, true)) {
        LOGW("vtag: Failed to set new vtag = %d on %s",
             g_state.link.vtag.tag, cm2_get_uplink_name());
        return false;
    }
    g_state.link.vtag.state = CM2_VTAG_PENDING;
    g_state.link.vtag.failure = 0;
    return true;
}

static bool cm2_block_vtag(void) {
    g_state.link.vtag.state = CM2_VTAG_BLOCKED;
    g_state.link.vtag.failure = 0;
    g_state.link.vtag.blocked_tag = g_state.link.vtag.tag;
    if (!cm2_ovsdb_update_Port_tag(cm2_get_uplink_name(), g_state.link.vtag.tag, false)) {
        LOGW("vtag: Failed to remove vtag = %d on %s",
             g_state.link.vtag.tag, cm2_get_uplink_name());
        return false;
    }
    return true;
}

static void cm2_disable_gw_offline_state(void)
{
    if (g_state.cnts.gw_offline != CM2_GW_OFFLINE_RETRY_THRESHOLD ||
        !cm2_ovsdb_is_gw_offline_active())
        return;

    if (cm2_ovsdb_disable_gw_offline_conf()) {
        g_state.cnts.gw_offline = 0;
        LOGI("GW offline configuration disabled");
    } else {
        LOGW("Disabling GW offline configuration failed");
    }
}

void cm2_update_state(cm2_reason_e reason)
{
    int  ret;
    char *uplink;

start:
    cm2_log_state(reason);
    cm2_state_e old_state = g_state.state;

    // check for AWLAN_Node and Manager tables
    if (!g_state.have_awlan) return;
    if (!g_state.have_manager) return;

    // EXTENDER ---
    bool link_sel = g_state.state == CM2_STATE_INIT ||
                    g_state.state == CM2_STATE_LINK_SEL ||
                    g_state.state == CM2_STATE_WAN_IP ||
                    g_state.state == CM2_STATE_NTP_CHECK;

    uplink = cm2_get_uplink_name();

    switch(reason)
    {
        case CM2_REASON_LINK_NOT_USED:
            cm2_set_state(true, CM2_STATE_LINK_SEL);
            break;
        case CM2_REASON_LINK_USED:
            ret = cm2_get_link_ip(uplink, &g_state.link.ip);
            if (ret < 0)
                LOGW("%s: Failed get ip info", uplink);
            cm2_set_backhaul_update_ble_state();

            if (g_state.link.is_bridge) {
                cm2_update_bridge_cfg(g_state.link.bridge_name, g_state.link.if_name, true,
                                      CM2_PAR_FALSE, true);
            } else {
                cm2_ovsdb_set_default_wan_bridge( g_state.link.if_name,  g_state.link.if_type);
            }

            cm2_ovsdb_connection_clean_link_counters(g_state.link.if_name);
            cm2_connection_req_stability_check(LINK_CHECK, false);
            cm2_set_state(true, CM2_STATE_WAN_IP);
            break;
        case CM2_REASON_SET_NEW_VTAG:
            LOGI("vtag: %d: creating", g_state.link.vtag.tag);
            if (cm2_set_new_vtag()) {
                cm2_ovsdb_refresh_dhcp(uplink);
                cm2_set_state(true, CM2_STATE_WAN_IP);
            }
            break;
        case CM2_REASON_BLOCK_VTAG:
            LOGI("vtag: %d: blocking", g_state.link.vtag.tag);
            if (cm2_block_vtag()) {
                cm2_ovsdb_refresh_dhcp(uplink);
                cm2_set_state(true, CM2_STATE_WAN_IP);
            }
            break;
        case CM2_REASON_OVS_INIT:
            if (g_state.state != CM2_STATE_WAN_IP &&
                !link_sel) {
                LOGI("set async OVS INIT state");
                cm2_ovsdb_set_Manager_target("");
                g_state.connected = false;
                g_state.fast_reconnect = false;
                cm2_set_state(true, CM2_STATE_WAN_IP);
            }
            break;
        default:
            break;
    }
    // --- EXTENDER

    // received new redirector address?
    if ( (g_state.state != CM2_STATE_OVS_INIT &&
          g_state.state != CM2_STATE_TRY_RESOLVE &&
          !link_sel)
        && g_state.addr_redirector.updated
        && g_state.addr_redirector.valid)
    {
        LOGI("Received new redirector address");

        cm2_set_state(true, CM2_STATE_OVS_INIT);
        g_state.addr_redirector.updated = false;
    }
    // received new manager address?
    else if ( !cm2_is_connected_to(CM2_DEST_MANAGER)
            && g_state.addr_manager.updated
            && g_state.addr_manager.valid)
    {
        LOGI("Received manager address");

        cm2_set_state(true, CM2_STATE_TRY_RESOLVE);
        g_state.dest = CM2_DEST_MANAGER;
    }

    switch (g_state.state)
    {
        default:
        case CM2_STATE_INIT:
            if (!cm2_is_extender()) {
                LOGD("%s Skip onboarding process target_type = %d",
                     __func__, g_state.target_type);
                cm2_set_state(true, CM2_STATE_OVS_INIT);
            } else {
                cm2_extender_init_state();
                g_state.resolve_retry = false;
                g_state.cnts.ovs_resolve_fail = 0;
                cm2_set_state(true, CM2_STATE_LINK_SEL);
            }
            break;

        case CM2_STATE_LINK_SEL: // EXTENDER only
            if (cm2_state_changed()) // first iteration
            {
                LOGI("Waiting for finish link selection");
                g_state.run_stability = false;
                g_state.fast_reconnect = g_state.connected ? true : false;
                if (g_state.fast_reconnect)
                    g_state.fast_backoff = true;

                cm2_ovsdb_set_Manager_target("");
                g_state.connected = false;
                g_state.ble_status = 0;
                cm2_ovsdb_connection_update_ble_phy_link();
            }
            if (g_state.link.is_used)
            {
                cm2_connection_req_stability_check(LINK_CHECK, false);
                cm2_set_backhaul_update_ble_state();
                cm2_set_state(true, CM2_STATE_WAN_IP);
            }
            else if (cm2_timeout(false))
            {
                cm2_trigger_restart_managers();
            }
            break;

        case CM2_STATE_WAN_IP: // EXTENDER only
            if (cm2_state_changed()) // first iteration
            {
                LOGI("Waiting for finish get WLAN IP");
            }
            WARN_ON(cm2_get_link_ip(uplink, &g_state.link.ip) < 0);
            ret = 0;
            if (g_state.link.ip.is_ipv4 || g_state.link.ip.is_ipv6) {
                ret = cm2_connection_req_stability_check(ROUTER_CHECK, false);
                cm2_set_ble_state(ret, BLE_ONBOARDING_STATUS_ROUTER_OK);
            }

            if (ret) {
                cm2_set_state(true, CM2_STATE_NTP_CHECK);
            }
            else if (cm2_timeout(false))
            {
                cm2_trigger_restart_managers();
            } else {
                cm2_ovsdb_set_Manager_target("");
            }
            break;

        case CM2_STATE_NTP_CHECK: // EXTENDER only
            if (cm2_state_changed()) // first iteration
            {
                LOGI("Waiting for finish NTP");
            }
            if (cm2_connection_req_stability_check(INTERNET_CHECK | NTP_CHECK, true))
            {
                cm2_state_e n_state;

                cm2_set_ble_state(true, BLE_ONBOARDING_STATUS_INTERNET_OK);

                n_state = g_state.fast_reconnect ? CM2_STATE_FAST_RECONNECT : CM2_STATE_OVS_INIT;
                cm2_set_state(true, n_state);
            }
            else if (cm2_timeout(false))
            {
                cm2_trigger_restart_managers();
            }
            break;

        case CM2_STATE_OVS_INIT:
            if (cm2_is_extender() && !g_state.link.is_used) {
                LOGN("Main link is not used, move to link selection");
                cm2_set_state(false, CM2_STATE_LINK_SEL);
                break;
            }

             /* Workaround for CAES-599 */
            cm2_ovsdb_remove_unused_gre_interfaces();
            g_state.connected = false;
            g_state.is_con_stable = false;

            if (g_state.addr_redirector.valid)
            {
                // have redirector address
                // clear manager_addr
                cm2_clear_manager_addr();
                // try to resolve redirector
                g_state.dest = CM2_DEST_REDIR;
                cm2_set_state(true, CM2_STATE_TRY_RESOLVE);
                g_state.disconnects = 0;
            }
            break;

        case CM2_STATE_TRY_RESOLVE:
            if (cm2_state_changed() || g_state.resolve_retry)
            {
                if (cm2_ovsdb_is_ipv6_global_link(uplink)) {
                    g_state.link.ip.ipv6 = CM2_IPV6_DHCP;
                    g_state.link.ip.is_ipv6 = true;
                }

                if (cm2_is_extender())
                    cm2_connection_req_stability_check(LINK_CHECK | ROUTER_CHECK | INTERNET_CHECK, false);

                if (g_state.resolve_retry) {
                    LOGI("Trigger retry resolving, cnt: %d/%d",
                         g_state.cnts.ovs_resolve, CM2_RESOLVE_RETRY_THRESHOLD);
                    g_state.resolve_retry = false;
                }

                LOGI("Trying to resolve %s: %s", cm2_curr_dest_name(),
                     cm2_curr_addr()->hostname);

                if (!cm2_resolve(g_state.dest)) {
                    g_state.cnts.ovs_resolve_fail++;
                    cm2_restart_ovs_connection(true);
                    return;
                }
            }

            if (cm2_is_addr_resolved(cm2_curr_addr()))
            {
                LOGN("Address %s resolved", cm2_curr_addr()->hostname);
                // successfully resolved
                g_state.cnts.ovs_resolve = 0;
                g_state.cnts.ovs_resolve_fail = 0;
                cm2_set_state(true, CM2_STATE_RE_CONNECT);
            }
            else
            {
                if (cm2_timeout(false) ||
                    g_state.cnts.ovs_resolve > CM2_RESOLVE_RETRY_THRESHOLD) {
                    LOGI("Restart channel due to exceeded resolve threshold [%d/%d] or timeout",
                         g_state.cnts.ovs_resolve, CM2_RESOLVE_RETRY_THRESHOLD);
                    g_state.cnts.ovs_resolve = 0;
                    cm2_resolve_timeout();
                    cm2_ovsdb_refresh_dhcp(uplink);
                    g_state.cnts.ovs_resolve_fail++;
                    cm2_restart_ovs_connection(false);
                    return;
                }
            }
            break;

        case CM2_STATE_RE_CONNECT:
            // disconnect, wait for disconnect then try to connect
            if (cm2_state_changed()) // first iteration
            {
                // disconnect
                g_state.connected = false;
                cm2_ovsdb_set_Manager_target("");
            }
            if (!g_state.connected)
            {
                // disconnected, go to try_connect
                cm2_set_state(true, CM2_STATE_TRY_CONNECT);
            }
            else if (cm2_timeout(false))
            {
                // stuck? back to init
                g_state.cnts.ovs_con++;
                cm2_restart_ovs_connection(false);
                return;
            }
            break;

        case CM2_STATE_TRY_CONNECT:
            if (cm2_is_extender())
            {
                /* Workaround for CAES-599, double check */
                cm2_ovsdb_remove_unused_gre_interfaces();
                cm2_ovsdb_connection_update_unreachable_cloud_counter(g_state.link.if_name, -1);
            }

            if (cm2_curr_addr()->updated)
            {
                // address changed while trying to connect, not normally expected
                // unless manually changed during development/debugging but
                // handle anyway: go back to resolve new address
                cm2_set_state(true, CM2_STATE_TRY_RESOLVE);
                break;
            }
            if (cm2_state_changed()) // first iteration
            {
                if (!cm2_write_current_target_addr())
                {
                    g_state.cnts.ovs_con++;
                    cm2_restart_ovs_connection(false);
                    return;
                }
            }
            if (g_state.connected)
            {
                // connected
                cm2_set_state(true, CM2_STATE_CONNECTED);
            }
            else if (cm2_timeout(false))
            {
                // timeout - write next address
                if (cm2_write_next_target_addr())
                {
                    cm2_reset_time();
                }
                else
                {
                    // no more addresses
                    g_state.cnts.ovs_con++;
                    cm2_restart_ovs_connection(false);
                    return;
                }
            }
            break;

        case CM2_STATE_FAST_RECONNECT:
            if (cm2_state_changed()) // first iteration
            {
                g_state.connected = false;
                cm2_ovsdb_set_Manager_target("");
                cm2_write_current_target_addr();
            }

            if (g_state.connected)
                cm2_set_state(true, CM2_STATE_CONNECTED);

            if (cm2_timeout(false))
                cm2_set_state(true, CM2_STATE_QUIESCE_OVS);

           break;
        case CM2_STATE_CONNECTED:
            if (cm2_state_changed()) // first iteration
            {
                LOG(NOTICE, "===== Connected to: %s", cm2_curr_dest_name());
                if (cm2_is_extender()) {
                    cm2_connection_req_stability_check(LINK_CHECK | ROUTER_CHECK | INTERNET_CHECK, true);
                    cm2_set_ble_state(true, BLE_ONBOARDING_STATUS_CLOUD_OK);
                    cm2_update_device_type(g_state.link.if_type);
                    g_state.cnts.skip_restart = 0;
                    g_state.cnts.ovs_con = 0;
                    g_state.run_stability = true;
                    cm2_ovsdb_connection_update_unreachable_cloud_counter(g_state.link.if_name, 0);
                    cm2_disable_gw_offline_state();
                }

            }

            if (g_state.connected) {
                if (!g_state.is_con_stable && cm2_get_time() > CM2_STABLE_PERIOD) {
                    LOGI("Connection stable by %d sec, disconnects: %d",
                         CM2_STABLE_PERIOD, g_state.disconnects);

                    g_state.is_con_stable = true;
                    g_state.disconnects = 0;
                    g_state.fast_backoff = false;

                    if (cm2_is_config_via_ble_enabled())
                        cm2_ovsdb_ble_set_connectable(false);

                    if (g_state.link.vtag.state == CM2_VTAG_PENDING) {
                        LOGI("vtag: %d: set as used", g_state.link.vtag.tag);
                        g_state.link.vtag.state = CM2_VTAG_USED;
                    }
                }
            } else {
                cm2_set_state(true, CM2_STATE_QUIESCE_OVS);
                g_state.is_con_stable = false;
            }
            break;

        case CM2_STATE_QUIESCE_OVS:
            /* Workaround for CAES-599, double check */
            cm2_ovsdb_remove_unused_gre_interfaces();

            if (cm2_state_changed())
            {
                // quiesce ovsdb-server, wait for timeout
                cm2_ovsdb_set_Manager_target("");
                g_state.disconnects += 1;
                cm2_set_ble_state(false, BLE_ONBOARDING_STATUS_CLOUD_OK);

                if (cm2_is_extender())
                    cm2_ovsdb_connection_update_unreachable_cloud_counter(g_state.link.if_name,
                                                                          g_state.disconnects);
                // Update timeouts based on AWLAN_Node contents
                cm2_compute_backoff();
                LOG(NOTICE, "===== Quiescing connection to: %s for %d seconds",
                cm2_curr_dest_name(), cm2_get_timeout());
            }

            if (g_state.connected)
            {
                // connected
                cm2_set_state(true, CM2_STATE_CONNECTED);
            }

            if (cm2_timeout(true))
            {
                g_state.disconnects += 1;
                if (cm2_is_extender())
                    cm2_ovsdb_connection_update_unreachable_cloud_counter(g_state.link.if_name,
                                                                          g_state.disconnects);

                if (g_state.disconnects > CM2_MAX_DISCONNECTS)
                {
                    // too many unsuccessful connect attempts, go back to redirector
                    LOGE("Too many disconnects (%d/%d) back to redirector",
                            g_state.disconnects, CM2_MAX_DISCONNECTS);
                    g_state.fast_backoff = false;
                    cm2_restart_ovs_connection(false);
                    return;
                } else {
                    // Try again connecting to the current controller
                    cm2_set_state(true, CM2_STATE_FAST_RECONNECT);
                }
            }
            break;
    }

    if (cm2_timeout(false))
    {
        // unexpected, just in case
        LOG(ERROR, "Unhandled timeout");
        cm2_ovsdb_dump_debug_data();
        if (cm2_is_extender())
            cm2_trigger_restart_managers();
        cm2_set_state(false, CM2_STATE_INIT);
    }

    if (old_state != g_state.state)
    {
        reason = CM2_REASON_CHANGE;
        goto start;
    }
    LOG(TRACE, "<== Update s: %s", cm2_curr_state_name());
}

void cm2_trigger_update(cm2_reason_e reason)
{
    (void)reason;
    LOG(TRACE, "Trigger s: %s r: %s", cm2_curr_state_name(), cm2_reason_name[reason]);
    g_state.reason = reason;
    ev_timer_stop(g_state.loop, &g_state.timer);
    ev_timer_set(&g_state.timer, 0.1, CM2_EVENT_INTERVAL);
    ev_timer_start(g_state.loop, &g_state.timer);
}

void cm2_event_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;
    cm2_reason_e reason = g_state.reason;
    // set reason for next iteration, unless triggered by something else
    g_state.reason = CM2_REASON_TIMER;
    cm2_update_state(reason);
}

void cm2_event_init(struct ev_loop *loop)
{
    LOGI("Initializing CM event");

    g_state.reason = CM2_REASON_TIMER;
    g_state.loop = loop;
    ev_timer_init(&g_state.timer, cm2_event_cb, CM2_EVENT_INTERVAL, CM2_EVENT_INTERVAL);
    g_state.timer.data = NULL;
    ev_timer_start(g_state.loop, &g_state.timer);
}

void cm2_event_close(struct ev_loop *loop)
{
    LOGI("Stopping CM event");
    (void)loop;
}
