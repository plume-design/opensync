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

#include <stdbool.h>

#include "log.h"
#include "schema.h"
#include "cm2.h"
#include "kconfig.h"
#include "cm2_stability.h"

bool cm2_vtag_stability_check(void) {
    cm2_vtag_t *vtag = &g_state.link.vtag;

    if (vtag->state == CM2_VTAG_PENDING) {
        vtag->failure++;
        LOGI("vtag: %d connectivity failed: %d out of %d retries",
             vtag->tag, vtag->failure, CONFIG_CM2_STABILITY_THRESH_VTAG);

        if (vtag->failure > CONFIG_CM2_STABILITY_THRESH_VTAG) {
            LOGN("vtag: %d trigger rollback", vtag->tag);
            cm2_update_state(CM2_REASON_BLOCK_VTAG);
        }
        return false;
    }
    return true;
}

static bool cm2_cpu_is_low_loadavg(void) {
    char   line[128];
    bool   retval;
    char   *s_val;
    float  val;
    FILE   *f1;

    f1 = NULL;
    retval = false;

    f1 = popen("cat /proc/loadavg", "r");
    if (!f1) {
        LOGE("Failed to retrieve loadavg command");
        return retval;
    }

    if (fgets(line, sizeof(line), f1) == NULL) {
        LOGW("No loadavg found");
        goto error;
    }

    while (line[strlen(line) - 1] == '\r' || line[strlen(line) - 1] == '\n') {
        line[strlen(line) - 1] = '\0';

        s_val=strtok(line," ");
        val = atof(s_val);

        if (val > atof(CONFIG_CM2_STABILITY_THRESH_CPU)) {
            LOGI("Skip stability check due to high CPU usage, load avg: %f", val);
            break;
        }
        retval = true;
    }

    error:
        pclose(f1);

    return retval;
}

#ifdef CONFIG_CM2_STABILITY_USE_RESTORE_SWITCH_CFG
void cm2_restore_switch_cfg_params(int counter, int thresh, cm2_restore_con_t *ropt)
{
    *ropt |= 1 << CM2_RESTORE_SWITCH_FIX_PORT_MAP;

    if (counter % thresh == 0)
        *ropt |=  (1 << CM2_RESTORE_SWITCH_DUMP_DATA) | (1 << CM2_RESTORE_SWITCH_FIX_AUTON);
}

void cm2_restore_switch_cfg(cm2_restore_con_t opt)
{
    char command[128];
    int  ret;

    if (opt & (1 << CM2_RESTORE_SWITCH_DUMP_DATA)) {
        LOGI("Switch: Dump debug data");
        sprintf(command, "sh "CONFIG_INSTALL_PREFIX"/scripts/kick-ethernet.sh 2 ");
        LOGD("%s: Command: %s", __func__, command);
        ret = target_device_execute(command);
        if (!ret)
            LOGW("kick-ethernet.sh: Dump data failed");
    }

    if (opt & (1 << CM2_RESTORE_SWITCH_FIX_PORT_MAP)) {
       LOGI("Switch: Trigger fixing port map");
       sprintf(command, "sh "CONFIG_INSTALL_PREFIX"/scripts/kick-ethernet.sh 3 %s",
              g_state.link.gateway_hwaddr);
       LOGD("%s: Command: %s", __func__, command);
       ret = target_device_execute(command);
       if (!ret)
          LOGW("kick-ethernet.sh: Fixing port map failed");
    }

    if (opt & (1 << CM2_RESTORE_SWITCH_FIX_AUTON)) {
        LOGI("Switch: Trigger autoneg restart");
        sprintf(command, "sh "CONFIG_INSTALL_PREFIX"/scripts/kick-ethernet.sh 4 ");
        LOGD("%s: Command: %s", __func__, command);
        ret = target_device_execute(command);
        if (!ret)
            LOGW("kick-ethernet.sh: autoneg restart failed");
    }
}
#endif /* CONFIG_CM2_STABILITY_USE_RESTORE_SWITCH_CFG */

static void cm2_restore_connection(cm2_restore_con_t opt)
{
    if (opt == 0)
        return;

    if (!cm2_is_eth_type(g_state.link.if_type))
        return;

    if (opt & (1 << CM2_RESTORE_IP)) {
        cm2_ovsdb_refresh_dhcp(cm2_get_uplink_name());
    }
    else if (opt & (1 << CM2_RESTORE_MAIN_LINK)) {
        WARN_ON(!cm2_ovsdb_set_Wifi_Inet_Config_interface_enabled(false, cm2_get_uplink_name()));
        WARN_ON(!cm2_ovsdb_set_Wifi_Inet_Config_interface_enabled(true, cm2_get_uplink_name()));
    }
    else {
        cm2_restore_switch_cfg(opt);
    }
}

