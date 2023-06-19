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

#include <stdbool.h>
#include <stdint.h>
#include <log.h>
#include <const.h>
#include <util.h>
#include <memutil.h>
#include <ds_dlist.h>
#include <ds_tree.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_conf.h>
#include <osw_module.h>
#include "ow_conf.h"
#include "ow_wps.h"

struct ow_wps {
    struct ow_wps_ops ops;
    struct ds_dlist jobs;
    struct osw_timer work;
};

enum ow_wps_job_sm_state {
    OW_WPS_JOB_PREPARING, /* ow_wps_job_start_cb() to move forward */
    OW_WPS_JOB_SCHEDULING, /* ow_wps_work() can move to ACTIVATING */
    OW_WPS_JOB_ACTIVATING, /* osw_state can move to RUNNING */
    OW_WPS_JOB_RUNNING, /* osw_state can move to INTERRUPTING */
    OW_WPS_JOB_CANCELLING, /* moved by: ow_wps_job_cancel_cb() or ow_wps_job_drop_cb() */
    OW_WPS_JOB_INTERRUPTING, /* osw_state can move back to RUNNING */
    OW_WPS_JOB_DEACTIVATING, /* waiting for osw_state to settle */
    OW_WPS_JOB_DROPPING, /* job fully idle, can be free()-d */
};

struct ow_wps_job {
    struct ow_wps *wps;
    char *vif_name;
    enum ow_wps_job_role role;
    enum ow_wps_job_method method;
    enum ow_wps_job_sm_state sm_state;

    unsigned long num_reactivated;
    bool prepared;
    bool scheduled;
    bool activated;
    bool cancelled;
    bool completed;
    bool dropped;
    bool reported_running;
    enum ow_wps_job_result result;
    const struct osw_state_vif_info *vif_info;

    uint64_t elapsed_nsec;
    uint64_t started_at_nsec;

    struct osw_timer timeout;
    struct osw_timer work;

    struct osw_state_observer obs;
    struct osw_conf_mutator mut;

    struct {
        void *priv;
        struct osw_wps_cred_list creds;
        ow_wps_job_started_fn_t *started_fn;
        ow_wps_job_finished_fn_t *finished_fn;
        int timeout_sec;
    } config;

    struct ds_dlist_node node;
};

#define ops_to_wps(ops) container_of(ops, struct ow_wps, ops)
#define mut_to_job(mut) container_of(mut, struct ow_wps_job, mut)
#define obs_to_job(mut) container_of(obs, struct ow_wps_job, obs)
#define job_work_to_job(t) container_of(t, struct ow_wps_job, work)
#define job_timeout_to_job(t) container_of(t, struct ow_wps_job, timeout)
#define wps_work_to_work(t) container_of(t, struct ow_wps, work)

#define OW_WPS_PBC_TIMEOUT_SEC 120

#define LOG_PREFIX(fmt, ...) \
    "ow: wps: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_VIF(vif_name, fmt, ...) \
    LOG_PREFIX("vif: %s: %s: ", \
               vif_name, \
               fmt, \
               ##__VA_ARGS__)


