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
#include <errno.h>

#include "log.h"
#include "schema.h"
#include "cm2.h"
#include "kconfig.h"
#include "cm2_stability.h"


#define BLOCKING_LINK_THRESHOLD     5
#define UNBLOCKING_LINK_THRESHOLD 100

/* ev_child must be the first element of the structure
 * based on that fact we can store additional data, in that case
 * interface name and interface type */
typedef struct {
    ev_child                           cw;
    target_connectivity_check_option_t opts;
    bool                               db_update;
    bool                               repeat;
    int                                i_fail;
    int                                i_router;
} astab_check_t;

static int  ipv4_i_fail = 0;
static int  ipv6_i_fail = 0;
static int  ipv4_r_fail = 0;
static int  ipv6_r_fail = 0;

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
static bool cm2_util_skip_restore_switch_fix_auton()
{
    if (g_state.dev_type == CM2_DEVICE_BRIDGE &&
        cm2_ovsdb_is_gw_offline_enabled() &&
        (cm2_ovsdb_is_gw_offline_active() || cm2_ovsdb_is_gw_offline_ready())) {
        LOGI("GW offline skip restore fix auton");
        return true;
    }
    return false;
}

void cm2_restore_switch_cfg_params(int counter, int thresh, cm2_restore_con_t *ropt)
{
    *ropt |= 1 << CM2_RESTORE_SWITCH_FIX_PORT_MAP;


    if (counter % thresh == 0) {
        *ropt |=  (1 << CM2_RESTORE_SWITCH_DUMP_DATA);
        if (!cm2_util_skip_restore_switch_fix_auton())
            *ropt |= (1 << CM2_RESTORE_SWITCH_FIX_AUTON);
    }
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
        cm2_restart_iface(cm2_get_uplink_name());
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

    if (cm2_enable_gw_offline())
        return;

    if (cm2_vtag_stability_check() &&
        g_state.dev_type != CM2_DEVICE_ROUTER &&
        counter + 1 > CONFIG_CM2_STABILITY_THRESH_FATAL) {
        LOGW("Restart managers due to exceeding the threshold for fatal failures");
        cm2_ovsdb_dump_debug_data();
        cm2_tcpdump_stop(g_state.link.if_name);
        WARN_ON(!target_device_wdt_ping());
        target_device_restart_managers();
    }
}

target_connectivity_check_option_t
cm2_util_add_ip_opts(target_connectivity_check_option_t opts)
{
    if (!g_state.link.ipv4.blocked && g_state.link.ipv4.is_ip)
        opts |= IPV4_CHECK;

    if (!g_state.link.ipv6.blocked && g_state.link.ipv6.is_ip)
        opts |= IPV6_CHECK;

    return opts;
}

static void
cm2_util_update_connectivity_failures(target_connectivity_check_option_t opts, target_connectivity_check_t *cstate)
{
    if (!g_state.link.ipv4.is_ip) {
        ipv4_r_fail = 0;
        ipv4_i_fail = 0;
        g_state.link.ipv4.blocked = false;
    }

    if (!g_state.link.ipv6.is_ip) {
        ipv6_r_fail = 0;
        ipv6_i_fail = 0;
        g_state.link.ipv6.blocked = false;
    }

    if (g_state.link.ipv4.blocked) {
        if (ipv4_r_fail > 0)
            ipv4_r_fail++;
        if (ipv4_i_fail > 0)
            ipv4_i_fail++;
    }
 
    if (g_state.link.ipv6.blocked) {
        if (ipv6_r_fail > 0)
            ipv6_r_fail++;
        if (ipv6_i_fail > 0)
            ipv6_i_fail++;
    }

    if ((opts & ROUTER_CHECK) && (opts & IPV4_CHECK))
        ipv4_r_fail = cstate->router_ipv4_state ? 0 : ipv4_r_fail + 1;

    if ((opts & ROUTER_CHECK) && (opts & IPV6_CHECK))
        ipv6_r_fail = cstate->router_ipv6_state ? 0 : ipv6_r_fail + 1;

    if ((opts & INTERNET_CHECK) && (opts & IPV4_CHECK))
        ipv4_i_fail = cstate->internet_ipv4_state ? 0 : ipv4_i_fail + 1;

    if ((opts & INTERNET_CHECK) && (opts & IPV6_CHECK))
        ipv6_i_fail = cstate->internet_ipv6_state ? 0 : ipv6_i_fail + 1;

    if (ipv4_r_fail == BLOCKING_LINK_THRESHOLD || ipv4_i_fail == BLOCKING_LINK_THRESHOLD) {
        LOGI("Blocking invalid ipv4 link");
        g_state.link.ipv4.blocked = true;
    }

    if (ipv6_r_fail == BLOCKING_LINK_THRESHOLD || ipv6_i_fail == BLOCKING_LINK_THRESHOLD) {
        LOGI("Blocking invalid ipv6 link");
        g_state.link.ipv6.blocked = true;
    }

    if (g_state.link.ipv4.blocked && g_state.link.ipv6.blocked) {
        LOGI("IPv4 and IPv6 blocked, connectivity issue on both uplinks, unblocking...");
        g_state.link.ipv4.blocked = false;
        g_state.link.ipv6.blocked = false;
    }
    if (ipv4_r_fail == UNBLOCKING_LINK_THRESHOLD || ipv4_i_fail == UNBLOCKING_LINK_THRESHOLD) {
        ipv4_r_fail = 0;
        ipv4_i_fail = 0;
        g_state.link.ipv4.blocked = false;
    }

    if (ipv6_r_fail == UNBLOCKING_LINK_THRESHOLD || ipv6_i_fail == UNBLOCKING_LINK_THRESHOLD) {
        ipv6_r_fail = 0;
        ipv6_i_fail = 0;
        g_state.link.ipv6.blocked = false;
    }
}

