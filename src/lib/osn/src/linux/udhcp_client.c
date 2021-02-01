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
 *  DHCP Client driver for UDHCP; should be used on OpenWRT and derivatives
 * ===========================================================================
 */

#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "const.h"
#include "os.h"
#include "log.h"

#include "udhcp_client.h"

static daemon_atexit_fn_t udhcp_client_atexit;
/*
 * The option file is created by the udhcpc script and contains
 * key=value pairs of the DHCP options received from the server.
 */
static void dhcpc_opt_watcher(struct ev_loop *loop, ev_stat *w, int revent);
static void dhcpc_opt_read(struct ev_loop *loop, ev_debounce *w, int revent);

bool udhcp_client_init(udhcp_client_t *self, const char *ifname)
{
    memset(self, 0, sizeof(*self));

    if (STRSCPY(self->uc_ifname, ifname) < 0)
    {
        return false;
    }

    /* Create the daemon process definition */
    if (!daemon_init(&self->uc_proc, CONFIG_OSN_UDHCPC_PATH, DAEMON_LOG_ALL))
    {
        LOG(ERR, "dhcp_client: Failed to initialize udhcpc daemon.");
        return false;
    }

    /* Register atexit handler */
    daemon_atexit(&self->uc_proc, udhcp_client_atexit);

    /* udhcpc should auto-restart if it dies */
    if (!daemon_restart_set(&self->uc_proc, true, 0.0, 0))
    {
        LOG(WARN, "dhcp_client: Failed to set auto-restart policy.");
    }

    /* Remember the options file full path */
    snprintf(self->uc_opt_path, sizeof(self->uc_opt_path), "/var/run/udhcpc-%s.opts", self->uc_ifname);

    /* Setup the debounce timer to trigger on 300ms */
    ev_debounce_init(&self->uc_opt_debounce, dhcpc_opt_read, 0.3);
    self->uc_opt_debounce.data = self;

    /* Setup the options file watcher */
    ev_stat_init(&self->uc_opt_stat, dhcpc_opt_watcher, self->uc_opt_path, 0.0);
    self->uc_opt_stat.data = self;

    return true;
}

bool udhcp_client_fini(udhcp_client_t *self)
{
    int ii;
    bool retval = true;

    if (!udhcp_client_stop(self))
    {
        LOG(WARN, "dhcp_client: %s: Error stopping DHCP.", self->uc_ifname);
        retval = false;
    }

    /* Free option list */
    for (ii = 0; ii < DHCP_OPTION_MAX; ii++)
    {
        if (self->uc_option_set[ii] != NULL)
        {
            free(self->uc_option_set[ii]);
            self->uc_option_set[ii] = NULL;
        }
    }

    return retval;
}

typedef struct cmdarg
{
    const char *option;
    char value[128];
} cmdarg_t;

/**
 * @brief Encodes DHCP options to command arguments understood by udhcpc DHCP client
 * 
 * @param opt option ID to encode
 * @param val value of DHCP option to encode
 * @param out output buffer for encoded options
 * @return 0 when option not encoded, 1 when only option encoded, 2 when option and value encoded
 */
