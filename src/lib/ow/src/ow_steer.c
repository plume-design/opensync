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

#include <memutil.h>
#include <ds_dlist.h>
#include <ds_tree.h>
#include <util.h>
#include <const.h>
#include <module.h>
#include <log.h>
#include <osw_conf.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_bss_map.h>
#include <osw_timer.h>
#include <osw_stats.h>
#include <osw_stats_defs.h>
#include "ow_steer_bm_priv.h"
#include "ow_steer_sta.h"
#include "ow_steer_candidate_assessor.h"
#include "ow_steer_sta_priv.h"
#include "ow_steer_sta_i.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_stack.h"

#define OW_STEER_STATS_PERIOD_SEC 0.5

static void
ow_steer_mutator_cb(struct osw_conf_mutator *mutator,
                    struct ds_tree *phy_tree);

static struct ds_dlist g_sta_list = DS_DLIST_INIT(struct ow_steer_sta, node);
static struct ev_signal g_sigusr1;
static struct osw_stats_subscriber *g_stats_sub = NULL;
static struct osw_conf_mutator g_mutator = {
    .name = __FILE__,
    .mutate_fn = ow_steer_mutator_cb,
    .type = OSW_CONF_TAIL,
};

static void
ow_steer_sigusr1_cb(EV_P_ ev_signal *arg,
                    int events)
{
    ow_steer_bm_sigusr1_dump();
    ow_steer_sta_sigusr1_dump();
}

static void
ow_steer_stats_report_cb(enum osw_stats_id id,
                         const struct osw_tlv *data,
                         const struct osw_tlv *last,
                         void *priv)
{
    const struct osw_stats_defs *stats_defs = osw_stats_defs_lookup(OSW_STATS_STA);
    const struct osw_tlv_hdr *tb[OSW_STATS_STA_MAX__] = {0};
    const struct osw_state_vif_info *vif_info;
    const struct osw_hwaddr *bssid;
    const struct osw_hwaddr *sta_addr;
    const char *phy_name;
    const char *vif_name;

    if (id != OSW_STATS_STA)
        return;

    osw_tlv_parse(data->data, data->used, stats_defs->tpolicy, tb, OSW_STATS_STA_MAX__);

    if (tb[OSW_STATS_STA_PHY_NAME] == NULL ||
        tb[OSW_STATS_STA_VIF_NAME] == NULL ||
        tb[OSW_STATS_STA_MAC_ADDRESS] == NULL)
        return;
    if (tb[OSW_STATS_STA_SNR_DB] == NULL &&
        (tb[OSW_STATS_STA_TX_BYTES] == NULL && tb[OSW_STATS_STA_RX_BYTES] == NULL))
        return;

    sta_addr = osw_tlv_get_data(tb[OSW_STATS_STA_MAC_ADDRESS]);
    phy_name = osw_tlv_get_string(tb[OSW_STATS_STA_PHY_NAME]);
    vif_name = osw_tlv_get_string(tb[OSW_STATS_STA_VIF_NAME]);

    vif_info = osw_state_vif_lookup(phy_name, vif_name);
    if (vif_info == NULL)
        return;
    if (vif_info->drv_state == NULL)
        return;

    bssid = &vif_info->drv_state->mac_addr;

    if (tb[OSW_STATS_STA_SNR_DB] != NULL) {
        struct ow_steer_sta *sta;
        const uint32_t snr_db = osw_tlv_get_u32(tb[OSW_STATS_STA_SNR_DB]);

        ds_dlist_foreach(&g_sta_list, sta) {
            if (osw_hwaddr_cmp(ow_steer_sta_get_addr(sta), sta_addr) != 0)
                continue;

            struct ow_steer_policy_stack *policy_stack = ow_steer_sta_get_policy_stack(sta);
            ow_steer_policy_stack_sta_snr_change(policy_stack, sta_addr, bssid, snr_db);
        }
    }

    if (tb[OSW_STATS_STA_TX_BYTES] != NULL || tb[OSW_STATS_STA_RX_BYTES] != NULL) {
        struct ow_steer_sta *sta;
        uint64_t data_vol = 0;

        data_vol += tb[OSW_STATS_STA_TX_BYTES] != NULL ? osw_tlv_get_u32(tb[OSW_STATS_STA_TX_BYTES]) : 0;
        data_vol += tb[OSW_STATS_STA_RX_BYTES] != NULL ? osw_tlv_get_u32(tb[OSW_STATS_STA_RX_BYTES]) : 0;

        ds_dlist_foreach(&g_sta_list, sta) {
            if (osw_hwaddr_cmp(ow_steer_sta_get_addr(sta), sta_addr) != 0)
                continue;

            struct ow_steer_policy_stack *policy_stack = ow_steer_sta_get_policy_stack(sta);
            ow_steer_policy_stack_sta_data_vol_change(policy_stack, sta_addr, bssid, data_vol);
        }
    }
}

static void
ow_steer_mutator_cb(struct osw_conf_mutator *mutator,
                    struct ds_tree *phy_tree)
{
    assert(mutator != NULL);
    assert(phy_tree != NULL);

    struct ow_steer_sta *sta;
    ds_dlist_foreach(&g_sta_list, sta)
        ow_steer_sta_conf_mutate(sta, phy_tree);
}

static void
ow_steer_init(void)
{
    ev_signal_init(&g_sigusr1, ow_steer_sigusr1_cb, SIGUSR1);
    ev_signal_start(EV_DEFAULT_ &g_sigusr1);
    ev_unref(EV_DEFAULT);

    assert(g_stats_sub == NULL);
    g_stats_sub = osw_stats_subscriber_alloc();
    osw_stats_subscriber_set_report_seconds(g_stats_sub, OW_STEER_STATS_PERIOD_SEC);
    osw_stats_subscriber_set_poll_seconds(g_stats_sub, OW_STEER_STATS_PERIOD_SEC);
    osw_stats_subscriber_set_report_fn(g_stats_sub, ow_steer_stats_report_cb, NULL);
    osw_stats_subscriber_set_sta(g_stats_sub, true);
    osw_stats_register_subscriber(g_stats_sub);

    osw_conf_register_mutator(&g_mutator);
}

struct ds_dlist*
ow_steer_get_sta_list(void)
{
    return &g_sta_list;
}

struct osw_conf_mutator*
ow_steer_get_mutator(void)
{
    return &g_mutator;
}

OSW_MODULE(ow_steer)
{
    OSW_MODULE_LOAD(osw_stats);
    OSW_MODULE_LOAD(osw_conf);
    ow_steer_init();
    return NULL;
}