bool cm2_connection_req_stability_process(target_connectivity_check_option_t opts,
                                          bool db_update,
                                          bool status,
                                          target_connectivity_check_t *cstate_ptr)
{
    struct schema_Connection_Manager_Uplink con;
    target_connectivity_check_t             cstate;
    cm2_restore_con_t                       ropt;
    const char                              *bridge;
    bool                                    ret;
    int                                     counter;

    if (!cm2_is_extender()) {
        return true;
    }

    //TODO for all active links
    const char *if_name = g_state.link.if_name;

    memcpy(&cstate, cstate_ptr, sizeof(cstate));
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

    bridge = con.bridge_exists ? con.bridge : "none";
    LOGN("Connection status %d, main link: %s bridge: %s opts: = %x",
         status, if_name, bridge, opts);
    LOGI("%s: Stability counters: [%d, %d, %d, %d]",if_name,
         con.unreachable_link_counter, con.unreachable_router_counter,
         con.unreachable_internet_counter, con.unreachable_cloud_counter);
    LOGI("%s: Stability states: [%d, %d, %d]", if_name,
         cstate.link_state, cstate.router_ipv4_state | cstate.router_ipv6_state,
         cstate.internet_ipv4_state | cstate.internet_ipv6_state);

    cm2_util_update_connectivity_failures(opts, &cstate);

    if (opts & NTP_CHECK)
        g_state.ntp_check = cstate.ntp_state;

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
        if (!cstate.router_ipv4_state && !cstate.router_ipv6_state) {
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
        if (!cstate.internet_ipv4_state && !cstate.internet_ipv6_state) {
            counter = con.unreachable_internet_counter < 0 ? 1 : con.unreachable_internet_counter + 1;
            LOGI("Detected broken Internet. Counter = %d", counter);
            cm2_restore_switch_cfg_params(counter, CONFIG_CM2_STABILITY_THRESH_INTERNET + 2, &ropt);
            if (counter % CONFIG_CM2_STABILITY_THRESH_INTERNET == 0)
                ropt |= (1 << CM2_RESTORE_IP);
            if (counter % CONFIG_CM2_STABILITY_THRESH_INTERNET + 1 == 0)
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
    }
    cm2_restore_connection(ropt);
    return status;
}

bool cm2_connection_req_stability_check(target_connectivity_check_option_t opts, bool db_update)
{
    const char *if_name = g_state.link.if_name;
    target_connectivity_check_t cstate = {0};
    bool ok;

    /* Ping WDT before run connectivity check */
    WARN_ON(!target_device_wdt_ping());
    ok = target_device_connectivity_check(if_name, &cstate, opts);

    return cm2_connection_req_stability_process(opts, db_update, ok, &cstate);
}

static
int cm2_util_cstate2mask(bool ok, const target_connectivity_check_t *cstate)
{
    return
        ((ok ? 1 : 0) << 0) |
        (cstate->link_state ? 1 : 0) << 1 |
        (cstate->router_ipv4_state ? 1 : 0) << 2 |
        (cstate->router_ipv6_state ? 1 : 0) << 3 |
        (cstate->internet_ipv4_state ? 1 : 0) << 4 |
        (cstate->internet_ipv6_state ? 1 : 0) << 5 |
        (cstate->internet_state ? 1 : 0) << 6 |
        (cstate->ntp_state ? 1 : 0) << 7;
}

static
bool cm2_util_mask2cstate(int mask, target_connectivity_check_t *cstate)
{
    bool ok = mask & (1 << 0);
    cstate->link_state = mask & (1 << 1);
    cstate->router_ipv4_state = mask & (1 << 2);
    cstate->router_ipv6_state = mask & (1 << 3);
    cstate->internet_ipv4_state = mask & (1 << 4);
    cstate->internet_ipv6_state = mask & (1 << 5);
    cstate->internet_state = mask & (1 << 6);
    cstate->ntp_state = mask & (1 << 7);
    return ok;
}

static
void cm2_util_req_stability_cb(struct ev_loop *loop, ev_child *w, int revents);

static
void cm2_util_req_stability_check_recalc(void)
{
    const char *if_name = g_state.link.if_name;
    bool update = g_state.stability_update_next;
    int opts = g_state.stability_opts_next;
    pid_t pid;

    if (g_state.stability_opts_now) {
        LOGD("stability: ongoing, will check %02x later",
             g_state.stability_opts_next);
        return;
    }

    if (!opts) {
        LOGD("stability: nothing to do anymore");
        return;
    }

    pid = fork();
    if (pid < 0) {
        LOGW("stability: failed to fork() stability checks: %d", errno);
        return;
    }

    if (pid == 0) {
        target_connectivity_check_t cstate = {0};
        bool ok = target_device_connectivity_check(if_name, &cstate, opts);
        int mask = cm2_util_cstate2mask(ok, &cstate);
        exit(mask);
        return; /* never reached */
    }

    g_state.stability_opts_now = opts;
    g_state.stability_opts_next = 0;
    g_state.stability_update_now = update;
    g_state.stability_update_next = 0;

    ev_child_init(&g_state.stability_child, cm2_util_req_stability_cb, pid, 0);
    ev_child_start(EV_DEFAULT_ &g_state.stability_child);

    LOGI("stability: started 0x%02x %supdate pid %d",
         opts, update ? "" : "no", pid);
}

static
void cm2_util_req_stability_cb(EV_P_ ev_child *w, int revents)
{
    target_connectivity_check_t cstate = {0};
    int mask = WIFEXITED(w->rstatus) ? WEXITSTATUS(w->rstatus) : 0xff;
    bool ok = cm2_util_mask2cstate(mask, &cstate);
    bool update = g_state.stability_update_now;
    bool repeat = g_state.stability_repeat;
    int opts = g_state.stability_opts_now;

    LOGI("stability: completed 0x%02x %supdate%s mask=0x%02x (%s) pid %d",
         opts,
         update ? "" : "no",
         repeat ? " recurring" : "",
         mask,
         ok ? "ok" : "fail",
         w->rpid);

    ev_child_stop(EV_A_ w);
    g_state.stability_opts_now = 0;
    g_state.stability_update_now = 0;

    cm2_connection_req_stability_process(opts, update, ok, &cstate);

    if (repeat) {
        if (ok) {
            g_state.stability_repeat = false;
        }
        else {
            if (!g_state.stability_opts_next)
                g_state.stability_opts_next = opts;
        }
    }

    cm2_util_req_stability_check_recalc();
}

void cm2_connection_req_stability_check_async(target_connectivity_check_option_t opts, bool db_update, bool repeat)
{
    opts = cm2_util_add_ip_opts(opts);
    g_state.stability_opts_next = opts;
    g_state.stability_update_next = db_update;
    g_state.stability_repeat = repeat;

    LOGI("stability: scheduling 0x%02x %supdate%s",
         opts,
         db_update ? "" : "no",
         repeat ? " recurring" : "");

    cm2_util_req_stability_check_recalc();
}

static void cm2_connection_stability_check(void)
{
    target_connectivity_check_option_t opts;

    if (g_state.connected && !cm2_cpu_is_low_loadavg())
        return;

    opts = LINK_CHECK | ROUTER_CHECK | NTP_CHECK;
    if (!g_state.connected)
        opts |= INTERNET_CHECK;

    cm2_connection_req_stability_check_async(opts, true, false);
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
