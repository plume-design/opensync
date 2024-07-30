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

#include "util.h"
#include "log.h"
#include "osn_types.h"
#include "const.h"
#include "execsh.h"
#include "ev.h"
#include "ds.h"

#include "osn_vpn.h"

struct osn_vpn
{
    char                         ov_name[C_MAXPATH_LEN];    /* Tunnel name */

    bool                         ov_tunnel_enable;          /* Enable/disable tunnel */

    bool                         ov_healthc_enable;         /* Healthcheck enable */
    osn_ipany_addr_t             ov_healthc_ip;             /* Healthcheck IP */
    int                          ov_healthc_interval;       /* Healthcheck interval */
    int                          ov_healthc_timeout;        /* Healthcheck timeout */
    char                        *ov_healthc_src;            /* Healthcheck source IP or interface */

    enum osn_vpn_health_status   ov_health_status;          /* Healthcheck status */

    osn_vpn_health_status_fn_t  *ov_healthc_status_notify;  /* Healthcheck status notify callback */

    ev_timer                     ov_healthc_timer;          /* ev timer for executing healthcheck */
    ev_tstamp                    ov_healthc_last_ping_ok;   /* Keeping track of when last ping was ok */
    execsh_async_t               ov_healthc_ping_execsh;    /* For running ping async in the background */
};

#define VPN_HEALTHC_PING_COUNT   1  /* Each ping to send this many ICMP echo requests */
#define VPN_HEALTHC_PING_WAIT    1  /* Time to wait for an ICMP echo reply */

static void healthcheck_timer_cb(struct ev_loop *loop, ev_timer *watcher, int revents);
static void vpn_healthcheck_execsh_fn(execsh_async_t *esa, int exit_status);
static void vpn_healthcheck_execsh_io_fn(execsh_async_t *esa, enum execsh_io io_type, const char *msg);

static bool vpn_init(osn_vpn_t *self, const char *name)
{
    STRSCPY(self->ov_name, name);

    ev_init(&self->ov_healthc_timer, healthcheck_timer_cb);
    execsh_async_init(&self->ov_healthc_ping_execsh, vpn_healthcheck_execsh_fn);
    execsh_async_set(&self->ov_healthc_ping_execsh, NULL, vpn_healthcheck_execsh_io_fn);

    return true;
}

osn_vpn_t *osn_vpn_new(const char *name)
{
    osn_vpn_t *self = CALLOC(1, sizeof(struct osn_vpn));

    if (!vpn_init(self, name))
    {
        LOG(ERR, "vpn: %s: Error initializing vpn object", name);
        FREE(self);
        return NULL;
    }
    return self;
}

const char *osn_vpn_name_get(osn_vpn_t *self)
{
    return self->ov_name;
}

bool osn_vpn_enable_set(osn_vpn_t *self, bool enable)
{
    LOG(TRACE, "vpn: %s: healthcheck: %s: enable=%d",
            self->ov_name, __func__, enable);

    self->ov_tunnel_enable = enable;
    return true;
}

/* Set ip to all zeros to unset */
bool osn_vpn_healthcheck_ip_set(osn_vpn_t *self, osn_ipany_addr_t *ip)
{
    LOG(TRACE, "vpn: %s: healthcheck: %s: ip="PRI_osn_ipany_addr,
            self->ov_name, __func__, FMT_osn_ipany_addr(*ip));

    self->ov_healthc_ip = *ip;
    return true;
}

bool osn_vpn_healthcheck_interval_set(osn_vpn_t *self, int interval)
{
    LOG(TRACE, "vpn: %s: healthcheck: %s: interval=%d",
            self->ov_name, __func__, interval);

    self->ov_healthc_interval = interval;
    return true;
}

bool osn_vpn_healthcheck_timeout_set(osn_vpn_t *self, int timeout)
{
    LOG(TRACE, "vpn: %s: healthcheck: %s: timeout=%d",
            self->ov_name, __func__, timeout);

    self->ov_healthc_timeout = timeout;
    return true;
}

bool osn_vpn_healthcheck_src_set(osn_vpn_t *self, const char *src)
{
    LOG(TRACE, "vpn: %s: healthcheck: %s: src=%s",
            self->ov_name, __func__, src);

    FREE(self->ov_healthc_src);

    self->ov_healthc_src = src && *src ? strdup(src) : NULL;

    return true;
}

bool osn_vpn_healthcheck_enable_set(osn_vpn_t *self, bool enable)
{
    LOG(TRACE, "vpn: %s: healthcheck: %s: enable=%d",
            self->ov_name, __func__, enable);

    self->ov_healthc_enable = enable;
    return true;
}

