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

#include "fcm_priv.h"
#include "fcm_mgr.h"
#include "log.h"
#include "neigh_table.h"

// Intervals and timeouts in seconds
#define FCM_TIMER_INTERVAL   5
#define FCM_MGR_INTERVAL   120

/**
 * @brief periodic routine.
 */
static void
fcm_event_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    fcm_collect_plugin_t *plugin;
    fcm_collector_t *collector;
    ds_tree_t *collectors;
    struct mem_usage mem = { 0 };
    fcm_mgr_t *mgr;
    time_t now;
    bool reset;

    (void)loop;
    (void)watcher;
    (void)revents;

    mgr = fcm_get_mgr();

    now = time(NULL);

    if ((now - mgr->periodic_ts) < FCM_MGR_INTERVAL) return;

    mgr->periodic_ts = now;
    fcm_get_memory(&mem);
    LOGI("%s: pid %s: mem usage: real mem: %u, virt mem %u", __func__,
         mgr->pid, mem.curr_real_mem, mem.curr_virt_mem);

    reset = ((uint64_t)mem.curr_real_mem > mgr->max_mem);

    if (reset)
    {
        sleep(2);
        LOGEM("%s: max mem usage %" PRIu64 " kB reached, restarting",
              __func__, mgr->max_mem);
        exit(EXIT_SUCCESS);
    }

    collectors = &mgr->collect_tree;
    collector = ds_tree_head(collectors);
    while (collector != NULL)
    {
        plugin = &collector->plugin;
        if (plugin->periodic != NULL) plugin->periodic(plugin);
        collector = ds_tree_next(collectors, collector);
    }

    neigh_table_ttl_cleanup(mgr->neigh_cache_ttl, NEIGH_TBL_SYSTEM);
}


/**
 * @brief periodic timer initialization
 */
void
fcm_event_init(void)
{
    fcm_mgr_t *mgr = fcm_get_mgr();

    LOGI("Initializing FCM event");
    ev_timer_init(&mgr->timer, fcm_event_cb,
                  FCM_TIMER_INTERVAL, FCM_MGR_INTERVAL);
    mgr->timer.data = NULL;
    mgr->periodic_ts = time(NULL);
    ev_timer_start(mgr->loop, &mgr->timer);
}


/**
 * @brief place holder
 */

void fcm_event_close(void) {};


/**
 * @brief compute mem usage in kB
 *
 * @param file the source of system mem usage counters
 * @param counter a mem usage counter
 * @param the system mem usage unit for the counter
 */
void
fcm_mem_adjust_counter(FILE *file, int counter, char *unit)
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
fcm_get_memory(struct mem_usage *mem)
{
    fcm_mgr_t *mgr = fcm_get_mgr();
    char buffer[1024] = "";
    char fname[128];

    mgr = fcm_get_mgr();

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
            fcm_mem_adjust_counter(file, mem->curr_real_mem,
                                   mem->curr_real_mem_unit);
        }
        else if (strcmp(buffer, "VmHWM:") == 0)
        {
            fscanf(file, " %d", &mem->peak_real_mem);
        }
        else if (strcmp(buffer, "VmSize:") == 0)
        {
            fscanf(file, " %d %s", &mem->curr_virt_mem, mem->curr_virt_mem_unit);
            fcm_mem_adjust_counter(file, mem->curr_virt_mem,
                                   mem->curr_virt_mem_unit);
        }
        else if (strcmp(buffer, "VmPeak:") == 0)
        {
            fscanf(file, " %d", &mem->peak_virt_mem);
        }
    }
    fclose(file);
}
