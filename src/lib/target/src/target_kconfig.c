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
 * ===========================================================================
 *  Generic TARGET layer implementation using Kconfig configuration
 * ===========================================================================
 */

#define MODULE_ID LOG_MODULE_ID_TARGET

#include "log.h"
#include "os_nif.h"
#include "target.h"

#if defined(CONFIG_TARGET_CAP_GATEWAY) || defined(CONFIG_TARGET_CAP_EXTENDER)
int target_device_capabilities_get()
{
    int cap = 0;

#if defined(CONFIG_TARGET_CAP_GATEWAY)
    cap |= TARGET_GW_TYPE;
#endif

#if defined(CONFIG_TARGET_CAP_EXTENDER)
    cap |= TARGET_EXTENDER_TYPE;
#endif

    return cap;
}
#endif

#if defined(CONFIG_TARGET_LAN_BRIDGE_NAME)
const char **target_ethclient_brlist_get()
{
    static const char *brlist[] = { CONFIG_TARGET_LAN_BRIDGE_NAME, NULL };
    return brlist;
}
#endif

#if defined(CONFIG_TARGET_ETH_LIST)
const char **target_ethclient_iflist_get()
{
    static const char *iflist[] =
    {
#if defined(CONFIG_TARGET_ETH0_LIST)
        CONFIG_TARGET_ETH0_NAME,
#endif
#if defined(CONFIG_TARGET_ETH1_LIST)
        CONFIG_TARGET_ETH1_NAME,
#endif
#if defined(CONFIG_TARGET_ETH2_LIST)
        CONFIG_TARGET_ETH2_NAME,
#endif
#if defined(CONFIG_TARGET_ETH3_LIST)
        CONFIG_TARGET_ETH3_NAME,
#endif
#if defined(CONFIG_TARGET_ETH4_LIST)
        CONFIG_TARGET_ETH4_NAME,
#endif
#if defined(CONFIG_TARGET_ETH5_LIST)
        CONFIG_TARGET_ETH5_NAME,
#endif
        NULL
    };

    return iflist;
}
#endif

#if defined(CONFIG_TARGET_PATH_BIN)
const char *target_bin_dir(void)
{
    return CONFIG_TARGET_PATH_BIN;
}
#endif /* CONFIG_TARGET_PATH_BIN */

#if defined(CONFIG_TARGET_PATH_TOOLS)
const char *target_tools_dir(void)
{
    return CONFIG_TARGET_PATH_TOOLS;
}
#endif /* CONFIG_TARGET_PATH_TOOLS */

#if defined(CONFIG_TARGET_PATH_SCRIPTS)
const char *target_scripts_dir(void)
{
    return CONFIG_TARGET_PATH_SCRIPTS;
}
#endif /* CONFIG_TARGET_PATH_SCRIPTS */

#if defined(CONFIG_TARGET_PATH_PERSISTENT)
const char *target_persistent_storage_dir(void)
{
    return CONFIG_TARGET_PATH_PERSISTENT;
}
#endif /* CONFIG_TARGET_PATH_PERSISTENT */


#if defined(CONFIG_TARGET_RESTART_SCRIPT)
bool target_device_restart_managers()
{
    if (access(CONFIG_TARGET_PATH_DISABLE_FATAL_STATE, F_OK) == 0) {
        LOGEM("FATAL condition triggered, not restarting managers by request "
        "(%s exists)", CONFIG_TARGET_PATH_DISABLE_FATAL_STATE);
    }
    else {
        pid_t pid;
        char *argv[] = {NULL} ;

        LOGI("FATAL condition triggered, restarting managers...");
        pid = fork();
        if (pid == 0) {
            int rc = execvp(CONFIG_TARGET_RESTART_SCRIPT_CMD, argv);
            exit((rc == 0) ? 0 : 1);
        }
        while(1); // Sit in loop and wait to be restarted
    }
    return true;
}
#endif

