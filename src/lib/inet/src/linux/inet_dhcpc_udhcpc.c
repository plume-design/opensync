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

#include "const.h"
#include "util.h"
#include "log.h"
#include "daemon.h"

#include "inet_dhcpc.h"

#if !defined(CONFIG_USE_KCONFIG)

#define CONFIG_INET_UDHCPC_PATH "/sbin/udhcpc"

#endif /* CONFIG_USE_KCONFIG */

bool inet_dhcpc_init(inet_dhcpc_t *self, const char *ifname);
bool inet_dhcpc_fini(inet_dhcpc_t *self);

static bool __dhcpc_xopt_encode(enum inet_dhcp_option opt, const char *value, char *out, size_t outsz);
static daemon_atexit_fn_t __dhcpc_atexit;

/*
 * ===========================================================================
 *  DHCP Client driver for UDHCP; should be used on OpenWRT and derivatives
 * ===========================================================================
 */
struct __inet_dhcpc
{
    char                    dc_ifname[C_IFNAME_LEN];
    daemon_t                dc_proc;
    uint8_t                 dc_option_req[C_SET_LEN(DHCP_OPTION_MAX, uint8_t)];
    char                    *dc_option_set[DHCP_OPTION_MAX];
    inet_dhcpc_error_fn_t   *dc_err_fn;
};

inet_dhcpc_t *inet_dhcpc_new(const char *ifname)
{
    inet_dhcpc_t *self = malloc(sizeof(inet_dhcpc_t));

    if (!inet_dhcpc_init(self, ifname))
    {
        free(self);
        return NULL;
    }

    return self;
}

bool inet_dhcpc_del(inet_dhcpc_t *self)
{
    bool retval = inet_dhcpc_fini(self);

    free(self);

    return retval;
}

bool inet_dhcpc_init(inet_dhcpc_t *self, const char *ifname)
{
    memset(self, 0, sizeof(*self));

    if (STRSCPY(self->dc_ifname, ifname) < 0)
    {
        return false;
    }

    /* Create the daemon process definition */
    if (!daemon_init(&self->dc_proc, CONFIG_INET_UDHCPC_PATH, DAEMON_LOG_ALL))
    {
        LOG(ERR, "dhcpc: Failed to initialize udhcpc daemon.");
        return false;
    }

    /* Register atexit handler */
    daemon_atexit(&self->dc_proc, __dhcpc_atexit);

    /* udhcpc should auto-restart if it dies */
    if (!daemon_restart_set(&self->dc_proc, true, 0.0, 0))
    {
        LOG(WARN, "dhcpc: Failed to set auto-restart policy.");
    }

    return true;
}

bool inet_dhcpc_fini(inet_dhcpc_t *self)
{
    int ii;

    bool retval = true;

    if (!daemon_stop(&self->dc_proc))
    {
        LOG(WARN, "dhcpc: Error stopping DHCP client on interface %s.", self->dc_ifname);
        retval = false;
    }

    /* Free option list */
    for (ii = 0; ii < DHCP_OPTION_MAX; ii++)
    {
        if (self->dc_option_set[ii] != NULL)
            free(self->dc_option_set[ii]);
    }

    return retval;
}


bool inet_dhcpc_start(inet_dhcpc_t *self)
{
    char pidfile[C_MAXPATH_LEN];

    int ii;

    snprintf(pidfile, sizeof(pidfile), "/var/run/udhcpc-%s.pid", self->dc_ifname);

    if (!daemon_pidfile_set(&self->dc_proc, pidfile, false))
    {
        LOG(ERR, "dhcpc: Error setting the pidfile path. DHCP client on interface %s will not start.", self->dc_ifname);
        return false;
    }

    /* Build the DHCP options here */
    if (!daemon_arg_reset(&self->dc_proc))
    {
        LOG(ERR, "dhcpc: Error resetting argument list. DHCP client on interface %s will not start.", self->dc_ifname);
        return false;
    }

    daemon_arg_add(&self->dc_proc, "-i", self->dc_ifname);              /* Interface to listeon on */
    daemon_arg_add(&self->dc_proc, "-f");                               /* Run in foreground */
    daemon_arg_add(&self->dc_proc, "-p", pidfile);                      /* PID file path */
    daemon_arg_add(&self->dc_proc, "-s", "/usr/plume/bin/udhcpc.sh");   /* DHCP client script */
    daemon_arg_add(&self->dc_proc, "-t", "60");                         /* Send up to N discover packets */
    daemon_arg_add(&self->dc_proc, "-T", "1");                          /* Pause between packets */
    daemon_arg_add(&self->dc_proc, "-S");                               /* Log to syslog too */
    daemon_arg_add(&self->dc_proc, "-C");                               /* Do not send MAC as client id */

    /* Check if we have custom requested options */
    bool have_options;

    for (ii = 0; ii < DHCP_OPTION_MAX; ii++)
    {
        have_options = C_IS_SET(self->dc_option_req, ii);
        if (have_options) break;
    }

    if (have_options)
    {
        char optstr[C_INT8_LEN];

        daemon_arg_add(&self->dc_proc, "--no-default-options");   /* Do not send default options */

        for (ii = 0; ii < DHCP_OPTION_MAX; ii++)
        {
            if (!C_IS_SET(self->dc_option_req, ii)) continue;

            snprintf(optstr, sizeof(optstr), "%d", ii);
            daemon_arg_add(&self->dc_proc, "-O", optstr);
        }
    }

    for (ii = 0; ii < DHCP_OPTION_MAX; ii++)
    {
        char optstr[128];

        /* The vendor class is handled below */
        if (ii == DHCP_OPTION_VENDOR_CLASS) continue;

        if (self->dc_option_set[ii] == NULL) continue;

        if (!__dhcpc_xopt_encode(ii, self->dc_option_set[ii], optstr, sizeof(optstr)))
        {
            LOG(WARN, "dhcpc: Error encoding option %d:%s.", ii, self->dc_option_set[ii]);
            continue;
        }

        daemon_arg_add(&self->dc_proc, "-x", optstr);
    }

    if (self->dc_option_set[DHCP_OPTION_VENDOR_CLASS] != NULL)
    {
        daemon_arg_add(&self->dc_proc, "--vendorclass", self->dc_option_set[DHCP_OPTION_VENDOR_CLASS]);
    }

    if (!daemon_start(&self->dc_proc))
    {
        LOG(ERR, "dhcpc: Error starting udhcpc on interface %s.", self->dc_ifname);
        return false;
    }

    return true;
}

