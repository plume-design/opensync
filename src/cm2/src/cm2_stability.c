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
#include "ds_tree.h"
#include "cm2_uplink_event.h"


#include "os_time.h"

#define BLOCKING_LINK_THRESHOLD            2
#define UPLINKS_TIMER_TIMEOUT              120
/* All links checking is triggered when counter
 * achieve UPLINKS_CHECKING_ALL_THRESHOLD value.
 * Interval of counter incremental based on
 * CONFIG_CM2_STABILITY_SHORT_INTERVAL and
 * CONFIG_CM2_STABILITY_INTERVAL values.
*/
#define UPLINKS_CHECKING_ALL_THRESHOLD     60

/* ev_child must be the first element of the structure
 * based on that fact we can store additional data, in that case
 * interface name and interface type */
typedef struct {
    ev_child                           cw;
    ds_tree_node_t                     node;
    target_connectivity_check_option_t opts;
    char                               uname[C_IFNAME_LEN];
    char                               utype[IFTYPE_SIZE];
    char                               clink[C_IFNAME_LEN];
    bool                               db_update;
    bool                               repeat;
    pid_t                              pid;
} async_check_t;

static ds_tree_t g_async_checks = DS_TREE_INIT(ds_str_cmp, async_check_t, node);

static const char* cm2_util_bool2str(bool v)
{
   return v ? "yes" : "no";
}

static void cm2_reset_restart_time(void)
{
    g_state.restart_timestamp = time_monotonic();
}

int cm2_get_restart_time(void)
{
    return time_monotonic() - g_state.restart_timestamp;
}

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
#endif /* CONFIG_CM2_STABILITY_USE_RESTORE_SWITCH_CFG */

static void cm2_restore_connection(cm2_restore_con_t opt)
{
    if (!cm2_wan_link_selection_enabled())
        return;

    if (opt == 0)
        return;

    if (g_state.connected)
        return;

    if (cm2_ovsdb_is_tunnel_created(g_state.link.if_name))
        return;

    if (!cm2_is_eth_type(g_state.link.if_type))
        return;

    if (opt & (1 << CM2_RESTORE_IP)) {
        cm2_ovsdb_refresh_dhcp(cm2_get_uplink_name());
    }
    else if (opt & (1 << CM2_RESTORE_MAIN_LINK)) {
        cm2_restart_iface(cm2_get_uplink_name());
    }
}

static void cm2_stability_handle_fatal_state(int counter)
{
    if (!cm2_wan_link_selection_enabled())
        return;

    // Resetting the restart timer when the counter starts incrementing
    if (counter == 0) {
        cm2_reset_restart_time();
    }

    if (counter + 1 > CONFIG_CM2_STABILITY_THRESH_FATAL) {
        LOGW("Restart managers due to exceeding the threshold for fatal failures");
        cm2_tcpdump_stop(g_state.link.if_name);
        cm2_trigger_restart_managers();
    }
}

static void
cm2_util_set_ip_opts(const char *uname, const char *utype,
                     target_connectivity_check_option_t *opts)
{
    cm2_uplink_state_t ipv4, ipv6;
    bool               ret;

    ret = cm2_ovsdb_CMU_get_ip_state(uname, &ipv4, &ipv6);
    if (!ret) {
        LOGW("%s: IP state not available", uname);
        return;
    }

    if (ipv4 == CM2_UPLINK_UNBLOCKING ||
        ipv6 == CM2_UPLINK_UNBLOCKING)
        *opts |= ROUTER_CHECK | INTERNET_CHECK;

    if (ipv4 != CM2_UPLINK_BLOCKED &&
        ipv4 != CM2_UPLINK_NONE) {
        *opts |= IPV4_CHECK;
    } else {
        *opts &= ~IPV4_CHECK;
    }

    if (ipv6 != CM2_UPLINK_BLOCKED &&
        ipv6 != CM2_UPLINK_NONE) {
        *opts |= IPV6_CHECK;
    } else {
        *opts &= ~IPV6_CHECK;
    }

    if (ipv4 != CM2_UPLINK_INACTIVE && ipv6 == CM2_UPLINK_NONE)
        *opts |= IPV4_CHECK;

    if (ipv6 != CM2_UPLINK_INACTIVE && ipv4 == CM2_UPLINK_NONE)
        *opts |= IPV6_CHECK;

    if (!(*opts & IPV4_CHECK) && !(*opts & IPV6_CHECK)) {
        LOGI("Unexpected configuration of IP, force use IPv4");
        *opts |= IPV4_CHECK;
    }

    if (ipv4 == CM2_UPLINK_INACTIVE ||
        ipv6 == CM2_UPLINK_INACTIVE)
        *opts |= FAST_CHECK;

    /* Skip Router checking for LTE interfaces */
    if (cm2_is_lte_type(utype)) {
        *opts &= ~ROUTER_CHECK;
    }
}

