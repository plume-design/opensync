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

#include <sys/file.h>
#include <stdlib.h>
#include <unistd.h>
#include <ev.h>

#include "util.h"
#include "os_util.h"

#include "const.h"
#include "log.h"

#include "odhcp6_client.h"

/*
 * ===========================================================================
 *  Global declarations
 * ===========================================================================
 */
#define ODHCP6C_DEBOUNCE_TIME   0.250       /* 250ms */

static void odhcp6_client_debounce_fn(struct ev_loop *loop, ev_debounce *w, int revent);
static void odhcp6_client_stat_fn(struct ev_loop *loop, ev_stat *w, int revent);
static int odhcp6_client_tag_from_str(const char *str);
static bool odhcp6_client_b64_to_b16(char *b16, ssize_t b16sz, char *b64);
static bool odhcp6_client_apply_option_send(odhcp6_client_t *self, int tag, char *value);
static bool odhcp6_client_apply_option_request(odhcp6_client_t *self);
static char* odhcp6_client_process_value(int optidx, const char *value);

/*
 * ===========================================================================
 *  Public API implementation
 * ===========================================================================
 */

/*
 * Create new DHCPv6 client
 */
bool odhcp6_client_init(odhcp6_client_t *self, const char *ifname)
{
    if (strscpy(self->oc_ifname, ifname, sizeof(self->oc_ifname)) < 0)
    {
        LOG(ERR, "dhcpv6_client: Interface name too long: %s", self->oc_ifname);
        return false;
    }

    if (!daemon_init(&self->oc_daemon, CONFIG_OSN_ODHCP6_PATH, DAEMON_LOG_ALL))
    {
        LOG(ERR, "dhcpv6_client: Unable to initialize daemon object for interface: %s",
                 self->oc_ifname);
        return false;
    }

    /* odhcp6c doesn't exit on its own when NM is killed, we have to take care of stale instances */
    snprintf(self->oc_pid_file, sizeof(self->oc_pid_file), CONFIG_OSN_ODHCP6_PID_FILE, self->oc_ifname);
    daemon_pidfile_set(&self->oc_daemon, self->oc_pid_file, true);

    snprintf(self->oc_opts_file, sizeof(self->oc_opts_file), CONFIG_OSN_ODHCP6_OPTS_FILE, self->oc_ifname);

    ev_debounce_init(&self->oc_debounce, odhcp6_client_debounce_fn, ODHCP6C_DEBOUNCE_TIME);
    self->oc_debounce.data = self;

    ev_stat_init(&self->oc_opts_ev, odhcp6_client_stat_fn, self->oc_opts_file, 1.0);
    self->oc_opts_ev.data = self;

    return true;
}

/*
 * Stop the DHCPv6 client, clear up resources
 */
bool odhcp6_client_fini(odhcp6_client_t *self)
{
    int ii;

    /* Stop watchers */
    ev_debounce_stop(EV_DEFAULT, &self->oc_debounce);
    ev_stat_stop(EV_DEFAULT, &self->oc_opts_ev);

    /* Stop the current service, if it is running */
    if (!daemon_stop(&self->oc_daemon))
    {
        LOG(ERR, "dhcpv6_client: Error stopping daemon.");
    }

    /* Stop the service */
    daemon_fini(&self->oc_daemon);

    (void)unlink(self->oc_opts_file);
    (void)unlink(self->oc_pid_file);

    /* Clear the status structure */
    for (ii = 0; ii < OSN_DHCP_OPTIONS_MAX; ii++)
    {
        if (self->oc_status.d6c_recv_options[ii] == NULL) continue;

        free(self->oc_status.d6c_recv_options[ii]);
        self->oc_status.d6c_recv_options[ii] = NULL;
    }

    self->oc_status.d6c_connected = false;

    /* Send out notification that the DHCP option list was cleared */
    if (self->oc_notify_fn != NULL)
    {
        LOG(TRACE, "dhcpv6_client: Invoking dhcpv6_client notification handler (delete).");
        self->oc_notify_fn(self, &self->oc_status);
    }

    /* Free send options */
    for (ii = 0; ii < OSN_DHCP_OPTIONS_MAX; ii++)
    {
        if (self->oc_option_send[ii] == NULL) continue;

        free(self->oc_option_send[ii]);
        self->oc_option_send[ii] = NULL;
    }

    return true;
}

/*
 * Apply current configuration
 */
