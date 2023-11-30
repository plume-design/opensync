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

#include <osw_ut.h>
#include <os.h>

struct ow_wps_ut_simple {
    struct ow_wps wps;
    struct ow_wps_job *job;
    struct osw_drv_vif_state vif0_state;
    struct osw_state_vif_info vif0_info;
    unsigned int started_cnt;
    unsigned int finished_cnt;
    unsigned int invalidate_conf_cnt;
    enum ow_wps_job_result expected_result;
    struct osw_conf_observer obs;
};

static void
ow_wps_ut_osw_conf_invalidate_cb(struct osw_conf_observer *obs)
{
    struct ow_wps_ut_simple *ut = container_of(obs, struct ow_wps_ut_simple, obs);
    LOGD(LOG_PREFIX_JOB(ut->job, "ut: invalidated"));
    ut->invalidate_conf_cnt++;
}

static void
ow_wps_ut_job_started_fn(struct ow_wps_ops *ops,
                         struct ow_wps_job *job)
{
    struct ow_wps_ut_simple *ut = ow_wps_op_job_get_priv(ops, job);
    LOGD(LOG_PREFIX_JOB(job, "ut: started"));
    ut->started_cnt++;
}

static void
ow_wps_ut_job_finished_fn(struct ow_wps_ops *ops,
                          struct ow_wps_job *job)
{
    struct ow_wps_ut_simple *ut = ow_wps_op_job_get_priv(ops, job);
    const enum ow_wps_job_result result = ow_wps_op_job_get_result(ops, job);
    LOGD(LOG_PREFIX_JOB(job, "ut: finished: result=%s expected=%s",
                         ow_wps_job_result_to_cstr(result),
                         ow_wps_job_result_to_cstr(ut->expected_result)));
    ut->finished_cnt++;
    assert(ut->expected_result == result);
}

static void
ow_wps_ut_simple_init(struct ow_wps_ut_simple *ut,
                      const enum ow_wps_job_result expected_result)
{
    memset(ut, 0, sizeof(*ut));

    osw_ut_time_init();
    ow_wps_init(&ut->wps);

    ut->expected_result = expected_result;

    ut->vif0_state.status = OSW_VIF_DISABLED;
    ut->vif0_state.vif_type = OSW_VIF_AP;

    ut->vif0_info.vif_name = "vif0";
    ut->vif0_info.drv_state = &ut->vif0_state;

    ut->obs.mutated_fn = ow_wps_ut_osw_conf_invalidate_cb;
    osw_conf_register_observer(&ut->obs);

    ut->job = ow_wps_job_alloc("vif0",
                               OW_WPS_ROLE_ENROLLER,
                               OW_WPS_METHOD_PBC);
    OSW_UT_EVAL(ut->job != NULL);
    ow_wps_link_job(&ut->wps, ut->job);
    ow_wps_op_job_set_priv(&ut->wps.ops,
                           ut->job,
                           ut);
    ow_wps_op_job_set_callbacks(&ut->wps.ops,
                                ut->job,
                                ow_wps_ut_job_started_fn,
                                ow_wps_ut_job_finished_fn);

    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut->invalidate_conf_cnt == 0);
    OSW_UT_EVAL(ut->started_cnt == 0);
    OSW_UT_EVAL(ut->finished_cnt == 0);

    struct osw_wps_cred cred = {
        .psk = {
            .str = { "qwertyuiop" },
        },
    };
    const struct osw_wps_cred_list cred_list = {
        .list = &cred,
        .count = 1,
    };
    ow_wps_op_job_set_creds(&ut->wps.ops,
                            ut->job,
                            &cred_list);
    ow_wps_op_job_start(&ut->wps.ops,
                        ut->job);
}

static void
ow_wps_ut_simple_prepare(struct ow_wps_ut_simple *ut)
{
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut->invalidate_conf_cnt > 0);
    OSW_UT_EVAL(ut->started_cnt == 0);
    OSW_UT_EVAL(ut->finished_cnt == 0);

    ut->invalidate_conf_cnt = 0;
    ow_wps_op_job_start(&ut->wps.ops, ut->job);
    OSW_UT_EVAL(ut->invalidate_conf_cnt == 0);

    ut->invalidate_conf_cnt = 0;
    ow_wps_state_vif_added_cb(&ut->job->obs, &ut->vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut->invalidate_conf_cnt == 0);
    OSW_UT_EVAL(ut->started_cnt == 0);
    OSW_UT_EVAL(ut->finished_cnt == 0);
    OSW_UT_EVAL(ut->job->sm_state == OW_WPS_JOB_ACTIVATING);

    ut->vif0_state.status = OSW_VIF_ENABLED;
    ut->vif0_state.u.ap.mode.wps = true;
    ut->vif0_state.u.ap.wps_pbc = true;
    ut->vif0_state.u.ap.wps_cred_list = ut->job->config.creds;
}