static void cm2_updating_ip(struct schema_Connection_Manager_Uplink *con,
                            target_connectivity_check_option_t opts,
                            target_connectivity_check_t *cstate,
                            bool ipv6,
                            cm2_uplink_state_t s)
{
    target_connectivity_check_option_t c;
    cm2_uplink_state_t                 ns;
    bool                               s_updated;
    bool                               rs;
    bool                               rc;
    bool                               is;
    bool                               ic;
    bool                               con_ok;

    s_updated = false;
    ns = CM2_UPLINK_NONE;

    if (ipv6) {
        c = IPV6_CHECK;
        rs = cstate->router_ipv6_state;
        is = cstate->internet_ipv6_state;
        if (con->is_used)
            g_state.link.ipv6.blocked = false;

    }
    else {
        c = IPV4_CHECK;
        rs = cstate->router_ipv4_state;
        is = cstate->internet_ipv4_state;
        if (con->is_used)
            g_state.link.ipv4.blocked = false;
    }

    if (!(opts & c))
        return;

    rc = opts & ROUTER_CHECK;
    ic = opts & INTERNET_CHECK;

    if (!rc && !ic)
        return;

    con_ok = ((((ic && is) || (con->unreachable_internet_counter <= 0 && !ic)) &&
              ((rc && rs) || (con->unreachable_router_counter <= 0 && !rc))) ||
              (con->is_used && g_state.connected && ((ipv6 && g_state.ipv6_manager_con) || (!ipv6 && !g_state.ipv6_manager_con))));

    switch (s) {
       case CM2_UPLINK_BLOCKED:
           if (con->is_used) {
               if (ipv6)
                   g_state.link.ipv6.blocked = true;
               else
                   g_state.link.ipv4.blocked = true;
           }

           break;

        case CM2_UPLINK_NONE:
        case CM2_UPLINK_READY:
            if (con_ok) {
                s_updated = true;
                ns = CM2_UPLINK_ACTIVE;
            }
            else {
                s_updated = true;
                ns = CM2_UPLINK_INACTIVE;
            }
            break;

        case CM2_UPLINK_INACTIVE:
            if (con_ok) {
                s_updated = true;
                ns = CM2_UPLINK_ACTIVE;
            } else {
                if (con->unreachable_router_counter >= BLOCKING_LINK_THRESHOLD) {
                    s_updated = true;
                    ns = CM2_UPLINK_BLOCKED;
                }

                if (con->unreachable_internet_counter >= BLOCKING_LINK_THRESHOLD) {
                    s_updated = true;
                    ns = CM2_UPLINK_BLOCKED;
                }
            }
            break;

        case CM2_UPLINK_ACTIVE:
            if (!con_ok) {
                s_updated = true;
                ns = CM2_UPLINK_INACTIVE;
            }
            break;

       case CM2_UPLINK_UNBLOCKING:
            if (con_ok) {
                s_updated = true;
                ns = CM2_UPLINK_ACTIVE;
                cm2_ovsdb_update_route_metric(con->if_name, CM2_METRIC_UPLINK_DEFAULT);
            } else {
                s_updated = true;
                ns = CM2_UPLINK_BLOCKED;
            }
            break;

       default:
           LOGW("Unsupported uplink state");
           return;
    }

    if (!s_updated)
        return;

    if (ipv6)
       cm2_ovsdb_CMU_set_ipv6(con->if_name, ns);
    else
       cm2_ovsdb_CMU_set_ipv4(con->if_name, ns);
}

