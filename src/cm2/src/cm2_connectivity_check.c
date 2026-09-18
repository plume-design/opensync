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
 * CM2 Connectivity_Check probe runner.
 *
 * Monitors the Connectivity_Check OVSDB table. For each enabled row, runs
 * periodic probes (icmp) bound to the specified interface. Writes
 * status=nok when consecutive probe failures span >= timeout seconds;
 * status=ok when a probe succeeds.
 *
 * CM2 is a pure probe runner: no CMU awareness, no pipeline knowledge.
 * WANO reads Connectivity_Check.status to gate STA WAN activation.
 */

#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>

#include <ev.h>

#include "ds_tree.h"
#include "log.h"
#include "memutil.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "schema.h"

#define MODULE_ID LOG_MODULE_ID_MISC

#define CC_INTERVAL_MIN     5
#define CC_TIMEOUT_MIN      10
#define CC_INTERVAL_DEFAULT 5
#define CC_TIMEOUT_DEFAULT  10
#define CC_PING_WAIT_SEC    1

struct cc_probe
{
    char cp_name[64];
    char cp_if_name[C_IFNAME_LEN];
    char cp_target[256];
    char cp_type[16];
    int cp_interval;
    int cp_timeout;
    bool cp_enable;

    ev_timer cp_poll_timer;
    ev_child cp_child;
    pid_t cp_pid;
    double cp_last_success; /* CLOCK_MONOTONIC secs; failure window measured from last success */
    char cp_status[8];      /* last written status */

    ds_tree_node_t cp_tnode;
};

static ovsdb_table_t table_Connectivity_Check;
static ds_tree_t g_cc_probes = DS_TREE_INIT(ds_str_cmp, struct cc_probe, cp_tnode);

static void cc_probe_start_poll(struct cc_probe *cp);
static void cc_probe_stop(struct cc_probe *cp);
static void cc_run_probe(struct cc_probe *cp);
static void cc_poll_timer_cb(struct ev_loop *loop, ev_timer *w, int revents);
static void cc_child_cb(struct ev_loop *loop, ev_child *w, int revents);
static void callback_Connectivity_Check(
        ovsdb_update_monitor_t *mon,
        struct schema_Connectivity_Check *old,
        struct schema_Connectivity_Check *new);

static double cc_mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void cc_probe_write_status(struct cc_probe *cp, const char *status)
{
    struct schema_Connectivity_Check schema;
    char old_status[sizeof(cp->cp_status)];
    int rc;

    if (strcmp(cp->cp_status, status) == 0) return;

    STRSCPY(old_status, cp->cp_status);
    STRSCPY(cp->cp_status, status);

    MEMZERO(schema);
    schema._partial_update = true;
    SCHEMA_SET_STR(schema.status, status);

    rc = ovsdb_table_update_where(
            &table_Connectivity_Check,
            ovsdb_where_simple(SCHEMA_COLUMN(Connectivity_Check, name), cp->cp_name),
            &schema);
    if (rc <= 0)
    {
        STRSCPY(cp->cp_status, old_status);
        LOG(WARN, "cc: %s: failed to write status=%s", cp->cp_name, status);
    }
    else
        LOG(INFO, "cc: %s: status=%s", cp->cp_name, status);
}

static void cc_child_cb(struct ev_loop *loop, ev_child *w, int revents)
{
    struct cc_probe *cp = CONTAINER_OF(w, struct cc_probe, cp_child);
    double now = cc_mono_now();
    bool success;

    (void)revents;
    ev_child_stop(loop, w);
    cp->cp_pid = 0;

    success = WIFEXITED(w->rstatus) && WEXITSTATUS(w->rstatus) == 0;

    if (success)
    {
        cp->cp_last_success = now;
        cc_probe_write_status(cp, "ok");
    }
    else
    {
        if (cp->cp_last_success > 0.0 && (now - cp->cp_last_success) >= (double)cp->cp_timeout)
            cc_probe_write_status(cp, "nok");
    }

    if (cp->cp_enable)
    {
        ev_timer_set(&cp->cp_poll_timer, (double)cp->cp_interval, 0.0);
        ev_timer_start(EV_DEFAULT, &cp->cp_poll_timer);
    }
}

static void cc_run_probe(struct cc_probe *cp)
{
    char ping_wait_str[16];
    pid_t pid;

    if (cp->cp_pid != 0) return; /* previous probe still running */

    if (strcmp(cp->cp_type, "icmp") == 0)
    {
        snprintf(ping_wait_str, sizeof(ping_wait_str), "%d", CC_PING_WAIT_SEC);

        pid = fork();
        if (pid < 0)
        {
            LOG(WARN, "cc: %s: fork failed", cp->cp_name);
            return;
        }
        if (pid == 0)
        {
            if (cp->cp_if_name[0] != '\0')
            {
                char *argv[] = {"ping", "-c", "1", "-W", ping_wait_str, "-I", cp->cp_if_name, cp->cp_target, NULL};
                execvp("ping", argv);
            }
            else
            {
                char *argv[] = {"ping", "-c", "1", "-W", ping_wait_str, cp->cp_target, NULL};
                execvp("ping", argv);
            }
            _exit(127);
        }
    }
    else
    {
        LOG(WARN, "cc: %s: probe type '%s' not implemented", cp->cp_name, cp->cp_type);
        return;
    }

    cp->cp_pid = pid;
    ev_child_init(&cp->cp_child, cc_child_cb, pid, 0);
    ev_child_start(EV_DEFAULT, &cp->cp_child);
}