#if defined(CONFIG_TARGET_LINUX_LOGPULL)
bool target_log_pull_ext(
        const char *upload_location,
        const char *upload_token,
        const char *upload_method)
{
    // TODO: command cleanup (remove hc params, etc...)
    char shell_cmd[1024];
    snprintf(shell_cmd, sizeof(shell_cmd),
        "sh "CONFIG_TARGET_PATH_SCRIPTS"/lm_logs_collector.sh"
        " \"%s\""
        " %s"
        " "CONFIG_TARGET_PATH_LOG_LM
        " syslog"
        " syslog_copy"
        " tmp"
        " crash"
        " /tmp/etc/openvswitch/conf.db"
        " /tmp/ovsdb.log"
        " %s",
        upload_location,
        upload_token,
        upload_method);

    // On success we return true
    return !cmd_log(shell_cmd);
}

bool target_log_pull(const char *upload_location, const char *upload_token)
{
    return target_log_pull_ext(upload_location, upload_token, "lm-awlan");
}
#endif

#if !defined(CONFIG_TARGET_WATCHDOG)
/* Implement dummy watchdog function */
bool target_device_wdt_ping(void)
{
    return true;
}
#endif

#if defined(CONFIG_TARGET_LINUX_EXECUTE)
bool target_device_execute(const char *cmd)
{
    int rc = system(cmd);

    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0) return false;

    return true;
}
#endif

#if defined(CONFIG_TARGET_LED_SUPPORT)
const char *target_led_device_dir(void)
{
    return CONFIG_TARGET_LED_PATH;
}

int target_led_names(const char **leds[])
{
    static const char *led_names[] =
    {
#if defined(CONFIG_TARGET_LED0)
        CONFIG_TARGET_LED0_NAME,
#endif
#if defined(CONFIG_TARGET_LED1)
        CONFIG_TARGET_LED1_NAME,
#endif
#if defined(CONFIG_TARGET_LED2)
        CONFIG_TARGET_LED2_NAME,
#endif
#if defined(CONFIG_TARGET_LED3)
        CONFIG_TARGET_LED3_NAME,
#endif
        NULL
    };

    *leds = led_names;
    return (sizeof(led_names) / (sizeof(led_names[0]))) - 1;
}
#endif /* CONFIG_TARGET_LED_SUPPORT */


#if defined(CONFIG_TARGET_CM_LINUX_SUPPORT_PACKAGE)

#include <arpa/inet.h>
#include <errno.h>

#include "os_random.h"

/* NTP CHECK CONFIGURATION */
// Max number of times to check for NTP before continuing
#define NTP_CHECK_MAX_COUNT             10  // 10 x 5 = 50 seconds
// NTP check passes once time is greater then this
#define TIME_NTP_DEFAULT                1000000
// File used to disable waiting for NTP
#define DISABLE_NTP_CHECK               "/opt/tb/cm-disable-ntp-check"

/* CONNECTIVITY CHECK CONFIGURATION */
#define PROC_NET_ROUTE                  "/proc/net/route"
#define DEFAULT_PING_PACKET_SIZE        4
#define DEFAULT_PING_PACKET_CNT         2
#define DEFAULT_PING_TIMEOUT            4

#define DEFAULT_BACKHAUL_PREFIX         "169.254."
#define DEFAULT_TIMEOUT_ARG             5
#define DEFAULT_INTERNET_CNT_CHECK      2

/* Root Servers based on https://www.iana.org/domains/root/servers */
static char *util_connectivity_check_inet_addrs[] = {
    "a.root-servers.net",
    "b.root-servers.net",
    "c.root-servers.net",
    "d.root-servers.net",
    "f.root-servers.net",
    "h.root-servers.net",
    "i.root-servers.net",
    "j.root-servers.net",
    "k.root-servers.net",
    "l.root-servers.net",
    "m.root-servers.net",
    NULL
};