static int udhcpc_opt_encode(
        enum osn_dhcp_option opt,
        const char *val,
        cmdarg_t *out)
{
    int len = 0;
    int rv = 0;

    C_STATIC_ASSERT(sizeof(out->value) >= 128, "out->value[] output buffer too small");

    /* Figure out how to encode the new option */
    switch (opt)
    {
        case DHCP_OPTION_HOSTNAME:
            out->option = "-x";
            len = snprintf(out->value, sizeof(out->value), "hostname:%s", val);
            rv = 2;
            break;

        case DHCP_OPTION_ADDRESS_REQUEST:
            out->option = "-r";
            len = (int)strscpy(out->value, val, sizeof(out->value));
            rv = 2;
            break;

        case DHCP_OPTION_VENDOR_CLASS:
            out->option = "-V";
            len = (int)strscpy(out->value, val, sizeof(out->value));
            rv = 2;
            break;

        case DHCP_OPTION_OSYNC_SWVER:
        case DHCP_OPTION_OSYNC_PROFILE:
        case DHCP_OPTION_OSYNC_SERIAL_OPT:
            /* add option */
            out->option = "-x";
            /* add leading option ID value */
            len = snprintf(out->value, sizeof(out->value), "0x%02X:", opt);
            /* Encode string value */
            while (*val != '\0')
            {
                len += snprintf(out->value + len, sizeof(out->value) - len, "%02X", *val++);
                if ((size_t)len >= sizeof(out->value)) break;
            }
            rv = 2;
            break;

        default:
            /* Don't know how to encode the option */
            return 0;
    }

    if (len < 0 || (size_t)len >= sizeof(out->value))
    {
         LOG(WARN, "dhcp_client: option encoding error, output buffer too small.");
         return 0;
    }
    return rv;
}


static void set_env_variables(udhcp_client_t *self)
{
    /* List of variables creating DHCP actions */
    static const uint8_t ev_id[] =
    {
        DHCP_OPTION_ROUTER,
        DHCP_OPTION_DNS_SERVERS,
        DHCP_OPTION_ROUTES,
        DHCP_OPTION_STATIC_ROUTES,
        DHCP_OPTION_MS_STATIC_ROUTES
    };

    // export action enable/disable keys for the script
    size_t n;
    for(n = 0; n < ARRAY_SIZE(ev_id); n++)
    {
        char name[128];
        const char *value = C_IS_SET(self->uc_option_req, ev_id[n]) ? "true" : "false";
        (void)snprintf(name, sizeof(name), "action_set_%s", dhcp_option_name(ev_id[n]));
        // set OR update env variable
        if (0 != setenv(name, value, 1))
        {
            LOG(WARN, "dhcp_client: setting env variable %s failed.", name);
        }
    }

    // export options file path for the script
    (void)setenv("OPTS_FILE", self->uc_opt_path, 1);
}

bool udhcp_client_start(udhcp_client_t *self)
{
    char pidfile[C_MAXPATH_LEN];

    int ii;

    if (self->uc_started) return true;

    snprintf(pidfile, sizeof(pidfile), "/var/run/udhcpc-%s.pid", self->uc_ifname);

    if (!daemon_pidfile_set(&self->uc_proc, pidfile, false))
    {
        LOG(ERR, "dhcp_client: Error setting the pidfile path. DHCP client on interface %s will not start.", self->uc_ifname);
        return false;
    }

    /* Build the DHCP options here */
    if (!daemon_arg_reset(&self->uc_proc))
    {
        LOG(ERR, "dhcp_client: Error resetting argument list. DHCP client on interface %s will not start.", self->uc_ifname);
        return false;
    }

    daemon_arg_add(&self->uc_proc, "-i", self->uc_ifname);              /* Interface to listen on */
    daemon_arg_add(&self->uc_proc, "-f");                               /* Run in foreground */
    daemon_arg_add(&self->uc_proc, "-p", pidfile);                      /* PID file path */
    daemon_arg_add(&self->uc_proc, "-s", CONFIG_INSTALL_PREFIX"/bin/udhcpc.sh");   /* DHCP client script */
    daemon_arg_add(&self->uc_proc, "-t", "60");                         /* Send up to N discover packets */
    daemon_arg_add(&self->uc_proc, "-T", "1");                          /* Pause between packets */
    daemon_arg_add(&self->uc_proc, "-S");                               /* Log to syslog too */
#ifndef CONFIG_UDHCPC_OPTIONS_USE_CLIENTID
    daemon_arg_add(&self->uc_proc, "-C");                               /* Do not send MAC as client id */
#endif

    /* Check if we have custom requested options */
    bool have_options = !IS_MEMZERO(self->uc_option_req);
    if (have_options)
    {
        daemon_arg_add(&self->uc_proc, "-o");   /* Do not send default options */

        for (ii = 0; ii < DHCP_OPTION_MAX; ii++)
        {
            if (C_IS_SET(self->uc_option_req, ii))
            {
                char optstr[C_INT8_LEN];
                (void)snprintf(optstr, sizeof(optstr), "%d", ii);
                daemon_arg_add(&self->uc_proc, "-O", optstr);
            }
        }
    }

    for (ii = 0; ii < DHCP_OPTION_MAX; ii++)
    {
        if (!C_IS_SET(self->uc_option_req, ii) && NULL != self->uc_option_set[ii])
        {
            cmdarg_t carg;
            switch (udhcpc_opt_encode(ii, self->uc_option_set[ii], &carg))
            {
                case 1:
                    daemon_arg_add(&self->uc_proc, (char *)carg.option);
                    break;
                case 2:
                    daemon_arg_add(&self->uc_proc, (char *)carg.option, carg.value);
                    break;
                default:
                    LOG(WARN, "dhcp_client: Error encoding option %d:%s.", ii, self->uc_option_set[ii]);
                    break;
            }
        }
    }

    // prepare env variables before starting udhcpc to 
    // inherit the values in the DHCP client script
    set_env_variables(self);

    if (!daemon_start(&self->uc_proc))
    {
        LOG(ERR, "dhcp_client: Error starting udhcpc on interface %s.", self->uc_ifname);
        return false;
    }

    /* Remove any stale files first -- ignore errors */
    (void)unlink(self->uc_opt_path);

    ev_stat_start(EV_DEFAULT, &self->uc_opt_stat);

    self->uc_started = true;

    return true;
}

