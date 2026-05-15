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
#include <stdint.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <limits.h>

#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <sys/socket.h>

#include <errno.h>
#include "log.h"
#include "util.h"
#include "ovsdb.h"
#include "module.h"
#include "memutil.h"

#include "pm_erp.h"

#define MODULE_ID                     LOG_MODULE_ID_MAIN
#define CONFIG_PM_ERP_PERIOD_INTERVAL 5.0

MODULE(pm_erp, pm_erp_init, pm_erp_fini);

static struct osp_erp_ctx erp_ctx = {0};

static struct iface_list ifcw_list;
static struct iface_list ifce_list;

static void check_and_do_action(void);
static void pm_erp_set_mode(bool enable);

static int erp_active_flag = 0;

static int eth_set_100mb_fixed(const char *ifname)
{
    int fd;
    struct ifreq ifr;
    struct ethtool_cmd ec;
    struct ethtool_value eval;

    if (!ifname || ifname[0] == '\0') return -1;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        LOGE("ERP: eth_set_100mb_fixed: socket error: %s", strerror(errno));
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    // 1. GET current settings
    memset(&ec, 0, sizeof(ec));
    ec.cmd = ETHTOOL_GSET;
    ifr.ifr_data = (void *)&ec;

    if (ioctl(fd, SIOCETHTOOL, &ifr) < 0)
    {
        LOGE("ERP: ETHTOOL_GSET GET error: %s", strerror(errno));
        close(fd);
        return -1;
    }

    // 2. SET to 100Mb/s full duplex, autoneg off
    ec.cmd = ETHTOOL_SSET;
    ec.speed = SPEED_100;
    ec.duplex = DUPLEX_FULL;
    ec.autoneg = AUTONEG_DISABLE;
    ec.advertising = 0;  // if autoneg is off should be 0

    ifr.ifr_data = (void *)&ec;
    if (ioctl(fd, SIOCETHTOOL, &ifr) < 0)
    {
        LOGE("ERP: ETHTOOL_SSET SET error: %s", strerror(errno));
        close(fd);
        return -1;
    }

    // 3. NWAY reset (force renegotiation / link flap)
    memset(&eval, 0, sizeof(eval));
    eval.cmd = ETHTOOL_NWAY_RST;
    eval.data = 0;
    ifr.ifr_data = (void *)&eval;

    if (ioctl(fd, SIOCETHTOOL, &ifr) < 0)
    {
        // It is very likely that the command was successful, even though it returned ENOTSUP.
        if (errno != ENOTSUP) LOGE("ERP: ETHTOOL_NWAY_RST error: %s", strerror(errno));
    }
    LOGI("ERP: Interface %s set to 100Mb/s, full duplex, autoneg off\n", ifname);
    close(fd);
    return 0;
}

static int eth_restore_autoneg(const char *ifname)
{
    if (!ifname || ifname[0] == '\0') return -1;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "/usr/sbin/ethtool -s %s autoneg on", ifname);

    LOGD("ERP: Interface %s autonegotiation enabling cmd:%s ...\n", ifname, cmd);
    FILE *fp = popen(cmd, "r");
    if (!fp)
    {
        LOGE("ERP: popen failed error: %s", strerror(errno));
        return -1;
    }

    int status = pclose(fp);
    if (status == -1)
    {
        LOGE("ERP: pclose failed error: %s", strerror(errno));
        return -1;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        LOGD("ERP: Interface %s autonegotiation enabled successfully.\n", ifname);
        return 0;
    }
    else
    {
        LOGE("ERP: command %s failed", cmd);
        return -1;
    }
}