#define LOG_PREFIX_JOB(job, fmt, ...) \
    LOG_PREFIX("job: %s: %s: %s: " fmt, \
               job->vif_name, \
               ow_wps_job_method_to_cstr(job->method), \
               ow_wps_job_role_to_cstr(job->role), \
               ##__VA_ARGS__)

static const char *
ow_wps_job_sm_state_to_cstr(enum ow_wps_job_sm_state state)
{
    switch (state) {
        case OW_WPS_JOB_PREPARING: return "preparing";
        case OW_WPS_JOB_SCHEDULING: return "scheduling";
        case OW_WPS_JOB_ACTIVATING: return "activating";
        case OW_WPS_JOB_RUNNING: return "running";
        case OW_WPS_JOB_CANCELLING: return "cancelling";
        case OW_WPS_JOB_INTERRUPTING: return "interrupting";
        case OW_WPS_JOB_DEACTIVATING: return "deactivating";
        case OW_WPS_JOB_DROPPING: return "dropping";
    }
    return "unknown";
}

static const char *
ow_wps_job_result_to_cstr(enum ow_wps_job_result result)
{
    switch (result) {
        case OW_WPS_JOB_UNSPECIFIED: return "unspecified";
        case OW_WPS_JOB_TIMED_OUT_INTERNALLY: return "timed out internally";
        case OW_WPS_JOB_TIMED_OUT_EXTERNALLY: return "timed out externally";
        case OW_WPS_JOB_CANCELLED: return "cancelled";
        case OW_WPS_JOB_INTERRUPTED: return "interrupted";
        case OW_WPS_JOB_OVERLAPPED: return "overlapped";
        case OW_WPS_JOB_SUCCEEDED: return "succeeded";
    }
    return "unknown";
}

static const char *
ow_wps_job_method_to_cstr(enum ow_wps_job_method method)
{
    switch (method) {
        case OW_WPS_METHOD_PBC: return "pbc";
    }
    return "unknown";
}

static const char *
ow_wps_job_role_to_cstr(enum ow_wps_job_role role)
{
    switch (role) {
        case OW_WPS_ROLE_ENROLLER: return "enroller";
    }
    return "unknown";
}

static void
ow_wps_job_notify_started(struct ow_wps_job *job)
{
    if (job->config.started_fn == NULL) return;
    struct ow_wps_ops *ops = &job->wps->ops;
    job->config.started_fn(ops, job);
    LOGN(LOG_PREFIX_JOB(job, "started"));
}

static void
ow_wps_job_notify_finished(struct ow_wps_job *job)
{
    if (job->config.finished_fn == NULL) return;
    struct ow_wps_ops *ops = &job->wps->ops;
    const double elapsed_sec = OSW_TIME_TO_DBL(job->elapsed_nsec);
    job->config.finished_fn(ops, job);
    LOGN(LOG_PREFIX_JOB(job, "finished: %s after %lf seconds",
                        ow_wps_job_result_to_cstr(job->result)),
                        elapsed_sec);
}

static uint64_t
ow_wps_job_get_timeout_nsec(const struct ow_wps_job *job)
{
    const uint64_t timeout_nsec = OSW_TIME_SEC(job->config.timeout_sec);
    const uint64_t duration_nsec = timeout_nsec - job->elapsed_nsec;
    return duration_nsec;
}

static void
ow_wps_job_timeout_stop(struct ow_wps_job *job)
{
    struct osw_timer *t = &job->timeout;
    if (osw_timer_is_armed(t) == false) return;

    const uint64_t now_nsec = osw_time_mono_clk();
    const uint64_t elapsed_nsec = now_nsec - job->started_at_nsec;
    job->elapsed_nsec += elapsed_nsec;
    osw_timer_disarm(t);
}

static void
ow_wps_job_timeout_start(struct ow_wps_job *job)
{
    struct osw_timer *t = &job->timeout;
    if (WARN_ON(osw_timer_is_armed(t))) return;

    const uint64_t duration_nsec = ow_wps_job_get_timeout_nsec(job);
    if (duration_nsec == 0) return;

    const uint64_t now_nsec = osw_time_mono_clk();
    const uint64_t expire_at_nsec = now_nsec + duration_nsec;
    job->started_at_nsec = now_nsec;
    osw_timer_arm_at_nsec(t, expire_at_nsec);
}

static void
ow_wps_job_free(struct ow_wps_job *job)
{
    WARN_ON(job->wps != NULL);
    WARN_ON(osw_timer_is_armed(&job->timeout));

    osw_timer_disarm(&job->work);
    osw_timer_disarm(&job->timeout);

    FREE(job->config.creds.list);
    FREE(job->vif_name);
    FREE(job);
}

static void
ow_wps_schedule_work(struct ow_wps *wps)
{
    if (wps == NULL) return;
    osw_timer_arm_at_nsec(&wps->work, 0);
}

static void
ow_wps_job_schedule_work(struct ow_wps_job *job)
{
    if (WARN_ON(job == NULL)) return;
    osw_timer_arm_at_nsec(&job->work, 0);
    ow_wps_schedule_work(job->wps);
}

static void
ow_wps_job_set_prepared(struct ow_wps_job *job)
{
    if (WARN_ON(job == NULL)) return;
    if (job->prepared == true) return;
    job->prepared = true;
    LOGI(LOG_PREFIX_JOB(job, "prepared"));
    ow_wps_job_schedule_work(job);
    ow_wps_schedule_work(job->wps);
}

static void
ow_wps_job_set_scheduled(struct ow_wps_job *job)
{
    if (WARN_ON(job == NULL)) return;
    if (job->scheduled == true) return;
    job->scheduled = true;
    LOGI(LOG_PREFIX_JOB(job, "scheduled"));
    ow_wps_job_schedule_work(job);
}

static void
ow_wps_job_set_activated(struct ow_wps_job *job, bool activated)
{
    if (WARN_ON(job == NULL)) return;
    if (job->activated == activated) return;
    job->activated = activated;
    LOGI(LOG_PREFIX_JOB(job, "activated: %d", activated));
    ow_wps_job_schedule_work(job);
}

static void
ow_wps_job_set_cancelled(struct ow_wps_job *job)
{
    if (WARN_ON(job == NULL)) return;
    if (job->cancelled == true) return;
    job->cancelled = true;
    LOGI(LOG_PREFIX_JOB(job, "cancelled"));
    osw_conf_invalidate(&job->mut);
    ow_wps_job_schedule_work(job);
}

static void
ow_wps_job_set_completed(struct ow_wps_job *job,
                         const enum ow_wps_job_result result)
{
    if (WARN_ON(job == NULL)) return;
    if (job->completed == true) {
        if (job->result != result) {
            LOGI(LOG_PREFIX_JOB(job, "completed again with different result: original %s, new %s, using original",
                                ow_wps_job_result_to_cstr(job->result),
                                ow_wps_job_result_to_cstr(result)));
        }
        return;
    }
    job->completed = true;
    job->result = result;
    LOGI(LOG_PREFIX_JOB(job, "completed: %s",
                        ow_wps_job_result_to_cstr(result)));
    ow_wps_job_schedule_work(job);
}

static void
ow_wps_job_set_dropped(struct ow_wps_job *job)
{
    if (WARN_ON(job == NULL)) return;
    if (job->dropped == true) return;
    job->dropped = true;
    LOGI(LOG_PREFIX_JOB(job, "dropped"));
    ow_wps_job_schedule_work(job);
}

static void
ow_wps_job_set_vif_info(struct ow_wps_job *job,
                        const struct osw_state_vif_info *vif_info)
{
    const bool mismatched_vif = (strcmp(job->vif_name, vif_info->vif_name) != 0);
    if (WARN_ON(mismatched_vif)) return;

    const struct osw_drv_vif_state *vif = vif_info->drv_state;
    const struct osw_drv_vif_state_ap *ap = &vif->u.ap;
    const enum osw_vif_type vif_type = vif->vif_type;

    switch (vif_type) {
        case OSW_VIF_AP:
            {
                const bool cred_matches = osw_wps_cred_list_is_same(&ap->wps_cred_list,
                                                                    &job->config.creds);
                const bool pbc_activated = vif->enabled
                                        && ap->mode.wps
                                        && ap->wps_pbc
                                        && cred_matches;
                ow_wps_job_set_activated(job, pbc_activated);
            }
            break;
        case OSW_VIF_AP_VLAN:
        case OSW_VIF_STA:
        case OSW_VIF_UNDEFINED:
            return;
    }

    job->vif_info = vif_info;
}

static void
ow_wps_job_sm_enter(struct ow_wps_job *job)
{
    const enum ow_wps_job_sm_state state = job->sm_state;
    LOGT(LOG_PREFIX_JOB(job, "state: enter: %s",
                        ow_wps_job_sm_state_to_cstr(state)));
    switch (state) {
        case OW_WPS_JOB_PREPARING:
            break;
        case OW_WPS_JOB_SCHEDULING:
            break;
        case OW_WPS_JOB_ACTIVATING:
            break;
        case OW_WPS_JOB_RUNNING:
            ow_wps_job_timeout_start(job);
            if (job->reported_running == false) {
                ow_wps_job_notify_started(job);
                job->reported_running = true;
            }
            break;
        case OW_WPS_JOB_CANCELLING:
            break;
        case OW_WPS_JOB_INTERRUPTING:
            break;
        case OW_WPS_JOB_DEACTIVATING:
            break;
        case OW_WPS_JOB_DROPPING:
            if (job->reported_running) {
                job->reported_running = false;
                ow_wps_job_notify_finished(job);
            }
            break;
    }
}

static void
ow_wps_job_sm_leave(struct ow_wps_job *job)
{
    const enum ow_wps_job_sm_state state = job->sm_state;
    LOGT(LOG_PREFIX_JOB(job, "state: leave: %s",
                        ow_wps_job_sm_state_to_cstr(state)));
    switch (state) {
        case OW_WPS_JOB_PREPARING:
            break;
        case OW_WPS_JOB_SCHEDULING:
            break;
        case OW_WPS_JOB_ACTIVATING:
            break;
        case OW_WPS_JOB_RUNNING:
            ow_wps_job_timeout_stop(job);
            break;
        case OW_WPS_JOB_CANCELLING:
            break;
        case OW_WPS_JOB_INTERRUPTING:
            break;
        case OW_WPS_JOB_DEACTIVATING:
            break;
        case OW_WPS_JOB_DROPPING:
            break;
    }
}

static void
ow_wps_job_sm_set_state(struct ow_wps_job *job,
                        enum ow_wps_job_sm_state sm_state)
{
    if (job->sm_state == sm_state) return;

    LOGT(LOG_PREFIX_JOB(job, "state: set: %s",
                        ow_wps_job_sm_state_to_cstr(sm_state)));

    ow_wps_job_sm_leave(job);
    job->sm_state = sm_state;
    ow_wps_job_sm_enter(job);
    osw_conf_invalidate(&job->mut);
    ow_wps_job_schedule_work(job);
}

static void
ow_wps_unlink_job(struct ow_wps *wps,
                  struct ow_wps_job *job)
{
    if (job == NULL) return;
    if (job->wps == NULL) return;

    job->wps = NULL;
    ds_dlist_remove(&wps->jobs, job);
    ow_wps_schedule_work(wps);
}

static void
ow_wps_detach_job(struct ow_wps *wps,
                  struct ow_wps_job *job)
{
    if (wps == NULL) return;
    if (job == NULL) return;

    ow_wps_unlink_job(wps, job);
    osw_state_unregister_observer(&job->obs);
    osw_conf_unregister_mutator(&job->mut);
    ow_wps_schedule_work(wps);
}

static void
ow_wps_job_sm_work(struct ow_wps_job *job)
{
    const enum ow_wps_job_sm_state state = job->sm_state;

    LOGT(LOG_PREFIX_JOB(job, "work: state: %s",
                        ow_wps_job_sm_state_to_cstr(state)));

    switch (state) {
        case OW_WPS_JOB_PREPARING:
            if (job->prepared) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_SCHEDULING);
            }
            else if (job->dropped) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_DROPPING);
            }
            break;
        case OW_WPS_JOB_SCHEDULING:
            if (job->cancelled) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_CANCELLING);
            }
            else if (job->dropped) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_CANCELLING);
            }
            else if (job->scheduled) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_ACTIVATING);
            }
            else if (job->activated) {
                /* This handles case where other "sibling"
                 * BSSes activate WPS as well.
                 */
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_ACTIVATING);
            }
            break;
        case OW_WPS_JOB_ACTIVATING:
            if (job->cancelled) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_CANCELLING);
            }
            else if (job->dropped) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_CANCELLING);
            }
            else if (job->activated) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_RUNNING);
            }
            else if (job->completed) {
                /* The condition to consider a job activated
                 * is fulfilling state report parameters to
                 * configured ones, eg. credential data. If
                 * job non-running is marked completed (and
                 * not cancelled) then the actual completion
                 * state is unknown because its impossible
                 * to tell if the particular job completed.
                 *
                 * This shouldn't almost never happen. WPS
                 * grace periods in practice make sure
                 * there's ample time for state reports to
                 * bubble up and move the job->sm_state
                 * forward before completion is reported.
                 */
                const enum ow_wps_job_result override_result = OW_WPS_JOB_UNSPECIFIED;
                LOGN(LOG_PREFIX_JOB(job, "bogus completion, overriding: %s -> %s",
                                    ow_wps_job_result_to_cstr(job->result),
                                    ow_wps_job_result_to_cstr(override_result)));
                job->result = override_result;
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_RUNNING);
            }
            break;
        case OW_WPS_JOB_RUNNING:
            if (job->completed) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_DEACTIVATING);
            }
            else if (job->cancelled) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_CANCELLING);
            }
            else if (job->dropped) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_CANCELLING);
            }
            else if (job->activated == false) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_INTERRUPTING);
            }
            break;
        case OW_WPS_JOB_CANCELLING:
            if (job->completed) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_DEACTIVATING);
            }
            else if (job->activated == false) {
                ow_wps_job_set_completed(job, OW_WPS_JOB_CANCELLED);
            }
            break;
        case OW_WPS_JOB_INTERRUPTING:
            if (job->completed) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_DEACTIVATING);
            }
            else if (job->dropped) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_CANCELLING);
            }
            else if (job->activated == false) {
                ow_wps_job_set_completed(job, OW_WPS_JOB_INTERRUPTED);
            }
            else {
                job->num_reactivated++;
                LOGN(LOG_PREFIX_JOB(job, "re-activated %lu nth time after interrupted, continuing",
                                    job->num_reactivated));
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_RUNNING);
            }
            break;
        case OW_WPS_JOB_DEACTIVATING:
            if (job->activated == false) {
                ow_wps_job_sm_set_state(job, OW_WPS_JOB_DROPPING);
            }
            break;
        case OW_WPS_JOB_DROPPING:
            if (job->dropped) {
                ow_wps_detach_job(job->wps, job);
                ow_wps_job_free(job);
                return;
            }
            break;
    }
}

