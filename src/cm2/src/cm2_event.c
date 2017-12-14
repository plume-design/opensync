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

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <resolv.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "os.h"
#include "util.h"
#include "os_time.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_cache.h"
#include "schema.h"
#include "log.h"
#include "ds.h"
#include "json_util.h"
#include "cm2.h"

#define MODULE_ID LOG_MODULE_ID_EVENT

// interval and timeout in seconds
#define CM2_EVENT_INTERVAL      10
#define CM2_NO_TIMEOUT          -1
#define CM2_DEFAULT_TIMEOUT     60  // 1 min
#define CM2_MAX_DISCONNECTS     3
#define CM2_STABLE_PERIOD       300 // 5 min

typedef struct cm2_state_info
{
    char *name;
    int timeout;
} cm2_state_info_t;

cm2_state_info_t cm2_state_info[CM2_STATE_NUM] =
{
    [CM2_STATE_INIT]            = { "INIT",         CM2_NO_TIMEOUT },
    [CM2_STATE_TRY_RESOLVE]     = { "TRY_RESOLVE",  CM2_DEFAULT_TIMEOUT },
    [CM2_STATE_RE_CONNECT]      = { "RE_CONNECT",   CM2_DEFAULT_TIMEOUT },
    [CM2_STATE_TRY_CONNECT]     = { "TRY_CONNECT",  CM2_DEFAULT_TIMEOUT },
    [CM2_STATE_CONNECTED]       = { "CONNECTED",    CM2_NO_TIMEOUT },
    [CM2_STATE_QUIESCE_OVS]     = { "QUIESCE_OVS",  CM2_DEFAULT_TIMEOUT },
    [CM2_STATE_INTERNET]        = { "INTERNET",     CM2_DEFAULT_TIMEOUT },
};

// reason for update
char *cm2_reason_name[CM2_REASON_NUM] =
{
    "timer",
    "ovs-awlan",
    "ovs-manager",
    "state-change",
};


void append_sprintf(char **str, int *size, char *format, ...)
{
    if (!str || !size || *size < 1) return;
    int ret;
    va_list vargs;
    va_start(vargs, format);
    ret = vsnprintf(*str, *size, format, vargs);
    va_end(vargs);
    if (ret >= *size) {
        *str += *size - 1;
        *size = 0;
    } else if (ret > 0) {
        *str += ret;
        *size -= ret;
    }
}


char* cm2_dest_name(cm2_dest_e dest)
{
    return (dest == CM2_DEST_REDIR) ? "redirector" : "manager";
}

char* cm2_curr_dest_name()
{
    return cm2_dest_name(g_state.dest);
}

cm2_addr_t* cm2_get_addr(cm2_dest_e dest)
{
    if (dest == CM2_DEST_REDIR)
    {
        return &g_state.addr_redirector;
    }
    else
    {
        return &g_state.addr_manager;
    }
}

cm2_addr_t* cm2_curr_addr()
{
    return cm2_get_addr(g_state.dest);
}

void cm2_free_addrinfo(cm2_addr_t *addr)
{
    if (addr->ai_list) freeaddrinfo(addr->ai_list);
    addr->ai_list = NULL;
    addr->ai_curr = NULL;
}

void cm2_clear_addr(cm2_addr_t *addr)
{
    *addr->resource = 0;
    *addr->proto = 0;
    *addr->hostname = 0;
    addr->port = 0;
    addr->valid = false;
    cm2_free_addrinfo(addr);
}

bool cm2_parse_resource(cm2_addr_t *addr, cm2_dest_e dest)
{
    char *dstr = cm2_dest_name(dest);

    if (!parse_uri(addr->resource, addr->proto, addr->hostname, &(addr->port)))
    {
        LOGE("Fail to parse %s resource (%s)", dstr, addr->resource);
        cm2_clear_addr(addr);
    }
    else
    {
        addr->valid = true;
    }

    return addr->valid;
}