#ifdef CONFIG_PM_ERP_CFG_TOOL
static int pm_erp_set_radio_rxchainmask(const char *ifname, const char *command, unsigned int chainmask)
{
    if (!ifname || ifname[0] == '\0') return -1;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), CONFIG_PM_ERP_CFG_TOOL " %s %s %d", ifname, command, chainmask);

    LOGD("ERP: Changing mask for %s cmd:%s ...\n", ifname, cmd);
    FILE *fp = popen(cmd, "r");
    if (!fp)
    {
        LOGE("ERP: popen failed error: %s", strerror(errno));
        return -1;
    }

    int status = pclose(fp);
    if (status == -1)
    {
        LOGE("ERP: pclose failed error: %s", strerror(errno));
        return -1;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        LOGD("ERP: Interface %s rxchainmask changed \n", ifname);
        return 0;
    }
    else
    {
        LOGE("ERP: command %s failed", cmd);
        return -1;
    }
}
#endif

static void pm_erp_cb(struct ev_loop *loop, ev_timer *timer, int revents)
{
    (void)loop;
    (void)revents;
    (void)timer;
#ifdef ERP_DEBUG
    unsigned int i;

    LOGD("ERP: In erp CB revents=%d\n", revents);
    for (i = 0; i < ifcw_list.count; i++)
    {
        LOGD("ERP read from list if_name %s, tx_chainmas=%d",
             ifcw_list.entries[i].if_name,
             ifcw_list.entries[i].chainmask);
    }
#endif

    if (pm_erp_ovsdb_erp_mode_enabled() || erp_active_flag)
        check_and_do_action();
    else
    {
        LOGD("ERP: ERP mode disabled");
        pm_erp_set_node_state("ERP", "erp_mode", "disabled");
    }
}

void pm_erp_init(void *data)
{
    LOGN("Initializing ERP");

    if (pm_erp_ovsdb_init(&erp_ctx) != 0)
    {
        LOGE("Initializing ERP (failed to initialize TM OVSDB)");
        return;
    }

    // init timer for main erp loop
    LOGI("ERP: Starting ERP timer");
    ev_timer_init(&erp_ctx.erp_timer, pm_erp_cb, 1.0, (double)CONFIG_PM_ERP_PERIOD_INTERVAL);
    ev_timer_start(EV_DEFAULT, &erp_ctx.erp_timer);
    erp_ctx.erp_timer.data = &erp_ctx;
    pm_erp_ovsdb_get_radio_interfaces(&ifcw_list);
    pm_erp_ovsdb_get_eth_interfaces(&ifce_list);
}

void pm_erp_send_event(void)
{
    ev_feed_event(EV_DEFAULT, &erp_ctx.erp_timer, EV_TIMER);
    LOGD("ERP: Event feed");
}

static void pm_erp_set_erp_masks(bool set)
{
    unsigned int i;
    for (i = 0; i < ifcw_list.count; i++)
    {
        usleep(100000);
        if (set == true)
        {
#ifdef CONFIG_PM_ERP_CFG_TOOL
            pm_erp_set_radio_rxchainmask(ifcw_list.entries[i].if_name, "txchainmask", 1);
            pm_erp_set_radio_rxchainmask(ifcw_list.entries[i].if_name, "rxchainmask", 1);
            usleep(100000);
#endif
            if (pm_erp_ovsdb_set_radio_txchainmask(ifcw_list.entries[i].if_name, 1))
                LOGE("ERP: Failed to set chainmask to 1 for %s\n", ifcw_list.entries[i].if_name);
        }
        else
        {
#ifdef CONFIG_PM_ERP_CFG_TOOL
            pm_erp_set_radio_rxchainmask(ifcw_list.entries[i].if_name, "txchainmask", ifcw_list.entries[i].chainmask);
            pm_erp_set_radio_rxchainmask(ifcw_list.entries[i].if_name, "rxchainmask", ifcw_list.entries[i].chainmask);
            usleep(100000);
#endif
            if (pm_erp_ovsdb_set_radio_txchainmask(ifcw_list.entries[i].if_name, ifcw_list.entries[i].chainmask))
                LOGE("ERP: Failed to restore original chainmask for %s\n", ifcw_list.entries[i].if_name);
        }
    }
}

static void pm_erp_reduce_eth_speed(bool set)
{
    unsigned int i;
    usleep(100000);
    for (i = 0; i < ifce_list.count; i++)
    {
        usleep(100000);
        if (set == true)
            eth_set_100mb_fixed(ifce_list.entries[i].if_name);
        else
            eth_restore_autoneg(ifce_list.entries[i].if_name);
    }
}