/* IPv4 Root Servers */
static char *util_connectivity_check_inet_ipv4_addrs[] = {
    "198.41.0.4",
    "199.9.14.201",
    "192.33.4.12",
    "199.7.91.13",
    "192.5.5.241",
    "198.97.190.53",
    "192.36.148.17",
    "192.58.128.30",
    "193.0.14.129",
    "199.7.83.42",
    "202.12.27.33",
    NULL
};

/* IPv6 Root Servers */
static char *util_connectivity_check_inet_ipv6_addrs[] = {
    "2001:503:ba3e::2:30",
    "2001:500:200::b",
    "2001:500:2::c",
    "2001:500:2d::d",
    "2001:500:2f::f",
    "2001:500:1::53",
    "2001:7fe::53",
    "2001:503:c27::2:30",
    "2001:7fd::1",
    "2001:500:9f::42",
    "2001:dc3::35",
    NULL
};

static int util_connectivity_get_inet_addr_cnt(char **addr)
{
    char **p = addr;
    int n = 0;

    while (*p) {
        p++;
        n++;
    }
    return n;
}

/******************************************************************************
 * Utility: connectivity, ntp check
 *****************************************************************************/

static int
util_timespec_cmp_lt(struct timespec *cur, struct timespec *ref)
{
     if (cur == NULL || ref == NULL)
         return -1;

     if (cur->tv_sec < ref->tv_sec)
         return 1;

     if (cur->tv_sec == ref->tv_sec)
         return cur->tv_nsec < ref->tv_nsec;

     return 0;
}

static time_t
util_year_to_epoch(int year, int month)
{
     struct tm time_formatted;

     if (year < 1900)
        return -1;

    memset(&time_formatted, 0, sizeof(time_formatted));
        time_formatted.tm_year = year - 1900;
    time_formatted.tm_mday = 1;
        time_formatted.tm_mon  = month;

    return mktime(&time_formatted);
}

static bool
util_ntp_check(void)
{
    struct timespec cur;
    struct timespec target;
    int ret = true;

    target.tv_sec = util_year_to_epoch(2014, 1);
    if (target.tv_sec < 0)
        target.tv_sec = TIME_NTP_DEFAULT;

    target.tv_nsec = 0;

    if (clock_gettime(CLOCK_REALTIME, &cur) != 0) {
        LOGE("Failed to get wall clock, errno=%d", errno);
        return false;
    }
    ret = (util_timespec_cmp_lt(&cur, &target) == 0) ? true : false;

    return ret;
}

static void
util_set_timeout_cmd_arg(char *arg, size_t s, int timeout)
{
    static bool checked = false;
    static bool needs_t_arg = false;
    char       *t;

    if (!checked) {
        needs_t_arg = target_device_execute("timeout -t 0 true");
        checked = true;
    }

    t = needs_t_arg ? "-t" : "";
    snprintf(arg, s, "%s %d", t, timeout);
}

static int
util_system_cmd(const char *cmd)
{
    char command[512];
    char targ[64];

    util_set_timeout_cmd_arg(targ, sizeof(targ), DEFAULT_TIMEOUT_ARG);
    snprintf(command, sizeof(command), "timeout %s %s", targ, cmd);
    return target_device_execute(command);
}

static bool
util_ping_cmd(const char *ipstr, bool ipv6)
{
    char cmd[256];
    char *ipv6_s;
    bool rc;

    ipv6_s = ipv6 ? "-6" : "";

    snprintf(cmd, sizeof(cmd), "ping %s %s -s %d -c %d -w %d >/dev/null 2>&1",
             ipv6_s, ipstr, DEFAULT_PING_PACKET_SIZE, DEFAULT_PING_PACKET_CNT,
             DEFAULT_PING_TIMEOUT);

    rc = util_system_cmd(cmd);
    LOGD("Ping %s result %d (cmd=%s)", ipstr, rc, cmd);
    if (!rc)
        LOGI("Ping %s failed (cmd=%s)", ipstr, cmd);

    return rc;
}

