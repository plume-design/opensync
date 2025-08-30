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

#include <log.h>
#include <memutil.h>
#include <const.h>
#include <util.h>

#include <osw_module.h>
#include <osw_types.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_bss_map.h>
#include <osw_state.h>
#include <osw_sta_chan_cap.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_i.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_policy_snr_level.h"

#define LOG_PREFIX(fmt, ...) \
    "ow: steer: " fmt, ##__VA_ARGS__

#define LOG_WITH_PREFIX(policy, fmt, ...) \
    LOG_PREFIX("%s%s: " fmt, \
    ow_steer_policy_get_prefix((policy)->base), \
    ow_steer_policy_snr_level_state_to_cstr((policy)->state), \
    ## __VA_ARGS__)

#define OW_STEER_POLICY_SNR_LEVEL_ENFORCE_DURATION_SEC 5
#define OW_STEER_POLICY_SNR_LEVEL_BACKOFF_DURATION_SEC 60
#define OW_STEER_POLICY_SNR_LEVEL_AGEOUT_DURATION_SEC 30
#define OW_STEER_POLICY_SNR_LEVEL_BACKOFF_EXP_BASE 2

enum ow_steer_policy_snr_level_state {
    /* This state awaits configuration changes and state
     * updates. It can only lead to ENFORCE.
     */
    OW_STEER_POLICY_SNR_LEVEL_STATE_IDLE,

    /* Once in enforce, it cannot be cancelled by means
     * other tha a timer. Once that happens it can only lead
     * to SETTLING.
     */
    OW_STEER_POLICY_SNR_LEVEL_STATE_ENFORCE,

    /* This is where aftermath is done. This awaits for
     * number of links to settle down, in case of possible
     * overlap or missing deauth. This can either go back to
     * IDLE or to BACKOFF if the station did not respond as
     * expected to the candidate masking.
     */
    OW_STEER_POLICY_SNR_LEVEL_STATE_SETTLING,

    /* This is where the policy is doing absolutely nothing
     * for a period of time implies by the number of
     * subsequent ENFORCE (actually checked in SETTLING)
     * failures. It can only go back to IDLE once timer runs
     * out.
     */
    OW_STEER_POLICY_SNR_LEVEL_STATE_BACKOFF,
};

static const char *
ow_steer_policy_snr_level_state_to_cstr(enum ow_steer_policy_snr_level_state state)
{
    switch (state) {
        case OW_STEER_POLICY_SNR_LEVEL_STATE_IDLE: return "idle";
        case OW_STEER_POLICY_SNR_LEVEL_STATE_ENFORCE: return "enforce";
        case OW_STEER_POLICY_SNR_LEVEL_STATE_SETTLING: return "settling";
        case OW_STEER_POLICY_SNR_LEVEL_STATE_BACKOFF: return "backoff";
    }
    return "?";
}

static const char *
ow_steer_policy_snr_level_mode_to_cstr(enum ow_steer_policy_snr_level_mode mode)
{
    switch (mode) {
        case OW_STEER_POLICY_SNR_LEVEL_BLOCK_FROM_BSSIDS_WHEN_ABOVE: return "block from_bssids when above";
        case OW_STEER_POLICY_SNR_LEVEL_BLOCK_FROM_BSSIDS_WHEN_BELOW: return "block from_bssids when below";
    }
    return "";
}

struct ow_steer_policy_snr_level_sta_info {
    struct ds_tree_node node;
    const struct osw_state_sta_info *sta_info;
    uint32_t sta_snr;
    uint64_t sta_bytes;
    bool sta_snr_valid;
    bool sta_bytes_valid;
    bool enforced;
};

struct ow_steer_policy_snr_level {
    struct ow_steer_policy *base;
    char *name;

    struct osw_state_observer state_obs;
    struct ds_tree sta_infos;

    enum ow_steer_policy_snr_level_state state;
    struct osw_timer backoff;
    struct osw_timer enforce;
    struct osw_timer ageout;
    uint64_t backoff_pow;
    bool no_alternatives;

    struct osw_hwaddr_list from_bssids;
    struct osw_hwaddr_list to_bssids;
    enum ow_steer_policy_snr_level_mode mode;
    uint32_t threshold_snr;
    uint64_t threshold_bytes;
    bool threshold_snr_valid;
    bool threshold_bytes_valid;
    int backoff_exp_base;
    int enforce_duration_seconds;
    int backoff_duration_seconds;
    int ageout_duration_seconds;
};

static bool
ow_steer_policy_snr_level_sta_connected_on_poor_bssid(struct ow_steer_policy_snr_level *policy)
{
    const size_t links = ds_tree_len(&policy->sta_infos);
    const bool sta_disconnected = (links == 0);
    if (sta_disconnected) return false;

    const bool ambiguous = (links > 1);
    if (ambiguous) return false;

    const struct ow_steer_policy_snr_level_sta_info *info = ds_tree_head(&policy->sta_infos);
    const struct osw_hwaddr *sta_vif_bssid = &info->sta_info->vif->drv_state->mac_addr;
    const bool connected_to_poor_bssid = osw_hwaddr_list_contains(policy->from_bssids.list, policy->from_bssids.count, sta_vif_bssid);
    return connected_to_poor_bssid;
}

static bool
ow_steer_policy_snr_level_should_consider_moving(struct ow_steer_policy_snr_level *policy)
{
    const struct ow_steer_policy_snr_level_sta_info *info = ds_tree_head(&policy->sta_infos);

    if (info->sta_snr_valid == false) return false;
    if (policy->threshold_snr_valid == false) return false;
    switch (policy->mode) {
        case OW_STEER_POLICY_SNR_LEVEL_BLOCK_FROM_BSSIDS_WHEN_ABOVE:
            if (info->sta_snr <= policy->threshold_snr) return false;
            break;
        case OW_STEER_POLICY_SNR_LEVEL_BLOCK_FROM_BSSIDS_WHEN_BELOW:
            if (info->sta_snr >= policy->threshold_snr) return false;
            break;
    }
    if (policy->threshold_bytes_valid && info->sta_bytes_valid == false) return false;
    if (policy->threshold_bytes_valid && info->sta_bytes_valid && info->sta_bytes >= policy->threshold_bytes) return false;
    return true;
}

static bool
ow_steer_policy_snr_level_better_bssids_exist(const struct ow_steer_policy_snr_level *policy)
{
    return policy->to_bssids.count > 0;
}

static void
ow_steer_policy_snr_level_arm_enforce(struct ow_steer_policy_snr_level *policy)
{
    const uint64_t at = osw_time_mono_clk()
                      + OSW_TIME_SEC(policy->enforce_duration_seconds);
    osw_timer_arm_at_nsec(&policy->enforce, at);
    LOGT(LOG_WITH_PREFIX(policy, "arming enforce"));
}

static void
ow_steer_policy_snr_level_arm_backoff(struct ow_steer_policy_snr_level *policy)
{
    const uint64_t sec = policy->backoff_pow * policy->backoff_duration_seconds;
    const uint64_t at = osw_time_mono_clk()
                      + OSW_TIME_SEC(sec);
    osw_timer_arm_at_nsec(&policy->backoff, at);
    LOGI(LOG_WITH_PREFIX(policy, "arming backoff in %"PRIu64" seconds", sec));
}

static void
ow_steer_policy_snr_level_arm_ageout(struct ow_steer_policy_snr_level *policy)
{
    const uint64_t at = osw_time_mono_clk()
                      + OSW_TIME_SEC(policy->ageout_duration_seconds);
    osw_timer_arm_at_nsec(&policy->ageout, at);
    LOGT(LOG_WITH_PREFIX(policy, "arming ageout"));
}

static void
ow_steer_policy_snr_level_enter_backoff(struct ow_steer_policy_snr_level *policy)
{
    LOGT(LOG_WITH_PREFIX(policy, "starting backoff"));
    policy->state = OW_STEER_POLICY_SNR_LEVEL_STATE_BACKOFF;

    if (osw_timer_is_armed(&policy->ageout) == false) {
        policy->backoff_pow = 1;
    }

    if (policy->no_alternatives) {
        policy->backoff_pow = 1;
    }

    ow_steer_policy_snr_level_arm_backoff(policy);
    if (osw_timer_is_armed(&policy->ageout)) {
        LOGT(LOG_WITH_PREFIX(policy, "disarming ageout"));
        osw_timer_disarm(&policy->ageout);
    }

    policy->backoff_pow *= policy->backoff_exp_base;
    if (policy->backoff_pow > UINT32_MAX) {
        LOGD(LOG_WITH_PREFIX(policy, "clamping backoff time"));
        policy->backoff_pow = UINT32_MAX;
    }
}

static void
ow_steer_policy_snr_level_recalc(struct ow_steer_policy_snr_level *policy)
{
    LOGT(LOG_WITH_PREFIX(policy, "recalc"));
    for (;;) {
        switch (policy->state) {
            case OW_STEER_POLICY_SNR_LEVEL_STATE_IDLE:
                {
                    struct ow_steer_policy_snr_level_sta_info *info = ds_tree_head(&policy->sta_infos);

                    if (ow_steer_policy_snr_level_sta_connected_on_poor_bssid(policy) == false) {
                        LOGT(LOG_WITH_PREFIX(policy, "not connected"));
                        return;
                    }

                    if (ow_steer_policy_snr_level_should_consider_moving(policy) == false) {
                        LOGT(LOG_WITH_PREFIX(policy, "co-located AP criteria not met"));
                        return;
                    }

                    if (ow_steer_policy_snr_level_better_bssids_exist(policy) == false) {
                        LOGT(LOG_WITH_PREFIX(policy, "no suitable co-located APs"));
                        return;
                    }

                    ASSERT(osw_timer_is_armed(&policy->enforce) == false, "");
                    ASSERT(info != NULL, "");
                    LOGT(LOG_WITH_PREFIX(policy, "staring enforce"));
                    ow_steer_policy_snr_level_arm_enforce(policy);
                    ow_steer_policy_trigger_executor(policy->base);
                    ow_steer_policy_schedule_stack_recalc(policy->base);
                    info->enforced = true;
                    policy->state = OW_STEER_POLICY_SNR_LEVEL_STATE_ENFORCE;
                    break;
                }
            case OW_STEER_POLICY_SNR_LEVEL_STATE_ENFORCE:
                {
                    if (osw_timer_is_armed(&policy->enforce)) return;
                    LOGT(LOG_WITH_PREFIX(policy, "stopping enforce"));
                    LOGT(LOG_WITH_PREFIX(policy, "starting settling"));
                    ow_steer_policy_dismiss_executor(policy->base);
                    ow_steer_policy_schedule_stack_recalc(policy->base);
                    policy->no_alternatives = false;
                    policy->state = OW_STEER_POLICY_SNR_LEVEL_STATE_SETTLING;
                    break;
                }
            case OW_STEER_POLICY_SNR_LEVEL_STATE_SETTLING:
                {
                    const size_t links = ds_tree_len(&policy->sta_infos);
                    const bool ambiguous = (links > 1);
                    if (ambiguous) return;

                    struct ow_steer_policy_snr_level_sta_info *info = ds_tree_head(&policy->sta_infos);
                    const struct osw_hwaddr *bssid = info ? &info->sta_info->vif->drv_state->mac_addr : NULL;
                    const char *vif_name = info ? info->sta_info->vif->vif_name : NULL;
                    const bool connected_to_poor_bssid = bssid
                                                       ? osw_hwaddr_list_contains(policy->from_bssids.list,
                                                                                  policy->from_bssids.count,
                                                                                  bssid)
                                                       : false;
                    const bool connected_to_better_bssid = bssid
                                                         ? osw_hwaddr_list_contains(policy->to_bssids.list,
                                                                                    policy->to_bssids.count,
                                                                                    bssid)
                                                         : false;
                    const bool connected = (info != NULL);

                    if (connected_to_better_bssid) {
                        LOGN(LOG_WITH_PREFIX(policy, "steered to better co-located AP "OSW_HWADDR_FMT" on %s",
                                                OSW_HWADDR_ARG(bssid), vif_name));
                        LOGT(LOG_WITH_PREFIX(policy, "stopping settling"));
                        LOGT(LOG_WITH_PREFIX(policy, "starting idle"));
                        policy->state = OW_STEER_POLICY_SNR_LEVEL_STATE_IDLE;
                    }
                    else {
                        if (connected_to_poor_bssid) {
                            /* If osw_state_observer had fired
                             * disconnect+connect it would prompt the `info`
                             * structure to be re-spawned and having enforce
                             * back into init state, ie. false. If its still
                             * true it means the client did not ever leave.
                             */
                            if (info->enforced) {
                                if (policy->no_alternatives) {
                                    LOGN(LOG_WITH_PREFIX(policy, "remained on poor bssid "OSW_HWADDR_FMT
                                                            " on %s because there were no target BSSIDs available",
                                                            OSW_HWADDR_ARG(bssid),
                                                            vif_name));
                                }
                                else {
                                    LOGN(LOG_WITH_PREFIX(policy, "remained on poor bssid "OSW_HWADDR_FMT" on %s",
                                                            OSW_HWADDR_ARG(bssid), vif_name));
                                }
                                info->enforced = false;
                            }
                            else {
                                LOGN(LOG_WITH_PREFIX(policy, "reconnected back to poor bssid "OSW_HWADDR_FMT" on %s",
                                                        OSW_HWADDR_ARG(bssid), vif_name));
                            }
                        }
                        else if (connected) {
                            LOGN(LOG_WITH_PREFIX(policy, "steered to out-of-group AP "OSW_HWADDR_FMT" on %s",
                                                    OSW_HWADDR_ARG(bssid), vif_name));
                        }
                        else {
                            LOGN(LOG_WITH_PREFIX(policy, "steered away to different AP perhaps"));
                        }

                        LOGT(LOG_WITH_PREFIX(policy, "stopping settling"));
                        ow_steer_policy_snr_level_enter_backoff(policy);
                    }
                    break;
                }
            case OW_STEER_POLICY_SNR_LEVEL_STATE_BACKOFF:
                {
                    if (osw_timer_is_armed(&policy->backoff)) return;
                    LOGT(LOG_WITH_PREFIX(policy, "stopping backoff"));
                    ow_steer_policy_snr_level_arm_ageout(policy);
                    LOGT(LOG_WITH_PREFIX(policy, "starting idle"));
                    policy->state = OW_STEER_POLICY_SNR_LEVEL_STATE_IDLE;
                    break;
                }
        }
    }
}

static void
ow_steer_policy_snr_level_enforce_expire_cb(struct osw_timer *t)
{
    struct ow_steer_policy_snr_level *policy = container_of(t, struct ow_steer_policy_snr_level, enforce);
    LOGT(LOG_WITH_PREFIX(policy, "enforce expired"));
    ow_steer_policy_snr_level_recalc(policy);
}

static void
ow_steer_policy_snr_level_backoff_expire_cb(struct osw_timer *t)
{
    struct ow_steer_policy_snr_level *policy = container_of(t, struct ow_steer_policy_snr_level, backoff);
    LOGI(LOG_WITH_PREFIX(policy, "backoff expired"));
    ow_steer_policy_snr_level_recalc(policy);
}

static void
ow_steer_policy_snr_level_ageout_expire_cb(struct osw_timer *t)
{
    struct ow_steer_policy_snr_level *policy = container_of(t, struct ow_steer_policy_snr_level, ageout);
    LOGT(LOG_WITH_PREFIX(policy, "ageout expired"));
    ow_steer_policy_snr_level_recalc(policy);
}

static void
ow_steer_policy_snr_level_set_sta_info(struct ow_steer_policy_snr_level *policy,
                                       const struct osw_state_sta_info *sta_info,
                                       const bool connected)
{
    const struct osw_hwaddr *sta_addr = ow_steer_policy_get_sta_addr(policy->base);
    const bool other_sta = (osw_hwaddr_is_equal(sta_addr, sta_info->mac_addr) == false);
    if (other_sta) return;

    const struct osw_hwaddr *bssid = &sta_info->vif->drv_state->mac_addr;
    const char *vif_name = sta_info->vif->vif_name;
    struct ow_steer_policy_snr_level_sta_info *info = ds_tree_find(&policy->sta_infos, bssid);
            LOGT(LOG_WITH_PREFIX(policy, "station on %s ("OSW_HWADDR_FMT") info=%p bssid=%p connected=%d",
                            vif_name,
                            OSW_HWADDR_ARG(bssid),
                            info,
                            bssid,
                            connected));
    if (info == NULL) {
        if (connected) {
            info = CALLOC(1, sizeof(*info));
            info->sta_info = sta_info;
            LOGT(LOG_WITH_PREFIX(policy, "station connected to %s ("OSW_HWADDR_FMT")",
                            vif_name,
                            OSW_HWADDR_ARG(bssid)));
            ds_tree_insert(&policy->sta_infos, info, bssid);
        }
        else {
            LOGW(LOG_WITH_PREFIX(policy, "station disconnected from %s ("OSW_HWADDR_FMT"), "
                                    "but was never seen connect",
                                    vif_name,
                                    OSW_HWADDR_ARG(bssid)));
        }
    }
    else {
        if (connected) {
            LOGW(LOG_WITH_PREFIX(policy, "station re-connected to %s ("OSW_HWADDR_FMT"), "
                                    "but was never seen disconnect",
                                    vif_name,
                                    OSW_HWADDR_ARG(bssid)));
        }
        else {
            LOGT(LOG_WITH_PREFIX(policy, "station disconnected from %s ("OSW_HWADDR_FMT")",
                            vif_name,
                            OSW_HWADDR_ARG(bssid)));
            ds_tree_remove(&policy->sta_infos, info);
            FREE(info);
        }
    }
    ow_steer_policy_snr_level_recalc(policy);
}

static void
ow_steer_policy_snr_level_set_sta_snr(struct ow_steer_policy_snr_level *policy,
                                      const struct osw_hwaddr *bssid,
                                      uint32_t snr)
{
    struct ow_steer_policy_snr_level_sta_info *info = ds_tree_find(&policy->sta_infos, bssid);
    if (info == NULL) return;

    const bool unchanged = (info->sta_snr_valid)
                        && (info->sta_snr == snr);
    if (unchanged) return;

    LOGT(LOG_WITH_PREFIX(policy, "sta: snr: %"PRIu32" (%s)",
                    info->sta_snr,
                    info->sta_snr_valid ? "valid" : "invalid"));

    info->sta_snr = snr;
    info->sta_snr_valid = true;
    ow_steer_policy_snr_level_recalc(policy);
}

static void
ow_steer_policy_snr_level_set_sta_bytes(struct ow_steer_policy_snr_level *policy,
                                        const struct osw_hwaddr *bssid,
                                        uint64_t bytes)
{
    struct ow_steer_policy_snr_level_sta_info *info = ds_tree_find(&policy->sta_infos, bssid);
    if (info == NULL) return;

    const bool unchanged = (info->sta_bytes_valid)
                        && (info->sta_bytes == bytes);
    if (unchanged) return;

    LOGT(LOG_WITH_PREFIX(policy, "sta: bytes: %"PRIu64" (%s)",
                    info->sta_bytes,
                    info->sta_bytes_valid ? "valid" : "invalid"));

    info->sta_bytes = bytes;
    info->sta_bytes_valid = true;
    ow_steer_policy_snr_level_recalc(policy);
}

void
ow_steer_policy_snr_level_set_sta_threshold_snr(struct ow_steer_policy_snr_level *policy,
                                                const uint32_t *snr)
{
    const bool unchanged = (policy->threshold_snr_valid == (snr != NULL))
                        && (snr != NULL ? (policy->threshold_snr == *snr) : true);
    if (unchanged) return;

    policy->threshold_snr = snr ? *snr : 0;
    policy->threshold_snr_valid = snr ? true : false;
    LOGT(LOG_WITH_PREFIX(policy, "threshold: snr: %"PRIu32" (%s)",
                    policy->threshold_snr,
                    policy->threshold_snr_valid ? "valid" : "invalid"));
    ow_steer_policy_snr_level_recalc(policy);
}

void
ow_steer_policy_snr_level_set_sta_threshold_bytes(struct ow_steer_policy_snr_level *policy,
                                                  const uint64_t *bytes)
{
    const bool unchanged = (policy->threshold_bytes_valid == (bytes != NULL))
                        && (bytes != NULL ? (policy->threshold_bytes == *bytes) : true);
    if (unchanged) return;

    policy->threshold_bytes = bytes ? *bytes : 0;
    policy->threshold_bytes_valid = bytes ? true : false;
    LOGT(LOG_WITH_PREFIX(policy, "threshold: bytes: %"PRIu64" (%s)",
                    policy->threshold_bytes,
                    policy->threshold_bytes_valid ? "valid" : "invalid"));
    ow_steer_policy_snr_level_recalc(policy);
}

void
ow_steer_policy_snr_level_set_from_bssids(struct ow_steer_policy_snr_level *policy,
                                          const struct osw_hwaddr_list *from_bssids)
{
    if (osw_hwaddr_list_is_equal(&policy->from_bssids, from_bssids)) return;

    size_t i;
    char *list = NULL;
    for (i = 0; i < from_bssids->count; i++) {
        const struct osw_hwaddr *addr = &from_bssids->list[i];
        strgrow(&list, OSW_HWADDR_FMT",", OSW_HWADDR_ARG(addr));
    }
    if (list != NULL) {
        strchomp(list, ",");
    }

    LOGT(LOG_WITH_PREFIX(policy, "from_bssids: %s", list ? list : "empty"));
    FREE(list);

    FREE(policy->from_bssids.list);
    policy->from_bssids.list = NULL;
    policy->from_bssids.count = 0;

    if (from_bssids->list != NULL) {
        const size_t size = sizeof(from_bssids->list[0]) * from_bssids->count;
        policy->from_bssids.list = MEMNDUP(from_bssids->list, size);
        policy->from_bssids.count = from_bssids->count;
    }

    ow_steer_policy_snr_level_recalc(policy);
}

void
ow_steer_policy_snr_level_set_to_bssids(struct ow_steer_policy_snr_level *policy,
                                        const struct osw_hwaddr_list *to_bssids)
{
    if (osw_hwaddr_list_is_equal(&policy->to_bssids, to_bssids)) return;

    size_t i;
    char *list = NULL;
    for (i = 0; i < to_bssids->count; i++) {
        const struct osw_hwaddr *addr = &to_bssids->list[i];
        strgrow(&list, OSW_HWADDR_FMT",", OSW_HWADDR_ARG(addr));
    }
    if (list != NULL) {
        strchomp(list, ",");
    }

    LOGT(LOG_WITH_PREFIX(policy, "to_bssids: %s", list ? list : "empty"));
    FREE(list);

    FREE(policy->to_bssids.list);
    policy->to_bssids.list = NULL;
    policy->to_bssids.count = 0;

    if (to_bssids->list != NULL) {
        const size_t size = sizeof(to_bssids->list[0]) * to_bssids->count;
        policy->to_bssids.list = MEMNDUP(to_bssids->list, size);
        policy->to_bssids.count = to_bssids->count;
    }

    ow_steer_policy_snr_level_recalc(policy);
}

static void
ow_steer_policy_snr_level_sta_connected_cb(struct osw_state_observer *obs,
                                           const struct osw_state_sta_info *sta_info)
{
    struct ow_steer_policy_snr_level *policy = container_of(obs, struct ow_steer_policy_snr_level, state_obs);
    ow_steer_policy_snr_level_set_sta_info(policy, sta_info, true);
}

static void
ow_steer_policy_snr_level_sta_disconnected_cb(struct osw_state_observer *obs,
                                              const struct osw_state_sta_info *sta_info)
{
    struct ow_steer_policy_snr_level *policy = container_of(obs, struct ow_steer_policy_snr_level, state_obs);
    ow_steer_policy_snr_level_set_sta_info(policy, sta_info, false);
}

static void
ow_steer_policy_snr_level_sigusr1_dump_cb(osw_diag_pipe_t *pipe,
                                          struct ow_steer_policy *base)
{
    struct ow_steer_policy_snr_level *policy = ow_steer_policy_get_priv(base);
    struct ow_steer_policy_snr_level_sta_info *info;
    size_t i;

    osw_diag_pipe_writef(pipe, "ow: steer:         state:");
    osw_diag_pipe_writef(pipe, "ow: steer:          state: %s", ow_steer_policy_snr_level_state_to_cstr(policy->state));
    osw_diag_pipe_writef(pipe, "ow: steer:          backoff: %s", osw_timer_is_armed(&policy->backoff) ? "armed" : "disarmed");
    osw_diag_pipe_writef(pipe, "ow: steer:          enforce: %s", osw_timer_is_armed(&policy->enforce) ? "armed" : "disarmed");
    osw_diag_pipe_writef(pipe, "ow: steer:          ageout: %s", osw_timer_is_armed(&policy->ageout) ? "armed" : "disarmed");
    osw_diag_pipe_writef(pipe, "ow: steer:          backoff_pow: %"PRIu64, policy->backoff_pow);
    osw_diag_pipe_writef(pipe, "ow: steer:          no_alternatives: %s", policy->no_alternatives ? "yes" : "no");
    osw_diag_pipe_writef(pipe, "ow: steer:          sta_infos:");
    ds_tree_foreach(&policy->sta_infos, info) {
        osw_diag_pipe_writef(pipe, "ow: steer:           "OSW_HWADDR_FMT":", OSW_HWADDR_ARG(info->sta_info->mac_addr));
        osw_diag_pipe_writef(pipe, "ow: steer:            vif_name: %s", info->sta_info->vif->vif_name);
        osw_diag_pipe_writef(pipe, "ow: steer:            bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&info->sta_info->vif->drv_state->mac_addr));
        osw_diag_pipe_writef(pipe, "ow: steer:            snr: %"PRIu32" (%s)", info->sta_snr, info->sta_snr_valid ? "valid" : "invalid");
        osw_diag_pipe_writef(pipe, "ow: steer:            bytes: %"PRIu64" (%s)", info->sta_bytes, info->sta_bytes_valid ? "valid" : "invalid");
        osw_diag_pipe_writef(pipe, "ow: steer:            enforced: %s", info->enforced ? "yes" : "no");
    }

    osw_diag_pipe_writef(pipe, "ow: steer:         config:");
    osw_diag_pipe_writef(pipe, "ow: steer:          from_bssids:");
    for (i = 0; i < policy->from_bssids.count; i++) {
        const struct osw_hwaddr *bssid = &policy->from_bssids.list[i];
        osw_diag_pipe_writef(pipe, "ow: steer:           "OSW_HWADDR_FMT, OSW_HWADDR_ARG(bssid));
    }
    osw_diag_pipe_writef(pipe, "ow: steer:          to_bssids:");
    for (i = 0; i < policy->to_bssids.count; i++) {
        const struct osw_hwaddr *bssid = &policy->to_bssids.list[i];
        osw_diag_pipe_writef(pipe, "ow: steer:           "OSW_HWADDR_FMT, OSW_HWADDR_ARG(bssid));
    }
    osw_diag_pipe_writef(pipe, "ow: steer:          mode: %s", ow_steer_policy_snr_level_mode_to_cstr(policy->mode));
    osw_diag_pipe_writef(pipe, "ow: steer:          threshold_snr: %"PRIu32" (%s)", policy->threshold_snr, policy->threshold_snr_valid ? "set" : "not set");
    osw_diag_pipe_writef(pipe, "ow: steer:          threshold_bytes: %"PRIu64" (%s)", policy->threshold_bytes, policy->threshold_bytes_valid ? "set" : "not set");
    osw_diag_pipe_writef(pipe, "ow: steer:          backoff_exp_base: %d", policy->backoff_exp_base);
    osw_diag_pipe_writef(pipe, "ow: steer:          enforce_duration_seconds: %d", policy->enforce_duration_seconds);
    osw_diag_pipe_writef(pipe, "ow: steer:          backoff_duration_seconds: %d", policy->backoff_duration_seconds);
    osw_diag_pipe_writef(pipe, "ow: steer:          ageout_duration_seconds: %d", policy->ageout_duration_seconds);
}

static void
ow_steer_policy_snr_level_sta_snr_cb(struct ow_steer_policy *base,
                                     const struct osw_hwaddr *bssid,
                                     uint32_t snr_db)
{
    struct ow_steer_policy_snr_level *policy = ow_steer_policy_get_priv(base);
    ow_steer_policy_snr_level_set_sta_snr(policy, bssid, snr_db);
}

static void
ow_steer_policy_snr_level_sta_bytes_cb(struct ow_steer_policy *base,
                                       const struct osw_hwaddr *bssid,
                                       uint64_t bytes)
{
    struct ow_steer_policy_snr_level *policy = ow_steer_policy_get_priv(base);
    ow_steer_policy_snr_level_set_sta_bytes(policy, bssid, bytes);
}

static void
ow_steer_policy_snr_level_recalc_cb(struct ow_steer_policy *base,
                                    struct ow_steer_candidate_list *candidate_list)
{
    struct ow_steer_policy_snr_level *policy = ow_steer_policy_get_priv(base);

    switch (policy->state) {
        case OW_STEER_POLICY_SNR_LEVEL_STATE_IDLE:
        case OW_STEER_POLICY_SNR_LEVEL_STATE_BACKOFF:
        case OW_STEER_POLICY_SNR_LEVEL_STATE_SETTLING:
            return;
        case OW_STEER_POLICY_SNR_LEVEL_STATE_ENFORCE:
            break;
    }

    size_t available_to_bssids = 0;
    size_t i;
    for (i = 0; i < policy->to_bssids.count; i++) {
        const struct osw_hwaddr *bssid = &policy->to_bssids.list[i];
        struct ow_steer_candidate *c = ow_steer_candidate_list_lookup(candidate_list, bssid);
        if (c == NULL) continue;
        const enum ow_steer_candidate_preference p = ow_steer_candidate_get_preference(c);
        const char *reason = ow_steer_policy_get_name(base);
        switch (p) {
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                /* Make sure to mark the co-located BSS as
                 * available to prevent it from getting
                 * blocked by subsequent policies. This
                 * guarantees that the upsteer has a chance
                 * of succeeding. If co-located APs would
                 * become blocked and non-co-located APs
                 * would remain unblocked this could result
                 * in flapping between physical APs.
                 */
                ow_steer_candidate_set_preference(c, reason, OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
                /* FALLTHROUGH */
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                available_to_bssids++;
                break;
        }
    }

    /* This can happen if any of the other underlying
     * policies have already marked these co-located BSSIDs
     * as unavailable. This can be true for single-band
     * clients where 5GHz or 6GHz is unavailable.
     *
     * In that case do _not_ try to hard block the client on
     * any of the worse bssids. Let it stay.
     */
    policy->no_alternatives = (available_to_bssids == 0);
    if (policy->no_alternatives) {
        LOGI(LOG_WITH_PREFIX(policy, "no to_bssids candidate is available, won't block from_bssids"));
        return;
    }

    for (i = 0; i < policy->from_bssids.count; i++) {
        const struct osw_hwaddr *bssid = &policy->from_bssids.list[i];
        struct ow_steer_candidate *c = ow_steer_candidate_list_lookup(candidate_list, bssid);
        if (c == NULL) continue;
        const enum ow_steer_candidate_preference p = ow_steer_candidate_get_preference(c);
        const char *p_str = ow_steer_candidate_preference_to_cstr(p);
        const char *reason = ow_steer_policy_get_name(base);
        switch (p) {
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                /* This could collide with other policies,
                 * eg. cloud controller client steering
                 * (inbound, kick). At least log the
                 * occurrence.
                 */
                LOGI(LOG_WITH_PREFIX(policy, "candidate "OSW_HWADDR_FMT
                                        " is already marked as %s, can't hard block",
                                        OSW_HWADDR_ARG(bssid),
                                        p_str));
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                ow_steer_candidate_set_preference(c, reason, OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
                break;
        }
    }
}

struct ow_steer_policy_snr_level *
ow_steer_policy_snr_level_alloc(const char *name,
                                const struct osw_hwaddr *sta_addr,
                                enum ow_steer_policy_snr_level_mode mode,
                                const struct ow_steer_policy_mediator *mediator,
                                const char *log_prefix)
{
    static const struct ow_steer_policy_ops ops = {
        .sigusr1_dump_fn = ow_steer_policy_snr_level_sigusr1_dump_cb,
        .recalc_fn = ow_steer_policy_snr_level_recalc_cb,
        .sta_snr_change_fn = ow_steer_policy_snr_level_sta_snr_cb,
        .sta_data_vol_change_fn = ow_steer_policy_snr_level_sta_bytes_cb,
    };
    static const struct osw_state_observer state_obs = {
        .sta_connected_fn = ow_steer_policy_snr_level_sta_connected_cb,
        .sta_disconnected_fn = ow_steer_policy_snr_level_sta_disconnected_cb,
    };
    static const struct osw_hwaddr bcast = { .octet = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } };
    struct ow_steer_policy_snr_level *policy = CALLOC(1, sizeof(*policy));
    osw_timer_init(&policy->enforce, ow_steer_policy_snr_level_enforce_expire_cb);
    osw_timer_init(&policy->backoff, ow_steer_policy_snr_level_backoff_expire_cb);
    osw_timer_init(&policy->ageout, ow_steer_policy_snr_level_ageout_expire_cb);
    ds_tree_init(&policy->sta_infos, (ds_key_cmp_t *)osw_hwaddr_cmp, struct ow_steer_policy_snr_level_sta_info, node);
    policy->base = ow_steer_policy_create(name, sta_addr, &ops, mediator, log_prefix, policy);
    policy->enforce_duration_seconds = OW_STEER_POLICY_SNR_LEVEL_ENFORCE_DURATION_SEC;
    policy->backoff_duration_seconds = OW_STEER_POLICY_SNR_LEVEL_BACKOFF_DURATION_SEC;
    policy->ageout_duration_seconds = OW_STEER_POLICY_SNR_LEVEL_AGEOUT_DURATION_SEC;
    policy->backoff_exp_base = OW_STEER_POLICY_SNR_LEVEL_BACKOFF_EXP_BASE;
    policy->mode = mode;
    policy->state_obs = state_obs;
    policy->name = strfmt("%s: "OSW_HWADDR_FMT, name, OSW_HWADDR_ARG(sta_addr));
    policy->state_obs.name = policy->name;
    ow_steer_policy_set_bssid(policy->base, &bcast);
    osw_state_register_observer(&policy->state_obs);
    return policy;
}

void
ow_steer_policy_snr_level_free(struct ow_steer_policy_snr_level *policy)
{
    osw_state_unregister_observer(&policy->state_obs);
    ow_steer_policy_free(policy->base);
    osw_timer_disarm(&policy->backoff);
    osw_timer_disarm(&policy->enforce);
    osw_timer_disarm(&policy->ageout);
    FREE(policy->from_bssids.list);
    FREE(policy->to_bssids.list);
    ASSERT(ds_tree_is_empty(&policy->sta_infos),
           "osw_state_unregister_observer() should've emptied it");
    FREE(policy->name);
    FREE(policy);
}

struct ow_steer_policy *
ow_steer_policy_snr_level_get_base(struct ow_steer_policy_snr_level *policy)
{
    return policy->base;
}