static void
ow_wps_job_work_cb(struct osw_timer *t)
{
    struct ow_wps_job *job = job_work_to_job(t);
    LOGT(LOG_PREFIX_JOB(job, "work"));
    ow_wps_job_sm_work(job);
}

static void
ow_wps_job_timeout_cb(struct osw_timer *t)
{
    struct ow_wps_job *job = job_timeout_to_job(t);
    ow_wps_job_set_completed(job, OW_WPS_JOB_TIMED_OUT_INTERNALLY);
}

static void
ow_wps_job_mutate_vif_creds_flush(struct osw_conf_vif_ap *vap)
{
    struct osw_conf_wps_cred *cred;
    while ((cred = ds_dlist_remove_head(&vap->wps_cred_list)) != NULL) {
        FREE(cred);
    }
}

static void
ow_wps_job_mutate_vif_creds_populate(struct osw_conf_vif_ap *vap,
                                     const struct osw_wps_cred_list *creds)
{
    size_t i = 0;
    for (i = 0; i < creds->count; i++) {
        const struct osw_wps_cred *cred = &creds->list[i];
        struct osw_conf_wps_cred *conf = CALLOC(1, sizeof(*conf));
        STRSCPY_WARN(conf->cred.psk.str, cred->psk.str);
        ds_dlist_insert_tail(&vap->wps_cred_list, conf);
    }
}