bool osn_vpn_healthcheck_notify_status_set(osn_vpn_t *self, osn_vpn_health_status_fn_t *status_cb)
{
    self->ov_healthc_status_notify = status_cb;
    return true;
}

bool osn_vpn_healthcheck_apply(osn_vpn_t *self)
{
    if (self->ov_healthc_enable)
    {
        /* Enable healthcheck */

        if (self->ov_healthc_interval < OSN_VPN_HEALTH_MIN_INTERVAL)
        {
            LOG(ERR, "vpn: %s: healthcheck: interval %d less then min interval %d",
                    self->ov_name, self->ov_healthc_interval, OSN_VPN_HEALTH_MIN_INTERVAL);
            return false;
        }
        if (self->ov_healthc_timeout < OSN_VPN_HEALTH_MIN_TIMEOUT)
        {
            LOG(ERR, "vpn: %s: healthcheck: timeout %d less then min timeout %d",
                    self->ov_name, self->ov_healthc_timeout, OSN_VPN_HEALTH_MIN_TIMEOUT);
            return false;
        }
        if (self->ov_healthc_timeout < self->ov_healthc_interval)
        {
            LOG(ERR, "vpn: %s: healthcheck: timeout %d < interval %d",
                    self->ov_name, self->ov_healthc_timeout, self->ov_healthc_interval);
            return false;
        }
        if (!osn_ipany_addr_is_set(&self->ov_healthc_ip))
        {
            LOG(ERR, "vpn: %s: healthcheck: IP address to ping not configured", self->ov_name);
            return false;
        }

        /* Configure and start healthcheck timer: */
        if (ev_is_active(&self->ov_healthc_timer))
        {
            ev_timer_stop(EV_DEFAULT, &self->ov_healthc_timer);
        }
        self->ov_healthc_last_ping_ok = ev_now(EV_DEFAULT);
        ev_timer_set(&self->ov_healthc_timer, self->ov_healthc_interval, self->ov_healthc_interval);
        ev_timer_start(EV_DEFAULT, &self->ov_healthc_timer);

        LOG(NOTICE, "vpn: %s: healthcheck enabled", self->ov_name);
    }
    else
    {
        /* Disable healthcheck */

        if (ev_is_active(&self->ov_healthc_timer))
        {
            ev_timer_stop(EV_DEFAULT, &self->ov_healthc_timer);
        }
        self->ov_health_status = OSN_VPN_HEALTH_STATUS_NA;

        LOG(NOTICE, "vpn: %s: healthcheck disabled", self->ov_name);
    }
    return true;
}

/* Called after healthcheck action (ping running asynchronously in
 * the background) completes. */
static void vpn_healthcheck_execsh_fn(execsh_async_t *esa, int exit_status)
{
    enum osn_vpn_health_status health_status;
    osn_vpn_t *self;
    ev_tstamp now;
    bool ping_ok;

    self = CONTAINER_OF(esa, osn_vpn_t, ov_healthc_ping_execsh);
    ping_ok = (exit_status == 0);
    now = ev_now(EV_DEFAULT);

    if (self->ov_healthc_enable)
    {
        if (ping_ok)
        {
            LOG(DEBUG, "vpn: %s: healthcheck: ping to "PRI_osn_ipany_addr" OK",
                    self->ov_name, FMT_osn_ipany_addr(self->ov_healthc_ip));

            self->ov_healthc_last_ping_ok = now;
            health_status = OSN_VPN_HEALTH_STATUS_OK;
        }
        else
        {
            LOG(DEBUG, "vpn: %s: healthcheck: ping to "PRI_osn_ipany_addr" NOK",
                                self->ov_name, FMT_osn_ipany_addr(self->ov_healthc_ip));

            if ((now - self->ov_healthc_last_ping_ok) >= self->ov_healthc_timeout)
            {
                LOG(DEBUG, "vpn: %s: healthcheck: ping NOK timeout expired", self->ov_name);

                /* Declare healthcheck status as NOK when ping was NOK for
                 * the whole timeout period */
                health_status = OSN_VPN_HEALTH_STATUS_NOK;
            }
            else
            {
                health_status = OSN_VPN_HEALTH_STATUS_OK;
            }
        }
    }
    else
    {
        LOG(DEBUG, "vpn: %s: healthcheck: disabled, ignore ping status", self->ov_name);
        health_status = OSN_VPN_HEALTH_STATUS_NA;  /* set to healthcheck unknown/na */
    }

    /* Determine if current new healthcheck status differs from the previous: */
    if (health_status != self->ov_health_status)
    {
        LOG(NOTICE, "vpn: %s: health status changed: %s --> %s",
                self->ov_name,
                osn_vpn_health_status_to_str(self->ov_health_status),
                osn_vpn_health_status_to_str(health_status));

        self->ov_health_status = health_status;
        if (self->ov_healthc_status_notify != NULL)
        {
            /* Notify of health status change: */
            self->ov_healthc_status_notify(self, self->ov_health_status);
        }
    }

    if (self->ov_healthc_enable)
    {
        /* Restart the timer here -- we want that the count to the next interval
         * starts now (when the ping either completes as OK or timeouts to NOK).
         * This is for cases where ping would not work and we would wait
         * for an ICMP echo reply with a long timeout. We don't want to start a
         * new ping try too soon. */
        ev_timer_again(EV_DEFAULT, &self->ov_healthc_timer);
    }

}