bool odhcp6_client_apply(odhcp6_client_t *self)
{
    int ii;

    bool retval = false;

    /* Unlink opts file */
    (void)unlink(self->oc_opts_file);

    /* Start options file watcher */
    ev_stat_start(EV_DEFAULT, &self->oc_opts_ev);

    if (self->oc_renew)
    {
        LOG(NOTICE, "dhcpv6_client: renew option ignored");
    }

    if (self->oc_rapid_commit)
    {
        LOG(NOTICE, "dhcpv6_client: rapid_commit option ignored");
    }

    /* Rest arguments */
    daemon_arg_reset(&self->oc_daemon);
    odhcp6_client_apply_option_request(self);

    for (ii = 0; ii < OSN_DHCP_OPTIONS_MAX; ii++)
    {
        if (self->oc_option_send[ii] == NULL) continue;
        odhcp6_client_apply_option_send(self, ii, self->oc_option_send[ii]);
    }

    /* -v              Increase logging verbosity */
    daemon_arg_add(&self->oc_daemon, "-v");
    /* -e              Write log messages to stderr */
    daemon_arg_add(&self->oc_daemon, "-e");
    /* -Q              Do not track or update address lifetimes */
    daemon_arg_add(&self->oc_daemon, "-Q");
    /* -s <script>     Status update script (/usr/sbin/odhcp6c-update) */
    daemon_arg_add(&self->oc_daemon, "-s", CONFIG_OSN_ODHCP6_SCRIPT_PATH);

#if 0
    /* The -p switch of odhcpc6c doesn't seem to be working properly */
    /* -p <pidfile>    Set pidfile (/var/run/odhcp6c.pid) */
    daemon_arg_add(&self->oc_daemon, "-p", self->oc_pid_file);
#endif

    /* -P <length>     Request IPv6-Prefix (0 = auto) */
    if (self->oc_request_prefixes)
    {
        daemon_arg_add(&self->oc_daemon, "-P", "0");
    }

    if (self->oc_request_address)
    {
        /* -N <mode>       Mode for requesting addresses [try|force|none] */
        daemon_arg_add(&self->oc_daemon, "-N", "try");
    }

    /*
     *  Due to bugs in odhc6pc, this options only really takes effect if it's the
     *  last option.
     *
     *  -S <time>       Wait at least <time> sec for a DHCP-server (0)
     */
    daemon_arg_add(&self->oc_daemon, "-S3");

    /* Add the interface name */
    daemon_arg_add(&self->oc_daemon, self->oc_ifname);

    if (!daemon_start(&self->oc_daemon))
    {
        LOG(ERR, "dhcpv6_client: Error starting daemon.");
        goto error;
    }

    retval = true;

error:
    return retval;
}

bool odhcp6_client_set(
        odhcp6_client_t *self,
        bool request_address,
        bool request_prefixes,
        bool rapid_commit,
        bool renew)
{
    self->oc_request_address = request_address;
    self->oc_request_prefixes = request_prefixes;
    self->oc_rapid_commit = rapid_commit;
    self->oc_renew = renew;

    return true;
}

/*
 * Request a DHCP options
 */
bool odhcp6_client_option_request(odhcp6_client_t *self, int tag)
{
    if (tag <= 0 || tag >= OSN_DHCP_OPTIONS_MAX) return false;

    self->oc_option_request[tag] = true;

    return true;
}

/*
 * Set DHCP options that will be sent to the server.
 *
 * Value is expected to be in base64 format
 */
bool odhcp6_client_option_send(odhcp6_client_t *self, int tag, const char *value)
{
    if (tag <= 0 || tag >= OSN_DHCP_OPTIONS_MAX) return false;

    if (self->oc_option_send[tag] != NULL)
    {
        free((void *)self->oc_option_send[tag]);
        self->oc_option_send[tag] = NULL;
    }

    if (value == NULL) return true;

    self->oc_option_send[tag] = strdup(value);

    return true;
}

/*
 * Register the notification function
 */
void odhcp6_client_status_notify(odhcp6_client_t *self, odhcp6_client_status_fn_t *fn)
{
    self->oc_notify_fn = fn;

    if (self->oc_notify_fn != NULL) self->oc_notify_fn(self, &self->oc_status);
}

/*
 * ===========================================================================
 *  Private functions
 * ===========================================================================
 */

/*
 * Options file parser
 */
void odhcp6_client_stat_fn(struct ev_loop *loop, ev_stat *w, int revent)
{
    (void)loop;
    (void)revent;

    odhcp6_client_t *self = w->data;

    /*
     * The stat watcher can trigger many times before the file is fully written. If this happens, the file
     * is probably in an incomplete or empty state
     */
    ev_debounce_start(EV_DEFAULT, &self->oc_debounce);
}