static void cc_poll_timer_cb(struct ev_loop *loop, ev_timer *w, int revents)
{
    ev_timer_stop(loop, w);
    (void)revents;
    struct cc_probe *cp = CONTAINER_OF(w, struct cc_probe, cp_poll_timer);
    cc_run_probe(cp);
}

static void cc_probe_start_poll(struct cc_probe *cp)
{
    /* Fire once immediately; next probe gets scheduled in child callback. */
    ev_timer_init(&cp->cp_poll_timer, cc_poll_timer_cb, 0.0, 0.0);
    ev_timer_start(EV_DEFAULT, &cp->cp_poll_timer);
}

static void cc_probe_stop(struct cc_probe *cp)
{
    int wstatus;

    ev_timer_stop(EV_DEFAULT, &cp->cp_poll_timer);
    if (cp->cp_pid != 0)
    {
        ev_child_stop(EV_DEFAULT, &cp->cp_child);
        kill(cp->cp_pid, SIGTERM);
        if (waitpid(cp->cp_pid, &wstatus, 0) < 0) LOG(WARN, "cc: %s: waitpid(%d) failed", cp->cp_name, (int)cp->cp_pid);
        cp->cp_pid = 0;
    }
}

static void cc_probe_apply(struct cc_probe *cp, struct schema_Connectivity_Check *row)
{
    int new_interval = (row->interval_exists && row->interval >= CC_INTERVAL_MIN) ? row->interval : CC_INTERVAL_DEFAULT;
    int new_timeout = (row->timeout_exists && row->timeout >= CC_TIMEOUT_MIN) ? row->timeout : CC_TIMEOUT_DEFAULT;
    if (new_timeout < new_interval)
    {
        LOG(WARN,
            "cc: %s: timeout(%d) < interval(%d); clamping timeout to interval",
            cp->cp_name,
            new_timeout,
            new_interval);
        new_timeout = new_interval;
    }

    bool config_changed = strcmp(cp->cp_if_name, row->if_name_exists ? row->if_name : "") != 0
                          || strcmp(cp->cp_target, row->target) != 0 || strcmp(cp->cp_type, row->type) != 0
                          || cp->cp_interval != new_interval || cp->cp_timeout != new_timeout;

    if (!row->enable)
    {
        cc_probe_stop(cp);
        cc_probe_write_status(cp, "na");
        cp->cp_enable = false;
        cp->cp_last_success = 0.0;
        return;
    }

    STRSCPY(cp->cp_if_name, row->if_name_exists ? row->if_name : "");
    STRSCPY(cp->cp_target, row->target);
    STRSCPY(cp->cp_type, row->type);
    cp->cp_interval = new_interval;
    cp->cp_timeout = new_timeout;
    cp->cp_enable = true;

    if (config_changed || !ev_is_active(&cp->cp_poll_timer))
    {
        cc_probe_stop(cp);
        cp->cp_last_success = cc_mono_now();
        cc_probe_start_poll(cp);
    }
}

static void callback_Connectivity_Check(
        ovsdb_update_monitor_t *mon,
        struct schema_Connectivity_Check *old,
        struct schema_Connectivity_Check *new)
{
    struct cc_probe *cp;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            cp = ds_tree_find(&g_cc_probes, new->name);
            if (cp == NULL)
            {
                cp = CALLOC(1, sizeof(*cp));
                STRSCPY(cp->cp_name, new->name);
                STRSCPY(cp->cp_status, "na");
                ds_tree_insert(&g_cc_probes, cp, cp->cp_name);
            }
            cc_probe_apply(cp, new);
            break;

        case OVSDB_UPDATE_DEL:
            cp = ds_tree_find(&g_cc_probes, old->name);
            if (cp != NULL)
            {
                cc_probe_stop(cp);
                ds_tree_remove(&g_cc_probes, cp);
                FREE(cp);
            }
            break;

        default:
            break;
    }
}

void cm2_connectivity_check_init(void)
{
    OVSDB_TABLE_INIT(Connectivity_Check, name);
    OVSDB_TABLE_MONITOR_F(Connectivity_Check, C_VPACK("-", "_version", "status"));  // "status" is what we write to

    LOG(INFO, "cc: initialized");
}

void cm2_connectivity_check_close(void)
{
    struct cc_probe *cp;
    ds_tree_iter_t iter;

    for (cp = ds_tree_ifirst(&iter, &g_cc_probes); cp != NULL; cp = ds_tree_inext(&iter))
    {
        cc_probe_stop(cp);
        ds_tree_iremove(&iter);
        FREE(cp);
    }
}
