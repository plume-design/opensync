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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>

#include "ev.h"

#include "log.h"
#include "schema.h"
#include "target.h"
#include "osp_unit.h"
#include "cm2.h"
#include "kconfig.h"
#include "osn_types.h"

#define CM2_VAR_RUN_PATH               "/var/run"
#define CM2_VAR_PLUME_PATH             "/var/plume"

#define CM2_UDHCPC_DRYRUN_PREFIX_FILE  "udhcpc-cmdryrun"
#define CM2_TCPDUMP_PREFIX_FILE        "tcpdump"

/* Dryrun configuration */
#define CM2_DRYRUN_TRIES_THRESH        120
#define CM2_DRYRUN_T_PARAM             "1"
#define CM2_DRYRUN_t_PARAM             "5"
#define CM2_DRYRUN_A_PARAM             "2"

/* ev_child must be the first element of the structure
 * based on that fact we can store additional data, in that case
 * interface name and interface type */
typedef struct {
    ev_child cw;
    char     if_name[128];
    char     if_type[128];
    int      cnt;
} dhcp_dryrun_t;

/* ev_child must be the first element of the structure */
typedef struct {
    ev_child cw;
    char     if_name[128];
    char     pckfile[128];
} cm2_tcpdump_t;

typedef struct {
    ev_timer timer;
    char     if_name[128];
} cm2_delayed_eth_update_t;

static const char *cm2_get_timeout_cmd_arg(void)
{
    static int checked;
    static int needs_t_arg;

    if (!checked) {
        needs_t_arg = target_device_execute("timeout -t 0 true");
        checked = 1;
    }
    return needs_t_arg ? "-t 10" : "10";
}

static int cm2_ovs_insert_port_into_bridge(char *bridge, char *port, bool add)
{
    char *op_add = "add-port";
    char *op_del = "del-port";
    char *op_and = "&&";
    char *op_or  = "||";
    char command[512];
    char *op_log;
    char *op;

    if (add) {
        op = op_add;
        op_log = op_or;
    } else {
        op = op_del;
        op_log = op_and;
    }

    LOGI("OVS bridge: %s port = %s bridge = %s", op, port, bridge);

    /* add/delete it to/from OVS bridge */
    sprintf(command, "timeout %s ovs-vsctl port-to-br %s | grep %s %s timeout %s ovs-vsctl %s %s %s",
            cm2_get_timeout_cmd_arg(), port, bridge, op_log, cm2_get_timeout_cmd_arg(), op, bridge, port);

    LOGD("%s: Command: %s", __func__, command);

    return target_device_execute(command);
}

void cm2_update_bridge_cfg(char *bridge, char *port, bool brop,
                           cm2_par_state_t mstate, bool dhcp_update)
{
    bool macrep;
    int  r;

    if (mstate != CM2_PAR_NOT_SET) {
        macrep = (mstate == CM2_PAR_TRUE) ? true : false;
        r = cm2_ovsdb_update_mac_reporting(port, macrep);
        if (!r) {
            LOGW("%s: Failed to update mac reporting, state: %d",
                 port, macrep);
        }
    }

    r = cm2_ovs_insert_port_into_bridge(bridge, port, brop);
    if (!r)
        LOGI("Failed to update port %s in %s [state = %d]",
             port, bridge, brop);

    /* Update dhcp client on bridge */
    if (dhcp_update) {
        cm2_ovsdb_set_dhcp_client(bridge, brop);
        cm2_ovsdb_set_dhcpv6_client(bridge, brop);
    }
}

/**
 * Return the PID of the udhcpc client serving on interface @p ifname
 */
static int cm2_util_get_pid(char *pidname)
{
    char pid_file[256];
    int  pid;
    FILE *f;
    int  rc;

    tsnprintf(pid_file, sizeof(pid_file), "%s.pid", pidname);

    f = fopen(pid_file, "r");
    if (f == NULL)
        return 0;

    rc = fscanf(f, "%d", &pid);
    fclose(f);

    /* We should read exactly 1 element */
    if (rc != 1)
        return 0;

    if (kill(pid, 0) != 0)
        return 0;

    return pid;
}