static void
ow_wps_job_mutate_vif_pbc(struct ow_wps_job *job,
                          struct osw_conf_vif_ap *vap)
{
    ASSERT(job != NULL, "");
    ASSERT(vap != NULL, "");

    switch (job->sm_state) {
        case OW_WPS_JOB_ACTIVATING:
            ow_wps_job_mutate_vif_creds_flush(vap);
            ow_wps_job_mutate_vif_creds_populate(vap, &job->config.creds);
            vap->wps_pbc = vap->mode.wps == true
                        && job->cancelled == false;
            break;
        case OW_WPS_JOB_DEACTIVATING:
        case OW_WPS_JOB_CANCELLING:
            ow_wps_job_mutate_vif_creds_flush(vap);
            vap->wps_pbc = false;
            break;
        case OW_WPS_JOB_RUNNING:
            /* Don't touch anything. Leave osw_confsync
             * maintain state->config for these entries.
             */
            break;
        case OW_WPS_JOB_PREPARING:
        case OW_WPS_JOB_SCHEDULING:
        case OW_WPS_JOB_INTERRUPTING:
        case OW_WPS_JOB_DROPPING:
            break;
    }
}

static void
ow_wps_job_mutate_vif_as_enroller(struct ow_wps_job *job,
                                  struct osw_conf_vif *vif)
{
    ASSERT(job != NULL, "");
    ASSERT(vif != NULL, "");