static
void cm2_util_update_uplink_ip_state(struct schema_Connection_Manager_Uplink *con,
                                      target_connectivity_check_option_t opts,
                                      target_connectivity_check_t *cstate)
{
    if (!cm2_wan_link_selection_enabled())
        return;

    cm2_uplink_state_t ip;

    if (con->ipv4_exists) {
        ip = cm2_get_uplink_state_from_str(con->ipv4);
        cm2_updating_ip(con, opts, cstate, false, ip);
    }
    if (con->ipv6_exists) {
        ip = cm2_get_uplink_state_from_str(con->ipv6);
        cm2_updating_ip(con, opts, cstate, true, ip);
    }
}

static bool cm2_connection_req_stability_process(const char *if_name,
                                                 target_connectivity_check_option_t opts,
                                                 bool db_update,
                                                 bool status,
                                                 target_connectivity_check_t *cstate_ptr)
{
    struct schema_Connection_Manager_Uplink con;
    target_connectivity_check_t             cstate;
    cm2_restore_con_t                       ropt;
    char                                    *bridge;
    char                                    *muplink;
    bool                                    ret;
    int                                     counter;

    if (!g_state.link.is_used) {
        LOGN("Waiting for new active link");
        return false;
    }

    memcpy(&cstate, cstate_ptr, sizeof(cstate));
    ropt = 0;
    muplink = NULL;

    ret = cm2_ovsdb_connection_get_connection_by_ifname(if_name, &con);
    if (!ret) {
        LOGW("%s interface does not exist", __func__);
        return false;
    }

    bridge = con.bridge_exists ? con.bridge : "none";
    LOGI("Params to check: link: %s router: %s internet: %s ntp: %s ipv4: %s ipv6: %s opts = %d",
         cm2_util_bool2str(opts & LINK_CHECK), cm2_util_bool2str(opts & ROUTER_CHECK),
         cm2_util_bool2str(opts & INTERNET_CHECK), cm2_util_bool2str(opts & NTP_CHECK),
         cm2_util_bool2str(opts & IPV4_CHECK), cm2_util_bool2str(opts & IPV6_CHECK), opts);

    if (!strcmp(if_name, g_state.link.if_name)) {
        LOGN("Connection status %d, main link: %s bridge: %s ",
             status, if_name, bridge);
        muplink = bridge ? bridge : g_state.link.if_name;
    } else {
        LOGI("Uplink status %d, uplink: %s", status, if_name);
    }

    LOGD("%s: Stability counters: [%d, %d, %d, %d]",if_name,
         con.unreachable_link_counter, con.unreachable_router_counter,
         con.unreachable_internet_counter, con.unreachable_cloud_counter);
    LOGD("%s: Stability states: [%d, %d, %d]", if_name,
         cstate.link_state, cstate.router_ipv4_state | cstate.router_ipv6_state,
         cstate.internet_ipv4_state | cstate.internet_ipv6_state);

    cm2_util_update_uplink_ip_state(&con, opts, &cstate);

    if (opts & NTP_CHECK)
        g_state.ntp_check = cstate.ntp_state;

    if (!db_update)
        return status;

    if (muplink &&
        cm2_link_is_bridge(&g_state.link) &&
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
        else
        {
            // Corner case covering when device reboots during outage
            cm2_uplink_event_add_event(time_real(), true, CM2_UPLINK_LINK);
        }

        ret = cm2_ovsdb_connection_update_unreachable_link_counter(if_name, counter);
        if (!ret)
            LOGW("%s Failed update link counter in ovsdb table", __func__);
    }
    if (opts & ROUTER_CHECK) {
        counter = 0;
        if (((opts & IPV4_CHECK) && (!cstate.router_ipv4_state)) ||
            ((opts & IPV6_CHECK) && (!cstate.router_ipv6_state)) ||
            (!(opts & IPV4_CHECK) && !(opts & IPV6_CHECK))) {
            counter =  con.unreachable_router_counter < 0 ? 1 : con.unreachable_router_counter + 1;
            LOGI("Detected broken Router. Counter = %d", counter);
            cm2_restore_switch_cfg_params(counter, CONFIG_CM2_STABILITY_THRESH_ROUTER + 2, &ropt);
            if (counter % CONFIG_CM2_STABILITY_THRESH_ROUTER == 0)
            {
                ropt |= (1 << CM2_RESTORE_IP);
                if (cm2_uplink_event_is_wan_iface(if_name))
                {
                    cm2_uplink_event_add_event(time_real(), false, CM2_UPLINK_ALL_ROUTER);
                }
            }
            if (counter % (CONFIG_CM2_STABILITY_THRESH_ROUTER + 1) == 0)
                ropt |= (1 << CM2_RESTORE_MAIN_LINK);
        }
        else if (muplink &&
                 kconfig_enabled(CONFIG_CM2_USE_TCPDUMP) &&
                 con.unreachable_router_counter >= CONFIG_CM2_STABILITY_THRESH_TCPDUMP) {
                    cm2_tcpdump_stop(muplink);
        }
        else
        {
            // This will triggers the call periodically, actuall check if pod is in the
            // middle of outage event is done in cm2_network_state_add_event function
            if (cm2_uplink_event_is_wan_iface(if_name))
            {
                cm2_uplink_event_add_event(time_real(), true, CM2_UPLINK_ALL_ROUTER);
            }
        }

        ret = cm2_ovsdb_connection_update_unreachable_router_counter(if_name, counter);
        if (!ret)
            LOGW("%s Failed update router counter in ovsdb table", __func__);

        if (kconfig_enabled(CONFIG_CM2_USE_TCPDUMP) &&
            muplink &&
            counter == CONFIG_CM2_STABILITY_THRESH_TCPDUMP &&
            cm2_is_eth_type(g_state.link.if_type)) {
                cm2_tcpdump_start(muplink);
        }

        if (muplink && con.unreachable_router_counter + 1 == CONFIG_CM2_STABILITY_THRESH_FATAL)
            cm2_tcpdump_stop(muplink);

        if (con.is_used)
            cm2_stability_handle_fatal_state(con.unreachable_router_counter);
    }
    if (opts & INTERNET_CHECK) {
        counter = 0;
        if (((opts & IPV4_CHECK) && (!cstate.internet_ipv4_state)) ||
            ((opts & IPV6_CHECK) && (!cstate.internet_ipv6_state)) ||
            (!(opts & IPV4_CHECK) && !(opts & IPV6_CHECK))) {
            counter = con.unreachable_internet_counter < 0 ? 1 : con.unreachable_internet_counter + 1;
            LOGI("Detected broken Internet. Counter = %d", counter);
            cm2_restore_switch_cfg_params(counter, CONFIG_CM2_STABILITY_THRESH_INTERNET + 2, &ropt);
            if (counter % CONFIG_CM2_STABILITY_THRESH_INTERNET == 0)
            {
                ropt |= (1 << CM2_RESTORE_IP);
                if (cm2_uplink_event_is_wan_iface(if_name))
                {
                    cm2_uplink_event_add_event(time_real(), false, CM2_UPLINK_ALL_INTERNET);
                }
            }
            if (counter % (CONFIG_CM2_STABILITY_THRESH_INTERNET + 1) == 0)
                   ropt |= (1 << CM2_RESTORE_MAIN_LINK);
        }
        else
        {
            // This will triggers the call periodically, actuall check if pod is in the
            // middle of outage event is done in cm2_network_state_add_event function
            if (cm2_uplink_event_is_wan_iface(if_name))
            {
                cm2_uplink_event_add_event(time_real(), true, CM2_UPLINK_ALL_INTERNET);
            }
        }

        ret = cm2_ovsdb_connection_update_unreachable_internet_counter(if_name, counter);
        if (!ret)
            LOGW("%s Failed update internet counter in ovsdb table", __func__);
    }
    if (opts & NTP_CHECK) {
        // This makes little to no sense. NTP check is currently implemented to just check if date is newer than 2014
        // which is not of much use if ntp server goes down after date is already synced first time
        ret = cm2_ovsdb_connection_update_ntp_state(if_name,
                                                    cstate.ntp_state);
        if (!ret)
            LOGW("%s Failed update ntp state in ovsdb table", __func__);

        cm2_uplink_event_add_event(time_real(), cstate.ntp_state, CM2_UPLINK_NTP);
    }
    cm2_restore_connection(ropt);
    return status;
}