static inline bool option_notify(udhcp_client_t *self, enum osn_dhcp_option opt, const char *value)
{
    return (NULL != self->uc_opt_notify_fn) ?
        (self->uc_opt_notify_fn(self, opt, value), true) : false;
}

bool udhcp_client_stop(udhcp_client_t *self)
{
    if (!self->uc_started) return true;

    /* Invalidate data of all requested and read options but leave request flag 
     * to enable restart w/o reconfiguration needed */
    size_t n;
    for (n = 0; n < DHCP_OPTION_MAX; ++n)
    {
        if (NULL != self->uc_option_set[n] && C_IS_SET(self->uc_option_req, n))
        {
            free(self->uc_option_set[n]);
            self->uc_option_set[n] = NULL;
            option_notify(self, (enum osn_dhcp_option)n, NULL);
        }
    }

    /* Stop watchers */
    ev_stat_stop(EV_DEFAULT, &self->uc_opt_stat);
    ev_debounce_stop(EV_DEFAULT, &self->uc_opt_debounce);

    if (!daemon_stop(&self->uc_proc))
    {
        LOG(WARN, "dhcp_client: Error stopping DHCP client on interface %s.", self->uc_ifname);
        return false;
    }

    self->uc_started = false;

    return true;
}

/**
 * Request option @p opt from server
 */
bool udhcp_client_opt_request(udhcp_client_t *self, enum osn_dhcp_option opt, bool request)
{
    if (udhcp_client_is_req_option(opt))
    {
        C_SET_VAL(self->uc_option_req, opt, request);
        /* release data buffer when no more requested */
        if (!request && self->uc_option_set[opt] != NULL)
        {
            free(self->uc_option_set[opt]);
            self->uc_option_set[opt] = NULL;
        }
        return true;
    }
    return false;
}

/**
 * Set a DHCP option -- these options will be sent to the DHCP server, pass a NULL value to unset the option
 */
bool udhcp_client_opt_set(udhcp_client_t *self, enum osn_dhcp_option opt, const char *val)
{
    if (!udhcp_client_is_supported_option(opt)) return false;

    /* First, unset the old option */
    if (self->uc_option_set[opt] != NULL)
    {
        free(self->uc_option_set[opt]);
        self->uc_option_set[opt] = NULL;
    }

    if (val == NULL) return true;

    self->uc_option_set[opt] = strdup(val);

    return true;
}