bool cm2_is_iface_in_bridge(const char *bridge, const char *port)
{
    char command[256];

    LOGD("OVS bridge: port = %s bridge = %s", port, bridge);
    sprintf(command, "timeout %s ovs-vsctl list-ifaces %s | grep %s",
            cm2_get_timeout_cmd_arg(), bridge, port);

    LOGD("%s: Command: %s", __func__, command);
    return target_device_execute(command);
}

#ifdef CONFIG_CM2_USE_TCPDUMP
static void cm2_tcpdump_cb(struct ev_loop *loop, ev_child *w, int revents)
{
    cm2_tcpdump_t  *tcpdump;
    char           cmd[256];

    tcpdump = (cm2_tcpdump_t *) w;
    ev_child_stop (loop, w);

    LOGI("%s: tcpdump_cb: cleanup old pcaps", tcpdump->if_name);
    tsnprintf(cmd, sizeof(cmd), "ls -1t %s/*%s-%s | sed 1d | xargs rm -v",
              CM2_VAR_PLUME_PATH , CM2_TCPDUMP_PREFIX_FILE, tcpdump->if_name);

    LOGD("%s: Command: %s", __func__, cmd);
    target_device_execute(cmd);
    free(tcpdump);
}

void cm2_tcpdump_start(char* ifname)
{
    struct tm  *timeinfo;
    time_t     rawtime;
    pid_t      pid;
    char       pidname[512];
    char       pckfile[256];
    char       pidfile[256];
    char       timebuf[40];

    tsnprintf(pidname, sizeof(pidname), "%s/%s-%s",
              CM2_VAR_RUN_PATH , CM2_TCPDUMP_PREFIX_FILE, ifname);

    pid = cm2_util_get_pid(pidname);
    if (pid > 0) {
        LOGI("%s: tcpdump: skip new request (already running)", ifname);
        return;
    }

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(timebuf, sizeof(timebuf), "%F_%H-%M", timeinfo);

    tsnprintf(pidfile, sizeof(pidfile), "%s.pid", pidname);
    tsnprintf(pckfile, sizeof(pckfile), "%s/%s-%s-%s",
              CM2_VAR_PLUME_PATH , timebuf, CONFIG_CM2_TCPDUMP_PREFIX_FILE, ifname);

    LOGI("%s: tcpdump: starting [pidfile = %s pckfile = %s]",
         ifname, pidfile, pckfile);

    char *argv_tcp_dump[] = {
        CONFIG_CM2_TCPDUMP_START_STOP_DAEMON_PATH,
        "-p", pidfile,
        "-m",
        "-x", "timeout",
        "-S", "--",
        "-t", CONFIG_CM2_TCPDUMP_TIMEOUT_PARAM,
        CONFIG_CM2_TCPDUMP_PATH,
        "-i", ifname,
        "-w", pckfile,
        "-c", CONFIG_CM2_TCPDUMP_COUNT_PARAM,
        "-s", CONFIG_CM2_TCPDUMP_SNAPLEN_PARAM,
        NULL
    };

    pid = fork();
    if (pid == 0) {
        execv(argv_tcp_dump[0], argv_tcp_dump);
        LOGW("%s: tcpdump: execv failed: %d (%s)",
             ifname, errno, strerror(errno));
        exit(1);
    } else {
        cm2_tcpdump_t *tcpdump = (cm2_tcpdump_t *) malloc(sizeof(cm2_tcpdump_t));
        if (!tcpdump) {
            LOGW("%s: tcpdump: memory allocation failure", ifname);
            return;
        }
        memset(tcpdump, 0, sizeof(cm2_tcpdump_t));
        STRSCPY(tcpdump->if_name, ifname);
        STRSCPY(tcpdump->pckfile, pckfile);
        ev_child_init (&tcpdump->cw, cm2_tcpdump_cb, pid, 0);
        ev_child_start (EV_DEFAULT, &tcpdump->cw);
    }
}