static bool
util_arping_cmd(const char *ipstr)
{
    char cmd[256];
    bool ret;

    snprintf(ARRAY_AND_SIZE(cmd),
             "arping -I \"$(ip ro get %s"
             " | cut -d' ' -f3"
             " | sed 1q)\" -c %d -w %d %s",
             ipstr,
             DEFAULT_PING_PACKET_CNT,
             DEFAULT_PING_TIMEOUT,
             ipstr);

    ret = util_system_cmd(cmd);

    LOGI("Arping %s result %d (cmd=%s)", ipstr, ret, cmd);
    return ret;
}

static bool
util_get_router_ipv4(struct in_addr *dest)
{
    FILE *f1;
    char line[128];
    char *ifn, *dst, *gw, *msk, *sptr;
    int i, rc = false;

    if ((f1 = fopen(PROC_NET_ROUTE, "rt"))) {
        while(fgets(line, sizeof(line), f1)) {
            ifn = strtok_r(line, " \t", &sptr);         // Interface name
            dst = strtok_r(NULL, " \t", &sptr);         // Destination (base 16)
            gw  = strtok_r(NULL, " \t", &sptr);         // Gateway (base 16)
            for (i = 0;i < 4;i++) {
                // Skip: Flags, RefCnt, Use, Metric
                strtok_r(NULL, " \t", &sptr);
            }
            msk = strtok_r(NULL, " \t", &sptr);         // Netmask (base 16)
            // We don't care about the rest of the values

            if (!ifn || !dst || !gw || !msk) {
                // malformatted line
                continue;
            }

            if (!strcmp(dst, "00000000") && !strcmp(msk, "00000000")) {
                // Our default route
                memset(dest, 0, sizeof(*dest));
                dest->s_addr = strtoul(gw, NULL, 16);   // Router IP
                rc = true;
                break;
            }
        }
        fclose(f1);

        if (rc) {
            LOGD("%s: Found router IPv4 %s", PROC_NET_ROUTE, inet_ntoa(*dest));
        }
        else {
            LOGD("%s: No router IPv4 found", PROC_NET_ROUTE);
        }
    }
    else {
        LOGE("Failed to get router IPv4, unable to open %s", PROC_NET_ROUTE);
    }

    return rc;
}

static bool
util_get_router_ipv6(char *dest, int size)
{
    FILE *f1;
    char line[128];
    bool retval;
    char cmd[128];

    f1 = NULL;
    retval = false;

    snprintf(cmd, sizeof(cmd), "ip -6 route show default | awk '$2 != \"from\" {print $3 \"%%\" $5}'");
    f1 = popen(cmd, "r");
    if (!f1) {
        LOGE("Failed to get ipv6 route info");
        goto done;
    }

    if (fgets(line, sizeof(line), f1) == NULL) {
        LOGD("IPv6 default route not available");
        goto done;
    }

    while(line[strlen(line)-1] == '\r' || line[strlen(line)-1] == '\n') {
        line[strlen(line)-1] = '\0';
    }

    retval = true;
    strscpy(dest, line, size);

done:
    if (f1 != NULL)
        pclose(f1);

    return retval;
}

static bool
util_is_gretap_softwds_link(const char *ifname)
{
    char path[256];

    snprintf(path, sizeof(path), "/sys/class/net/g-%s/softwds/addr", ifname);
    return (access(path, F_OK) == 0);
}