/*
 * Return the request status and value a DHCP options
 */
bool udhcp_client_opt_get(udhcp_client_t *self, enum osn_dhcp_option opt, bool *request, const char **value)
{
    if (opt < 0 || opt >= DHCP_OPTION_MAX) return false;

    *value = self->uc_option_set[opt];
    *request = C_IS_SET(self->uc_option_req, opt);

    return true;
}

bool udhcp_client_state_get(udhcp_client_t *self, bool *enabled)
{
    return daemon_is_started(&self->uc_proc, enabled);
}

/*
 * Set option notification callback
 */
bool udhcp_client_opt_notify(udhcp_client_t *self, udhcp_client_opt_notify_fn_t *fn)
{
    self->uc_opt_notify_fn = fn;
    return true;
}

/**
 * Set the error callback function -- this function is called to signal that an error occurred
 * during normal operation. Should only be called between a _star() and _stop() function, not
 * before nor after.
 *
 * Use NULL to unset it.
 */
bool udhcp_client_error_notify(udhcp_client_t *self, udhcp_client_error_fn_t *errfn)
{
    self->uc_err_fn = errfn;

    return true;
}

bool udhcp_client_atexit(daemon_t *proc)
{
    udhcp_client_t *self = CONTAINER_OF(proc, udhcp_client_t, uc_proc);

    LOG(ERR, "dhcp_client: DHCP client exited abnormally on interface: %s.", self->uc_ifname);

    /* Call the error handler, if it exists */
    if (self->uc_err_fn != NULL) self->uc_err_fn(self);

    return true;
}

/*
 * Read the options file, parse the key=value pair and emit events
 */
void dhcpc_opt_read(struct ev_loop *loop, ev_debounce *w, int revent)
{
    (void)loop;
    (void)revent;

    udhcp_client_t *self = w->data;

    FILE *fopt;
    char buf[256];

    LOG(INFO, "dhcp_client: %s: Debounced: %s", self->uc_ifname, self->uc_opt_path);

    fopt = fopen(self->uc_opt_path, "r");
    if (fopt == NULL)
    {
        LOG(ERR, "dhcp_client: %s: Error opening options file: %s",
                self->uc_ifname,
                self->uc_opt_path);
        return;
    }

    while (fgets(buf, sizeof(buf), fopt) != NULL)
    {
        char *pbuf = buf;

        char *key = strsep(&pbuf, "=");
        /* Use "\n" as delimiter, so that terminating new lines are also stripped away */
        char *value = strsep(&pbuf, "\n");

        if ((key == NULL) || (value == NULL))
        {
            LOG(ERR, "dhcp_client: %s: Error parsing option file, line skipped.", self->uc_ifname);
            continue;
        }

        /* Skip empty lines */
        if (key[0] == '\0') continue;

        LOG(TRACE, "dhcp_client: %s: Options KEY = %s, VALUE = %s", self->uc_ifname, key, value);

        /* keep read option in internal buffer to enable access to received
         * option value by option id using udhcp_client_opt_get() 
         * Note: Only udhcpc supported and requested options are kept */
        int optid = udhcp_client_get_option_id(key);
        if (optid >= 0 && C_IS_SET(self->uc_option_req, optid))
        {
            if (udhcp_client_opt_set(self, optid, value))
            {
                (void)option_notify(self, optid, value);
            }
        }
    }
    fclose(fopt);
}

/*
 * UDHCPC option file watcher
 */
void dhcpc_opt_watcher(struct ev_loop *loop, ev_stat *w, int revent)
{
    (void)loop;
    (void)revent;

    udhcp_client_t *self = w->data;

    LOG(INFO, "dhcp_client: %s: File changed: %s", self->uc_ifname, self->uc_opt_path);
    ev_debounce_start(loop, &self->uc_opt_debounce);
}