bool cm2_set_addr(cm2_dest_e dest, char *resource)
{
    bool ret = false;
    cm2_addr_t *addr = cm2_get_addr(dest);
    strlcpy(addr->resource, resource, sizeof(addr->resource));
    addr->resolved = false;
    addr->updated = false;
    if (*resource)
    {
        ret = cm2_parse_resource(addr, dest);
        LOG(DEBUG, "Set %s addr: %s valid: %s",
                cm2_dest_name(dest), resource, str_bool(ret));
        cm2_free_addrinfo(addr);
    }
    else
    {
        cm2_clear_addr(addr);
        LOG(DEBUG, "Clear %s addr", cm2_dest_name(dest));
    }
    return ret;
}


int cm2_getaddrinfo(char *hostname, struct addrinfo **res, char *msg)
{
    struct addrinfo hints;
    int ret;

    // force reload resolv.conf
    res_init();

    // hints
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET; // IPv4 addresses only
    hints.ai_socktype = SOCK_STREAM; // Otherwise addresses are duplicated

    // resolve
    ret = getaddrinfo(hostname, NULL, &hints, res);
    if (ret != 0)
    {
        LOGE("%s DNS lookup failed (%s)", msg, gai_strerror(ret));
    }
    return ret;
}

bool cm2_resolve(cm2_dest_e dest)
{
    cm2_addr_t *addr = cm2_get_addr(dest);
    char *dstr = cm2_dest_name(dest);

    addr->updated = false;

    if (!addr->valid)
    {
        return false;
    }

    LOGI("resolving %s '%s'", dstr, addr->resource);

    cm2_free_addrinfo(addr);

    struct addrinfo *ai, *found = NULL;
    struct sockaddr_in  *sain;
    int ret;

    // resolve
    ret = cm2_getaddrinfo(addr->hostname, &addr->ai_list, dstr);
    if (ret != 0)
    {
        addr->ai_list = NULL;
        return false;
    }

    char buf[2048] = "", *bp = buf;
    int bsize = sizeof(buf);
    ai = addr->ai_list;
    while (ai)
    {
        if (ai->ai_addr->sa_family == AF_INET)
        {
            if (!found) found = ai;
            sain = (struct sockaddr_in *)ai->ai_addr;
            append_sprintf(&bp, &bsize, "%s ", inet_ntoa(sain->sin_addr));
        }
        ai = ai->ai_next;
    }
    if (!found)
    {
        LOGE("DNS did not return any usable addresses");
        return false;
    }
    LOGI("resolved %s '%s': %s", dstr, addr->hostname, buf);
    addr->resolved = true;
    addr->ai_curr = NULL;
    return true;
}

void cm2_reset_time()
{
    g_state.timestamp = time_monotonic();
}

int cm2_get_time()
{
    return time_monotonic() - g_state.timestamp;
}

cm2_state_info_t *cm2_get_state_info(cm2_state_e state)
{
    if ((int)state < 0 || state >= CM2_STATE_NUM)
    {
        LOG(ERROR, "Invalid state: %d", state);
        static cm2_state_info_t invalid = { "INVALID", 0 };
        return &invalid;
    }
    return &cm2_state_info[state];
}

cm2_state_info_t *cm2_curr_state_info()
{
    return cm2_get_state_info(g_state.state);
}

char *cm2_get_state_name(cm2_state_e state)
{
    return cm2_get_state_info(state)->name;
}

char *cm2_curr_state_name()
{
    return cm2_curr_state_info()->name;
}

int cm2_get_timeout()
{
    return cm2_curr_state_info()->timeout;
}

bool cm2_timeout()
{
    int seconds = cm2_get_timeout();
    int delta = cm2_get_time();
    if (seconds != CM2_NO_TIMEOUT && delta >= seconds)
    {
        LOG(WARNING, "State %s timeout: %d >= %d",
                cm2_curr_state_name(), delta, seconds);
        return true;
    }
    return false;
}

void cm2_set_state(bool success, cm2_state_e state)
{
    if (g_state.state == state)
    {
        LOG(WARNING, "Same state %s %s",
                str_success(success),
                cm2_get_state_name(state));
        return;
    }
    LOG_SEVERITY(success ? LOG_SEVERITY_NOTICE : LOG_SEVERITY_ERR,
            "State %s %s -> %s",
            cm2_curr_state_name(),
            str_success(success),
            cm2_get_state_name(state));
    g_state.state = state;
    cm2_reset_time();
    g_state.state_changed = true;
}

