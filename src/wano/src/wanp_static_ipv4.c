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

#include "log.h"
#include "module.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "wano.h"
#include "osa_assert.h"
#include "osp_ps.h"

#include "wano_localconfig.h"
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

    /* Local config */
    struct wano_localconfig localconfig;

    osn_ip_addr_t ip_addr;
    osn_ip_addr_t netmask;
    osn_ip_addr_t gateway;
    osn_ip_addr_t dns1;
    osn_ip_addr_t dns2;

    /** True if this plug-in detected a working WAN configuration */
    bool plugin_has_wan;
} wanp_static_ipv4_handle_t;

static void wanp_static_ipv4_module_start(void);
static void wanp_static_ipv4_module_stop(void);
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
        LOGI("wanp_static_ipv4: %s: ping succedded", h->handle.wh_ifname);
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
    wanp_static_ipv4_handle_t *h = CONTAINER_OF(state, wanp_static_ipv4_handle_t, state);
    struct wano_inet_state *is = data;

    const char *dns2 = NULL;

    switch (action)
    {
        case wanp_static_ipv4_do_STATE_INIT:
            LOG(INFO, "wanp_static_ipv4: %s: Setting ip_addr=%s netmask=%s gateway=%s",
                    h->handle.wh_ifname,
                    h->localconfig.staticIPv4.ip,
                    h->localconfig.staticIPv4.subnet,
                    h->localconfig.staticIPv4.gateway);

            if (h->localconfig.staticIPv4.secondaryDns[0] != '\0')
            {
                dns2 = h->localconfig.staticIPv4.secondaryDns;
            }

            WANO_INET_CONFIG_UPDATE(
                    h->handle.wh_ifname,
                    .enabled = WANO_TRI_TRUE,
                    .network = WANO_TRI_TRUE,
                    .ip_assign_scheme = "static",
                    .inet_addr = h->localconfig.staticIPv4.ip,
                    .netmask = h->localconfig.staticIPv4.subnet,
                    .gateway = h->localconfig.staticIPv4.gateway,
                    .dns1 = h->localconfig.staticIPv4.primaryDns,
                    .dns2 = dns2);

            wano_inet_state_event_refresh(&h->inet_state_watcher);
            break;

        case wanp_static_ipv4_do_INET_STATE_UPDATE:
            LOG(DEBUG, "static_ipv4:%s: enabled:%d network:%d scheme:%s "
                    "ipaddr:"PRI_osn_ip_addr" "
                    "netmask:"PRI_osn_ip_addr" "
                    "gateway:"PRI_osn_ip_addr" "
                    "dns1:"PRI_osn_ip_addr" "
                    "dns2:"PRI_osn_ip_addr,
                    h->handle.wh_ifname,
                    is->is_enabled,
                    is->is_network,
                    is->is_ip_assign_scheme,
                    FMT_osn_ip_addr(is->is_ipaddr),
                    FMT_osn_ip_addr(is->is_netmask),
                    FMT_osn_ip_addr(is->is_gateway),
                    FMT_osn_ip_addr(is->is_dns1),
                    FMT_osn_ip_addr(is->is_dns2));
            if (is->is_enabled != true) break;
            if (is->is_network != true) break;
            if (strcmp(is->is_ip_assign_scheme, "static") != 0) break;
            if (memcmp(&is->is_ipaddr, &h->ip_addr, sizeof(h->ip_addr)) != 0) break;
            if (memcmp(&is->is_netmask, &h->netmask, sizeof(h->netmask)) != 0) break;
            if (memcmp(&is->is_gateway, &h->gateway, sizeof(h->gateway)) != 0) break;
            if (memcmp(&is->is_dns1, &h->dns1, sizeof(h->dns1)) != 0) break;
            if (memcmp(&is->is_dns2, &h->dns2, sizeof(h->dns2)) != 0) break;

            wano_inet_state_event_fini(&h->inet_state_watcher);
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
                LOG(INFO, "wanp_static_ipv4: %s: Pinging %s", h->handle.wh_ifname, h->localconfig.staticIPv4.gateway);
                if (ping_async(h, h->localconfig.staticIPv4.gateway) == false)
                {
                    /* Ping failed, inform upper layers */
                    h->status_fn(&h->handle, &WANO_PLUGIN_STATUS(WANP_ERROR));
                }
                return 0;
            }

        case wanp_static_ipv4_do_PING_SUCCEEDED:
            return wanp_static_ipv4_RUNNING;

        default:
            return 0;
    }

    return wanp_static_ipv4_RUNNING;
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
    (void)state;
    (void)action;
    (void)data;

    LOG(INFO, "static_ipv4_EXCEPTION: %s", wanp_static_ipv4_action_str(action));

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

    h = calloc(1, sizeof(wanp_static_ipv4_handle_t));
    ASSERT(h != NULL, "Error allocating static_ipv4 object")

    h->handle.wh_plugin = wp;
    STRSCPY(h->handle.wh_ifname, ifname);
    h->status_fn = status_fn;

    h->ping_fd[0] = -1;
    h->ping_fd[1] = -1;

    h->ip_addr = OSN_IP_ADDR_INIT;
    h->netmask = OSN_IP_ADDR_INIT;
    h->gateway = OSN_IP_ADDR_INIT;
    h->dns1 = OSN_IP_ADDR_INIT;
    h->dns2 = OSN_IP_ADDR_INIT;

    /*
     * Load static configration
     */
    if (!wano_localconfig_load(&h->localconfig) || !h->localconfig.staticIPv4_exists)
    {
        LOG(NOTICE, "static_ipv4: No static IPv4 configuration present, skipping.");
        goto error;
    }

    /*
     * Verify that the IPs are valid
     */
    if (!osn_ip_addr_from_str(&h->gateway, h->localconfig.staticIPv4.gateway))
    {
        LOG(ERR, "static_ipv4: Invalid gateway configuration: %s", h->localconfig.staticIPv4.gateway);
        goto error;
    }

    if (!osn_ip_addr_from_str(&h->ip_addr, h->localconfig.staticIPv4.ip))
    {
        LOG(ERR, "static_ipv4: Invalid IP address configuration: %s", h->localconfig.staticIPv4.ip);
        goto error;
    }

    if (!osn_ip_addr_from_str(&h->netmask, h->localconfig.staticIPv4.subnet))
    {
        LOG(ERR, "static_ipv4: Invalid IP subnet configuration: %s", h->localconfig.staticIPv4.subnet);
        goto error;
    }

    if (!osn_ip_addr_from_str(&h->dns1, h->localconfig.staticIPv4.primaryDns))
    {
        LOG(ERR, "static_ipv4: Invalid DNS configuration: %s", h->localconfig.staticIPv4.primaryDns);
        goto error;
    }

    /* secondaryDNS is optional, do not abort if it cannot be parsed */
    if (h->localconfig.staticIPv4.secondaryDns[0] != '\0' &&
            !osn_ip_addr_from_str(&h->dns2, h->localconfig.staticIPv4.secondaryDns))
    {
        LOG(WARN, "static_ipv4: Invalid secondary DNS configuration: %s", h->localconfig.staticIPv4.secondaryDns);
    }

    return &h->handle;

error:
    if (h != NULL) free(h);
    return NULL;
}

void wanp_static_ipv4_run(wano_plugin_handle_t *wh)
{
    wanp_static_ipv4_handle_t *wsh = CONTAINER_OF(wh, wanp_static_ipv4_handle_t, handle);

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

    if (wsh->ping_pid > 0)
    {
        ev_child_stop(EV_DEFAULT_ &wsh->ping_pid_watcher);
        kill(wsh->ping_pid, SIGILL);
        waitpid(wsh->ping_pid, NULL, 0);
    }

    wano_inet_state_event_fini(&wsh->inet_state_watcher);

    ev_io_stop(EV_DEFAULT_ &wsh->ping_fd_watcher);
    close(wsh->ping_fd[0]);
    close(wsh->ping_fd[1]);

    free(wh);
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
void wanp_static_ipv4_module_start(void)
{
    wano_plugin_register(&wanp_static_ipv4);
}

void wanp_static_ipv4_module_stop(void)
{
    wano_plugin_unregister(&wanp_static_ipv4);
}

MODULE(wanp_static_ipv4, wanp_static_ipv4_module_start, wanp_static_ipv4_module_stop)