void cm2_tcpdump_stop(char *ifname)
{
    pid_t pid;
    char  pidname[256];
    char  cmd[512];

    tsnprintf(pidname, sizeof(pidname), "%s/%s-%s",
              CM2_VAR_RUN_PATH , CM2_TCPDUMP_PREFIX_FILE, ifname);

    pid = cm2_util_get_pid(pidname);
    if (!pid) {
        LOGI("%s: tcpdump: client not running", ifname);
        return;
    }

    LOGI("%s: tcpdump stop: pid: %d pid_name: %s", ifname, pid, pidname);

    tsnprintf(cmd, sizeof(cmd), "%s -p %s.pid -K; rm %s.pid",
              CONFIG_CM2_TCPDUMP_START_STOP_DAEMON_PATH, pidname, pidname);

    LOGD("%s: Command: %s", __func__, cmd);
    target_device_execute(cmd);
}
#endif /* CONFIG_CM2_USE_TCPDUMP */

static void
cm2_delayed_eth_update_cb(struct ev_loop *loop, ev_timer *timer, int revents)
{
    cm2_delayed_eth_update_t *p;

    p = (void *)timer;
    LOGI("%s: delayed eth update cb", p->if_name);
    ev_timer_stop(EV_DEFAULT, &p->timer);
    cm2_ovsdb_connection_update_loop_state(p->if_name, false);
    free(p);
}

void cm2_delayed_eth_update(char *if_name, int timeout)
{
    struct schema_Connection_Manager_Uplink con;
    cm2_delayed_eth_update_t                *p;

    if (!cm2_ovsdb_connection_get_connection_by_ifname(if_name, &con)) {
        LOGW("%s: eth_update: interface does not exist", if_name);
        return;
    }

    if (!(p = malloc(sizeof(*p)))) {
        LOGW("%s: eth_update: memory allocation failed", if_name);
        return;
    }

    cm2_ovsdb_connection_update_loop_state(if_name, true);
    STRSCPY(p->if_name, if_name);
    ev_timer_init(&p->timer, cm2_delayed_eth_update_cb, timeout, 0);
    ev_timer_start(EV_DEFAULT, &p->timer);
    LOGI("%s: scheduling delayed eth update, timeout = %d",
         if_name, timeout);
}

static void cm2_dhcpc_dryrun_cb(struct ev_loop *loop, ev_child *w, int revents)
{
    struct schema_Connection_Manager_Uplink con;
    cm2_par_state_t                         l3state;
    dhcp_dryrun_t                           *dhcp_dryrun;
    bool                                    status;
    int                                     ret;
    int                                     eth_timeout;

    dhcp_dryrun = (dhcp_dryrun_t *) w;

    ev_child_stop (loop, w);
    if (WIFEXITED(w->rstatus) && WEXITSTATUS(w->rstatus) == 0)
        status = true;
    else
        status = false;

    if (WIFEXITED(w->rstatus))
        LOGD("%s: %s: rstatus = %d", __func__,
             dhcp_dryrun->if_name, WEXITSTATUS(w->rstatus));

    LOGD("%s: dryrun state: %d cnt = %d", dhcp_dryrun->if_name, w->rstatus, dhcp_dryrun->cnt);

    ret = cm2_ovsdb_connection_get_connection_by_ifname(dhcp_dryrun->if_name, &con);
    if (!ret) {
        LOGD("%s: interface %s does not exist", __func__, dhcp_dryrun->if_name);
        goto release;
    }

    if (cm2_is_eth_type(dhcp_dryrun->if_type) &&
        cm2_is_iface_in_bridge(CONFIG_TARGET_LAN_BRIDGE_NAME, dhcp_dryrun->if_name)) {
        LOGI("%s: Skip trigger new dryrun, iface in %s",
            dhcp_dryrun->if_name, CONFIG_TARGET_LAN_BRIDGE_NAME);
        goto release;
    }

    if (!status && con.has_L2) {
        if (g_state.link.is_used && dhcp_dryrun->cnt > CM2_DRYRUN_TRIES_THRESH) {
                LOGI("%s: Stop dryruns due to exceeding the threshold [%d]",
                     dhcp_dryrun->if_name, CM2_DRYRUN_TRIES_THRESH);
                goto release;
        }

        if (cm2_is_eth_type(dhcp_dryrun->if_type) &&
                   g_state.link.is_used &&
                   cm2_is_wifi_type(g_state.link.if_type)) {
            LOGI("%s: Detected Leaf with pluged ethernet, connected = %d",
                 dhcp_dryrun->if_name, g_state.connected);
            eth_timeout = g_state.connected ? CONFIG_CM2_ETHERNET_SHORT_DELAY : CONFIG_CM2_ETHERNET_LONG_DELAY;
            cm2_delayed_eth_update(dhcp_dryrun->if_name, eth_timeout);
        }
        cm2_dhcpc_start_dryrun(dhcp_dryrun->if_name, dhcp_dryrun->if_type, dhcp_dryrun->cnt + 1);
    }

release:
    l3state = status ? CM2_PAR_TRUE : CM2_PAR_FALSE;
    ret = cm2_ovsdb_connection_update_L3_state(dhcp_dryrun->if_name, l3state);
    if (!ret)
        LOGW("%s: %s: Update L3 state failed status = %d ret = %d",
             __func__, dhcp_dryrun->if_name, status, ret);
    free(dhcp_dryrun);
}

