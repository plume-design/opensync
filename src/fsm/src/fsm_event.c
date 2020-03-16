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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>

#include "fsm.h"
#include "log.h"

// Intervals and timeouts in seconds
#define FSM_TIMER_INTERVAL 5
#define FSM_MGR_INTERVAL 120

/**
 * @brief gather pcap stats for an eligible a fsm session
 *
 * @param session the fsm session
 */
static void
fsm_pcap_stats(struct fsm_session *session)
{
    struct fsm_pcaps *pcaps;
    struct pcap_stat stats;
    pcap_t *pcap;
    int rc;

    if (!fsm_plugin_has_intf(session)) return;
    if (session->conf == NULL) return;

    pcaps = session->pcaps;
    if (pcaps == NULL) return;

    pcap = pcaps->pcap;
    memset(&stats, 0, sizeof(stats));

    rc = pcap_stats(pcap, &stats);
    if (rc < 0)
    {
        LOGT("%s: pcap_stats failed: %s",
             __func__, pcap_geterr(pcap));
        return;
    }

    LOGI("%s: %s: packets received: %u, dropped: %u",
         __func__, session->conf->if_name, stats.ps_recv, stats.ps_drop);
}

/**
 * @brief periodic routine. Calls fsm sessions' reiodic call backs
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

    session = ds_tree_head(sessions);
    while (session != NULL)
    {
        fsm_pcap_stats(session);
        session = ds_tree_next(sessions, session);
    }

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

    snprintf(fname, sizeof(fname), "/proc/%s/status", mgr->pid);
    FILE* file = fopen(fname, "r");

    if (file == NULL) return;

    memset(mem, 0, sizeof(*mem));

    // read the entire file
    while (fscanf(file, " %1023s", buffer) == 1)
    {
        if (strcmp(buffer, "VmRSS:") == 0)
        {
            fscanf(file, " %d %s", &mem->curr_real_mem, mem->curr_real_mem_unit);
            fsm_mem_adjust_counter(file, mem->curr_real_mem,
                                   mem->curr_real_mem_unit);
        }
        else if (strcmp(buffer, "VmHWM:") == 0)
        {
            fscanf(file, " %d", &mem->peak_real_mem);
        }
        else if (strcmp(buffer, "VmSize:") == 0)
        {
            fscanf(file, " %d %s", &mem->curr_virt_mem, mem->curr_virt_mem_unit);
            fsm_mem_adjust_counter(file, mem->curr_virt_mem,
                                   mem->curr_virt_mem_unit);
        }
        else if (strcmp(buffer, "VmPeak:") == 0)
        {
            fscanf(file, " %d", &mem->peak_virt_mem);
        }
    }
    fclose(file);
}