bool cm2_connection_req_stability_check(const char *uname,
                                        const char *utype,
                                        const char *clink,
                                        target_connectivity_check_option_t opts,
                                        bool db_update)
{
    target_connectivity_check_t cstate = {0};
    bool ok;


    cm2_util_set_ip_opts(uname, utype, &opts);
    /* Ping WDT before run connectivity check */
    WARN_ON(!target_device_wdt_ping());
    ok = target_device_connectivity_check(clink, &cstate, opts);

    return cm2_connection_req_stability_process(uname, opts, db_update, ok, &cstate);
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
void cm2_util_req_stability_check_recalc(const char *uname,
                                         const char *utype,
                                         const char *clink,
                                         target_connectivity_check_option_t opts,
                                         bool db_update,
                                         bool repeat)
{
    pid_t pid;

    LOGI("%s: cm2_util_req_stability_check_recalc calling. Link check: %s", uname, clink);
    if (!opts) {
        LOGD("stability: nothing to do anymore");
        return;
    }

    if (cm2_is_stability_check_pending(g_state.link.if_name))
    {
        return;
    }

    cm2_util_set_ip_opts(uname, utype, &opts);
    pid = fork();
    if (pid < 0) {
        LOGW("stability: failed to fork() stability checks: %d", errno);
        return;
    }

    if (pid == 0) {
        target_connectivity_check_t cstate = {0};
        bool ok = target_device_connectivity_check(clink, &cstate, opts);
        int mask = cm2_util_cstate2mask(ok, &cstate);
        exit(mask);
        return; /* never reached */
    }

    async_check_t *async_check = (async_check_t *) MALLOC(sizeof(async_check_t));
    memset(async_check, 0, sizeof(async_check_t));
    STRSCPY(async_check->uname, uname);
    STRSCPY(async_check->utype, utype);
    STRSCPY(async_check->clink, clink);
    async_check->pid = pid;
    async_check->opts = opts;
    async_check->repeat = repeat;
    async_check->db_update = db_update;
    ds_tree_insert(&g_async_checks, async_check, async_check->uname);
    ev_child_init(&async_check->cw, cm2_util_req_stability_cb, pid, 0);
    ev_child_start(EV_DEFAULT_ &async_check->cw);

    LOGD("%s: stability for %s: started 0x%02x %supdate pid %d",
         uname, clink, opts, db_update ? "" : "no", pid);
}

static
bool cm2_check_uplink_state(const char *if_name, const char *up_name)
{
    struct schema_Connection_Manager_Uplink con;
    bool blocked_ipv4;
    bool blocked_ipv6;

    memset(&con, 0, sizeof(con));
    blocked_ipv4 = false;
    blocked_ipv6 = false;

    if (!cm2_ovsdb_connection_get_connection_by_ifname(if_name, &con)) {
        LOGI("%s does not exist on uplinks list", if_name);
        return false;
    }

    if ((con.ipv4_exists && cm2_get_uplink_state_from_str(con.ipv4) == CM2_UPLINK_BLOCKED) ||
         !con.ipv4_exists)
        blocked_ipv4 = true;

    if ((con.ipv6_exists && cm2_get_uplink_state_from_str(con.ipv6) == CM2_UPLINK_BLOCKED) ||
         !con.ipv6_exists)
        blocked_ipv6 = true;

    if (blocked_ipv4 && blocked_ipv6) {
        LOGI("IP on uplink not available");
        return false;
    }

    if (con.bridge_exists && strcmp(up_name, con.bridge) != 0) {
        LOGI("Mismatch between uplink: %s and bridge name: %s", up_name, con.bridge);
        return false;
    }

    if (!con.bridge_exists && strcmp(if_name, up_name) != 0) {
        LOGI("Mismatch between uplink: %s and ifname: %s", up_name, if_name);
        return false;
    }

    return true;
}

static
void cm2_util_req_stability_cb(EV_P_ ev_child *w, int revents)
{
    target_connectivity_check_t cstate = {0};
    async_check_t *async_check;

    async_check = (async_check_t *) w;
    ds_tree_remove(&g_async_checks, async_check);

    int mask = WIFEXITED(w->rstatus) ? WEXITSTATUS(w->rstatus) : 0xff;
    bool ok = cm2_util_mask2cstate(mask, &cstate);
    bool db_update = async_check->db_update;
    bool repeat = async_check->repeat;
    int opts = async_check->opts;
    char uname[C_IFNAME_LEN];
    char clink[C_IFNAME_LEN];
    char utype[IFTYPE_SIZE];

    LOGD("%s: stability for %s: completed 0x%02x %supdate%s mask=0x%02x (%s) pid %d",
         async_check->uname,
         async_check->clink,
         opts,
         db_update ? "" : "no",
         repeat ? " recurring" : "",
         mask,
         ok ? "ok" : "fail",
         w->rpid);

    STRSCPY(uname, async_check->uname);
    STRSCPY(utype, async_check->utype);
    STRSCPY(clink, async_check->clink);

    ev_child_stop(EV_A_ w);
    FREE(async_check);

    /* Check if link is still up to date, if not stop repeating */
    if (!cm2_check_uplink_state(uname, clink) && !ok) {
        LOGI("Force clean repeat and update state");
        repeat = false;
        db_update = false;
    }

    cm2_connection_req_stability_process(uname, opts, db_update, ok, &cstate);
    if (repeat && !ok) {
        if (!g_state.connected) {
            db_update = true;
            opts |= FAST_CHECK;
        }

        cm2_util_req_stability_check_recalc(uname, utype, clink, opts,
                                            db_update, repeat);
        return;
    }
}

void cm2_connection_req_stability_check_async(const char *uname,
                                              const char *utype,
                                              const char *clink,
                                              target_connectivity_check_option_t opts,
                                              bool db_update,
                                              bool repeat)
{

    cm2_util_set_ip_opts(uname, utype, &opts);

    LOGI("%s: stability for %s: scheduling 0x%02x %supdate%s",
         uname,
         clink,
         opts,
         db_update ? "" : "no",
         repeat ? " recurring" : "");

    cm2_util_req_stability_check_recalc(uname, utype, clink, opts, db_update, repeat);
}

static void cm2_connection_stability_check(void)
{
    struct schema_Connection_Manager_Uplink *uplinks;
    struct schema_Connection_Manager_Uplink *uplink_i;
    target_connectivity_check_option_t      opts;
    const char                              *c_uplink;
    int                                     cnts;
    int                                     i;

    opts = LINK_CHECK | ROUTER_CHECK | NTP_CHECK | INTERNET_CHECK;
    uplinks = NULL;

    cnts = cm2_ovsdb_get_connection_uplinks(&uplinks, CM2_CONNECTION_REQ_UNBLOCKING_IPV4);
    uplink_i = uplinks;
    for (i = 0; i < cnts && uplink_i; i++, uplink_i++) {
        c_uplink = uplink_i->bridge_exists && uplink_i->is_used ? uplink_i->bridge : uplink_i->if_name;
        opts |= FAST_CHECK;
        cm2_connection_req_stability_check_async(uplink_i->if_name, uplink_i->if_type, c_uplink, opts, true, false);
    }

    if (uplinks)
        FREE(uplinks);

    cnts = cm2_ovsdb_get_connection_uplinks(&uplinks, CM2_CONNECTION_REQ_UNBLOCKING_IPV6);
    uplink_i = uplinks;
    for (i = 0; i < cnts && uplink_i; i++, uplink_i++) {
        c_uplink = uplink_i->bridge_exists && uplink_i->is_used ? uplink_i->bridge : uplink_i->if_name;
        opts |= FAST_CHECK;
        cm2_connection_req_stability_check_async(uplink_i->if_name, uplink_i->if_type, c_uplink, opts, true, false);
    }

    if (uplinks)
        FREE(uplinks);

    cnts = cm2_ovsdb_get_connection_uplinks(&uplinks, CM2_CONNECTION_REQ_ALL_ACTIVE_UPLINKS);

    if (g_state.stability_cnts > UPLINKS_CHECKING_ALL_THRESHOLD) {
        g_state.stability_cnts = 0;
        LOGI("Checking all links, p = %p", uplinks);
        uplink_i = uplinks;
        for (i = 0; i < cnts && uplink_i; i++, uplink_i++) {
            LOGI("Checking active link: %s", uplink_i->if_name);
            c_uplink = uplink_i->bridge_exists && uplink_i->is_used ? uplink_i->bridge : uplink_i->if_name;
            cm2_connection_req_stability_check_async(uplink_i->if_name, uplink_i->if_type, c_uplink, opts, true, false);
        }
    }
    else {
        c_uplink = cm2_link_is_bridge(&g_state.link) ? g_state.link.bridge_name : g_state.link.if_name;
        g_state.stability_cnts++;
        if (g_state.connected &&
            (!g_state.link.ipv4.blocked && !g_state.link.ipv6.blocked) &&
            !cm2_cpu_is_low_loadavg()) {
            if (uplinks)
                FREE(uplinks);
            return;
        }

        if (g_state.connected)
            opts &= ~INTERNET_CHECK;
        else if (cnts > 1)
            opts |= FAST_CHECK;

        cm2_connection_req_stability_check_async(g_state.link.if_name, g_state.link.if_type, c_uplink, opts, true, false);
    }
    if (uplinks)
        FREE(uplinks);
}

void cm2_stability_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;

    if (g_state.run_stability)
        cm2_connection_stability_check();
}

