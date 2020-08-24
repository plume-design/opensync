/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "iotm.h"
#include "iotm_ev.h"
#include "iotm_ev_private.h"
#include "iotm_session.h"

void iotm_get_memory(struct mem_usage *mem)
{
    struct iotm_mgr *mgr = iotm_get_mgr();
    char buffer[1024] = "";
    char fname[128];

    snprintf(fname, sizeof(fname), "/proc/%s/status", mgr->pid);
    FILE* file = fopen(fname, "r");
    memset(mem, 0, sizeof(*mem));

    // read the entire file
    while (fscanf(file, " %1023s", buffer) == 1)
    {
        if (strcmp(buffer, "VmRSS:") == 0)
        {
            fscanf(file, " %d", &mem->curr_real_mem);
        }
        if (strcmp(buffer, "VmHWM:") == 0)
        {
            fscanf(file, " %d", &mem->peak_real_mem);
        }
        if (strcmp(buffer, "VmSize:") == 0)
        {
            fscanf(file, " %d", &mem->curr_virt_mem);
        }
        if (strcmp(buffer, "VmPeak:") == 0)
        {
            fscanf(file, " %d", &mem->peak_virt_mem);
        }
    }
    fclose(file);
}

/**
 * @brief periodic routine. Calls iotm sessions' reiodic call backs
 */
static void iotm_ev_cb(struct ev_loop *loop, ev_timer *watcher, int revs)
{
    struct iotm_mgr *mgr = iotm_get_mgr();
    ds_tree_t *sessions = iotm_get_sessions();
    struct iotm_session *session = ds_tree_head(sessions);
    struct mem_usage mem = { 0 };
    time_t now = time(NULL);

    (void)loop;
    (void)watcher;
    (void)revs;

    while (session != NULL)
    {
        if (session->ops.periodic != NULL) session->ops.periodic(session);
        session = ds_tree_next(sessions, session);
    }

    now = time(NULL);
    if ((now - mgr->periodic_ts) < IOTM_MGR_INTERVAL) return;

    mgr->periodic_ts = now;
    iotm_get_memory(&mem);
    LOGI("pid %s: mem usage: real mem: %u, virt mem %u",
            mgr->pid, mem.curr_real_mem, mem.curr_virt_mem);
}

/**
 * @brief periodic timer initialization
 */
    void
iotm_ev_init(void)
{
    struct iotm_mgr *mgr = iotm_get_mgr();
    LOGI("Initializing IOTM ev");
    ev_timer_init(&mgr->timer, iotm_ev_cb,
            IOTM_TIMER_INTERVAL, IOTM_TIMER_INTERVAL);
    mgr->timer.data = NULL;
    mgr->periodic_ts = time(NULL);
    ev_timer_start(mgr->loop, &mgr->timer);
}

/**
 * @brief place holder
 */
void iotm_ev_close(void)
{ 
    // NO-OP 
};