void cm2_dhcpc_start_dryrun(char* ifname, char *iftype, int cnt)
{
    char  udhcpc_s_option[256];
    char  vendor_classid[256];
    char  pidfile[256];
    char  pidname[512];
    char  *argv[30];
    int   argc;
    pid_t pid;

    LOGD("%s: Trigger dryrun [%d]", ifname, cnt);

    tsnprintf(pidname, sizeof(pidname), "%s/%s-%s",
              CM2_VAR_RUN_PATH , CM2_UDHCPC_DRYRUN_PREFIX_FILE, ifname);

    pid = cm2_util_get_pid(pidname);
    if (pid > 0)
    {
        LOGI("%s: DHCP client [%d] already running", ifname, pid);
        return;
    }

    tsnprintf(pidfile, sizeof(pidfile), "%s.pid", pidname);
    tsnprintf(udhcpc_s_option, sizeof(udhcpc_s_option),
              CONFIG_INSTALL_PREFIX"/bin/udhcpc-dryrun.sh");

    if (kconfig_enabled(CONFIG_CM2_USE_UDHCPC_VENDOR_CLASSID))
    {
        char vendor_classid[256];

        if (osp_unit_model_get(vendor_classid, sizeof(vendor_classid)) == false)
        {
            tsnprintf(vendor_classid, sizeof(vendor_classid),
                      TARGET_NAME);
        }
    }

    argc = 0;
    argv[argc++] = "/sbin/udhcpc";
    argv[argc++] = "-p";
    argv[argc++] = pidfile;
    argv[argc++] = "-n";
    argv[argc++] = "-t";
    argv[argc++] = CM2_DRYRUN_t_PARAM;
    argv[argc++] = "-T";
    argv[argc++] = CM2_DRYRUN_T_PARAM;
    argv[argc++] = "-A";
    argv[argc++] = CM2_DRYRUN_A_PARAM;
    argv[argc++] = "-f";
    argv[argc++] = "-i";
    argv[argc++] = ifname;
    argv[argc++] = "-s";
    argv[argc++] =  udhcpc_s_option;
    if (!kconfig_enabled(CONFIG_UDHCPC_OPTIONS_USE_CLIENTID)) {
        argv[argc++] = "-C";
    }
    argv[argc++] = "-S";
    if (kconfig_enabled(CONFIG_CM2_USE_CUSTOM_UDHCPC)) {
        argv[argc++] = "-Q";
    }
    if (kconfig_enabled(CONFIG_CM2_USE_UDHCPC_VENDOR_CLASSID)) {
        argv[argc++] = "-V";
        argv[argc++] = vendor_classid;
    }
    argv[argc++] = "-q",
    argv[argc++] = NULL;

    pid = fork();
    if (pid == 0) {
        execv(argv[0], argv);
        LOGW("%s: %s: failed to exec dry dhcp: %d (%s)",
             __func__, ifname, errno, strerror(errno));
        exit(1);
    } else {
        dhcp_dryrun_t *dhcp_dryrun = (dhcp_dryrun_t *) malloc(sizeof(dhcp_dryrun_t));

        memset(dhcp_dryrun, 0, sizeof(dhcp_dryrun_t));
        STRSCPY(dhcp_dryrun->if_name, ifname);
        STRSCPY(dhcp_dryrun->if_type, iftype);
        dhcp_dryrun->cnt = cnt;

        ev_child_init (&dhcp_dryrun->cw, cm2_dhcpc_dryrun_cb, pid, 0);
        ev_child_start (EV_DEFAULT, &dhcp_dryrun->cw);
    }
}