bool cm2_state_changed()
{
    bool changed = g_state.state_changed;
    g_state.state_changed = false;
    return changed;
}

struct addrinfo* cm2_get_next_addrinfo(cm2_addr_t *addr)
{
    struct addrinfo *ai;
    if (addr->ai_curr)
    {
        ai = addr->ai_curr->ai_next;
    }
    else
    {
        ai = addr->ai_list;
    }
    while (ai)
    {
        if (ai->ai_addr->sa_family == AF_INET) break;
        ai = ai->ai_next;
    }
    if (!ai)
    {
        LOGE("No more addresses left");
        return NULL;
    }
    addr->ai_curr = ai;
    return ai;
}

bool cm2_write_target_addr(cm2_addr_t *addr, struct addrinfo *ai)
{
    if (!ai) return false;
    char target[256];
    struct sockaddr_in *sain;
    sain = (struct sockaddr_in *)ai->ai_addr;
    snprintf(target, sizeof(target), "%s:%s:%d",
            addr->proto, inet_ntoa(sain->sin_addr), addr->port);
    bool ret = cm2_ovsdb_set_Manager_target(target);
    if (ret)
    {
        addr->ai_curr = ai;
        LOGI("Trying to connect to: %s : %s", cm2_curr_dest_name(), target);
    }
    return ret;
}

bool cm2_write_current_target_addr()
{
    cm2_addr_t *addr = cm2_get_addr(g_state.dest);
    struct addrinfo *ai = addr->ai_curr;
    return cm2_write_target_addr(addr, ai);
}


bool cm2_write_next_target_addr()
{
    cm2_addr_t *addr = cm2_get_addr(g_state.dest);
    struct addrinfo *ai = cm2_get_next_addrinfo(addr);
    return cm2_write_target_addr(addr, ai);
}

void cm2_clear_manager_addr()
{
    cm2_ovsdb_set_AWLAN_Node_manager_addr("");
    cm2_set_addr(CM2_DEST_MANAGER, "");
}

bool cm2_is_connected_to(cm2_dest_e dest)
{
    return (g_state.state == CM2_STATE_CONNECTED && g_state.dest == dest);
}

#define CM2_STATE_DIR  "/tmp/plume/"
#define CM2_STATE_FILE CM2_STATE_DIR"cm.state"
#define CM2_STATE_TMP  CM2_STATE_DIR"cm.state.tmp"

void cm2_log_state(cm2_reason_e reason)
{
    LOG(DEBUG, "=== Update s: %s r: %s t: %d o: %d",
            cm2_curr_state_name(), cm2_reason_name[reason],
            cm2_get_time(), cm2_get_timeout());
    // dump current state to /tmp
    time_t t = time_real();
    struct tm *lt = localtime(&t);
    char   timestr[80];
    char   str[1024] = "";
    char   *s = str;
    int    ss = sizeof(str);
    int    len, ret;
    strftime(timestr, sizeof(timestr), "%d %b %H:%M:%S %Z", lt);
    append_sprintf(&s, &ss, "%s\n", timestr);
    append_sprintf(&s, &ss, "s: %s to: %s\n",
            cm2_curr_state_name(),
            cm2_curr_dest_name());
    append_sprintf(&s, &ss, "r: %s t: %d o: %d dis: %d\n",
            cm2_reason_name[reason],
            cm2_get_time(),
            cm2_get_timeout(),
            g_state.disconnects);
    append_sprintf(&s, &ss, "redir:  u:%d v:%d r:%d '%s'\n",
            g_state.addr_redirector.updated,
            g_state.addr_redirector.valid,
            g_state.addr_redirector.resolved,
            g_state.addr_redirector.resource);
    append_sprintf(&s, &ss, "manager: u:%d v:%d r:%d '%s'\n",
            g_state.addr_manager.updated,
            g_state.addr_manager.valid,
            g_state.addr_manager.resolved,
            g_state.addr_manager.resource);
    int fd;
    mkdir(CM2_STATE_DIR, 0755);
    fd = open(CM2_STATE_TMP, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (!fd) return;
    len = strlen(str);
    ret = write(fd, str, len);
    close(fd);
    if (ret == len) {
        rename(CM2_STATE_TMP, CM2_STATE_FILE);
    }
}

static void cm2_compute_backoff()
{
    unsigned int backoff = g_state.max_backoff;
    // Get a random value from /dev/urandom
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1) {
	LOGE("Error opening /dev/urandom");
	goto set_backoff;
    }
    int ret = read(fd, &backoff, sizeof(backoff));
    if (ret <= 0) {
	LOGE("Error reading /dev/urandom");
	goto set_backoff;
    }
    backoff = (backoff % (g_state.max_backoff - g_state.min_backoff)) +
		g_state.min_backoff;

  set_backoff:
    cm2_state_info[CM2_STATE_QUIESCE_OVS].timeout = backoff;
}