static void
ow_wps_ut_simple_set_active(struct ow_wps_ut_simple *ut,
                            const bool active)
{
    ut->vif0_state.u.ap.wps_pbc = active;
    ow_wps_state_vif_changed_cb(&ut->job->obs, &ut->vif0_info);
}

static void
ow_wps_ut_simple_activate(struct ow_wps_ut_simple *ut)
{
    ow_wps_state_vif_changed_cb(&ut->job->obs, &ut->vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut->job->sm_state == OW_WPS_JOB_RUNNING);
    OSW_UT_EVAL(ut->started_cnt == 1);
}

static void
ow_wps_ut_simple_deactivate(struct ow_wps_ut_simple *ut)
{
    ow_wps_ut_simple_set_active(ut, false);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut->job->sm_state == OW_WPS_JOB_DROPPING);
    OSW_UT_EVAL(ut->finished_cnt == 1);
}

static void
ow_wps_ut_simple_start(struct ow_wps_ut_simple *ut)
{
    ow_wps_ut_simple_prepare(ut);
    ow_wps_ut_simple_activate(ut);
}

static void
ow_wps_ut_simple_check_result(struct ow_wps_ut_simple *ut)
{
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ow_wps_op_job_get_result(&ut->wps.ops, ut->job) == ut->expected_result);
    OSW_UT_EVAL(ut->started_cnt == 1);
    OSW_UT_EVAL(ut->finished_cnt == 1);
}

OSW_UT(ow_wps_ap_pbc_success)
{
    struct ow_wps_ut_simple ut;
    ow_wps_ut_simple_init(&ut, OW_WPS_JOB_SUCCEEDED);
    ow_wps_ut_simple_start(&ut);
    ow_wps_state_success_cb(&ut.job->obs, &ut.vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_DEACTIVATING);
    ow_wps_ut_simple_deactivate(&ut);
    ow_wps_ut_simple_check_result(&ut);
    OSW_UT_EVAL(ds_dlist_is_empty(&ut.wps.jobs) == false);
}

OSW_UT(ow_wps_ap_pbc_timeout_external)
{
    struct ow_wps_ut_simple ut;
    ow_wps_ut_simple_init(&ut, OW_WPS_JOB_TIMED_OUT_EXTERNALLY);
    ow_wps_ut_simple_start(&ut);
    osw_ut_time_advance(OSW_TIME_SEC(OW_WPS_PBC_TIMEOUT_SEC / 2));
    ow_wps_state_timeout_cb(&ut.job->obs, &ut.vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_DEACTIVATING);
    ow_wps_ut_simple_deactivate(&ut);
    ow_wps_ut_simple_check_result(&ut);
}

OSW_UT(ow_wps_ap_pbc_timeout_internal)
{
    struct ow_wps_ut_simple ut;
    ow_wps_ut_simple_init(&ut, OW_WPS_JOB_TIMED_OUT_INTERNALLY);
    ow_wps_ut_simple_start(&ut);
    osw_ut_time_advance(OSW_TIME_SEC(OW_WPS_PBC_TIMEOUT_SEC) + 1);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_DEACTIVATING);
    ow_wps_ut_simple_deactivate(&ut);
    ow_wps_ut_simple_check_result(&ut);
}

OSW_UT(ow_wps_ap_pbc_overlap)
{
    struct ow_wps_ut_simple ut;
    ow_wps_ut_simple_init(&ut, OW_WPS_JOB_OVERLAPPED);
    ow_wps_ut_simple_start(&ut);
    osw_ut_time_advance(OSW_TIME_SEC(OW_WPS_PBC_TIMEOUT_SEC / 2));
    ow_wps_state_overlap_cb(&ut.job->obs, &ut.vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_DEACTIVATING);
    ow_wps_ut_simple_deactivate(&ut);
    ow_wps_ut_simple_check_result(&ut);
}

OSW_UT(ow_wps_ap_pbc_cancel)
{
    struct ow_wps_ut_simple ut;
    ow_wps_ut_simple_init(&ut, OW_WPS_JOB_CANCELLED);
    ow_wps_ut_simple_start(&ut);
    osw_ut_time_advance(OSW_TIME_SEC(OW_WPS_PBC_TIMEOUT_SEC / 2));
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_RUNNING);
    ow_wps_op_job_cancel(&ut.wps.ops, ut.job);
    ow_wps_ut_simple_set_active(&ut, false);
    ow_wps_ut_simple_check_result(&ut);
}

