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

#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "const.h"
#include "log.h"
#include "module.h"
#include "osa_assert.h"
#include "osn_types.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "wano.h"
#include "memutil.h"

#include "wano_wan.h"
#include "wanp_static_ipv4_stam.h"

typedef struct
{
    wano_plugin_handle_t      handle;
    wanp_static_ipv4_state_t  state;
    wano_plugin_status_fn_t  *status_fn;
    wano_inet_state_event_t   inet_state_watcher;
    osn_ip_addr_t             ipaddr;

    /* ping state */
    pid_t    ping_pid;
    ev_child ping_pid_watcher;
    int      ping_fd[2];
    ev_io    ping_fd_watcher;
    char     ping_buf[20148];
    size_t   ping_buf_offset;

    struct wano_wan_config  wan_config;
    bool     have_config;

    /** True if this plug-in detected a working WAN configuration */
    bool plugin_has_wan;
} wanp_static_ipv4_handle_t;

static void wanp_static_ipv4_module_start(void *data);
static void wanp_static_ipv4_module_stop(void *data);
static wano_plugin_ops_init_fn_t wanp_static_ipv4_init;
static wano_plugin_ops_run_fn_t wanp_static_ipv4_run;
static wano_plugin_ops_fini_fn_t wanp_static_ipv4_fini;
static wano_inet_state_event_fn_t wanp_static_ipv4_inet_state_event_fn;

static struct wano_plugin wanp_static_ipv4 = WANO_PLUGIN_INIT(
        "static_ipv4",
        90,
        WANO_PLUGIN_MASK_IPV4,
        wanp_static_ipv4_init,
        wanp_static_ipv4_run,
        wanp_static_ipv4_fini);

/*
 * This variable will be set to true if a plug-in instance successfully probes
 * a static IPv4 WAN configuration. In such case all other instances should fail
 * until this flag is cleared.
 */
static bool wanp_static_ipv4_wan_lock = false;

static void ping_fd_watcher_cb(struct ev_loop *loop, ev_io *w, int revents)
{
    (void)loop;
    (void)revents;

    wanp_static_ipv4_handle_t *h = CONTAINER_OF(w, wanp_static_ipv4_handle_t, ping_fd_watcher);
    const size_t remaining_len = sizeof(h->ping_buf) - h->ping_buf_offset - 1; /* Space for final \0 */
    size_t read_len;

    read_len = read(h->ping_fd[0], h->ping_buf + h->ping_buf_offset, remaining_len);
    h->ping_buf_offset += read_len > 0 ? read_len : 0;
}

static void ping_pid_watcher_cb(struct ev_loop *loop, ev_child *w, int revents)
{
    (void)loop;
    (void)revents;

    wanp_static_ipv4_handle_t *h = CONTAINER_OF(w, wanp_static_ipv4_handle_t, ping_pid_watcher);
    enum wanp_static_ipv4_action action;

    LOGD("wanp_static_ipv4: %s: ping output\n%s", h->handle.wh_ifname, h->ping_buf);

    ev_child_stop(EV_DEFAULT_ &h->ping_pid_watcher);
    h->ping_pid = 0;

    ev_io_stop(EV_DEFAULT_ &h->ping_fd_watcher);
    close(h->ping_fd[0]);
    h->ping_fd[0] = -1;
    memset(h->ping_buf, '\0', sizeof(h->ping_buf));
    h->ping_buf_offset = 0;

    if (WIFEXITED(w->rstatus) && WEXITSTATUS(w->rstatus) == 0)
    {
        LOGI("wanp_static_ipv4: %s: ping succeeded", h->handle.wh_ifname);
        action = wanp_static_ipv4_do_PING_SUCCEEDED;
    }
    else
    {
        LOGW("wanp_static_ipv4: %s: ping faild to reach the host", h->handle.wh_ifname);
        action = wanp_static_ipv4_do_PING_FAILED;
    }

    if (wanp_static_ipv4_state_do(&h->state, action, NULL) < 0)
    {
        LOG(ERR, "wanp_static_ipv4: Error sending action %s to state machine.",
            wanp_static_ipv4_action_str(action));
    }
}