static void cm2_stability_handle_fatal_state(int counter)
{
    if (counter <= 0)
        return; 

    if (cm2_is_config_via_ble_enabled() &&
        g_state.dev_type == CM2_DEVICE_NONE &&
        (!g_state.link.is_used || cm2_is_eth_type(g_state.link.if_type))) {
        LOGI("Stability: Enabling two way mode comunication");
        cm2_ovsdb_ble_set_connectable(true);
        return;
    }

    if (cm2_vtag_stability_check() &&
        g_state.dev_type != CM2_DEVICE_ROUTER &&
        g_state.cnts.gw_offline == 0 &&
        counter + 1 > CONFIG_CM2_STABILITY_THRESH_FATAL) {
        LOGW("Restart managers due to exceeding the threshold for fatal failures");
        cm2_ovsdb_dump_debug_data();
        cm2_tcpdump_stop(g_state.link.if_name);
        WARN_ON(!target_device_wdt_ping());
        target_device_restart_managers();
    }
}

bool cm2_connection_req_stability_check(target_connectivity_check_option_t opts, bool db_update)
{
    struct schema_Connection_Manager_Uplink con;
    target_connectivity_check_t             cstate;
    cm2_restore_con_t                       ropt;
    const char                              *bridge;
    bool                                    status;
    bool                                    ret;
    int                                     counter;

    if (!cm2_is_extender()) {
        return true;
    }

    //TODO for all active links
    const char *if_name = g_state.link.if_name;

    ropt = 0;

    if (!g_state.link.is_used) {
        LOGN("Waiting for new active link");
        g_state.ble_status = 0;
        cm2_ovsdb_connection_update_ble_phy_link();
        return false;
    }

    ret = cm2_ovsdb_connection_get_connection_by_ifname(if_name, &con);
    if (!ret) {
        LOGW("%s interface does not exist", __func__);
        return false;
    }

    /* Ping WDT before run connectivity check */
    WARN_ON(!target_device_wdt_ping());

    status = target_device_connectivity_check(if_name, &cstate, opts);
    bridge = con.bridge_exists ? con.bridge : "none";
    LOGN("Connection status %d, main link: %s bridge: %s opts: = %x",
         status, if_name, bridge, opts);
    LOGD("%s: Stability counters: [%d, %d, %d, %d]",if_name,
         con.unreachable_link_counter, con.unreachable_router_counter,
         con.unreachable_internet_counter, con.unreachable_cloud_counter);
    LOGD("%s: Stability states: [%d, %d, %d]", if_name,
         cstate.link_state, cstate.router_state, cstate.internet_state);

    if (!db_update)
        return status;

    if (g_state.link.is_bridge &&
        !strcmp(g_state.link.bridge_name, con.bridge) &&
        con.bridge_exists &&
        !cm2_ovsdb_validate_bridge_port_conf(con.bridge, g_state.link.if_name))
    {
        LOGW("Detected abnormal situation, main link %s con.bridge = %s", if_name, con.bridge);

        counter = con.unreachable_link_counter < 0 ? 1 : con.unreachable_link_counter + 1;
        if (counter > 0) {
            LOGI("Detected broken link. Counter = %d", counter);

            if (counter == CONFIG_CM2_STABILITY_THRESH_LINK) {
                ret = cm2_ovsdb_set_Wifi_Inet_Config_network_state(false, g_state.link.if_name);
                if (!ret)
                    LOGW("Force disable main uplink interface failed");
                else
                    g_state.link.restart_pending = true;
                ret = cm2_ovsdb_set_Wifi_Inet_Config_network_state(true, g_state.link.if_name);
                if (counter + 1 > CONFIG_CM2_STABILITY_THRESH_FATAL) {
                    cm2_stability_handle_fatal_state(counter);
                    counter = 0;
                }
            }

            ret = cm2_ovsdb_connection_update_unreachable_link_counter(if_name, counter);
            if (!ret)
                LOGW("%s Failed update link counter in ovsdb table", __func__);
            return false;
        }
    }

    if (opts & LINK_CHECK) {
        counter = 0;
        if (!cstate.link_state) {
            counter = con.unreachable_link_counter < 0 ? 1 : con.unreachable_link_counter + 1;
            LOGI("Detected broken link. Counter = %d", counter);
        }
        ret = cm2_ovsdb_connection_update_unreachable_link_counter(if_name, counter);
        if (!ret)
            LOGW("%s Failed update link counter in ovsdb table", __func__);
    }
    if (opts & ROUTER_CHECK) {
        counter = 0;
        if (!cstate.router_state) {
            counter =  con.unreachable_router_counter < 0 ? 1 : con.unreachable_router_counter + 1;
            LOGI("Detected broken Router. Counter = %d", counter);
            cm2_restore_switch_cfg_params(counter, CONFIG_CM2_STABILITY_THRESH_ROUTER + 2, &ropt);
            if (counter % CONFIG_CM2_STABILITY_THRESH_ROUTER == 0)
                ropt |= (1 << CM2_RESTORE_IP);
            if (counter % CONFIG_CM2_STABILITY_THRESH_ROUTER + 1 == 0)
                ropt |= (1 << CM2_RESTORE_MAIN_LINK);
        }
        else if (kconfig_enabled(CONFIG_CM2_USE_TCPDUMP) &&
                 con.unreachable_router_counter >= CONFIG_CM2_STABILITY_THRESH_TCPDUMP) {
                    cm2_tcpdump_stop(g_state.link.if_name);
        }

        ret = cm2_ovsdb_connection_update_unreachable_router_counter(if_name, counter);
        if (!ret)
            LOGW("%s Failed update router counter in ovsdb table", __func__);

        if (kconfig_enabled(CONFIG_CM2_USE_TCPDUMP) &&
            counter == CONFIG_CM2_STABILITY_THRESH_TCPDUMP &&
            cm2_is_eth_type(g_state.link.if_type)) {
                cm2_tcpdump_start(g_state.link.if_name);
        }

        if (con.unreachable_router_counter + 1 == CONFIG_CM2_STABILITY_THRESH_FATAL)
            cm2_tcpdump_stop(g_state.link.if_name);

        cm2_stability_handle_fatal_state(con.unreachable_router_counter);
    }
    if (opts & INTERNET_CHECK) {
        counter = 0;
        if (!cstate.internet_state) {
            counter = con.unreachable_internet_counter < 0 ? 1 : con.unreachable_internet_counter + 1;
            LOGI("Detected broken Internet. Counter = %d", counter);
            cm2_restore_switch_cfg_params(counter, CONFIG_CM2_STABILITY_THRESH_INTERNET + 2, &ropt);
            if (counter % CONFIG_CM2_STABILITY_THRESH_INTERNET == 0)
                ropt |= (1 << CM2_RESTORE_IP);
            if (counter % CONFIG_CM2_STABILITY_THRESH_ROUTER + 1 == 0)
                ropt |= (1 << CM2_RESTORE_MAIN_LINK);
        }

        ret = cm2_ovsdb_connection_update_unreachable_internet_counter(if_name, counter);
        if (!ret)
            LOGW("%s Failed update internet counter in ovsdb table", __func__);
    }
    if (opts & NTP_CHECK) {
        ret = cm2_ovsdb_connection_update_ntp_state(if_name,
                                                    cstate.ntp_state);
        if (!ret)
            LOGW("%s Failed update ntp state in ovsdb table", __func__);
        else
            g_state.ntp_check = cstate.ntp_state;
    }
    cm2_restore_connection(ropt);
    return status;
}