bool cm2_is_stability_check_pending(char *if_name)
{
    async_check_t *async_check = ds_tree_find(&g_async_checks, if_name);
    if (async_check != NULL) return true;

    return false;
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

void cm2_stability_update_interval(struct ev_loop *loop, bool short_int)
{
    int t;

    t = short_int ? CONFIG_CM2_STABILITY_SHORT_INTERVAL : CONFIG_CM2_STABILITY_INTERVAL;

    ev_timer_stop (loop, &g_state.stability_timer);
    ev_timer_set (&g_state.stability_timer, t, t);
    ev_timer_start (loop, &g_state.stability_timer);
}

void cm2_stability_close(struct ev_loop *loop)
{
    LOGD("Stopping stability check");
    ev_timer_stop (loop, &g_state.stability_timer);
}

void cm2_update_links_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;

    cm2_ovsdb_recalc_links(false);
}

void cm2_update_uplinks_init(struct ev_loop *loop)
{
    LOGD("Initializing stability connection check");
    ev_timer_init(&g_state.uplinks_timer,
                  cm2_update_links_cb,
                  UPLINKS_TIMER_TIMEOUT,
                  UPLINKS_TIMER_TIMEOUT);
    g_state.uplinks_timer.data = NULL;
    ev_timer_start(g_state.loop, &g_state.uplinks_timer);
}

void cm2_update_uplinks_set_interval(struct ev_loop *loop, int value)
{

   ev_tstamp ts;

   if (value < 0) {
        LOGW("Invalid timer value %d", value);
        return;
    }
    ts = ev_timer_remaining(loop, &g_state.uplinks_timer);
    if ((int) ts < value)
        return;

    ev_timer_stop (loop, &g_state.uplinks_timer);
    ev_timer_set (&g_state.uplinks_timer, value, UPLINKS_TIMER_TIMEOUT);
    ev_timer_start (loop, &g_state.uplinks_timer);
}

void cm2_update_uplinks_close(struct ev_loop *loop)
{
    LOGD("Stopping updating uplinks");
    ev_timer_stop (loop, &g_state.uplinks_timer);
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