bool __dhcpc_atexit(daemon_t *proc)
{
    inet_dhcpc_t *self = container_of(proc, inet_dhcpc_t, dc_proc);

    LOG(ERR, "dhcpc: DHCP client exited abnormally on interface: %s.", self->dc_ifname);

    /* Call the error handler, if it exists */
    if (self->dc_err_fn != NULL) self->dc_err_fn(self);

    return true;
}

bool __dhcpc_xopt_encode(
        enum inet_dhcp_option opt,
        const char *val,
        char *out,
        size_t outsz)
{
    size_t len;
    size_t ii;

    /* Figure out how to encode the new option */
    switch (opt)
    {
        case DHCP_OPTION_HOSTNAME:
            len = snprintf(out, outsz, "hostname:%s", val);
            if (len >= outsz)
            {
                LOG(WARN, "dhcpc: Xopt encoding error, buffer too small for hostname.");
                return false;
            }
            break;

        case DHCP_OPTION_PLUME_SWVER:
        case DHCP_OPTION_PLUME_PROFILE:
        case DHCP_OPTION_PLUME_SERIAL_OPT:
            /* HEX encode */
            len = snprintf(out, outsz, "0x%02X:", opt);
            /* Output was truncated */
            if (len >= outsz)
            {
                LOG(WARN, "dhcpc: Xopt encoding error, buffer too small for hex option.");
                return false;
            }

            out += len; outsz -= len;

            /* Encode value */
            for (ii = 0; ii < strlen(val); ii++)
            {
                len = snprintf(out, outsz, "%02X", val[ii]);
                if (len >= outsz)
                {
                    LOG(WARN, "dhcpc: Xopt encoding error, buffer too small for hex value: %s\n", val);
                    return false;
                }

                out += len; outsz -= len;
            }
            break;

        default:
            LOG(WARN, "dhcpc: Don't know how to encode Xopt %d.", opt);
            /* Don't know how to encode the rest of the options */
            return false;
    }

    return true;
}

bool inet_dhcpc_stop(inet_dhcpc_t *self)
{
    return daemon_stop(&self->dc_proc);
}

/**
 * Request option @p opt from server
 */
bool inet_dhcpc_opt_request(inet_dhcpc_t *self, enum inet_dhcp_option opt, bool request)
{
    if (opt >= DHCP_OPTION_MAX)
    {
        return false;
    }

    C_SET_VAL(self->dc_option_req, opt, request);

    return true;
}

/**
 * Set a DHCP option -- these options will be sent to the DHCP server, pass a NULL value to unset the option
 */
bool inet_dhcpc_opt_set(
        inet_dhcpc_t *self,
        enum inet_dhcp_option opt,
        const char *val)
{
    if (opt >= DHCP_OPTION_MAX) return false;

    /* First, unset the old option */
    if (self->dc_option_set[opt] != NULL)
    {
        free(self->dc_option_set[opt]);
        self->dc_option_set[opt] = NULL;
    }

    if (val == NULL) return true;

    self->dc_option_set[opt] = strdup(val);

    return true;
}

/*
 * Return the request status and value a DHCP options
 */
bool inet_dhcpc_opt_get(inet_dhcpc_t *self, enum inet_dhcp_option opt, bool *request, const char **value)
{
    if (opt >= DHCP_OPTION_MAX) return false;

    *value = self->dc_option_set[opt];
    *request = C_IS_SET(self->dc_option_req, opt);

    return true;
}


/**
 * Set the error callback function -- this function is called to signal that an error occurred
 * during normal operation. Should only be called between a _star() and _stop() function, not
 * before nor after.
 *
 * Use NULL to unset it.
 */
bool inet_dhcpc_error_fn_set(inet_dhcpc_t *self, inet_dhcpc_error_fn_t *errfn)
{
    self->dc_err_fn = errfn;

    return true;
}

bool inet_dhcpc_state_get(inet_dhcpc_t *self, bool *enabled)
{
    return daemon_is_started(&self->dc_proc, enabled);
}