/* Healthcheck timer -- called every healthcheck interval seconds */
static void healthcheck_timer_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    char vpn_healthcheck_ping_cmd[C_MAXPATH_LEN+128];
    char ping_src_str[C_MAXPATH_LEN];
    osn_vpn_t *self = CONTAINER_OF(watcher, osn_vpn_t, ov_healthc_timer);

    LOG(TRACE, "vpn: %s: healthcheck timer", self->ov_name);

    /* Construct a ping shell command to run in the background: */
    if (self->ov_healthc_src != NULL)
    {
        snprintf(ping_src_str, sizeof(ping_src_str), " -I %s", self->ov_healthc_src);
    }
    else
    {
        ping_src_str[0] = '\0';
    }
    snprintf(
            vpn_healthcheck_ping_cmd,
            sizeof(vpn_healthcheck_ping_cmd),
            "timeout %d ping -c %d -W %d "PRI_osn_ipany_addr"%s",
            VPN_HEALTHC_PING_WAIT+3,
            VPN_HEALTHC_PING_COUNT,
            VPN_HEALTHC_PING_WAIT,
            FMT_osn_ipany_addr(self->ov_healthc_ip),
            ping_src_str
    );

    if (self->ov_healthc_ping_execsh.esa_running)
    {
        /* Unlikely: would happen if healthcheck interval would be less or close
         * to ping wait time */
        LOG(WARN, "vpn: %s: previous healthcheck ping still running", self->ov_name);
        return;
    }

    /* Run ping in the background: */
    execsh_async_start(&self->ov_healthc_ping_execsh, vpn_healthcheck_ping_cmd);
}

static bool vpn_fini(osn_vpn_t *self)
{
    /* Disable healthcheck, stop any running healthcheck timers: */
    self->ov_healthc_enable = false;
    osn_vpn_healthcheck_apply(self);

    /* Stop any running background healthcheck tasks: */
    execsh_async_stop(&self->ov_healthc_ping_execsh);

    if (self->ov_healthc_src != NULL)
    {
        FREE(self->ov_healthc_src);
    }

    return true;
}

bool osn_vpn_del(osn_vpn_t *self)
{
    vpn_fini(self);
    FREE(self);

    return true;
}

static void vpn_healthcheck_execsh_io_fn(execsh_async_t *esa, enum execsh_io io_type, const char *msg)
{
    /* Log the healthcheck background task's stdout and stderr with TRACE level */
    LOG(TRACE, "execsh: %jd%s %s",
            (intmax_t)esa->esa_child_pid,
            io_type == EXECSH_IO_STDOUT ? ">" : "|",
            msg);
}

const char *osn_vpn_conn_state_to_str(enum osn_vpn_conn_state vpn_conn_state)
{
    char *state_str[] = {
        [OSN_VPN_CONN_STATE_DOWN] = "down",
        [OSN_VPN_CONN_STATE_CONNECTING] = "connecting",
        [OSN_VPN_CONN_STATE_UP] = "up",
        [OSN_VPN_CONN_STATE_ERROR] = "error",
    };

    if (vpn_conn_state >= OSN_VPN_CONN_STATE_MAX) return "unknown";

    return state_str[vpn_conn_state];
}

const char *osn_vpn_health_status_to_str(enum osn_vpn_health_status vpn_health_state)
{
    char *state_str[] = {
        [OSN_VPN_HEALTH_STATUS_NA] = "na",
        [OSN_VPN_HEALTH_STATUS_OK] = "ok",
        [OSN_VPN_HEALTH_STATUS_NOK] = "nok",
        [OSN_VPN_HEALTH_STATUS_ERR] = "error"
    };

    if (vpn_health_state >= OSN_VPN_HEALTH_STATUS_MAX) return state_str[OSN_VPN_HEALTH_STATUS_NA];

    return state_str[vpn_health_state];
}
