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

#include <endian.h>
#include <log.h>
#include <const.h>
#include <memutil.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_conf.h>
#include <osw_mux.h>
#include <osw_drv_mediator.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_ut.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_executor_action.h"
#include "ow_steer_executor_action_priv.h"
#include "ow_steer_executor_action_deauth.h"

#define OW_STEER_EXECUTOR_ACTION_DEAUTH_DELAY_SEC 10
#define DOT11_DEAUTH_REASON_CODE_UNSPECIFIED 1

struct ow_steer_executor_action_deauth {
    struct ow_steer_executor_action *base;
    struct osw_drv_frame_tx_desc *tx_desc;
    struct osw_timer delay_timer;
    float delay_seconds;
};

void
ow_steer_executor_action_deauth_set_delay_sec(struct ow_steer_executor_action_deauth *deauth_action,
                                              float seconds)
{
    if (deauth_action->delay_seconds == seconds) return;

    LOGD("%s deauth delay set from %f to %f seconds",
         ow_steer_executor_action_get_prefix(deauth_action->base),
         deauth_action->delay_seconds,
         seconds);

    deauth_action->delay_seconds = seconds;
}

static bool
ow_steer_executor_action_deauth_call_fn(struct ow_steer_executor_action *action,
                                        const struct ow_steer_candidate_list *candidate_list,
                                        struct osw_conf_mutator *mutator)
{
    struct ow_steer_executor_action_deauth *deauth_action = ow_steer_executor_action_get_priv(action);
    const bool need_kick = ow_steer_executor_action_check_kick_needed(action, candidate_list);
    const bool deauth_pending = osw_timer_is_armed(&deauth_action->delay_timer);
    if (need_kick == true) {
        if (deauth_pending == true) {
            const uint64_t delay_remaining_nsec = osw_timer_get_remaining_nsec(&deauth_action->delay_timer, osw_time_mono_clk());
            LOGD("%s deauth already scheduled, remaining delay: %.2lf sec", ow_steer_executor_action_get_prefix(deauth_action->base), OSW_TIME_TO_DBL(delay_remaining_nsec));
        }
        else {
            const uint64_t delay_nsec = OSW_TIME_SEC(deauth_action->delay_seconds);
            LOGI("%s scheduled deauth, delay: %.2lf sec", ow_steer_executor_action_get_prefix(deauth_action->base), OSW_TIME_TO_DBL(delay_nsec));
            osw_timer_arm_at_nsec(&deauth_action->delay_timer, osw_time_mono_clk() + delay_nsec);
        }
    }
    else {
        if (deauth_pending == true)
            LOGI("%s canceled scheduled deauth", ow_steer_executor_action_get_prefix(deauth_action->base));

        osw_timer_disarm(&deauth_action->delay_timer);
    }
    return true;
}

static void
ow_steer_executor_action_deauth_tx_free(struct ow_steer_executor_action_deauth *deauth_action)
{
    osw_drv_frame_tx_desc_free(deauth_action->tx_desc);
    deauth_action->tx_desc = NULL;
}

static void
ow_steer_executor_action_deauth_tx_done_cb(struct osw_drv_frame_tx_desc *desc,
                                           enum osw_frame_tx_result result,
                                           void *priv)
{
    struct ow_steer_executor_action_deauth *deauth_action = priv;

    switch (result) {
        case OSW_FRAME_TX_RESULT_SUBMITTED:
            LOGI("%s submitted deauth", ow_steer_executor_action_get_prefix(deauth_action->base));
            break;
        case OSW_FRAME_TX_RESULT_FAILED:
            LOGI("%s failed to deauth", ow_steer_executor_action_get_prefix(deauth_action->base));
            break;
        case OSW_FRAME_TX_RESULT_DROPPED:
            LOGI("%s dropped deauth", ow_steer_executor_action_get_prefix(deauth_action->base));
            break;
    }

    ow_steer_executor_action_deauth_tx_free(deauth_action);
}

static struct osw_drv_frame_tx_desc *
ow_steer_executor_action_deauth_tx_spawn(struct ow_steer_executor_action_deauth *deauth_action,
                                         const struct osw_hwaddr *bssid,
                                         const struct osw_hwaddr *sta_addr,
                                         const uint16_t reason_code)
{
    struct osw_drv_frame_tx_desc *tx_desc = osw_drv_frame_tx_desc_new(ow_steer_executor_action_deauth_tx_done_cb, deauth_action);
    const struct osw_drv_dot11_frame frame = {
        .header = {
            .frame_control = htole16(DOT11_FRAME_CTRL_SUBTYPE_DEAUTH),
            .da = { OSW_HWADDR_ARG(sta_addr) },
            .sa = { OSW_HWADDR_ARG(bssid) },
            .bssid = { OSW_HWADDR_ARG(bssid) },
        },
        .u = {
            .deauth = {
                .reason_code = htole16(reason_code),
            },
        },
    };
    const void *frame_start = &frame;
    const void *frame_end = &frame.u.deauth.variable;
    const size_t frame_len = (frame_end - frame_start);

    osw_drv_frame_tx_desc_set_frame(tx_desc, frame_start, frame_len);
    return tx_desc;
}