static bool
util_get_link_ip(const char *ifname, struct in_addr *dest)
{
    char  line[128];
    char  cmd[128];
    bool  retval;
    FILE  *f1;

    f1 = NULL;
    retval = false;

    if (util_is_gretap_softwds_link(ifname)) {
        snprintf(cmd, sizeof(cmd),
                 "cat /sys/class/net/g-%s/softwds/ip4gre_remote_ip",
                 ifname);
    } else {
        /* Given ifname==wl0 it is expected to match a line looking like so:
         *  gretap remote 169.254.3.129 local 169.254.3.247 dev wl0 ttl inherit tos inherit
         *
         * And to extract:
         *  169.254.3.129
         */
         snprintf(cmd, sizeof(cmd), "ip -d link | "
                  "awk '$1 == \"gretap\" && $7 == \"%s\" {print $3}'", ifname);
    }

    f1 = popen(cmd, "r");
    if (!f1) {
        LOGE("Failed to retrieve Wifi Link remote IP address");
        goto error;
    }

    if (fgets(line, sizeof(line), f1) == NULL) {
        LOGW("No Wifi Link remote IP address found");
        goto error;
    }

    while(line[strlen(line)-1] == '\r' || line[strlen(line)-1] == '\n') {
        line[strlen(line)-1] = '\0';
    }

    LOGD("local IP addr = %s dest addr = %s", line, inet_ntoa(*dest));
    if (inet_pton(AF_INET, line, dest) != 1) {
        LOGW("Failed to parse Wifi Link remote IP address (%s)", line);
        goto error;
    }

    retval = true;

error:
    if (f1 != NULL)
        pclose(f1);

    return retval;
}

static bool
util_connectivity_link_check(const char *ifname)
{
    struct in_addr link_ip;
    int            iflen;

    if (!strstr(ifname, "g-"))
        return true;

    iflen = strlen(ifname);
    if (iflen - 2 <= 0) {
        LOGW("Interface name corrupted [len = %d]", iflen);
        return false;
    }

    if (util_get_link_ip(ifname + 2, &link_ip))
    {
        if (util_ping_cmd(inet_ntoa(link_ip), false) == false)
        {
            /* ARP traffic tends to be treated differently, i.e.
             * it lands on different TID in Wi-Fi driver.
             * There's a chance its choking up on default TID0
             * but works fine on TID7 which handles ARP/DHCP.
             * It's nice to detect that as it helps debugging.
             */
            if (strstr(inet_ntoa(link_ip), DEFAULT_BACKHAUL_PREFIX)) {
                util_arping_cmd(inet_ntoa(link_ip));
                return false;
            }
        }
    }
    return true;
}

static bool
util_get_ipv6_global_interface(char *ifn, int ifn_size)
{
    FILE *f1;
    char line[128];
    bool retval;
    char cmd[128];

    f1 = NULL;
    retval = false;

    snprintf(cmd, sizeof(cmd), "ip -6 route show default | awk '$2 != \"from\" {print $5}'");
    f1 = popen(cmd, "r");
    if (!f1) {
        LOGE("Failed to get ipv6 route info");
        goto done;
    }

    if (fgets(line, sizeof(line), f1) == NULL) {
        LOGD("IPv6 default route not available");
        goto done;
    }

    while(line[strlen(line)-1] == '\r' || line[strlen(line)-1] == '\n') {
        line[strlen(line)-1] = '\0';
    }

    retval = true;
    strscpy(ifn, line, ifn_size);
done:
    if (f1 != NULL)
        pclose(f1);

    return retval;
}


static bool
util_connectivity_router_ipv4_check(void)
{
    struct in_addr r_addr;
    bool           ret;

    if (util_get_router_ipv4(&r_addr) == false) {
        /* If we don't have a router, that's considered a failure for IPv4 */
        return false;
    }

    ret  = util_ping_cmd(inet_ntoa(r_addr), false);
    if (!ret) {
        ret = util_arping_cmd(inet_ntoa(r_addr));
        LOGI("Router check: ping ipv4 failed, arping ret = %d", ret);
    }

    return ret;
}

static bool
util_connectivity_router_ipv6_check(void)
{
    char r_6addr[128];
    bool ret;

    ret = util_get_router_ipv6(r_6addr, sizeof(r_6addr));
    if (!ret)
        return false;

    return util_ping_cmd(r_6addr, true);
}