    const enum osw_vif_type vif_type = vif->vif_type;

    switch (job->method) {
        case OW_WPS_METHOD_PBC:
            switch (vif_type) {
                case OSW_VIF_AP:
                    ow_wps_job_mutate_vif_pbc(job, &vif->u.ap);
                    break;
                case OSW_VIF_AP_VLAN:
                case OSW_VIF_STA:
                case OSW_VIF_UNDEFINED:
                    break;
            }
            return;
    }
}

static void
ow_wps_job_mutate_vif(struct ow_wps_job *job,
                      struct osw_conf_vif *vif)
{
    ASSERT(job != NULL, "");
    ASSERT(vif != NULL, "");

    switch (job->role) {
        case OW_WPS_ROLE_ENROLLER:
            ow_wps_job_mutate_vif_as_enroller(job, vif);
            break;
    }
}

static struct osw_conf_vif *
ow_wps_conf_lookup_vif(struct ds_tree *phy_tree,
                       const char *vif_name)
{
    struct osw_conf_phy *phy;
    ds_tree_foreach(phy_tree, phy) {
        struct osw_conf_vif *vif = ds_tree_find(&phy->vif_tree, vif_name);
        if (vif != NULL) {
            return vif;
        }
    }
    return NULL;
}

static void
ow_wps_conf_mutate_cb(struct osw_conf_mutator *mut,
                      struct ds_tree *phy_tree)
{
    struct ow_wps_job *job = mut_to_job(mut);
    struct osw_conf_vif *vif = ow_wps_conf_lookup_vif(phy_tree, job->vif_name);
    if (vif == NULL) return;

    ow_wps_job_mutate_vif(job, vif);
}

