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
#include "target.h"
#include "cm2.h"

/* WATCHDOG CONFIGURATION */
#define CM2_WDT_INTERVAL                10

/* CONNECTIVITY CHECK CONFIGURATION */
#define CM2_STABILITY_INTERVAL          10
#define CM2_STABILITY_THRESHOLD         5

cm2_main_link_type cm2_util_get_link_type()
{
    cm2_main_link_type type = CM2_LINK_NOT_DEFINED;

    if (!g_state.link.is_used)
        return type;

    if (!strcmp(g_state.link.if_type, "eth")) {
        if (cm2_ovsdb_is_port_name("patch-w2h"))
           type = CM2_LINK_ETH_BRIDGE;
        else
           type = CM2_LINK_ETH_ROUTER;
    }

    if (!strcmp(g_state.link.if_type, "gre")) {
        type = CM2_LINK_GRE;
    }

    return type;
}

void cm2_connection_stability_check()
{
    struct schema_Connection_Manager_Uplink con;
    target_connectivity_check_t             cstate;
    bool                                    ret;
    int                                     counter;

    if (!cm2_is_extender()) {
        return;
    }

    //TODO for all active links
    const char *if_name = g_state.link.if_name;

    if (!g_state.link.is_used) {
        LOGN("%s Waiting for new active link", __func__);
        return;
    }

    ret = cm2_ovsdb_connection_get_connection_by_ifname(if_name, &con);
    if (!ret) {
        LOGW("%s interface does not exist", __func__);
        return;
    }

    ret = target_device_connectivity_check(if_name, &cstate);
    LOGN("%s status %d", __func__, ret);

    counter = 0;
    if (!cstate.link_state) {
        LOGW("%s link is broken", __func__);
        counter = con.unreachable_link_counter + 1;
    }
    ret = cm2_ovsdb_connection_update_unreachable_link_counter(if_name, counter);
    if (!ret)
        LOGW("%s Failed update link counter in ovsdb table", __func__);

    counter = 0;
    if (!cstate.router_state) {
        LOGW("%s router is broken", __func__);
        counter =  con.unreachable_router_counter + 1;
    }
    ret = cm2_ovsdb_connection_update_unreachable_router_counter(if_name, counter);
    if (!ret)
        LOGW("%s Failed update router counter in ovsdb table", __func__);

    if (cm2_util_get_link_type() != CM2_LINK_ETH_ROUTER &&
        con.unreachable_router_counter + 1 > CM2_STABILITY_THRESHOLD) {
        LOGW("%s Restart managers due to exceeding the threshold router failures", __func__);
        target_device_restart_managers();
    }

    counter = 0;
    if (!cstate.internet_state) {
        LOGW("%s internet connection is broken", __func__);
        counter = con.unreachable_internet_counter + 1;
    }
    ret = cm2_ovsdb_connection_update_unreachable_internet_counter(if_name, counter);
    if (!ret)
        LOGW("%s Failed update internet counter in ovsdb table", __func__);

    ret = cm2_ovsdb_connection_update_ntp_state(if_name,
                                                cstate.ntp_state);
    if (!ret)
        LOGW("%s Failed update ntp state in ovsdb table", __func__);

    return;
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
    ev_timer_init(&g_state.stability_timer, cm2_stability_cb, CM2_STABILITY_INTERVAL, CM2_STABILITY_INTERVAL);
    g_state.stability_timer.data = NULL;
    ev_timer_start(g_state.loop, &g_state.stability_timer);
}

void cm2_stability_close(struct ev_loop *loop)
{
    LOGI("Stopping stability check");
    ev_timer_stop (loop, &g_state.stability_timer);
}

void cm2_wdt_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;

    target_device_wdt_ping();
}

void cm2_wdt_init(struct ev_loop *loop)
{
    LOGD("Initializing WDT connection");
    ev_timer_init(&g_state.wdt_timer, cm2_wdt_cb, CM2_WDT_INTERVAL, CM2_WDT_INTERVAL);
    g_state.wdt_timer.data = NULL;
    ev_timer_start(g_state.loop, &g_state.wdt_timer);
}

void cm2_wdt_close(struct ev_loop *loop)
{
    LOGI("Stopping WDT");
    (void)loop;
}