static bool ping_async(wanp_static_ipv4_handle_t* h, const char* ip_addr)
{
    const char* if_name = h->handle.wh_ifname;
    pid_t pid;

    if (pipe(h->ping_fd) != 0)
    {
        LOGW("wanp_static_ipv4: %s: Failed to open pipe for ping", if_name);
        return false;
    }

    pid = fork();

    switch (pid)
    {
        case 0:
            close(STDOUT_FILENO);
            close(h->ping_fd[0]);
            dup2(h->ping_fd[1], STDOUT_FILENO);
            execlp("ping", "ping", ip_addr, "-c", "5", "-I", if_name, NULL);
            LOGW("wanp_static_ipv4: %s: Failed to run ping in forked process", if_name);
            exit(1);
        case -1:
            LOGW("wanp_static_ipv4: %s: Cannot run ping, fork failed", if_name);
            return false;
        default:
            h->ping_pid = pid;
            ev_child_init(&h->ping_pid_watcher, ping_pid_watcher_cb, pid, 0);
            ev_child_start(EV_DEFAULT_ &h->ping_pid_watcher);

            close(h->ping_fd[1]);
            h->ping_fd[1] = -1;
            ev_io_init(&h->ping_fd_watcher, ping_fd_watcher_cb, h->ping_fd[0], EV_READ);
            ev_io_start(EV_DEFAULT_ &h->ping_fd_watcher);
            return true;
    }
}

void ping_stop(wanp_static_ipv4_handle_t *h)
{
    if (h->ping_pid > 0)
    {
        ev_child_stop(EV_DEFAULT_ &h->ping_pid_watcher);
        kill(h->ping_pid, SIGKILL);
        waitpid(h->ping_pid, NULL, 0);
    }

    ev_io_stop(EV_DEFAULT_ &h->ping_fd_watcher);
    if (h->ping_fd[0] >= 0) close(h->ping_fd[0]);
    if (h->ping_fd[1] >= 0) close(h->ping_fd[1]);
}

/*
 * ===========================================================================
 *  State Machine
 * ===========================================================================
 */
enum wanp_static_ipv4_state wanp_static_ipv4_state_INIT(
        wanp_static_ipv4_state_t *state,
        enum wanp_static_ipv4_action action,
        void *data)
{
    (void)state;
    (void)action;
    (void)data;

    return wanp_static_ipv4_CONFIGURE_IP;
}

enum wanp_static_ipv4_state wanp_static_ipv4_state_CONFIGURE_IP(
        wanp_static_ipv4_state_t *state,
        enum wanp_static_ipv4_action action,
        void *data)
{
    char ipaddr[C_IP4ADDR_LEN];
    char netmask[C_IP4ADDR_LEN];

    wanp_static_ipv4_handle_t *h = CONTAINER_OF(state, wanp_static_ipv4_handle_t, state);
    struct wano_inet_state *is = data;
    struct wano_wan_config_static_ipv4 *pwc = &h->wan_config.wc_type_static_ipv4;

    switch (action)
    {
        case wanp_static_ipv4_do_STATE_INIT:
            snprintf(ipaddr, sizeof(ipaddr),
                    PRI(osn_ip_addr),
                    FMT(osn_ip_addr, pwc->wc_ipaddr));

            snprintf(netmask, sizeof(netmask),
                    PRI(osn_ip_addr),
                    FMT(osn_ip_addr, pwc->wc_netmask));

            LOG(INFO, "wanp_static_ipv4: %s: Setting ip_addr=%s netmask=%s",
                    h->handle.wh_ifname,
                    ipaddr,
                    netmask);

            WANO_INET_CONFIG_UPDATE(
                    h->handle.wh_ifname,
                    .enabled = WANO_TRI_TRUE,
                    .network = WANO_TRI_TRUE,
                    .ip_assign_scheme = "static",
                    .inet_addr = ipaddr,
                    .netmask = netmask);

            wano_inet_state_event_refresh(&h->inet_state_watcher);
            break;

        case wanp_static_ipv4_do_INET_STATE_UPDATE:
            LOG(DEBUG, "static_ipv4:%s: enabled:%d network:%d scheme:%s "
                    "ipaddr:"PRI_osn_ip_addr" "
                    "netmask:"PRI_osn_ip_addr,
                    h->handle.wh_ifname,
                    is->is_enabled,
                    is->is_network,
                    is->is_ip_assign_scheme,
                    FMT_osn_ip_addr(is->is_ipaddr),
                    FMT_osn_ip_addr(is->is_netmask));
            if (is->is_enabled != true) break;
            if (is->is_network != true) break;
            if (strcmp(is->is_ip_assign_scheme, "static") != 0) break;
            if (memcmp(&is->is_ipaddr, &pwc->wc_ipaddr, sizeof(is->is_ipaddr)) != 0) break;
            if (memcmp(&is->is_netmask, &pwc->wc_netmask, sizeof(is->is_netmask)) != 0) break;
            return wanp_static_ipv4_PROBE;

        default:
            break;
    }

    return 0;
}