static void
ow_wps_state_vif_cb(struct osw_state_observer *obs,
                    const struct osw_state_vif_info *vif_info,
                    const bool exists)
{
    struct ow_wps_job *job = obs_to_job(obs);
    const char *vif_name = vif_info->vif_name;
    const bool different_vif = (strcmp(vif_name, job->vif_name) != 0);
    if (different_vif) return;
    ow_wps_job_set_vif_info(job, exists ? vif_info : NULL);
}

static void
ow_wps_state_wps_cb(struct osw_state_observer *obs,
                    const struct osw_state_vif_info *vif_info,
                    enum ow_wps_job_result result)
{
    struct ow_wps_job *job = obs_to_job(obs);
    const char *vif_name = vif_info->vif_name;
    const bool different_vif = (strcmp(vif_name, job->vif_name) != 0);
    if (different_vif) return;
    switch (result) {
        case OW_WPS_JOB_UNSPECIFIED:
        case OW_WPS_JOB_TIMED_OUT_INTERNALLY:
        case OW_WPS_JOB_CANCELLED:
        case OW_WPS_JOB_INTERRUPTED:
            WARN_ON(1); /* unexpected */
            break;
        case OW_WPS_JOB_TIMED_OUT_EXTERNALLY:
        case OW_WPS_JOB_OVERLAPPED:
            if (job->cancelled) {
                /* ignore these, we're canceling anyway */
                return;
            }
            break;
        case OW_WPS_JOB_SUCCEEDED:
            if (job->cancelled) {
                LOGN(LOG_PREFIX_JOB(job, "succeeded before cancelled"));
            }
            break;
    }
    ow_wps_job_set_completed(job, result);
}

static void
ow_wps_state_vif_added_cb(struct osw_state_observer *obs,
                          const struct osw_state_vif_info *vif_info)
{
    ow_wps_state_vif_cb(obs, vif_info, true);
}

static void
ow_wps_state_vif_changed_cb(struct osw_state_observer *obs,
                            const struct osw_state_vif_info *vif_info)
{
    ow_wps_state_vif_cb(obs, vif_info, true);
}

static void
ow_wps_state_vif_removed_cb(struct osw_state_observer *obs,
                            const struct osw_state_vif_info *vif_info)
{
    ow_wps_state_vif_cb(obs, vif_info, true);
}

static void
ow_wps_state_success_cb(struct osw_state_observer *obs,
                        const struct osw_state_vif_info *vif_info)
{
    ow_wps_state_wps_cb(obs, vif_info, OW_WPS_JOB_SUCCEEDED);
}

static void
ow_wps_state_overlap_cb(struct osw_state_observer *obs,
                        const struct osw_state_vif_info *vif_info)
{
    ow_wps_state_wps_cb(obs, vif_info, OW_WPS_JOB_OVERLAPPED);
}

static void
ow_wps_state_timeout_cb(struct osw_state_observer *obs,
                        const struct osw_state_vif_info *vif_info)
{
    ow_wps_state_wps_cb(obs, vif_info, OW_WPS_JOB_TIMED_OUT_EXTERNALLY);
}

static bool
ow_wps_jobs_are_running(struct ow_wps *wps)
{
    struct ds_dlist *jobs = &wps->jobs;
    struct ow_wps_job *job;
    size_t n_jobs = 0;

    ds_dlist_foreach(jobs, job) {
        switch (job->sm_state) {
            case OW_WPS_JOB_PREPARING:
            case OW_WPS_JOB_DROPPING:
            case OW_WPS_JOB_SCHEDULING:
                break;
            case OW_WPS_JOB_ACTIVATING:
            case OW_WPS_JOB_RUNNING:
            case OW_WPS_JOB_CANCELLING:
            case OW_WPS_JOB_INTERRUPTING:
            case OW_WPS_JOB_DEACTIVATING:
                n_jobs++;
                break;
        }
    }

    return n_jobs > 0;
}

static bool
ow_wps_job_has_doppleganger_running(struct ow_wps *wps,
                                    const char *vif_name)
{
    struct ow_wps_job *job;
    ds_dlist_foreach(&wps->jobs, job) {
        const bool other_vif = (strcmp(vif_name, job->vif_name) != 0);
        if (other_vif) continue;
        switch (job->sm_state) {
            case OW_WPS_JOB_PREPARING:
            case OW_WPS_JOB_SCHEDULING:
            case OW_WPS_JOB_DROPPING:
                return false;
            case OW_WPS_JOB_ACTIVATING:
            case OW_WPS_JOB_RUNNING:
            case OW_WPS_JOB_CANCELLING:
            case OW_WPS_JOB_INTERRUPTING:
            case OW_WPS_JOB_DEACTIVATING:
                return false;
        }
    }
    return false;
}

