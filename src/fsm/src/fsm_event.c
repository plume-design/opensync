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

#include <ev.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include "kconfig.h"

#include "nf_utils.h"
#include  "dpi_stats.h"
#include "fsm.h"
#include "log.h"
#include "fsm_fn_trace.h"
#include "os_ev_trace.h"

// Intervals and timeouts in seconds
#define FSM_TIMER_INTERVAL 5
#define FSM_MGR_INTERVAL CONFIG_FSM_MEM_CHECK_PERIOD

/**
 * @brief periodic routine. Calls fsm sessions' periodic call backs
 */
static void
fsm_event_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    struct fsm_mgr *mgr = fsm_get_mgr();
    ds_tree_t *sessions = fsm_get_sessions();
    struct fsm_session *session = ds_tree_head(sessions);
    struct mem_usage mem = { 0 };
    time_t now = time(NULL);
    bool reset;

    (void)loop;
    (void)watcher;
    (void)revents;

    while (session != NULL)
    {
        if (session->ops.periodic != NULL) session->ops.periodic(session);
        session = ds_tree_next(sessions, session);
    }

    now = time(NULL);
    if ((now - mgr->periodic_ts) < FSM_MGR_INTERVAL) return;

    mgr->periodic_ts = now;

    fsm_get_memory(&mem);
    LOGI("pid %s: mem usage: real mem: %u, virt mem %u",
         mgr->pid, mem.curr_real_mem, mem.curr_virt_mem);

    reset = ((uint64_t)mem.curr_real_mem > mgr->max_mem);
    if (reset)
    {
        sleep(2);
        LOGEM("%s: max mem usage %" PRIu64 " kB reached, restarting",
              __func__, mgr->max_mem);
        exit(EXIT_SUCCESS);
    }
}


/**
 * @brief periodic timer initialization
 */
void
fsm_event_init(void)
{
    struct fsm_mgr *mgr = fsm_get_mgr();
    LOGI("Initializing FSM event");
    OS_EV_TRACE_MAP(fsm_event_cb);
    ev_timer_init(&mgr->timer, fsm_event_cb,
                  FSM_TIMER_INTERVAL, FSM_TIMER_INTERVAL);
    mgr->timer.data = NULL;
    mgr->periodic_ts = time(NULL);
    ev_timer_start(mgr->loop, &mgr->timer);
}


/**
 * @brief place holder
 */

void fsm_event_close(void) {};


/**
 * @brief compute mem usage in kB
 *
 * @param file the source of system mem usage counters
 * @param counter a mem usage counter
 * @param the system mem usage unit for the counter
 */
void
fsm_mem_adjust_counter(FILE *file, int counter, char *unit)
{
    int rc;

    rc = strcmp(unit, "kB");
    if (rc != 0)
    {
        LOGE("%s: expected kB units, got %s", __func__,
             unit);
        return;
    }
}


/**
 * @brief gather process memory usage
 *
 * @param mem memory usage counters container
 */
void
fsm_get_memory(struct mem_usage *mem)
{
    struct fsm_mgr *mgr = fsm_get_mgr();
    char buffer[1024] = "";
    char fname[128];
    int rc;

    snprintf(fname, sizeof(fname), "/proc/%s/status", mgr->pid);
    FILE* file = fopen(fname, "r");

    if (file == NULL) return;

    memset(mem, 0, sizeof(*mem));

    // read the entire file
    while ((rc = fscanf(file, " %1023s", buffer)) == 1)
    {
        errno = 0;
        if (strcmp(buffer, "VmRSS:") == 0)
        {
            rc = fscanf(file, " %d %s", &mem->curr_real_mem, mem->curr_real_mem_unit);
            if ((rc != 1) && (errno != 0)) goto err_scan;

            fsm_mem_adjust_counter(file, mem->curr_real_mem,
                                   mem->curr_real_mem_unit);
        }
        else if (strcmp(buffer, "VmHWM:") == 0)
        {
            rc = fscanf(file, " %d", &mem->peak_real_mem);
            if ((rc != 1) && (errno != 0)) goto err_scan;
        }
        else if (strcmp(buffer, "VmSize:") == 0)
        {
            rc = fscanf(file, " %d %s", &mem->curr_virt_mem, mem->curr_virt_mem_unit);
            if ((rc != 1) && (errno != 0)) goto err_scan;

            fsm_mem_adjust_counter(file, mem->curr_virt_mem,
                                   mem->curr_virt_mem_unit);
        }
        else if (strcmp(buffer, "VmPeak:") == 0)
        {
            rc = fscanf(file, " %d", &mem->peak_virt_mem);
            if ((rc != 1) && (errno != 0)) goto err_scan;
        }
    }

    fclose(file);
    return;

err_scan:
    LOGD("%s: error scanning %s: %s", __func__, fname, strerror(errno));
    fclose(file);
}

/**
 * @brief get netfilters queue stats
 *
 * @param none
 */
void
fsm_get_nfqueue_stats(void)
{
    char filename[] = "/proc/net/netfilter/nfnetlink_queue";
    struct nfqnl_counters nfq_counters;
    char line[256];
    FILE *fp;
    int rc;

    fp = fopen(filename, "r");
    if (fp == NULL) return;

   /* Read the nfqueue counters */
    while (fgets(line, sizeof(line), fp))
    {
        errno = 0;
        MEMZERO(nfq_counters);
        rc = sscanf(line,
                    "%d %u %u %hhu %u %u %u %u",
                    &nfq_counters.queue_num,
                    &nfq_counters.portid,
                    &nfq_counters.queue_total,
                    &nfq_counters.copy_mode,
                    &nfq_counters.copy_range,
                    &nfq_counters.queue_dropped,
                    &nfq_counters.queue_user_dropped,
                    &nfq_counters.id_sequence);
        if ((rc != 1) && (errno != 0))
        {
            LOGD("%s: error scanning %s: %s", __func__, filename, strerror(errno));
            break;
        }

        LOGI(
            "netlink queue stats: queue num: %d, port id: %u, queue total: %u copy mode: %hhu copy range: %u qdrop: %u user drop: %u seq id: %u",
            nfq_counters.queue_num,
            nfq_counters.portid,
            nfq_counters.queue_total,
            nfq_counters.copy_mode,
            nfq_counters.copy_range,
            nfq_counters.queue_dropped,
            nfq_counters.queue_user_dropped,
            nfq_counters.id_sequence);

        nfq_log_err_counters(nfq_counters.queue_num);

        /* store the collected stats, before reporting */
        dpi_stats_store_nfq_stats(&nfq_counters);
        dpi_stats_store_nfq_err_cnt(nfq_counters.queue_num);
    }

    fclose(fp);
}