void odhcp6_client_debounce_fn(struct ev_loop *loop, ev_debounce *w, int revent)
{
    (void)loop;
    (void)revent;

    char buf[8200];
    int ii;

    odhcp6_client_t *self = w->data;

    /* Clear the current list */
    for (ii = 0; ii < ARRAY_LEN(self->oc_status.d6c_recv_options); ii++)
    {
        if (self->oc_status.d6c_recv_options[ii] == NULL) continue;

        free(self->oc_status.d6c_recv_options[ii]);
        self->oc_status.d6c_recv_options[ii] = NULL;
    }

    /* Open the options file */
    FILE *f = fopen(self->oc_opts_file, "r");
    if (f == NULL)
    {
        LOG(TRACE, "dhcpv6_client: Error opening options file: %s", self->oc_opts_file);
        self->oc_status.d6c_connected = false;
        return;
    }

    /*
     * The options file is just an exported list of environment variables from the odhcp6c script.
     * Each line corresponds to the following format:
     *
     * export NAME='VALUE'
     */
    while (fgets(buf, sizeof(buf), f) != NULL)
    {
        char *p = buf;
        char *name;
        char *value;
        char *dvalue;

        int optidx = -1;

        LOG(TRACE, "dhcpv6_client: Parsing option file line: %s", buf);

        if (strncmp(p, "export ", strlen("export ")) != 0) continue;

        /* Skip the "export keyword" */
        p += strlen("export ");

        name = strsep(&p, "=");
        if (name == NULL) continue;

        /* Skip the first ' */
        value = strsep(&p, "'");
        if (value == NULL || value[0] != '\0') continue;

        value = strsep(&p, "'");

        /* The last ' should be followed by a new line, if it's not, discard this line */
        if (p[0] != '\n') continue;

        /*
         * Parse various options
         */
        optidx = odhcp6_client_tag_from_str(name);
        if (optidx < 0) continue;

        /* Free old value, if any */
        if (self->oc_status.d6c_recv_options[optidx] != NULL)
        {
            free(self->oc_status.d6c_recv_options[optidx]);
            self->oc_status.d6c_recv_options[optidx] = NULL;
        }

        dvalue = odhcp6_client_process_value(optidx, value);
        if (dvalue == NULL)
        {
            LOG(DEBUG, "dhcpv6_client: Error processing option %d value: %s", optidx, value);
            self->oc_status.d6c_recv_options[optidx] = NULL;
        }

        self->oc_status.d6c_recv_options[optidx] = dvalue;

        LOG(TRACE, "dhcpv6_client: Option %d = %s", optidx, value);
    }

    fclose(f);

    self->oc_status.d6c_connected = true;

    /* Send out notification */
    if (self->oc_notify_fn != NULL)
    {
        LOG(TRACE, "dhcpv6_client: Invoking dhcpv6_client notification handler.");
        self->oc_notify_fn(self, &self->oc_status);
    }
}

char *odhcp6_client_process_value(int optidx, const char *value)
{
    switch (optidx)
    {
        /*
         * This not needed anymore. An option was added to odhcp6c which
         * inhibits lifetime update tracking within the odhcp6c client.
         *
         * They are always sent as received from the server.
         */
#if 0
        /*
         * Remove the lifetimes from the ADDRESS option, they cause unnecessary OVSDB updates
         *
         * The string below:
         *
         * "11:22:33:44::55/64,700,800 66:77:88:99::1/64,200,300"
         *
         * becomes
         *
         * "11:22:33:44:55/64 66:77:88:99::1/64"
         */
        case 5:
        {
            osn_ip6_addr_t addr;
            ssize_t out_len;
            int out_pos;
            char *pvalue;
            char *paddr;
            char *pbuf;
            char *out;

            /*
             * We need to remove the lifetimes from the buffer -- the resulting buffer will be shorter than the original
             * so we can safely use strlen() to determine the maximum buffer size
             */
            out_len = strlen(value);
            out = malloc(out_len);

            /* strtok() modifies the buffer, copy it so we don't modify the original */
            pvalue = strdup(value);

            out[0] = '\0';
            out_pos = 0;
            for (paddr = strtok_r(pvalue, " ", &pbuf);
                    paddr != NULL;
                    paddr = strtok_r(NULL, " ", &pbuf))
            {
                if (!osn_ip6_addr_from_str(&addr, paddr))
                {
                    LOG(DEBUG, "dhcpv6_client: Error parsing IPv6 address '%s' on line: %s", paddr, value);
                    continue;
                }

                /* Remove lifetimes */
                addr.ia6_pref_lft = INT_MIN;
                addr.ia6_valid_lft = INT_MIN;

                out_pos += snprintf(out + out_pos, out_len - out_pos, PRI_osn_ip6_addr" ", FMT_osn_ip6_addr(addr));
                if (out_pos > out_len)
                {
                    LOG(DEBUG, "dhcpv6_client: Error reconstructing option 5, string too long. Line: %s", value);
                    free(pvalue);
                    free(out);
                    break;
                }
            }

            strchomp(out, " ");
            free(pvalue);
            return out;
        }
#endif
        default:
            /* The rest of the options are just pass through */
            return strdup(value);
    }

    return NULL;
}