static bool
ow_wps_job_is_eligible(const struct ow_wps_job *job)
{
    const bool job_is_ready = (job->sm_state == OW_WPS_JOB_SCHEDULING);
    const bool no_doppleganger = (ow_wps_job_has_doppleganger_running(job->wps, job->vif_name) == false);
    return job_is_ready && no_doppleganger;
}

static bool
ow_wps_can_run_single_job_only(void)
{
    /* FIXME: ow_wps_job needs to be reworked to have a list
     * of vif_names[] instead of a single one. This is
     * necessary to allow configuring shared WPS across
     * multiple interfaces.
     */
    return false;
}

static struct ow_wps_job *
ow_wps_get_next_job(struct ow_wps *wps)
{
    const bool is_busy = (ow_wps_jobs_are_running(wps) > 0);
    if (ow_wps_can_run_single_job_only() && is_busy) return NULL;

    struct ds_dlist *jobs = &wps->jobs;
    struct ow_wps_job *job;

    ds_dlist_foreach(jobs, job) {
        if (ow_wps_job_is_eligible(job)) {
            return job;
        }
    }

    return NULL;
}

static void
ow_wps_work(struct ow_wps *wps)
{
    struct ow_wps_job *job = ow_wps_get_next_job(wps);
    if (job == NULL) return;
    ow_wps_job_set_scheduled(job);
}

static void
ow_wps_work_cb(struct osw_timer *t)
{
    struct ow_wps *wps = wps_work_to_work(t);
    LOGD(LOG_PREFIX("work"));
    ow_wps_work(wps);
}

static struct ow_wps_job *
ow_wps_job_alloc(const char *vif_name,
                 enum ow_wps_job_role role,
                 enum ow_wps_job_method method)
{
    static const struct osw_state_observer obs = {
        .name = __FILE__,
        .vif_added_fn = ow_wps_state_vif_added_cb,
        .vif_changed_fn = ow_wps_state_vif_changed_cb,
        .vif_removed_fn = ow_wps_state_vif_removed_cb,
        .wps_success_fn = ow_wps_state_success_cb,
        .wps_overlap_fn = ow_wps_state_overlap_cb,
        .wps_pbc_timeout_fn = ow_wps_state_timeout_cb,
    };
    static const struct osw_conf_mutator mut = {
        .name = __FILE__,
        .type = OSW_CONF_TAIL,
        .mutate_fn = ow_wps_conf_mutate_cb,
    };

    ASSERT(vif_name != NULL, "");

    struct ow_wps_job *job = CALLOC(1, sizeof(*job));
    job->vif_name = STRDUP(vif_name);
    job->role = role;
    job->method = method;
    job->sm_state = OW_WPS_JOB_PREPARING;

    osw_timer_init(&job->work, ow_wps_job_work_cb);
    osw_timer_init(&job->timeout, ow_wps_job_timeout_cb);

    job->obs = obs;
    job->mut = mut;

    return job;
}

static void
ow_wps_link_job(struct ow_wps *wps,
                struct ow_wps_job *job)
{
    if (WARN_ON(job->wps != NULL)) return;

    job->wps = wps;
    ds_dlist_insert_tail(&wps->jobs, job);
}

static void
ow_wps_attach_job(struct ow_wps *wps,
                  struct ow_wps_job *job)
{
    if (wps == NULL) return;
    if (job == NULL) return;

    ow_wps_link_job(wps, job);
    osw_state_register_observer(&job->obs);
    osw_conf_register_mutator(&job->mut);
    ow_wps_schedule_work(wps);
}

static struct ow_wps_job *
ow_wps_alloc_job_cb(struct ow_wps_ops *ops,
                    const char *vif_name,
                    enum ow_wps_job_role role,
                    enum ow_wps_job_method method)
{
    if (ops == NULL) return NULL;
    struct ow_wps *wps = ops_to_wps(ops);
    struct ow_wps_job *job = ow_wps_job_alloc(vif_name, role, method);
    ow_wps_attach_job(wps, job);
    return job;
}

static void
ow_wps_job_drop_cb(struct ow_wps_ops *ops,
                  struct ow_wps_job *job)
{
    if (ops == NULL) return;
    if (job == NULL) return;
    job->config.started_fn = NULL;
    job->config.finished_fn = NULL;
    ow_wps_job_set_dropped(job);
}

static void
ow_wps_job_set_priv_cb(struct ow_wps_ops *ops,
                       struct ow_wps_job *job,
                       void *priv)
{
    if (ops == NULL) return;
    if (job == NULL) return;
    if (WARN_ON(job->sm_state != OW_WPS_JOB_PREPARING)) return;

    job->config.priv = priv;
}