enum wanp_static_ipv4_state wanp_static_ipv4_state_PROBE(
        wanp_static_ipv4_state_t *state,
        enum wanp_static_ipv4_action action,
        void *data)
{
    wanp_static_ipv4_handle_t *h;

    (void)data;

    h = CONTAINER_OF(state, wanp_static_ipv4_handle_t, state);

    switch (action)
    {
        case wanp_static_ipv4_do_STATE_INIT:
        case wanp_static_ipv4_do_PING_FAILED:
            {
                char sgateway[C_IP4ADDR_LEN];

                snprintf(sgateway, sizeof(sgateway),
                        PRI(osn_ip_addr),
                        FMT(osn_ip_addr, h->wan_config.wc_type_static_ipv4.wc_gateway));

                LOG(INFO, "wanp_static_ipv4: %s: Pinging %s", h->handle.wh_ifname, sgateway);
                if (ping_async(h, sgateway) == false)
                {
                    /* Ping failed, inform upper layers */
                    h->status_fn(&h->handle, &WANO_PLUGIN_STATUS(WANP_ERROR));
                }
                return 0;
            }

        case wanp_static_ipv4_do_PING_SUCCEEDED:
            return wanp_static_ipv4_CONFIGURE_GW;

        default:
            break;
    }

    return 0;
}

enum wanp_static_ipv4_state wanp_static_ipv4_state_CONFIGURE_GW(
        wanp_static_ipv4_state_t *state,
        enum wanp_static_ipv4_action action,
        void *data)
{
    char gateway[C_IP4ADDR_LEN];
    char dns1[C_IP4ADDR_LEN];
    char dns2[C_IP4ADDR_LEN];

    wanp_static_ipv4_handle_t *h = CONTAINER_OF(state, wanp_static_ipv4_handle_t, state);
    struct wano_inet_state *is = data;
    struct wano_wan_config_static_ipv4 *pwc = &h->wan_config.wc_type_static_ipv4;

    switch (action)
    {
        case wanp_static_ipv4_do_STATE_INIT:
            snprintf(gateway, sizeof(gateway),
                    PRI(osn_ip_addr),
                    FMT(osn_ip_addr, pwc->wc_gateway));

            snprintf(dns1, sizeof(dns1),
                    PRI(osn_ip_addr),
                    FMT(osn_ip_addr, pwc->wc_primary_dns));

            dns2[0] = '\0';
            if (osn_ip_addr_cmp(&pwc->wc_secondary_dns, &OSN_IP_ADDR_INIT) != 0)
            {
                snprintf(dns2, sizeof(dns2),
                        PRI(osn_ip_addr),
                        FMT(osn_ip_addr, pwc->wc_secondary_dns));
            }

            LOG(INFO, "wanp_static_ipv4: %s: Setting gateway=%s dns1=%s dns2=%s",
                    h->handle.wh_ifname,
                    gateway,
                    dns1,
                    dns2[0] == '\0' ?"(none)" : dns2);

            WANO_INET_CONFIG_UPDATE(
                    h->handle.wh_ifname,
                    .enabled = WANO_TRI_TRUE,
                    .network = WANO_TRI_TRUE,
                    .gateway = gateway,
                    .dns1 = dns1,
                    .dns2 = dns2[0] == '\0' ? NULL : dns2);

            wano_inet_state_event_refresh(&h->inet_state_watcher);
            break;

        case wanp_static_ipv4_do_INET_STATE_UPDATE:
            LOG(DEBUG, "static_ipv4:%s: "
                    "gateway:"PRI_osn_ip_addr" "
                    "dns1:"PRI_osn_ip_addr" "
                    "dns2:"PRI_osn_ip_addr,
                    h->handle.wh_ifname,
                    FMT_osn_ip_addr(is->is_gateway),
                    FMT_osn_ip_addr(is->is_dns1),
                    FMT_osn_ip_addr(is->is_dns2));
            if (memcmp(&is->is_gateway, &pwc->wc_gateway, sizeof(is->is_gateway)) != 0) break;
            if (memcmp(&is->is_dns1, &pwc->wc_primary_dns, sizeof(is->is_dns1)) != 0) break;
            if (memcmp(&is->is_dns2, &pwc->wc_secondary_dns, sizeof(is->is_dns2)) != 0) break;

            wano_inet_state_event_fini(&h->inet_state_watcher);
            return wanp_static_ipv4_RUNNING;

        default:
            break;
    }

    return 0;
}

enum wanp_static_ipv4_state wanp_static_ipv4_state_RUNNING(
        wanp_static_ipv4_state_t *state,
        enum wanp_static_ipv4_action action,
        void *data)
{
    (void)action;
    (void)data;

    wanp_static_ipv4_handle_t *h = CONTAINER_OF(state, wanp_static_ipv4_handle_t, state);

    if (action == wanp_static_ipv4_do_STATE_INIT)
    {
        struct wano_plugin_status ws = WANO_PLUGIN_STATUS(WANP_OK);

        if (h->have_config)
        {
            wano_wan_status_set(
                    wano_wan_from_plugin_handle(&h->handle),
                    WC_TYPE_STATIC_IPV4,
                    WC_STATUS_SUCCESS);
        }

        h->plugin_has_wan = true;
        wanp_static_ipv4_wan_lock = true;
        h->status_fn(&h->handle, &ws);
    }

    return 0;
}