static void check_and_do_action(void)
{
    static bool prev_state = false;
    bool curr_state;
    static unsigned int erp_time = 0;
    static unsigned int cnt = 0;
    static int restore = 0;

    if (pm_erp_ovsdb_erp_mode_ready())
    {
        erp_time += ((unsigned int)CONFIG_PM_ERP_PERIOD_INTERVAL);
    }
    else
    {
        erp_time = 0;
        cnt++;
    }
    if (erp_time > CONFIG_PM_ERP_TIMEOUT)
    {
        curr_state = true;
        pm_erp_set_node_state("ERP", "erp_mode", "active");
    }
    else
    {
        curr_state = false;
        pm_erp_set_node_state("ERP", "erp_mode", "enabled");
    }
    LOGD("ERP: In erp CB  check_action erp_time=%d  curr_state=%d, prev_state=%d\n", erp_time, curr_state, prev_state);
    if (curr_state != prev_state)
    {
        if (curr_state)
        {
            pm_erp_ovsdb_get_radio_interfaces(&ifcw_list);
            LOGI("ERP: Entering ErP mode ...");
            LOGD("ERP: Read old chainmasks");
            if (pm_erp_get_mask() & RX_CHAINMASK)
            {
                usleep(500000);
                pm_erp_set_mode(true);
                LOGI("ERP: Set chainmasks to 1");
                usleep(500000);
                pm_erp_set_erp_masks(true);
                erp_active_flag |= RX_CHAINMASK;
            }
            if (pm_erp_get_mask() & ETH_PORT)
            {
                usleep(500000);
                pm_erp_reduce_eth_speed(true);
                erp_active_flag |= ETH_PORT;
            }
        }
        else
        {
            LOGI("ERP: Exiting ErP mode ...");
            if (pm_erp_get_mask() & RX_CHAINMASK)
            {
                LOGI("ERP: Set chainmasks back to original");
                pm_erp_set_erp_masks(false);
                erp_active_flag &= ~RX_CHAINMASK;
            }
            restore = 1;
            cnt = 0;
        }
        prev_state = curr_state;
    }
    if ((cnt > 2) && (restore == 1))
    {
        restore = 0;
        pm_erp_set_mode(false);
        LOGD("ERP: restored erp mode to 0");
        if (pm_erp_get_mask() & ETH_PORT)
        {
            pm_erp_reduce_eth_speed(false);
            erp_active_flag &= ~ETH_PORT;
        }
    }
}

#ifdef CONFIG_PM_ERP_CFG_TOOL
static bool erp_mode_activity = false;
static void pm_erp_set_mode(bool enable)
{
    erp_mode_activity = enable;
}
#else
int pm_erp_set_mode(bool enable)
{
    FILE *f = fopen(CONFIG_PM_ERP_MODE_PATH, "w");
    if (!f)
    {
        LOGE("Cannot open %s", CONFIG_PM_ERP_MODE_PATH);
        return -1;
    }

    if (fprintf(f, "%d\n", enable ? 1 : 0) < 0)
    {
        fclose(f);
        return -1;
    }

    fclose(f);

    return 0;
}
#endif

#ifdef CONFIG_PM_ERP_CFG_TOOL
bool pm_erp_is_active(void)
{
    return erp_mode_activity;
}
#else
bool pm_erp_is_active(void)
{
    FILE *f = fopen(CONFIG_PM_ERP_MODE_PATH, "r");
    if (!f)
    {
        LOGE("ERP: pm_erp_is_active fopen error");
        return false;
    }

    char buf[16];
    if (!fgets(buf, sizeof(buf), f))
    {
        fclose(f);
        return false;
    }
    fclose(f);

    int val = atoi(buf);
    return val == 1;
}
#endif

void pm_erp_fini(void *data)
{
    LOGN("Deinitializing ERP");
    FREE(ifcw_list.entries);
    FREE(ifce_list.entries);
}