static void
ow_steer_executor_action_deauth_tx_push(struct ow_steer_executor_action_deauth *deauth_action,
                                        const char *phy_name,
                                        const char *vif_name,
                                        const struct osw_hwaddr *bssid,
                                        const struct osw_hwaddr *sta_addr,
                                        const uint16_t reason_code)
{
    /* Try generic tx submission path */
    if (deauth_action->tx_desc != NULL) {
        LOGI("%s overrun deauth", ow_steer_executor_action_get_prefix(deauth_action->base));
        ow_steer_executor_action_deauth_tx_free(deauth_action);
    }

    deauth_action->tx_desc = ow_steer_executor_action_deauth_tx_spawn(deauth_action, bssid, sta_addr, reason_code);
    osw_mux_frame_tx_schedule(phy_name, vif_name, deauth_action->tx_desc);
}

static void
ow_steer_executor_action_deauth_delay_timer_cb(struct osw_timer *timer)
{
    struct ow_steer_executor_action_deauth *deauth_action = container_of(timer, struct ow_steer_executor_action_deauth, delay_timer);
    const struct osw_hwaddr *sta_addr = ow_steer_executor_action_get_sta_addr(deauth_action->base);
    const struct osw_state_sta_info *sta_info = osw_state_sta_lookup_newest(sta_addr);
    if (WARN_ON(sta_info == NULL))
        return;
    if (WARN_ON(sta_info->vif == NULL))
        return;
    if (WARN_ON(sta_info->vif->phy == NULL))
        return;

    const char *phy_name = sta_info->vif->phy->phy_name;
    const char *vif_name = sta_info->vif->vif_name;
    const struct osw_hwaddr *bssid = &sta_info->vif->drv_state->mac_addr;
    const uint16_t rc_unspec = 1;

    const bool deauth_success = osw_mux_request_sta_deauth(phy_name, vif_name, sta_addr, DOT11_DEAUTH_REASON_CODE_UNSPECIFIED);
    if (deauth_success == true) {
        LOGI("%s issued deauth", ow_steer_executor_action_get_prefix(deauth_action->base));
        return;
    }

    /* try using generic tx submission */
    ow_steer_executor_action_deauth_tx_push(deauth_action, phy_name, vif_name, bssid, sta_addr, rc_unspec);
}

struct ow_steer_executor_action_deauth*
ow_steer_executor_action_deauth_create(const struct osw_hwaddr *sta_addr,
                                       const struct ow_steer_executor_action_mediator *mediator,
                                       const char *log_prefix)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(mediator != NULL, "");

    const struct ow_steer_executor_action_ops ops = {
        .call_fn = ow_steer_executor_action_deauth_call_fn,
    };
    struct ow_steer_executor_action_deauth *deauth_action = CALLOC(1, sizeof(*deauth_action));
    osw_timer_init(&deauth_action->delay_timer, ow_steer_executor_action_deauth_delay_timer_cb);
    deauth_action->base = ow_steer_executor_action_create("deauth", sta_addr, &ops, mediator, log_prefix, deauth_action);
    ow_steer_executor_action_deauth_set_delay_sec(deauth_action, OW_STEER_EXECUTOR_ACTION_DEAUTH_DELAY_SEC);

    return deauth_action;
}

void
ow_steer_executor_action_deauth_free(struct ow_steer_executor_action_deauth *deauth_action)
{
    ASSERT(deauth_action != NULL, "");
    osw_timer_disarm(&deauth_action->delay_timer);
    ow_steer_executor_action_deauth_tx_free(deauth_action);
    ow_steer_executor_action_free(deauth_action->base);
    FREE(deauth_action);
}

struct ow_steer_executor_action*
ow_steer_executor_action_deauth_get_base(struct ow_steer_executor_action_deauth *deauth_action)
{
    ASSERT(deauth_action != NULL, "");
    return deauth_action->base;
}

OSW_UT(ow_steer_executor_action_deauth_tx_frame)
{
    struct ow_steer_executor_action_deauth ctx = {0};
    const struct osw_hwaddr one = { .octet = {1} };
    const struct osw_hwaddr two = { .octet = {2} };
    struct osw_drv_frame_tx_desc *tx_desc = ow_steer_executor_action_deauth_tx_spawn(&ctx, &one, &two, 3);
    const uint8_t expected[] = {
        0xC0, 0x00,
        0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x03, 0x00,
    };
    assert(osw_drv_frame_tx_desc_get_frame_len(tx_desc) ==  (2 + 2 + 6 + 6 + 6 + 2 + 2));
    assert(memcmp(osw_drv_frame_tx_desc_get_frame(tx_desc), expected, sizeof(expected)) == 0);
}