enum wanp_static_ipv4_state wanp_static_ipv4_state_EXCEPTION(
        wanp_static_ipv4_state_t *state,
        enum wanp_static_ipv4_action action,
        void *data)
{
    (void)action;
    (void)data;

    LOG(INFO, "static_ipv4_EXCEPTION: %s", wanp_static_ipv4_action_str(action));

    wanp_static_ipv4_handle_t *h = CONTAINER_OF(state, wanp_static_ipv4_handle_t, state);

    ping_stop(h);

    return 0;
}

/*
 * ===========================================================================
 *  Plugin implementation
 * ===========================================================================
 */
wano_plugin_handle_t *wanp_static_ipv4_init(
        const struct wano_plugin *wp,
        const char *ifname,
        wano_plugin_status_fn_t *status_fn)
{
    wanp_static_ipv4_handle_t *h;

    if (wanp_static_ipv4_wan_lock)
    {
        LOG(INFO, "static_ipv4: %s: Another plug-in instance is active.",
                ifname);
        return NULL;
    }

    h = CALLOC(1, sizeof(wanp_static_ipv4_handle_t));

    h->handle.wh_plugin = wp;
    STRSCPY(h->handle.wh_ifname, ifname);
    h->status_fn = status_fn;

    h->ping_fd[0] = -1;
    h->ping_fd[1] = -1;

    return &h->handle;
}

void wanp_static_ipv4_run(wano_plugin_handle_t *wh)
{
    wanp_static_ipv4_handle_t *wsh = CONTAINER_OF(wh, wanp_static_ipv4_handle_t, handle);

    /*
     * Load static configuration
     */
    wsh->have_config = wano_wan_config_get(
                wano_wan_from_plugin_handle(wh),
                WC_TYPE_STATIC_IPV4,
                &wsh->wan_config);
    if (!wsh->have_config)
    {
        LOG(NOTICE, "static_ipv4: No static IPv4 configuration present, skipping.");
        wsh->status_fn( wh, &WANO_PLUGIN_STATUS(WANP_SKIP));
        return;
    }

    /* Register to Wifi_Inet_State events, this will also kick-off the state machine */
    wano_inet_state_event_init(
            &wsh->inet_state_watcher,
            wsh->handle.wh_ifname,
            wanp_static_ipv4_inet_state_event_fn);
}

void wanp_static_ipv4_fini(wano_plugin_handle_t *wh)
{
    wanp_static_ipv4_handle_t *wsh = CONTAINER_OF(wh, wanp_static_ipv4_handle_t, handle);

    /* If this plug-in holds the global WAN lock, release it */
    if (wsh->plugin_has_wan)
    {
        wanp_static_ipv4_wan_lock = false;
    }

    if (wsh->have_config)
    {
        wano_wan_status_set(
                wano_wan_from_plugin_handle(&wsh->handle),
                WC_TYPE_STATIC_IPV4,
                WC_STATUS_ERROR);
    }

    ping_stop(wsh);
    wano_inet_state_event_fini(&wsh->inet_state_watcher);

    FREE(wsh);
}

/*
 * ===========================================================================
 *  Misc
 * ===========================================================================
 */
void wanp_static_ipv4_inet_state_event_fn(
        wano_inet_state_event_t *ise,
        struct wano_inet_state *is)
{
    wanp_static_ipv4_handle_t *h = CONTAINER_OF(ise, wanp_static_ipv4_handle_t, inet_state_watcher);

    h->ipaddr = is->is_ipaddr;

    if (wanp_static_ipv4_state_do(&h->state, wanp_static_ipv4_do_INET_STATE_UPDATE, is) < 0)
    {
        LOG(ERR, "wanp_static_ipv4: Error sending action INET_STATE_UPDATE to state machine.");
    }
}

/*
 * ===========================================================================
 *  Module Support
 * ===========================================================================
 */
void wanp_static_ipv4_module_start(void *data)
{
    (void)data;
    wano_plugin_register(&wanp_static_ipv4);
}

void wanp_static_ipv4_module_stop(void *data)
{
    (void)data;
    wano_plugin_unregister(&wanp_static_ipv4);
}

MODULE(wanp_static_ipv4, wanp_static_ipv4_module_start, wanp_static_ipv4_module_stop)