static bool
util_connectivity_internet_ipv4_check(void) {
    bool  ret;
    int   cnt_addr;
    int   tries;
    int   r1, r2;

    cnt_addr = util_connectivity_get_inet_addr_cnt(util_connectivity_check_inet_addrs);
    tries = DEFAULT_INTERNET_CNT_CHECK;
    ret = false;

    while (tries--) {
        r1 = os_rand() % cnt_addr;
        ret = util_ping_cmd(util_connectivity_check_inet_addrs[r1], false);
        if (!ret) {
            cnt_addr = util_connectivity_get_inet_addr_cnt(util_connectivity_check_inet_ipv4_addrs);
            r2 = os_rand() % cnt_addr;
            ret = util_ping_cmd(util_connectivity_check_inet_ipv4_addrs[r2], false);
            if (!ret)
                LOGI("Internet IPv4 checking failed, dns1: %s, dns2: %s",
                     util_connectivity_check_inet_addrs[r1], util_connectivity_check_inet_ipv4_addrs[r2]);
        }
    }
    return ret;
}

static bool
util_connectivity_internet_ipv6_check(void) {
    char  ipv6_addr[256];
    char  ipv6_if[126];
    bool  ret;
    int   cnt_addr;
    int   tries;
    int   r1, r2;

    cnt_addr = util_connectivity_get_inet_addr_cnt(util_connectivity_check_inet_addrs);
    tries = DEFAULT_INTERNET_CNT_CHECK;
    ret = false;

    ret = util_get_ipv6_global_interface(ipv6_if, sizeof(ipv6_if));
    if (!ret) {
        LOGI("IPv6 global interface not available");
        return false;
    }

    while (tries--) {
        r1 = os_rand() % cnt_addr;

        ret = util_ping_cmd(util_connectivity_check_inet_addrs[r1], true);
        if (!ret) {
            cnt_addr = util_connectivity_get_inet_addr_cnt(util_connectivity_check_inet_ipv6_addrs);
            r2 = os_rand() % cnt_addr;
            snprintf(ipv6_addr, sizeof(ipv6_addr), "%s%%%s", util_connectivity_check_inet_ipv6_addrs[r2], ipv6_if);
            ret = util_ping_cmd(ipv6_addr, true);
            if (!ret)
                LOGI("Internet IPv6 checking failed, dns1: %s, dns2: %s",
                     util_connectivity_check_inet_addrs[r1], util_connectivity_check_inet_ipv6_addrs[r2]);
        }
    }
    return ret;
}

/******************************************************************************
 * target device connectivity check
 *****************************************************************************/

bool target_device_connectivity_check(const char *ifname,
                                      target_connectivity_check_t *cstate,
                                      target_connectivity_check_option_t opts)
{
    int ret;

    memset(cstate, 0 , sizeof(target_connectivity_check_t));
    ret = true;

    if (opts & LINK_CHECK) {
        WARN_ON(!target_device_wdt_ping());
        cstate->link_state = util_connectivity_link_check(ifname);
        if (!cstate->link_state)
            ret = false;
    }

    if (opts & ROUTER_CHECK) {
        WARN_ON(!target_device_wdt_ping());
        if (opts & IPV4_CHECK) {
            cstate->router_ipv4_state = util_connectivity_router_ipv4_check();
            if (!cstate->router_ipv4_state)
                ret = false;
        }
        if (opts & IPV6_CHECK) {
            cstate->router_ipv6_state = util_connectivity_router_ipv6_check();
            if (!cstate->router_ipv6_state)
                ret = false;
        }
    }

    if (opts & INTERNET_CHECK) {
        WARN_ON(!target_device_wdt_ping());
        if (opts & IPV4_CHECK) {
            cstate->internet_ipv4_state = util_connectivity_internet_ipv4_check();
            if (!cstate->internet_ipv4_state)
                ret = false;
        }
        if (opts & IPV6_CHECK) {
            cstate->internet_ipv6_state = util_connectivity_internet_ipv6_check();
            if (!cstate->internet_ipv6_state)
                ret = false;
        }
    }

    if (opts & NTP_CHECK) {
        WARN_ON(!target_device_wdt_ping());
        cstate->ntp_state = util_ntp_check();
        if (!cstate->ntp_state)
            ret = false;
    }

    return ret;
}
#endif
