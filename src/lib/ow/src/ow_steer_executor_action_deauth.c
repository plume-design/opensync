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
#include <osw_sta_assoc.h>
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
    struct osw_timer delay_timer;
    float delay_seconds;
    ds_tree_t tx;
    osw_sta_assoc_observer_t *assoc_obs;
};

struct ow_steer_executor_action_deauth_tx {
    struct ow_steer_executor_action_deauth *deauth_action;
    ds_tree_node_t node;
    struct osw_hwaddr sta_addr;
    struct osw_drv_frame_tx_desc *tx_desc;
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
ow_steer_executor_action_deauth_tx_drop(struct ow_steer_executor_action_deauth_tx *tx)
{
    if (tx == NULL) return;

    if (tx->deauth_action != NULL) {
        ds_tree_remove(&tx->deauth_action->tx, tx);
        tx->deauth_action = NULL;
    }

    osw_drv_frame_tx_desc_free_no_result(tx->tx_desc);
    FREE(tx);
}

static void
ow_steer_executor_action_deauth_tx_drop_all(struct ow_steer_executor_action_deauth *deauth_action)
{
    struct ow_steer_executor_action_deauth_tx *tx;
    while ((tx = ds_tree_head(&deauth_action->tx)) != NULL) {
        ow_steer_executor_action_deauth_tx_drop(tx);
    }
}

static void
ow_steer_executor_action_deauth_tx_done_cb(struct osw_drv_frame_tx_desc *desc,
                                           enum osw_frame_tx_result result,
                                           void *priv)
{
    struct ow_steer_executor_action_deauth_tx *tx = priv;
    struct ow_steer_executor_action_deauth *deauth_action = tx->deauth_action;

    if (deauth_action != NULL) {
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
    }

    ow_steer_executor_action_deauth_tx_drop(tx);
}

static struct osw_drv_frame_tx_desc *
ow_steer_executor_action_deauth_tx_desc_alloc(struct ow_steer_executor_action_deauth_tx *tx,
                                              const struct osw_hwaddr *bssid,
                                              const struct osw_hwaddr *sta_addr,
                                              const uint16_t reason_code)
{
    struct osw_drv_frame_tx_desc *tx_desc = osw_drv_frame_tx_desc_new(ow_steer_executor_action_deauth_tx_done_cb, tx);
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
    struct ow_steer_executor_action_deauth_tx *tx = ds_tree_find(&deauth_action->tx, sta_addr);
    if (tx != NULL) {
        LOGI("%s deauth already in flight for "OSW_HWADDR_FMT", retrying", ow_steer_executor_action_get_prefix(deauth_action->base), OSW_HWADDR_ARG(sta_addr));
        ow_steer_executor_action_deauth_tx_drop(tx);
    }

    tx = CALLOC(1, sizeof(*tx));
    tx->deauth_action = deauth_action;
    tx->sta_addr = *sta_addr;
    tx->tx_desc = ow_steer_executor_action_deauth_tx_desc_alloc(tx, bssid, sta_addr, reason_code);
    ds_tree_insert(&deauth_action->tx, tx, &tx->sta_addr);
    WARN_ON(osw_mux_frame_tx_schedule(phy_name, vif_name, tx->tx_desc) == false);
}

static void
ow_steer_executor_action_deauth_addr(struct ow_steer_executor_action_deauth *deauth_action,
                                     const char *phy_name,
                                     const char *vif_name,
                                     const struct osw_hwaddr *sta_addr,
                                     const struct osw_hwaddr *bssid,
                                     const uint16_t reason_code)
{
    LOGD("%s deauth "OSW_HWADDR_FMT" on "OSW_HWADDR_FMT" reason %u",
         ow_steer_executor_action_get_prefix(deauth_action->base),
         OSW_HWADDR_ARG(sta_addr),
         OSW_HWADDR_ARG(bssid),
         reason_code);

    const bool deauth_success = osw_mux_request_sta_deauth(phy_name, vif_name, sta_addr, reason_code);
    if (deauth_success == true) {
        LOGI("%s issued deauth", ow_steer_executor_action_get_prefix(deauth_action->base));
        return;
    }

    /* try using generic tx submission */
    ow_steer_executor_action_deauth_tx_push(deauth_action, phy_name, vif_name, bssid, sta_addr, reason_code);
}

static void
ow_steer_executor_action_deauth_link(struct ow_steer_executor_action_deauth *deauth_action,
                                     const osw_sta_assoc_link_t *link,
                                     const struct osw_hwaddr *addr_override,
                                     const uint16_t reason_code)
{
    if (WARN_ON(link == NULL)) return;
    const struct osw_hwaddr *bssid = &link->local_sta_addr;
    const struct osw_state_vif_info *vif = osw_state_vif_lookup_by_mac_addr(bssid);
    if (WARN_ON(vif == NULL)) return;
    if (WARN_ON(vif->phy == NULL)) return;

    const char *phy_name = vif->phy->phy_name;
    const char *vif_name = vif->vif_name;
    const struct osw_hwaddr *sta_addr = addr_override ?: &link->remote_sta_addr;
    ow_steer_executor_action_deauth_addr(deauth_action, phy_name, vif_name, sta_addr, bssid, reason_code);
}

static void
ow_steer_executor_action_deauth_delay_timer_cb(struct osw_timer *timer)
{
    struct ow_steer_executor_action_deauth *deauth_action = container_of(timer, struct ow_steer_executor_action_deauth, delay_timer);
    const osw_sta_assoc_entry_t *assoc_entry = osw_sta_assoc_observer_get_entry(deauth_action->assoc_obs);
    const osw_sta_assoc_links_t *active_links = osw_sta_assoc_entry_get_active_links(assoc_entry);
    if (WARN_ON(active_links == NULL)) return;
    if (WARN_ON(active_links->count == 0)) return;

    /* This tries to be as defensive as possible anticipating underlying
     * drivers that may behave slightly differently. Deauthing multiple
     * variants gives better chance of actually deauthenticating.
     */

    const uint16_t reason_code = DOT11_DEAUTH_REASON_CODE_UNSPECIFIED;
    size_t i;
    for (i = 0; i < active_links->count; i++) {
        const osw_sta_assoc_link_t *link = &active_links->links[i];
        ow_steer_executor_action_deauth_link(deauth_action, link, NULL, reason_code);
    }

    const bool is_mlo = osw_sta_assoc_entry_is_mlo(assoc_entry);
    if (is_mlo == true) {
        const struct osw_hwaddr *entry_addr = osw_sta_assoc_entry_get_addr(assoc_entry);
        const bool mld_addr_is_link_addr = (osw_sta_assoc_links_lookup(active_links, NULL, entry_addr) != NULL);
        const osw_sta_assoc_link_t *first_link = &active_links->links[0];
        /* No need to send another deauth for the MLD address if it matches one
         * of the link addresses. These were handled already.
         */
        if (mld_addr_is_link_addr == false) {
            ow_steer_executor_action_deauth_link(deauth_action, first_link, entry_addr, reason_code);
        }
    }
}

static osw_sta_assoc_observer_t *
ow_steer_executor_action_deauth_assoc_observer(const struct osw_hwaddr *sta_addr)
{
    osw_sta_assoc_t *m_sta_assoc = OSW_MODULE_LOAD(osw_sta_assoc);
    struct osw_sta_assoc_observer_params *params = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_addr(params, sta_addr);
    return osw_sta_assoc_observer_alloc(m_sta_assoc, params);
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
    deauth_action->assoc_obs = ow_steer_executor_action_deauth_assoc_observer(sta_addr);
    ow_steer_executor_action_deauth_set_delay_sec(deauth_action, OW_STEER_EXECUTOR_ACTION_DEAUTH_DELAY_SEC);
    ds_tree_init(&deauth_action->tx, (ds_key_cmp_t *)osw_hwaddr_cmp, struct ow_steer_executor_action_deauth_tx, node);

    return deauth_action;
}

void
ow_steer_executor_action_deauth_free(struct ow_steer_executor_action_deauth *deauth_action)
{
    ASSERT(deauth_action != NULL, "");
    osw_timer_disarm(&deauth_action->delay_timer);
    osw_sta_assoc_observer_drop(deauth_action->assoc_obs);
    ow_steer_executor_action_deauth_tx_drop_all(deauth_action);
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
    struct ow_steer_executor_action_deauth_tx *ctx = CALLOC(1, sizeof(*ctx));
    const struct osw_hwaddr one = { .octet = {1} };
    const struct osw_hwaddr two = { .octet = {2} };
    struct osw_drv_frame_tx_desc *tx_desc = ow_steer_executor_action_deauth_tx_desc_alloc(ctx, &one, &two, 3);
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