/*
 * Convert a string (variable name) as returned by odhcp6c to its DHCPv6 option id
 *
 * If it cannot be resolved, return -1
 */
int odhcp6_client_tag_from_str(const char *str)
{
    int optidx = -1;
    /* OPTION_IAADDR */
    if (strcmp(str, "ADDRESSES") == 0)
    {
        optidx = 5;
    }
    /* OPTION_DNS_SERVERS */
    else if (strcmp(str, "RDNSS") == 0)
    {
        optidx = 23;
    }
    /* OPTION_DOMAIN_LIST */
    else if (strcmp(str, "DOMAINS") == 0)
    {
        optidx = 24;
    }
    /* OPTION_IAPREFIX */
    else if (strcmp(str, "PREFIXES") == 0)
    {
        optidx = 26;
    }
    /* Custom option */
    else if (strncmp(str, "OPTION_", strlen("OPTION_")) == 0)
    {
        long val;
        const char *popt = str + strlen("OPTION_");
        if (os_atol((char *)popt, &val)) optidx = val;
    }

    return optidx;
}
/*
 * ===========================================================================
 *  Utility functions
 * ===========================================================================
 */

/*
 * Apply request options
 */
bool odhcp6_client_apply_option_request(odhcp6_client_t *self)
{
    int ii;
    char buf[256];
    ssize_t buf_pos;

    /*
     * Build the "-r" option:
     * -r <options>    Options to be requested (comma-separated)
     */
    buf_pos = 0;
    buf[0] = '\0';
    for (ii = 0; ii < OSN_DHCP_OPTIONS_MAX; ii++)
    {
        if (!self->oc_option_request[ii]) continue;

        buf_pos += snprintf(buf + buf_pos, sizeof(buf) - buf_pos, ",%d", ii);
        if (buf_pos >= (ssize_t)sizeof(buf))
        {
            LOG(ERR, "dhcpv6_client: Error creating required options list.");
            return false;
        }
    }

    if (buf[0] != '\0')
    {
        daemon_arg_add(&self->oc_daemon, "-R");
        /* The + 1 below is to chop off the ',' at the beginning of the string */
        daemon_arg_add(&self->oc_daemon, "-r", buf + 1);
    }

    return true;
}

/*
 * Apply send options
 */
bool odhcp6_client_apply_option_send(odhcp6_client_t *self, int tag, char *value)
{
    char *opt;
    char buf[256];

    /* The following options must be base16 encoded:
     *
     *         -V <class>      Set vendor-class option (base-16 encoded)
     *         -u <user-class> Set user-class option string
     *         -c <clientid>   Override client-ID (base-16 encoded 16-bit type + value)
     *
     * These are the only request options supported.
     */

    switch (tag)
    {
        case 1:
            opt = "-V";
            break;

        case 15:
            opt = "-u";
            break;

        case 16:
            opt = "-c";
            break;

        case 17:
            opt = "-X";
            break;

        default:
            LOG(NOTICE, "dhcpv6_client: Option %d not supported by odhcp6c.", tag);
            return true;
    }

    if (!odhcp6_client_b64_to_b16(buf, sizeof(buf), value))
    {
        LOG(ERR, "dhcpv6_client: Unable to add option %d (%s), cannot parse buffer: %s", tag, opt, value);
        return false;
    }

    /* Add the options to argument list of odhcp6c */
    daemon_arg_add(&self->oc_daemon, opt);
    daemon_arg_add(&self->oc_daemon, buf);

    return true;
}

/*
 * Convert a base64 to a base16 encoded string
 */
static char odhcp6_client_b16_chars[] = "0123456789abcdef";

bool odhcp6_client_b64_to_b16(
        char *b16,
        ssize_t b16sz,
        char *b64)
{
    ssize_t ii;
    ssize_t nr;
    char buf[256];

    /* Decode the base64 buffer */
    nr = base64_decode(buf, sizeof(buf), b64);
    if (nr < 0)
    {
        return false;
    }

    /* Check the space needed for the b16 buffer */
    if (b16sz < (nr * 2 + 1)) return false;

    /* Re-encode it to base-16 */
    for (ii = 0; ii < nr; ii++)
    {
        int c = (uint8_t)buf[ii];
        b16[ii*2    ] = odhcp6_client_b16_chars[(c & 0xF0) >> 4];
        b16[ii*2 + 1] = odhcp6_client_b16_chars[c & 0x0F];
    }

    b16[ii*2] = '\0';

    return true;
}