static void cm2_connection_stability_check(void)
{
    target_connectivity_check_option_t opts;

    if (g_state.connected && !cm2_cpu_is_low_loadavg())
        return;

    opts = LINK_CHECK | ROUTER_CHECK | NTP_CHECK;
    if (!g_state.connected)
        opts |= INTERNET_CHECK;

    cm2_connection_req_stability_check(opts, true);
}

void cm2_stability_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;

    if (g_state.run_stability)
        cm2_connection_stability_check();
}

void cm2_stability_init(struct ev_loop *loop)
{
    LOGD("Initializing stability connection check");
    ev_timer_init(&g_state.stability_timer,
                  cm2_stability_cb,
                  CONFIG_CM2_STABILITY_INTERVAL,
                  CONFIG_CM2_STABILITY_INTERVAL);
    g_state.stability_timer.data = NULL;
    ev_timer_start(g_state.loop, &g_state.stability_timer);
}

void cm2_stability_close(struct ev_loop *loop)
{
    LOGD("Stopping stability check");
    ev_timer_stop (loop, &g_state.stability_timer);
}

#ifdef CONFIG_CM2_USE_WDT
void cm2_wdt_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;

    WARN_ON(!target_device_wdt_ping());
}

void cm2_wdt_init(struct ev_loop *loop)
{
    LOGD("Initializing WDT connection");

    ev_timer_init(&g_state.wdt_timer, cm2_wdt_cb,
                  CONFIG_CM2_WDT_INTERVAL,
                  CONFIG_CM2_WDT_INTERVAL);
    g_state.wdt_timer.data = NULL;
    ev_timer_start(g_state.loop, &g_state.wdt_timer);
}

void cm2_wdt_close(struct ev_loop *loop)
{
    LOGD("Stopping WDT");
    (void)loop;
}
#endif /* CONFIG_CM2_USE_WDT */
