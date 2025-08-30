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

#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>

#include <ds_tree.h>
#include <memutil.h>
#include <log.h>
#include <util.h>

#include "glob.h"
#include "errno.h"
#include "sm_lat_sys.h"

#define EPPING_PATH    "/usr/opensync/tools/epping/"
#define EPPING_LIBRARY EPPING_PATH
#define EPPING_BINARY  EPPING_PATH "pping"
#define EPPING_LOADER  EPPING_PATH "ld-musl-armhf.so.1"
#define EPPING_PID_DIR "/tmp/epping"

#define LOG_PREFIX(fmt, ...) "sm: lat: sys: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_IFNAME(i, fmt, ...) LOG_PREFIX("ifname: %s: " fmt, (i)->name, ##__VA_ARGS__)

#define LOG_PREFIX_DEST(dest, fmt, ...)                       \
    LOG_PREFIX_IFNAME(                                        \
            (dest)->sif,                                      \
            "(m:%02x:%02x:%02x:%02x:%02x:%02x d:%hhu): " fmt, \
            (dest)->flow.mac[0],                              \
            (dest)->flow.mac[1],                              \
            (dest)->flow.mac[2],                              \
            (dest)->flow.mac[3],                              \
            (dest)->flow.mac[4],                              \
            (dest)->flow.mac[5],                              \
            (dest)->flow.dscp,                                \
            ##__VA_ARGS__)

struct flow_tuple
{
    uint8_t mac[6];
    uint8_t dscp;
};

struct sm_lat_sys_dest
{
    ds_tree_node_t node;
    struct flow_tuple flow; /* sm_lat_sys_ifname::dests */
    uint32_t min_ms;
    uint32_t max_ms;
    uint32_t avg_ms;
    uint32_t last_ms;
    uint32_t num_pkts;
    struct sm_lat_sys_ifname *sif;
};

struct sm_lat_sys_ifname
{
    ds_tree_node_t node;
    ds_tree_t dests; /* sm_lat_sys_dest (node) */
    char *name;      /* sm_lat_sys::ifnames */
    struct pipe_buf
    {
        char *data;
        size_t len;
    } buf;
    struct ev_io io;
    struct ev_child child;
    struct ev_timer retry;
    struct ev_timer kill;
    bool enabled;
    sm_lat_sys_t *sys;
};

struct sm_lat_sys
{
    ds_tree_t ifnames; /* sm_lat_sys_ifname (node) */
    sm_lat_sys_report_fn_t *report_fn;
    void *report_fn_priv;
    struct ev_loop *loop;
    bool dscp_enabled;
    bool min_enabled;
    bool max_enabled;
    bool avg_enabled;
    bool last_enabled;
    bool num_enabled;
};

struct sm_lat_sys_poll
{
};

struct sm_lat_sys_sample
{
    char *ifname;
    struct sm_lat_sys_dest *dest;
    sm_lat_sys_t *sys;
};

static void sm_lat_sys_ifname_start(struct sm_lat_sys_ifname *sif);
static void sm_lat_sys_ifname_stop(struct sm_lat_sys_ifname *sif);

const char *sm_lat_sys_sample_get_ifname(const sm_lat_sys_sample_t *s)
{
    return s->ifname;
}
const uint8_t *sm_lat_sys_sample_get_mac_address(const sm_lat_sys_sample_t *s)
{
    return s->dest->flow.mac;
}
const uint8_t *sm_lat_sys_sample_get_dscp(const sm_lat_sys_sample_t *s)
{
    return s->sys->dscp_enabled ? &s->dest->flow.dscp : NULL;
}
const uint32_t *sm_lat_sys_sample_get_min(const sm_lat_sys_sample_t *s)
{
    return s->sys->min_enabled ? &s->dest->min_ms : NULL;
}
const uint32_t *sm_lat_sys_sample_get_max(const sm_lat_sys_sample_t *s)
{
    return s->sys->max_enabled ? &s->dest->max_ms : NULL;
}
const uint32_t *sm_lat_sys_sample_get_avg(const sm_lat_sys_sample_t *s)
{
    return s->sys->avg_enabled ? &s->dest->avg_ms : NULL;
}
const uint32_t *sm_lat_sys_sample_get_last(const sm_lat_sys_sample_t *s)
{
    return s->sys->last_enabled ? &s->dest->last_ms : NULL;
}
const uint32_t *sm_lat_sys_sample_get_num_pkts(const sm_lat_sys_sample_t *s)
{
    return s->sys->num_enabled ? &s->dest->num_pkts : NULL;
}

#define SYS_SET_ENABLED(sys, var, toggle)                                          \
    if (sys == NULL) return;                                                       \
    if ((sys)->var##_enabled == toggle) LOGI(LOG_PREFIX("%s: not changed", #var)); \
    LOGI(LOG_PREFIX("%s: changed to %s", #var, toggle ? "true" : "false"));        \
    (sys)->var##_enabled = toggle;

void sm_lat_sys_dscp_set(sm_lat_sys_t *s, bool enable)
{
    SYS_SET_ENABLED(s, dscp, enable)
}
void sm_lat_sys_kind_set_min(sm_lat_sys_t *s, bool enable)
{
    SYS_SET_ENABLED(s, min, enable)
}
void sm_lat_sys_kind_set_max(sm_lat_sys_t *s, bool enable)
{
    SYS_SET_ENABLED(s, max, enable)
}
void sm_lat_sys_kind_set_avg(sm_lat_sys_t *s, bool enable)
{
    SYS_SET_ENABLED(s, avg, enable)
}
void sm_lat_sys_kind_set_last(sm_lat_sys_t *s, bool enable)
{
    SYS_SET_ENABLED(s, last, enable)
}
void sm_lat_sys_kind_set_num_pkts(sm_lat_sys_t *s, bool enable)
{
    SYS_SET_ENABLED(s, num, enable)
}

static int sm_lat_sys_flow_cmp(const void *a, const void *b)
{
    const struct flow_tuple *x = a;
    const struct flow_tuple *y = b;
    const int r1 = memcmp(x->mac, y->mac, sizeof(x->mac));
    const int r2 = (int)x->dscp - (int)y->dscp;
    if (r1) return r1;
    if (r2) return r2;
    return 0;
}

static int sm_lat_sys_pid_file_create(const pid_t pid)
{
    int fd;
    if ((fd = open(strfmta("%s/%d", EPPING_PID_DIR, pid), O_CREAT, 000)) == -1)
    {
        LOGE(LOG_PREFIX("failed to create epping pid file [%d]", pid));
        return -1;
    }
    LOGD(LOG_PREFIX("created epping pid file [%d]", pid));
    close(fd);
    return 0;
}

static void sm_lat_sys_pid_file_remove(const pid_t pid)
{
    if (unlink(strfmta("%s/%d", EPPING_PID_DIR, pid)) != 0)
    {
        LOGE(LOG_PREFIX("failed to remove epping pid file [%d]", pid));
        return;
    }
    LOGD(LOG_PREFIX("removed epping pid file [%d]", pid));
}

static bool sm_lat_sys_pid_file_is_valid(const pid_t pid)
{
    char exe[128];

    if (pid && os_readlink(strfmta("/proc/%d/exe", pid), exe, sizeof(exe)) != -1 && strstr(exe, EPPING_LOADER) != NULL)
    {
        return true;
    }
    return false;
}

static int sm_lat_sys_state_clean(void)
{
    glob_t g = {0};

    LOGI(LOG_PREFIX("cleaning epping state"));
    if (mkdir(EPPING_PID_DIR, 0700) != 0)
    {
        if (errno == EEXIST)
        {
            size_t i;
            glob(EPPING_PID_DIR "/*", 0, NULL, &g);
            LOGD(LOG_PREFIX("%d pids to clean", g.gl_pathc));

            for (i = 0; i < g.gl_pathc; i++)
            {
                char *path = strdupa(g.gl_pathv[i]);
                int pid = atoi(basename(path));
                if (sm_lat_sys_pid_file_is_valid(pid))
                {
                    LOGD(LOG_PREFIX("killing epping pid[%d]", pid));
                    kill(pid, SIGKILL);
                }
                else
                {
                    LOGI(LOG_PREFIX("can't kill pid[%d], epping process not valid or already dead", pid));
                }
                unlink(g.gl_pathv[i]);
            }
            globfree(&g);
        }
        else
        {
            LOGE(LOG_PREFIX("error creating epping pid folder, aborting"));
            return -1;
        }
    }
    return 0;
}

sm_lat_sys_t *sm_lat_sys_alloc(void)
{
    if (sm_lat_sys_state_clean() != 0) return NULL;
    sm_lat_sys_t *sys = CALLOC(1, sizeof(*sys));
    ds_tree_init(&sys->ifnames, ds_str_cmp, struct sm_lat_sys_ifname, node);
    sys->loop = EV_DEFAULT;
    LOGI(LOG_PREFIX("allocated"));
    return sys;
}

void sm_lat_sys_drop(sm_lat_sys_t *s)
{
    if (s == NULL) return;
    LOGI(LOG_PREFIX("dropping"));
    sm_lat_sys_ifname_flush(s);
    FREE(s);
}

static struct sm_lat_sys_dest *sm_lat_sys_dest_alloc(struct sm_lat_sys_ifname *sif, const struct flow_tuple *flow)
{
    struct sm_lat_sys_dest *dest = CALLOC(1, sizeof(*dest));
    memcpy(&dest->flow, flow, sizeof(dest->flow));
    dest->sif = sif;
    dest->min_ms = UINT32_MAX;
    ds_tree_insert(&sif->dests, dest, &dest->flow);
    LOGD(LOG_PREFIX_DEST(dest, "allocated"));
    return dest;
}

static struct sm_lat_sys_dest *sm_lat_sys_dest_lookup_or_alloc(
        struct sm_lat_sys_ifname *sif,
        const struct flow_tuple *flow)
{
    return ds_tree_find(&sif->dests, flow) ?: sm_lat_sys_dest_alloc(sif, flow);
}

static void sm_lat_sys_dest_drop(struct sm_lat_sys_dest *dest)
{
    ds_tree_remove(&dest->sif->dests, dest);
    FREE(dest);
}

static void sm_lat_sys_drop_dests(struct sm_lat_sys_ifname *sif)
{
    struct sm_lat_sys_dest *dest;
    while ((dest = ds_tree_head(&sif->dests)) != NULL)
    {
        sm_lat_sys_dest_drop(dest);
    }
}

static bool sm_lat_sys_ifname_is_running(struct sm_lat_sys_ifname *sif)
{
    return (sif->child.pid != 0);
}

static bool sm_lat_sys_ifname_is_enabled(struct sm_lat_sys_ifname *sif)
{
    return sif->enabled;
}

static void sm_lat_sys_ifname_retry_cb(EV_P_ ev_timer *arg, int events)
{
    struct sm_lat_sys_ifname *sif = CONTAINER_OF(arg, struct sm_lat_sys_ifname, retry);
    sm_lat_sys_ifname_start(sif);
}

static void sm_lat_sys_ifname_kill_cb(EV_P_ ev_timer *arg, int events)
{
    /* If SIGTERM and child_cb were successful we don't enter here due to ev_timer_stop() */
    struct sm_lat_sys_ifname *sif = CONTAINER_OF(arg, struct sm_lat_sys_ifname, kill);

    /* SIGTERM worked and child_cb() is pending */
    if (!sm_lat_sys_ifname_is_running(sif)) return;
    /* Check if in meantime service was started again */
    if (sm_lat_sys_ifname_is_enabled(sif)) return;

    LOGI(LOG_PREFIX_IFNAME(sif, "epping didn't stop, terminating pid[%d]", sif->child.pid));
    kill(sif->child.pid, SIGKILL);
}

static struct sm_lat_sys_ifname *sm_lat_sys_ifname_alloc(sm_lat_sys_t *s, const char *ifname)
{
    struct sm_lat_sys_ifname *sif = CALLOC(1, sizeof(*sif));
    ds_tree_init(&sif->dests, sm_lat_sys_flow_cmp, struct sm_lat_sys_dest, node);
    sif->name = STRDUP(ifname);
    sif->sys = s;
    ds_tree_insert(&s->ifnames, sif, sif->name);
    ev_timer_init(&sif->retry, sm_lat_sys_ifname_retry_cb, 5, 0);
    ev_timer_init(&sif->kill, sm_lat_sys_ifname_kill_cb, 3, 0);
    LOGI(LOG_PREFIX_IFNAME(sif, "allocated"));
    return sif;
}

static void sm_lat_sys_ifname_drop(struct sm_lat_sys_ifname *sif)
{
    if (sif == NULL) return;
    sm_lat_sys_drop_dests(sif);
    ds_tree_remove(&sif->sys->ifnames, sif);
    ev_child_stop(sif->sys->loop, &sif->child);
    ev_io_stop(sif->sys->loop, &sif->io);
    ev_timer_stop(sif->sys->loop, &sif->retry);
    ev_timer_stop(sif->sys->loop, &sif->kill);
    FREE(sif->name);
    FREE(sif->buf.data);
    FREE(sif);
}

void sm_lat_sys_ifname_set(sm_lat_sys_t *s, const char *if_name, bool enable)
{
    if (s == NULL) return;
    struct sm_lat_sys_ifname *sif = ds_tree_find(&s->ifnames, if_name);

    if (enable)
    {
        if (sif == NULL) sif = sm_lat_sys_ifname_alloc(s, if_name);
        sif->enabled = true;
        LOGI(LOG_PREFIX_IFNAME(sif, "starting"));
        sm_lat_sys_ifname_start(sif);
    }
    else
    {
        if (sif == NULL) return;
        sif->enabled = false;
        LOGI(LOG_PREFIX_IFNAME(sif, "stopping"));
        sm_lat_sys_ifname_stop(sif);
    }
}

void sm_lat_sys_ifname_flush(sm_lat_sys_t *s)
{
    if (s == NULL) return;
    struct sm_lat_sys_ifname *sif;

    ds_tree_foreach (&s->ifnames, sif)
    {
        /* Terminating all epping processes */
        sif->enabled = false;
        sm_lat_sys_ifname_stop(sif);
    }
}

void sm_lat_sys_set_report_fn_t(sm_lat_sys_t *s, sm_lat_sys_report_fn_t *fn, void *priv)
{
    if (s == NULL) return;
    s->report_fn = fn;
    s->report_fn_priv = priv;
}

sm_lat_sys_poll_t *sm_lat_sys_poll(sm_lat_sys_t *s, sm_lat_sys_done_fn_t *fn, void *priv)
{
    if (s == NULL) return NULL;
    struct sm_lat_sys_ifname *sif;
    struct sm_lat_sys_dest *d;
    struct sm_lat_sys_sample sample;
    sample.sys = s;

    ds_tree_foreach (&s->ifnames, sif)
    {
        sample.ifname = sif->name;
        while ((d = ds_tree_head(&sif->dests)) != NULL)
        {
            sample.dest = d;
            sample.dest->avg_ms /= sample.dest->num_pkts;
            if (s->report_fn != NULL)
            {
                s->report_fn(priv, &sample);
            }
            sm_lat_sys_dest_drop(d);
        }
    }

    /* All reports sent - notify core*/
    if (fn != NULL) fn(priv);

    return NULL;
}

void sm_lat_sys_poll_drop(sm_lat_sys_poll_t *p)
{
}

static void sm_lat_sys_parse_line(struct sm_lat_sys_ifname *sif, const char *line)
{
    uint32_t rtt;
    struct flow_tuple flow;
    char *str = strdupa(line);
    char *token;
    int i;

    /* line = "<datetime> <rtt> ms <rtt_min> <proto> <SRC_IP>+<DST_IP> \
     * MAC <SRC_MAC>/<DST_MAC> DSCP: <DSCP>
     * Example:
     * 06:23:33.175317859 4.440791 ms 1.823458 ms ICMP 172.29.0.173:447+172.29.1.50:447 \
     * MAC ae:5d:81:76:a8:c7/80:ee:73:f5:a3:d4 DSCP: 0
     */
    i = 0;
    while ((token = strsep(&str, " ")) != NULL)
    {
        if (i == 1)
        {
            sscanf(token, "%" PRIu32 "", &rtt);
        }
        else if (i == 2)
        {
            if (strcmp(token, "ms") != 0) break;
        }
        else if (i == 8)
        {
            sscanf(token,
                   "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &flow.mac[0],
                   &flow.mac[1],
                   &flow.mac[2],
                   &flow.mac[3],
                   &flow.mac[4],
                   &flow.mac[5]);
        }
        else if (i == 10)
        {
            sscanf(token, "%hhu", &flow.dscp);
        }
        i++;
    }
    if (i != 11)
    {
        LOGT(LOG_PREFIX_IFNAME(sif, "parsing error, not valid epping data: %s", line));
        return;
    }

    /* TODO
     * epping might track more than one packet between outputs, we could expose that
     * information in the output itself and use number of packets as weight in the
     * reported values eg. average
     */
    struct sm_lat_sys_dest *dest = sm_lat_sys_dest_lookup_or_alloc(sif, &flow);
    dest->num_pkts++;
    dest->last_ms = rtt;
    if (dest->min_ms > rtt) dest->min_ms = rtt;
    if (dest->max_ms < rtt) dest->max_ms = rtt;
    /* Average is calculated at the end before report as a cumulated value divided by
     * number of packets
     */
    dest->avg_ms += rtt;
    LOGT(LOG_PREFIX_DEST(
            dest,
            "min:%" PRIu32 " max:%" PRIu32 " avg:%" PRIu32 " lst:%" PRIu32 " num:%" PRIu32 "",
            dest->min_ms,
            dest->max_ms,
            dest->avg_ms,
            dest->last_ms,
            dest->num_pkts));
}

static int sm_lat_sys_ifname_buf_read(struct sm_lat_sys_ifname *sif, int fd)
{
    char chunk[4096];
    ssize_t n;
    struct pipe_buf *buf = &sif->buf;

    /* Read chunk of data from process pipe
     * if failed there are 2 scenarios:
     * - process is already dead, stopping it makes no harm
     * - other error occurred; if service is enabled, stop will initiate process restart
     */
    n = read(fd, chunk, sizeof(chunk));
    if (n <= 0)
    {
        LOGD(LOG_PREFIX_IFNAME(sif, "epping read error, process most likely dead, stopping"));
        ev_io_stop(sif->sys->loop, &sif->io);
        sm_lat_sys_ifname_stop(sif);
        return -1;
    }

    /* Extend cache buffer with the new data */
    char *tmp = REALLOC(buf->data, buf->len + n);
    buf->data = tmp;
    memcpy(buf->data + buf->len, chunk, n);
    buf->len += n;
    return 0;
}

static char *sm_lat_sys_buf_next_line(struct pipe_buf *buf)
{
    char *newline_pos;
    char *line;

    /* Check if there are any complete lines in the buffer */
    if (buf == NULL || buf->data == NULL) return NULL;
    newline_pos = memchr(buf->data, '\n', buf->len);
    if (newline_pos == NULL) return NULL;

    /* Extract line and update buffer length to reflect remaining data */
    *newline_pos = 0;
    line = STRDUP(buf->data);
    buf->len -= (newline_pos - buf->data + 1);

    /* Move remaining buffer content to the beginning */
    memmove(buf->data, newline_pos + 1, buf->len);

    /* Shrink buffer to include moved data */
    buf->data = REALLOC(buf->data, buf->len);

    /* Realloc with len 0 is equivalent to free(), NULL data ptr will avoid
     * double FREE() when cleaning sm_lat_sys_ifname structure in the future
     */
    if (buf->len == 0) buf->data = NULL;
    return line;
}

static void sm_lat_sys_io_cb(struct ev_loop *loop, ev_io *arg, int events)
{
    struct sm_lat_sys_ifname *sif = CONTAINER_OF(arg, struct sm_lat_sys_ifname, io);
    char *line;

    /* If read didn't succeed we end up with no data or leftovers which will
     * be discarded by the parser */
    if (sm_lat_sys_ifname_buf_read(sif, arg->fd) != 0) return;
    while ((line = sm_lat_sys_buf_next_line(&sif->buf)) != NULL)
    {
        sm_lat_sys_parse_line(sif, line);
        FREE(line);
    }
}

static void sm_lat_sys_child_cb(EV_P_ ev_child *arg, int events)
{
    struct sm_lat_sys_ifname *sif = CONTAINER_OF(arg, struct sm_lat_sys_ifname, child);

    if (WIFEXITED(arg->rstatus))
    {
        LOGI(LOG_PREFIX_IFNAME(sif, "epping pid[%d] exited with status %d", sif->child.pid, WEXITSTATUS(arg->rstatus)));
    }
    else if (WIFSIGNALED(arg->rstatus))
    {
        LOGI(LOG_PREFIX_IFNAME(sif, "epping pid[%d] terminated by signal %d", sif->child.pid, WTERMSIG(arg->rstatus)));
    }

    sm_lat_sys_pid_file_remove(sif->child.pid);
    sif->child.pid = 0;
    close(sif->io.fd);

    ev_timer_stop(sif->sys->loop, &sif->kill);

    /* Try to restart if died unexpectedly or service has been started again */
    if (sm_lat_sys_ifname_is_enabled(sif))
    {
        ev_child_stop(sif->sys->loop, &sif->child);
        ev_io_stop(sif->sys->loop, &sif->io);

        LOGI(LOG_PREFIX_IFNAME(sif, "retrying to start epping"));
        ev_timer_stop(sif->sys->loop, &sif->retry);
        ev_timer_set(&sif->retry, 5, 0);
        ev_timer_start(sif->sys->loop, &sif->retry);
        return;
    }
    /* Expected termination, clean memory */
    sm_lat_sys_ifname_drop(sif);
}

static int sm_lat_sys_epping_start(const char *ifname, pid_t *pid, int *read_fd)
{
    const char *argv[] =
            {"env", "LD_LIBRARY_PATH=" EPPING_LIBRARY, EPPING_LOADER, EPPING_BINARY, "-f", "-i", ifname, NULL};

    return strexread_spawn(argv[0], argv, pid, read_fd);
}

static void sm_lat_sys_ifname_schedule_retry(struct sm_lat_sys_ifname *sif)
{
    ev_timer_stop(sif->sys->loop, &sif->retry);
    ev_timer_set(&sif->retry, 5, 0);
    ev_timer_start(sif->sys->loop, &sif->retry);
}

static void sm_lat_sys_ifname_start(struct sm_lat_sys_ifname *sif)
{
    int read_fd;
    pid_t pid;

    if (sm_lat_sys_ifname_is_running(sif)) return;

    if (sm_lat_sys_epping_start(sif->name, &pid, &read_fd) != 0)
    {
        LOGE(LOG_PREFIX_IFNAME(sif, "epping spawn failed, scheduling retry"));
        sm_lat_sys_ifname_schedule_retry(sif);
        return;
    }
    LOGI(LOG_PREFIX_IFNAME(sif, "epping spawned with pid[%d]", pid));

    if (sm_lat_sys_pid_file_create(pid) != 0)
    {
        LOGE(LOG_PREFIX_IFNAME(sif, "unable to track pid[%d], scheduling retry", pid));
        kill(pid, SIGKILL);
        sm_lat_sys_ifname_schedule_retry(sif);
        return;
    }

    ev_child_init(&sif->child, sm_lat_sys_child_cb, pid, 0);
    ev_io_init(&sif->io, sm_lat_sys_io_cb, read_fd, EV_READ);

    ev_child_start(sif->sys->loop, &sif->child);
    ev_io_start(sif->sys->loop, &sif->io);
}

static void sm_lat_sys_ifname_stop(struct sm_lat_sys_ifname *sif)
{
    if (sif == NULL) return;
    if (!sm_lat_sys_ifname_is_running(sif))
    {
        if (!sm_lat_sys_ifname_is_enabled(sif))
        {
            sm_lat_sys_ifname_drop(sif);
        }
        return;
    }

    LOGI(LOG_PREFIX_IFNAME(sif, "stopping epping pid[%d]", sif->child.pid));
    ev_io_stop(sif->sys->loop, &sif->io);
    kill(sif->child.pid, SIGTERM);
    /* In case SIGTERM will fail, send SIGKILL */
    ev_timer_start(sif->sys->loop, &sif->kill);
}