/*

Basic logic:

Start by connecting to redirector_addr

In any state:
  if redirector_addr is updated then try to connect to redirector.
In any state except if connected to manager:
  if manager_addr is updated then try to connect to manager.

Each connect attempt goes through all resolved IPs until connection established

If we get disconnected from manager:
  count disconnects += 1
  if disconnects > 3 go back to redirector
  wait up to 1 minute if ovsdb is able to re-connect (*Note-1)
  if re-connect is successful and connection is stable for more than 10 minutes
    then reset disconnects = 0
  if re-connect does not happen, go back to redirector

If anything else goes wrong go back to redirector

Note-1: the wait for re-connect back to same manager addr because
  while doing channel/topology change we expect to loose the connection
  for a short while, but to speed up re-connect to controller we try to
  avoid going through redirector.

*/


void cm2_update_state(cm2_reason_e reason)
{
start:
    cm2_log_state(reason);
    cm2_state_e old_state = g_state.state;

    // check for AWLAN_Node and Manager tables
    if (!g_state.have_awlan) return;
    if (!g_state.have_manager) return;

    // received new redirector address?
    if (  (g_state.state != CM2_STATE_INIT)
        && g_state.addr_redirector.updated
        && g_state.addr_redirector.valid)
    {
        cm2_set_state(true, CM2_STATE_INIT);
        g_state.addr_redirector.updated = false;
    }
    // received new manager address?
    else if ( !cm2_is_connected_to(CM2_DEST_MANAGER)
            && g_state.addr_manager.updated
            && g_state.addr_manager.valid)
    {
        LOGI("Received manager address");
        cm2_set_state(true, CM2_STATE_TRY_RESOLVE);
        g_state.dest = CM2_DEST_MANAGER;
    }

    switch (g_state.state)
    {

        default:
        case CM2_STATE_INIT:
            if (g_state.addr_redirector.valid)
            {
                // have redirector address
                // clear manager_addr
                cm2_clear_manager_addr();
                // try to resolve redirector
                g_state.dest = CM2_DEST_REDIR;
                cm2_set_state(true, CM2_STATE_TRY_RESOLVE);
                g_state.disconnects = 0;
            }
            break;


        case CM2_STATE_TRY_RESOLVE:
            if (cm2_state_changed()) // first iteration
            {
                LOGI("Trying to resolve %s: %s", cm2_curr_dest_name(),
                        cm2_get_addr(g_state.dest)->hostname);
            }
            if (cm2_resolve(g_state.dest))
            {
                // succesfully resolved
                cm2_set_state(true, CM2_STATE_RE_CONNECT);
            }
            else if (cm2_timeout())
            {
                cm2_set_state(false, CM2_STATE_INIT);
            }
            // else keep re-trying
            break;


        case CM2_STATE_RE_CONNECT:
            // disconnect, wait for disconnect then try to connect
            if (cm2_state_changed()) // first iteration
            {
                // disconnect
                cm2_ovsdb_set_Manager_target("");
            }
            if (!g_state.connected)
            {
                // disconnected, go to try_connect
                cm2_set_state(true, CM2_STATE_TRY_CONNECT);
            }
            else if (cm2_timeout())
            {
                // stuck? back to init
                cm2_set_state(false, CM2_STATE_INIT);
            }
            break;


        case CM2_STATE_TRY_CONNECT:
            if (cm2_curr_addr()->updated)
            {
                // address changed while trying to connect, not normally expected
                // unless manually changed during development/debugging but
                // handle anyway: go back to resolve new address
                cm2_set_state(true, CM2_STATE_TRY_RESOLVE);
                break;
            }
            if (cm2_state_changed()) // first iteration
            {
                if (!cm2_write_next_target_addr())
                {
                    cm2_set_state(false, CM2_STATE_INIT);
                }
            }
            if (g_state.connected)
            {
                // connected
                cm2_set_state(true, CM2_STATE_CONNECTED);
            }
            else if (cm2_timeout())
            {
                // timeout - write next address
                if (cm2_write_next_target_addr())
                {
                    cm2_reset_time();
                }
                else
                {
                    // no more addresses
                    cm2_set_state(false, CM2_STATE_INIT);
                }
            }
            break;


        case CM2_STATE_CONNECTED:
            if (cm2_state_changed()) // first iteration
            {
                LOG(NOTICE, "===== Connected to: %s", cm2_curr_dest_name());
            }
            if (g_state.connected && g_state.disconnects
                    && cm2_get_time() > CM2_STABLE_PERIOD)
            {
                LOGI("Stable connection (%d > %d) - reset disconnect count (%d -> 0)",
                        cm2_get_time(), CM2_STABLE_PERIOD, g_state.disconnects);
                g_state.disconnects = 0;
            }
            if (!g_state.connected)
            {
                cm2_set_state(false, CM2_STATE_QUIESCE_OVS);
            }
            break;


        case CM2_STATE_QUIESCE_OVS:
	    if (cm2_state_changed())
	    {
		// quiesce ovsdb-server, wait for timeout
                cm2_ovsdb_set_Manager_target("");
		// Update timeouts based on AWLAN_Node contents
		cm2_compute_backoff();
		LOG(NOTICE, "===== Quiescing connection to: %s for %d seconds",
		    cm2_curr_dest_name(), cm2_get_timeout());
	    }

	    if (g_state.connected)
	    {
                // connected
                cm2_set_state(true, CM2_STATE_CONNECTED);
            }

	    if (cm2_timeout())
	    {
                g_state.disconnects += 1;
                if (g_state.disconnects > CM2_MAX_DISCONNECTS)
                {
                    // too many unsuccessful connect attempts, go back to redirector
                    LOGE("Too many disconnects (%d/%d) back to redirector",
                            g_state.disconnects, CM2_MAX_DISCONNECTS);
                    cm2_set_state(false, CM2_STATE_INIT);
		} else {
		    // Try again connecting to the current controller
		    cm2_write_current_target_addr();
		    cm2_compute_backoff();
		    cm2_reset_time();
		}
	    }
	    break;
    }

    if (cm2_timeout())
    {
        // unexpected, just in case
        LOG(ERROR, "Unhandled timeout");
        cm2_set_state(false, CM2_STATE_INIT);
    }

    if (old_state != g_state.state)
    {
        reason = CM2_REASON_CHANGE;
        goto start;
    }
    LOG(TRACE, "<== Update s: %s", cm2_curr_state_name());
}

void cm2_trigger_update(cm2_reason_e reason)
{
    (void)reason;
    LOG(TRACE, "Trigger s: %s r: %s", cm2_curr_state_name(), cm2_reason_name[reason]);
    g_state.reason = reason;
    ev_timer_stop(g_state.loop, &g_state.timer);
    ev_timer_set(&g_state.timer, 0.1, CM2_EVENT_INTERVAL);
    ev_timer_start(g_state.loop, &g_state.timer);
}

void cm2_event_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;
    cm2_reason_e reason = g_state.reason;
    // set reason for next iteration, unless triggered by something else
    g_state.reason = CM2_REASON_TIMER;
    cm2_update_state(reason);
}

void cm2_event_init(struct ev_loop *loop)
{
    LOGI("Initializing CM event");

    g_state.reason = CM2_REASON_TIMER;
    g_state.loop = loop;
    ev_timer_init(&g_state.timer, cm2_event_cb, CM2_EVENT_INTERVAL, CM2_EVENT_INTERVAL);
    g_state.timer.data = NULL;
    ev_timer_start(g_state.loop, &g_state.timer);
}

void cm2_event_close(struct ev_loop *loop)
{
    LOGI("Stopping CM event");
    (void)loop;
}