void cm2_dhcpc_stop_dryrun(char *ifname)
{
    pid_t pid;
    char  pidname[512];
    char  cmd[256];

    tsnprintf(pidname, sizeof(pidname), "%s/%s-%s",
              CM2_VAR_RUN_PATH , CM2_UDHCPC_DRYRUN_PREFIX_FILE, ifname);

    pid = cm2_util_get_pid(pidname);
    if (!pid) {
        LOGI("%s: DHCP client is not running", ifname);
        return;
    }

    LOGI("%s: pid: %d pid_name: %s", ifname, pid, pidname);

    if (kill(pid, SIGKILL) < 0) {
        LOGW("%s: %s: failed to send kill signal: %d (%s)",
             __func__, ifname, errno, strerror(errno));
    }
    tsnprintf(cmd, sizeof(cmd), "rm -f %s.pid", pidname);
    LOGD("%s: Command: %s", __func__, cmd);
    WARN_ON(!target_device_execute(cmd));
}

bool cm2_is_eth_type(const char *if_type) {
    return !strcmp(if_type, ETH_TYPE_NAME) ||
           !strcmp(if_type, VLAN_TYPE_NAME)||
           !strcmp(if_type, PPPOE_TYPE_NAME);
}

bool cm2_is_wifi_type(const char *if_type) {
    return !strcmp(if_type, VIF_TYPE_NAME) ||
           !strcmp(if_type, GRE_TYPE_NAME);
}

char* cm2_get_uplink_name(void)
{
    if (g_state.link.is_bridge)
        return g_state.link.bridge_name;

    return g_state.link.if_name;
}

void cm2_update_device_type(const char *iftype)
{
    bool bridge_mode;

    if (cm2_is_wifi_type(iftype)) {
        g_state.dev_type = CM2_DEVICE_LEAF;
    } else {
        if (cm2_is_wan_bridge())
            bridge_mode = cm2_ovsdb_is_port_name("patch-w2h");
        else
            bridge_mode = g_state.link.is_bridge;

        g_state.dev_type = bridge_mode ? CM2_DEVICE_BRIDGE : CM2_DEVICE_ROUTER;
    }

    LOGI("Device type: %d", g_state.dev_type);
}

bool cm2_osn_is_ipv6_global_link(const char *ifname, const char *ipv6_addr)
{
    osn_ip6_addr_t addr;

    if (!osn_ip6_addr_from_str(&addr, ipv6_addr)) {
        LOGW("%s: Invalid IPv6 address: %s", ifname, ipv6_addr);
        return false;
    }

    if (osn_ip6_addr_type(&addr) != OSN_IP6_ADDR_GLOBAL) {
        LOGI("%s: Not a global IPv6 address: %s", ifname, ipv6_addr);
        return false;
    }

    LOGI("%s: Global IPv6 address: %s", ifname, ipv6_addr);
    return true;
}

void cm2_restart_iface(char *ifname)
{
    WARN_ON(!cm2_ovsdb_set_Wifi_Inet_Config_interface_enabled(false, ifname));
    WARN_ON(!cm2_ovsdb_set_Wifi_Inet_Config_interface_enabled(true, ifname));
}