static void *
ow_wps_job_get_priv_cb(struct ow_wps_ops *ops,
                       const struct ow_wps_job *job)
{
    if (ops == NULL) return NULL;
    if (job == NULL) return NULL;
    return job->config.priv;
}

static enum ow_wps_job_result
ow_wps_job_get_result_cb(struct ow_wps_ops *ops,
                         const struct ow_wps_job *job)
{
    if (ops == NULL) return OW_WPS_JOB_UNSPECIFIED;
    if (job == NULL) return OW_WPS_JOB_UNSPECIFIED;
    return job->result;
}

static void
ow_wps_job_set_creds_cb(struct ow_wps_ops *ops,
                        struct ow_wps_job *job,
                        const struct osw_wps_cred_list *creds)
{
    if (ops == NULL) return;
    if (job == NULL) return;
    if (WARN_ON(job->sm_state != OW_WPS_JOB_PREPARING)) return;

    const size_t creds_count = (creds != NULL)
                             ? creds->count
                             : 0;
    const size_t creds_size = (creds_count * sizeof(*creds->list));
    const bool len_changed = creds_count != job->config.creds.count;
    const bool changed = (len_changed)
                      || (creds == NULL)
                      || (creds->list == NULL)
                      || (job->config.creds.list == NULL)
                      || (memcmp(creds->list, job->config.creds.list, creds_size) != 0);
    const bool not_changed = !changed;
    if (not_changed) return;

    FREE(job->config.creds.list);
    job->config.creds.list = creds ? MEMNDUP(creds->list, creds_size) : NULL;
    job->config.creds.count = creds_count;

    osw_conf_invalidate(&job->mut);
}

static void
ow_wps_job_set_timeout_sec(struct ow_wps_job *job,
                           int timeout_sec)
{
    if (job == NULL) return;
    if (job->config.timeout_sec == timeout_sec) return;
    if (WARN_ON(job->sm_state != OW_WPS_JOB_PREPARING)) return;

    job->config.timeout_sec = timeout_sec;
    ow_wps_job_schedule_work(job);
}

static void
ow_wps_job_set_callbacks_cb(struct ow_wps_ops *ops,
                            struct ow_wps_job *job,
                            ow_wps_job_started_fn_t *started_fn,
                            ow_wps_job_finished_fn_t *finished_fn)
{
    if (ops == NULL) return;
    if (job == NULL) return;
    if (WARN_ON(job->sm_state != OW_WPS_JOB_PREPARING)) return;

    job->config.started_fn = started_fn;
    job->config.finished_fn = finished_fn;
}

static void
ow_wps_job_start_cb(struct ow_wps_ops *ops,
                    struct ow_wps_job *job)
{
    if (ops == NULL) return;
    if (job == NULL) return;
    ow_wps_job_set_timeout_sec(job, OW_WPS_PBC_TIMEOUT_SEC);
    ow_wps_job_set_prepared(job);
}

static void
ow_wps_job_cancel_cb(struct ow_wps_ops *ops,
                     struct ow_wps_job *job)
{
    if (ops == NULL) return;
    if (job == NULL) return;
    ow_wps_job_set_cancelled(job);
}

static void
ow_wps_init(struct ow_wps *wps)
{
    static const struct ow_wps_ops ops = {
        .alloc_job_fn = ow_wps_alloc_job_cb,
        .job_drop_fn = ow_wps_job_drop_cb,
        .job_set_priv_fn = ow_wps_job_set_priv_cb,
        .job_get_priv_fn = ow_wps_job_get_priv_cb,
        .job_get_result_fn = ow_wps_job_get_result_cb,
        .job_set_creds_fn = ow_wps_job_set_creds_cb,
        .job_set_callbacks_fn = ow_wps_job_set_callbacks_cb,
        .job_start_fn = ow_wps_job_start_cb,
        .job_cancel_fn = ow_wps_job_cancel_cb,
    };

    memset(wps, 0, sizeof(*wps));
    ds_dlist_init(&wps->jobs, struct ow_wps_job, node);
    wps->ops = ops;
    osw_timer_init(&wps->work, ow_wps_work_cb);
}

static void
ow_wps_attach(struct ow_wps *wps)
{
    OSW_MODULE_LOAD(osw_state);
    OSW_MODULE_LOAD(osw_conf);
}

OSW_MODULE(ow_wps)
{
    static struct ow_wps wps;
    ow_wps_init(&wps);
    ow_wps_attach(&wps);
    return &wps;
}

#include "ow_wps_ut.c"