OSW_UT(ow_wps_ap_pbc_interrupted)
{
    struct ow_wps_ut_simple ut;
    ow_wps_ut_simple_init(&ut, OW_WPS_JOB_INTERRUPTED);
    ow_wps_ut_simple_start(&ut);
    osw_ut_time_advance(OSW_TIME_SEC(OW_WPS_PBC_TIMEOUT_SEC / 2));
    ow_wps_ut_simple_set_active(&ut, false);
    ow_wps_ut_simple_check_result(&ut);
}

OSW_UT(ow_wps_ap_pbc_reactivated)
{
    struct ow_wps_ut_simple ut;
    ow_wps_ut_simple_init(&ut, OW_WPS_JOB_TIMED_OUT_INTERNALLY);
    ow_wps_ut_simple_start(&ut);
    osw_ut_time_advance(OSW_TIME_SEC(OW_WPS_PBC_TIMEOUT_SEC / 2));
    ow_wps_ut_simple_set_active(&ut, false);
    ow_wps_job_sm_work(ut.job);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_INTERRUPTING);
    ow_wps_ut_simple_set_active(&ut, true);
    ow_wps_job_sm_work(ut.job);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_RUNNING);
    osw_ut_time_advance(OSW_TIME_SEC(OW_WPS_PBC_TIMEOUT_SEC / 2) + 1);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_DEACTIVATING);
    ow_wps_ut_simple_deactivate(&ut);
    ow_wps_ut_simple_check_result(&ut);
    OSW_UT_EVAL(ut.job->num_reactivated == 1);
}

OSW_UT(ow_wps_ap_pbc_finished_before_activated)
{
    struct ow_wps_ut_simple ut;

    ow_wps_ut_simple_init(&ut, OW_WPS_JOB_UNSPECIFIED);
    ow_wps_ut_simple_prepare(&ut);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_ACTIVATING);

    ut.vif0_state.u.ap.wps_pbc = false;
    ow_wps_state_vif_changed_cb(&ut.job->obs, &ut.vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_ACTIVATING);

    ow_wps_state_success_cb(&ut.job->obs, &ut.vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_DROPPING);

    ow_wps_ut_simple_check_result(&ut);
}

OSW_UT(ow_wps_ap_pbc_fragmented_activation)
{
    struct ow_wps_ut_simple ut;

    ow_wps_ut_simple_init(&ut, OW_WPS_JOB_SUCCEEDED);
    ow_wps_ut_simple_prepare(&ut);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_ACTIVATING);

    ut.vif0_state.status = OSW_VIF_DISABLED;
    ut.vif0_state.u.ap.mode.wps = false;
    ut.vif0_state.u.ap.wps_pbc = false;
    MEMZERO(ut.vif0_state.u.ap.wps_cred_list);

    ut.vif0_state.u.ap.wps_pbc = true;
    ow_wps_state_vif_changed_cb(&ut.job->obs, &ut.vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_ACTIVATING);

    ut.vif0_state.u.ap.mode.wps = true;
    ow_wps_state_vif_changed_cb(&ut.job->obs, &ut.vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_ACTIVATING);

    ut.vif0_state.status = OSW_VIF_ENABLED;
    ow_wps_state_vif_changed_cb(&ut.job->obs, &ut.vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_ACTIVATING);

    ut.vif0_state.u.ap.wps_cred_list = ut.job->config.creds;
    ow_wps_state_vif_changed_cb(&ut.job->obs, &ut.vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_RUNNING);

    ow_wps_state_success_cb(&ut.job->obs, &ut.vif0_info);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_DEACTIVATING);

    ow_wps_ut_simple_deactivate(&ut);
    ow_wps_ut_simple_check_result(&ut);
}

OSW_UT(ow_wps_ap_pbc_drop_before_prep)
{
    struct ow_wps_ut_simple ut;

    ow_wps_ut_simple_init(&ut, OW_WPS_JOB_UNSPECIFIED);
    OSW_UT_EVAL(ds_dlist_is_empty(&ut.wps.jobs) == false);
    ow_wps_op_job_drop(&ut.wps.ops, ut.job);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ds_dlist_is_empty(&ut.wps.jobs) == true);
}

OSW_UT(ow_wps_ap_pbc_drop_before_running)
{
    struct ow_wps_ut_simple ut;

    ow_wps_ut_simple_init(&ut, OW_WPS_JOB_CANCELLED);
    ow_wps_ut_simple_prepare(&ut);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ut.job->sm_state == OW_WPS_JOB_ACTIVATING);

    ow_wps_op_job_drop(&ut.wps.ops, ut.job);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ow_wps_op_job_get_result(&ut.wps.ops, ut.job) == ut.expected_result);

    OSW_UT_EVAL(ds_dlist_is_empty(&ut.wps.jobs) == true);
}
