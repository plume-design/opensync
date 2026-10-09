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

#include <glob.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <limits.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>
#include <libgen.h>

#include <log.h>
#include <util.h>
#include <const.h>
#include <os_nif.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_drv.h>
#include <osw_drv_common.h>
#include <osw_drv_nl80211.h>
#include <osw_hostap.h>
#include <osw_util.h>
#include <osw_tlv.h>
#include <osw_etc.h>

#include <nl_cmd.h>
#include <nl_cmd_task.h>
#include <nl_conn.h>
#include <nl_ev.h>
#include <nl_80211.h>

#include <rq.h>
#include <hostap_ev_ctrl.h>
#include <hostap_sock.h>
#include <hostap_rq_task.h>

#include <osn_netif.h>

#include "nl80211_copy.h"
#include "osw_types.h"

/* FIXME: osw_drv: make get_*_ list async */

#define OSW_DRV_NL80211_STA_DELETE_EXPIRY_SEC 3
#define msec_to_tu(msec) (((msec) * 1000) / 1024)
#define ops_to_mod(ops) container_of(ops, struct osw_drv_nl80211, mod_ops)

struct osw_drv_nl80211 {
    struct osw_drv_nl80211_ops mod_ops;
    struct osw_hostap *hostap;
    struct osw_drv_ops drv_ops;
    struct osw_drv *drv;
    struct nl_conn *nl_conn;
    struct nl_conn_subscription *nl_conn_sub;
    struct nl_80211 *nl_80211;
    struct nl_ev *nl_ev;
    struct nl_80211_sub *nl_80211_sub;
    struct ds_tree phys_by_name;
    struct ds_tree vifs_by_name;
    struct ds_tree stas;
    struct ds_dlist hooks;
    struct rq q_request_config;
    unsigned int stats_mask;
    bool rsno_supported;
};

struct osw_drv_nl80211_phy_sub {
    struct ds_tree phys_by_index;
    unsigned int combined_antenna_mask;
};

struct osw_drv_nl80211_freq_range {
    int start_mhz;
    int end_mhz;
};

struct osw_drv_nl80211_phy {
    struct osw_drv_nl80211 *m;
    struct ds_tree_node node_by_name;
    struct ds_tree_node node_by_index;
    char *phy_name;
    int radio_index;
    unsigned int antenna_mask;
    struct osw_drv_nl80211_freq_range *freq_ranges;
    size_t n_freq_ranges;
    const struct nl_80211_phy *info;
    struct osw_drv_phy_state state;
    struct nl_cmd_task task_nl_set_antenna;
    struct rq q_req;
    struct nl_cmd_task task_nl_get_wiphy;
    struct nl_cmd_task task_nl_get_reg;
    enum osw_drv_nl80211_phy_group_impl_type phy_group_impl_type;
    enum osw_drv_nl80211_csa_impl_type csa_impl_type;
    enum osw_drv_nl80211_acl_impl_type acl_impl_type;
    enum osw_drv_nl80211_dump_survey_impl_type dump_survey_impl_type;
    enum osw_drv_nl80211_dump_sta_impl_type dump_sta_impl_type;
};

struct osw_drv_nl80211_vif {
    struct osw_drv_nl80211 *m;
    struct ds_tree_node node_by_name;
    char *vif_name;
    const struct nl_80211_vif *info;
    struct osw_timer push_frame_tx_timer;
    struct nl_cmd *push_frame_tx_cmd;
    struct nl_cmd_task task_nl_disconnect;
    struct nl_cmd_task task_nl_set_power;
    struct rq q_req;
    struct nl_cmd_task task_nl_get_intf;
    struct nl_cmd_task task_nl_dump_scan;
    struct nl_cmd *scan_cmd;
    struct osw_timer scan_timeout;
    struct rq q_stats;
    struct nl_cmd_task task_nl_dump_scan_stats;
    struct nl_cmd_task task_nl_dump_survey_stats;
    struct nl_cmd_task task_nl_dump_sta_stats;
    struct osw_drv_vif_state state;
    struct {
        bool connected;
        struct osw_hwaddr bssid;
    } state_bits;
    struct osw_hostap_bss *hostap_bss;
    osn_netif_t *netif;
    bool tx_power_auto;
};

struct osw_drv_nl80211_sta_id {
    struct osw_ifname phy_name;
    struct osw_ifname vif_name;
    struct osw_hwaddr sta_addr;
};

struct osw_drv_nl80211_sta {
    struct osw_drv_nl80211_sta_id id;
    struct ds_tree_node node;

    struct osw_drv_nl80211 *m;
    const struct nl_80211_sta *info;
    bool hostap;
    bool authorized_valid;
    bool authorized_set;
    struct osw_hostap_bss_sta hostap_info;
    struct osw_drv_sta_state state;
    struct osw_hwaddr mld_addr;
    struct rq q_req;
    struct nl_cmd_task task_nl_get_sta;
    struct osw_timer delete_expiry;
    struct rq q_delete;
    struct rq q_deauth;
};

struct osw_drv_nl80211_hook {
    struct osw_drv_nl80211 *owner;
    struct ds_dlist_node node;
    const struct osw_drv_nl80211_hook_ops *ops;
    void *priv;
};

#define CALL_HOOK(hook, fn, ...) \
    do { \
        if (hook->ops->fn != NULL) { \
            hook->ops->fn(hook, ##__VA_ARGS__, hook->priv); \
        } \
    } while (0)

#define CALL_HOOKS(m, fn, ...) \
    do { \
        struct osw_drv_nl80211_hook *hook; \
        ds_dlist_foreach(&m->hooks, hook) { \
            CALL_HOOK(hook, fn, ##__VA_ARGS__); \
        } \
    } while (0)

#define CALL_RETURN_HOOK(hook, fn, res, ...) \
    do { \
        if (hook->ops->fn != NULL) { \
            if (hook->ops->fn(hook, ##__VA_ARGS__, hook->priv) == OSW_DRV_NL80211_HOOK_BREAK) res = true; \
        } \
    } while (0)

#define CALL_RETURN_HOOKS(m, fn, res, ...) \
    do { \
        struct osw_drv_nl80211_hook *hook; \
        ds_dlist_foreach(&m->hooks, hook) { \
            CALL_RETURN_HOOK(hook, fn, res, ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_PREFIX(fmt, ...) "osw: drv: nl80211: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_WIPHY(wiphy, fmt, ...) LOG_PREFIX("wiphy: %s: " fmt,  wiphy, ##__VA_ARGS__)
#define LOG_PREFIX_PHY(phy, fmt, ...) LOG_PREFIX("%s: " fmt,  phy, ##__VA_ARGS__)
#define LOG_PREFIX_VIF(phy, vif, fmt, ...) LOG_PREFIX_PHY(phy, "%s: " fmt, vif, ##__VA_ARGS__)
#define LOG_PREFIX_STA(phy, vif, sta, fmt, ...) LOG_PREFIX_VIF(phy, vif, OSW_HWADDR_FMT": " fmt, OSW_HWADDR_ARG(sta), ##__VA_ARGS__)

static bool
osw_drv_nl80211_phy_is_ignored(const char *phy_name)
{
    /* This is intended to allow masking, ie. not reporting
     * certain phy states, as if they didn't exist, and
     * therefore prevent these phys from being managed. This
     * is highly platform and integration-level specific.
     * Use with caution and care.
     */
    char env_name[64];
    snprintf(env_name, sizeof(env_name),
             "OSW_DRV_NL80211_IGNORE_PHY_%s",
             phy_name);
    return osw_etc_get(env_name) != NULL;
}

static bool
osw_drv_nl80211_vif_is_ignored(const char *vif_name)
{
    /* This is intended to allow masking, ie. not reporting
     * certain vif states, as if they didn't exist, and
     * therefore prevent these vifs from being managed. This
     * is highly platform and integration-level specific.
     * Use with caution and care.
     */
    char env_name[64];
    snprintf(env_name, sizeof(env_name),
             "OSW_DRV_NL80211_IGNORE_VIF_%s",
             vif_name);
    return osw_etc_get(env_name) != NULL;
}

/* This includes grouped static helpers. There are
 * intentionally no forward declarations.
 */
static struct osw_drv_nl80211_phy *
osw_drv_nl80211_phy_lookup(struct osw_drv *drv,
                           const char *phy_name)
{
    if (osw_drv_nl80211_phy_is_ignored(phy_name)) return NULL;
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    return ds_tree_find(&m->phys_by_name, phy_name);
}

static struct osw_drv_nl80211_phy *
osw_drv_nl80211_phy_from_vif(const struct osw_drv_nl80211_vif *vif)
{
    if (vif == NULL) return NULL;
    struct osw_drv_nl80211 *m = vif->m;
    const struct nl_80211_phy *nl_phy = nl_80211_phy_by_wiphy(m->nl_80211, vif->info->wiphy);
    if (nl_phy == NULL) return NULL;
    struct osw_drv_nl80211_phy_sub *nl_phy_sub = nl_80211_sub_phy_get_priv(m->nl_80211_sub, nl_phy);
    if (nl_phy_sub == NULL) return NULL;
    const int radio_mask = vif->info->radio_mask;
    const int num_bits = __builtin_popcount(radio_mask);
    const int has_no_bits = num_bits == 0;
    const int has_one_bit = num_bits == 1;
    const int radio_index = __builtin_ffs(radio_mask) - 1;
    if (has_one_bit) {
        return ds_tree_find(&nl_phy_sub->phys_by_index, &radio_index);
    }
    else if (has_no_bits && ds_tree_len(&nl_phy_sub->phys_by_index) == 1) {
         return ds_tree_head(&nl_phy_sub->phys_by_index);
    }
    else {
        WARN_ON(1);
        return NULL;
    }
}

static struct osw_drv_nl80211_vif *
osw_drv_nl80211_vif_lookup(struct osw_drv *drv,
                           const char *vif_name)
{
    if (osw_drv_nl80211_vif_is_ignored(vif_name)) return NULL;
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct nl_80211 *nl = m->nl_80211;
    struct nl_80211_sub *sub = m->nl_80211_sub;
    const struct nl_80211_vif *nlvif = nl_80211_vif_by_name(nl, vif_name);
    if (nlvif == NULL) return NULL;
    struct osw_drv_nl80211_vif *vif = nl_80211_sub_vif_get_priv(sub, nlvif);
    return vif;
}

static struct osw_drv_nl80211_sta *
osw_drv_nl80211_sta_lookup(struct osw_drv_nl80211 *m,
                           const char *phy_name,
                           const char *vif_name,
                           const struct osw_hwaddr *sta_addr)
{
    struct osw_drv_nl80211_sta_id id;

    MEMZERO(id);
    STRSCPY_WARN(id.phy_name.buf, phy_name);
    STRSCPY_WARN(id.vif_name.buf, vif_name);
    id.sta_addr = *sta_addr;

    struct ds_tree *tree = &m->stas;
    struct osw_drv_nl80211_sta *sta = ds_tree_find(tree, &id);

    return sta;
}

static void
osw_drv_nl80211_scan_complete(struct osw_drv_nl80211_vif *vif,
                              enum osw_drv_scan_complete_reason reason)
{
    if (vif->scan_cmd != NULL) {
        nl_cmd_free(vif->scan_cmd);
        vif->scan_cmd = NULL;
    }

    struct osw_timer *timer = &vif->scan_timeout;
    osw_timer_disarm(timer);

    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    const char *phy_name = phy ? phy->phy_name : NULL;
    const char *vif_name = vif ? vif->vif_name : NULL;
    WARN_ON(vif_name == NULL);
    WARN_ON(phy_name == NULL);
    struct rq *q = &vif->q_stats;
    struct rq_task *dump_scan = &vif->task_nl_dump_scan_stats.task;

    switch (reason) {
        case OSW_DRV_SCAN_DONE:
            LOGD(LOG_PREFIX_VIF(phy_name ?: "", vif_name ?: "", "scan done"));
            rq_task_kill(dump_scan);
            rq_add_task(q, dump_scan);
            break;
        case OSW_DRV_SCAN_FAILED:
            LOGI(LOG_PREFIX_VIF(phy_name ?: "", vif_name ?: "", "scan failed"));
            break;
        case OSW_DRV_SCAN_ABORTED:
            LOGI(LOG_PREFIX_VIF(phy_name ?: "", vif_name ?: "", "scan aborted"));
            break;
        case OSW_DRV_SCAN_TIMED_OUT:
            LOGI(LOG_PREFIX_VIF(phy_name ?: "", vif_name ?: "", "scan timed out"));
            break;
    }

    osw_drv_report_scan_completed(drv,
                                  phy_name ?: "",
                                  vif_name ?: "",
                                  reason);
}

#include "osw_drv_nl80211_nla.c.h"
#include "osw_drv_nl80211_rfkill.c.h"
#include "osw_drv_nl80211_event.c.h"

static void
osw_drv_nl80211_init_cb(struct osw_drv *drv)
{
    /* FIXME: This shouldn't be looking at g_nl.
     * Instead osw drv init need to be reworked.
     */
    LOGI("osw: drv: nl80211: initializing");
    struct osw_drv_nl80211_ops *ops = OSW_MODULE_LOAD(osw_drv_nl80211);
    struct osw_drv_nl80211 *m = ops_to_mod(ops);
    osw_drv_set_priv(drv, m);
    WARN_ON(m->drv != NULL);
    m->drv = drv;
    LOGI("osw: drv: nl80211: initialized");
}

static void
osw_drv_nl80211_get_phy_list_cb(struct osw_drv *drv,
                                osw_drv_report_phy_fn_t *report_phy_fn,
                                void *fn_priv)
{
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct osw_drv_nl80211_phy *phy;
    ds_tree_foreach(&m->phys_by_name, phy) {
        if (osw_drv_nl80211_phy_is_ignored(phy->phy_name)) continue;
        report_phy_fn(phy->phy_name, fn_priv);
    }
}

static void
osw_drv_nl80211_get_vif_list_cb(struct osw_drv *drv,
                                const char *phy_name,
                                osw_drv_report_vif_fn_t *report_vif_fn,
                                void *fn_priv)
{
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_lookup(drv, phy_name);
    if (phy == NULL) return;

    struct osw_drv_nl80211_vif *vif;
    ds_tree_foreach(&m->vifs_by_name, vif) {
        struct osw_drv_nl80211_phy *vif_phy = osw_drv_nl80211_phy_from_vif(vif);
        const bool wrong_phy = (vif_phy == NULL) || (vif_phy != phy);
        if (wrong_phy) continue;
        if (osw_drv_nl80211_vif_is_ignored(vif->vif_name)) continue;
        report_vif_fn(vif->vif_name, fn_priv);
    }

    CALL_HOOKS(m, get_vif_list_fn, phy_name, report_vif_fn, fn_priv);
}

static void
osw_drv_nl80211_get_sta_list_cb(struct osw_drv *drv,
                                const char *phy_name,
                                const char *vif_name,
                                osw_drv_report_sta_fn_t *report_sta_fn,
                                void *fn_priv)
{
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct ds_tree *stas = &m->stas;
    struct osw_drv_nl80211_sta *sta;

    ds_tree_foreach(stas, sta) {
        const bool wrong_phy = (strcmp(sta->id.phy_name.buf, phy_name) != 0);
        if (wrong_phy) continue;

        const bool wrong_vif = (strcmp(sta->id.vif_name.buf, vif_name) != 0);
        if (wrong_vif) continue;

        const struct osw_hwaddr *addr = &sta->id.sta_addr;
        report_sta_fn(addr, fn_priv);
    }
}

static void
osw_drv_nl80211_request_phy_state_cb(struct osw_drv *drv,
                                     const char *phy_name)
{
    struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_lookup(drv, phy_name);
    struct rq_task *get_wiphy = &phy->task_nl_get_wiphy.task;
    struct rq_task *get_reg = &phy->task_nl_get_reg.task;
    struct rq *q = &phy->q_req;

    if (phy == NULL) {
        LOGI(LOG_PREFIX_PHY(phy_name, "does not exist, reporting empty"));
        struct osw_drv_phy_state state = {0};
        osw_drv_report_phy_state(drv, phy_name, &state);
        return;
    }

    LOGD(LOG_PREFIX_PHY(phy_name, "requesting state"));

    FREE(phy->freq_ranges);
    phy->freq_ranges = NULL;
    phy->n_freq_ranges = 0;
    phy->antenna_mask = 0;

    rq_stop(q);
    rq_kill(q);
    rq_resume(q);
    rq_add_task(q, get_wiphy);
    rq_add_task(q, get_reg);
    osw_drv_phy_state_report_free(&phy->state);
}

static void
osw_drv_nl80211_request_vif_state_cb(struct osw_drv *drv,
                                     const char *phy_name,
                                     const char *vif_name)
{
    struct osw_drv_nl80211_vif *vif = osw_drv_nl80211_vif_lookup(drv, vif_name);
    struct rq *q = &vif->q_req;
    struct rq_task *get_intf = &vif->task_nl_get_intf.task;
    struct rq_task *dump_scan = &vif->task_nl_dump_scan.task;

    if (vif == NULL) {
        struct osw_drv_vif_state state = {0};
        struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
        CALL_HOOKS(m, get_vif_state_fn, phy_name, vif_name, &state);
        if (state.exists) {
            CALL_HOOKS(m, fix_vif_state_fn, phy_name, vif_name, &state);
        }
        else {
            LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "does not exist, reporting empty"));
        }
        osw_drv_report_vif_state(drv, phy_name, vif_name, &state);
        osw_drv_vif_state_report_free(&state);
        return;
    }

    LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "requesting state"));

    rq_stop(q);
    rq_kill(q);
    rq_resume(q);
    MEMZERO(vif->state_bits);
    rq_add_task(q, get_intf);
    rq_add_task(q, dump_scan);
    rq_add_task(q, osw_hostap_bss_prep_state_task(vif->hostap_bss));
    osw_drv_vif_state_report_free(&vif->state);
}

static void
osw_drv_nl80211_request_sta_state_cb(struct osw_drv *drv,
                                     const char *phy_name,
                                     const char *vif_name,
                                     const struct osw_hwaddr *sta_addr)
{

    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct osw_drv_nl80211_sta *sta = osw_drv_nl80211_sta_lookup(m, phy_name, vif_name, sta_addr);
    struct osw_drv_nl80211_vif *vif = osw_drv_nl80211_vif_lookup(drv, vif_name);
    struct rq *q = &sta->q_req;
    struct rq_task *get_sta = &sta->task_nl_get_sta.task;

    if (sta == NULL) {
        LOGI(LOG_PREFIX_STA(phy_name, vif_name, sta_addr, "does not exist, reporting empty"));
        struct osw_drv_sta_state state = {0};
        osw_drv_report_sta_state(drv, phy_name, vif_name, sta_addr, &state);
        nl_80211_poll_stas(m->nl_80211, vif->info);
        return;
    }

    sta->authorized_valid = false;

    rq_stop(q);
    rq_kill(q);
    rq_resume(q);
    if (sta->info != NULL) rq_add_task(q, get_sta);
    osw_drv_sta_state_report_free(&sta->state);
    if (q->empty == true && q->empty_fn != NULL) q->empty_fn(q, q->priv);
}

static unsigned int
osw_drv_nl80211_phy_calc_combined_chainmask(struct osw_drv_nl80211_phy *phy,
                                            struct osw_drv_conf *conf)
{
    const struct nl_80211_phy *info = phy->info;
    struct osw_drv_nl80211_phy_sub *sub = nl_80211_sub_phy_get_priv(phy->m->nl_80211_sub, info);
    unsigned int chainmask = 0;
    struct osw_drv_nl80211_phy *phy_i;
    ds_tree_foreach(&sub->phys_by_index, phy_i) {
        int i;
        const int n = conf->n_phy_list;
        for (i = 0; i < n; i++) {
            const struct osw_drv_phy_config *phy_i_conf = &conf->phy_list[i];
            const bool matches_phy = strcmp(phy_i_conf->phy_name, phy_i->phy_name) == 0;
            if (matches_phy) {
                chainmask |= phy_i_conf->tx_chainmask;
                if (phy_i->antenna_mask != 0) {
                    const bool mask_out_of_range = (phy_i_conf->tx_chainmask & ~phy_i->antenna_mask) != 0;
                    if (mask_out_of_range) {
                        LOGI(LOG_PREFIX_PHY(phy_i->phy_name, "configured tx_chainmask 0x%x has bits outside of antenna mask 0x%x, fix your config",
                             phy_i_conf->tx_chainmask,
                             phy_i->antenna_mask));
                    }
                }
            }
        }
    }
    const bool mask_out_of_range = (chainmask & ~sub->combined_antenna_mask) != 0;
    if (mask_out_of_range) {
        LOGI(LOG_PREFIX_PHY(phy->phy_name, "combined tx_chainmask 0x%x has bits outside of combined antenna mask 0x%x, fix your config",
             chainmask,
             sub->combined_antenna_mask));
    }
    return chainmask;
}

static struct rq_task *
osw_drv_nl80211_phy_prep_set_antenna(struct osw_drv_nl80211_phy *phy,
                                     unsigned int tx_chainmask,
                                     unsigned int rx_chainmask)
{
    nl_cmd_task_fini(&phy->task_nl_set_antenna);

    const struct nl_80211_phy *info = phy->info;
    const uint32_t wiphy = info->wiphy;
    struct osw_drv_nl80211 *m = phy->m;
    struct nl_conn *conn = m->nl_conn;
    struct nl_80211 *nl = m->nl_80211;
    struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
    struct nl_msg *msg = nl_80211_alloc_set_phy_antenna(nl, wiphy, tx_chainmask, rx_chainmask);
    nl_cmd_task_init(&phy->task_nl_set_antenna, cmd, msg);

    const char *phy_name = phy->phy_name;
    nl_cmd_set_name(cmd, strfmta(LOG_PREFIX_PHY(phy_name, "set antenna: %x/%x", tx_chainmask, rx_chainmask)));

    return &phy->task_nl_set_antenna.task;
}

static void
osw_drv_nl80211_request_config_phy(struct osw_drv *drv,
                                   struct osw_drv_conf *conf,
                                   struct osw_drv_phy_config *phy)
{
    const char *phy_name = phy->phy_name;
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct rq *q = &m->q_request_config;
    struct osw_drv_nl80211_phy *m_phy = osw_drv_nl80211_phy_lookup(drv, phy_name);
    if (m_phy == NULL) return;

    if (phy->tx_chainmask_changed) {
        /* FIXME: When multiple single-wiphy radios have their antenna mask
         * changed the command will be issued more than once with the same
         * value. This is harmless but somewhat silly. This could be optimized
         * if the task is moved to the sub structure.
         */
        const unsigned int combined_chainmask = osw_drv_nl80211_phy_calc_combined_chainmask(m_phy, conf);
        struct rq_task *set_antenna = osw_drv_nl80211_phy_prep_set_antenna(m_phy,
                                                                           combined_chainmask,
                                                                           combined_chainmask);
        rq_add_task(q, set_antenna);
    }
}

static void
osw_drv_nl80211_request_config_vif_sta(struct osw_drv *drv,
                                       struct osw_drv_phy_config *phy,
                                       struct osw_drv_vif_config *vif)
{
    const char *vif_name = vif->vif_name;
    struct osw_drv_vif_config_sta *vsta = &vif->u.sta;
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct osw_drv_nl80211_vif *m_vif = osw_drv_nl80211_vif_lookup(drv, vif_name);
    if (m_vif == NULL) return;

    struct rq *q = &m->q_request_config;
    struct rq_task *disconnect = &m_vif->task_nl_disconnect.task;

    switch (vsta->operation) {
        case OSW_DRV_VIF_CONFIG_STA_NOP:
            break;
        case OSW_DRV_VIF_CONFIG_STA_CONNECT:
            break;
        case OSW_DRV_VIF_CONFIG_STA_RECONNECT:
            break;
        case OSW_DRV_VIF_CONFIG_STA_DISCONNECT:
            rq_add_task(q, disconnect);
            break;
    }
}

static void
osw_drv_nl80211_vif_tx_power_changed_cb(struct rq_task *task, void *priv)
{
    struct osw_drv_nl80211_vif *vif = priv;
    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (WARN_ON(phy == NULL)) return;

    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "vif: tx_power changed"));
    if (drv == NULL) return;

    osw_drv_report_vif_changed(drv, phy_name, vif_name);
}

static struct rq_task *
osw_drv_nl80211_vif_prep_set_power(struct osw_drv_nl80211_vif *vif,
                                   int dbm)
{
    if (vif == NULL) return NULL;

    nl_cmd_task_fini(&vif->task_nl_set_power);

    const int mbm = dbm * 100;
    const struct nl_80211_vif *info = vif->info;
    const uint32_t ifindex = info->ifindex;
    struct osw_drv_nl80211 *m = vif->m;
    struct nl_conn *conn = m->nl_conn;
    struct nl_80211 *nl = m->nl_80211;
    struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
    struct nl_msg *msg = (dbm == 0)
                       ? nl_80211_alloc_set_vif_power_auto(nl, ifindex)
                       : nl_80211_alloc_set_vif_power_fixed(nl, ifindex, mbm);
    nl_cmd_task_init(&vif->task_nl_set_power, cmd, msg);

    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    const char *phy_name = phy ? phy->phy_name : NULL;
    const char *vif_name = vif->vif_name;
    nl_cmd_set_name(cmd, strfmta(LOG_PREFIX_VIF(phy_name ?: "", vif_name, "set power: %ddBm", dbm)));

    vif->task_nl_set_power.task.completed_fn = osw_drv_nl80211_vif_tx_power_changed_cb;
    vif->task_nl_set_power.task.priv = vif;

    return &vif->task_nl_set_power.task;
}

static void
osw_drv_nl80211_request_config_base(struct osw_drv *drv,
                                    struct osw_drv_conf *conf)
{
    size_t i;
    for (i = 0; i < conf->n_phy_list; i++) {
        struct osw_drv_phy_config *phy = &conf->phy_list[i];
        osw_drv_nl80211_request_config_phy(drv, conf, phy);
        size_t j;
        for (j = 0; j < phy->vif_list.count; j++) {
            struct osw_drv_vif_config *vif = &phy->vif_list.list[j];
            const char *vif_name = vif->vif_name;
            switch (vif->vif_type) {
                case OSW_VIF_UNDEFINED:
                    break;
                case OSW_VIF_AP:
                    break;
                case OSW_VIF_AP_VLAN:
                    os_nif_up(vif->vif_name, vif->enabled);
                    break;
                case OSW_VIF_STA:
                    osw_drv_nl80211_request_config_vif_sta(drv, phy, vif);
                    break;
            }
            if (vif->enabled_changed) {
                switch (vif->vif_type) {
                    case OSW_VIF_UNDEFINED:
                        break;
                    case OSW_VIF_AP:
                        /* Put interface down in case if hostapd was not stopped cleanely */
                        if (!vif->enabled) os_nif_up(vif->vif_name, vif->enabled);
                        break;
                    case OSW_VIF_AP_VLAN:
                        break;
                    case OSW_VIF_STA:
                        break;
                }
            }
            if (vif->tx_power_changed) {
                struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
                struct rq *q = &m->q_request_config;
                struct osw_drv_nl80211_vif *m_vif = osw_drv_nl80211_vif_lookup(drv, vif_name);
                const bool use_auto = (vif->tx_power_percent == 100);
                const int power_dbm = use_auto ? 0 : vif->tx_power_dbm;
                struct rq_task *set_power = osw_drv_nl80211_vif_prep_set_power(m_vif,
                                                                               power_dbm);
                if (set_power != NULL) {
                    rq_add_task(q, set_power);
                }
                if (m_vif != NULL) {
                    m_vif->tx_power_auto = use_auto;
                }
            }
        }
    }
}

static void
osw_drv_nl80211_request_config_hostap_done_cb(struct rq_task *task,
                                              void *priv)
{
    LOGN(LOG_PREFIX("hostap: configuration task complete"));
}

static void
osw_drv_nl80211_request_config_cb(struct osw_drv *drv,
                                  struct osw_drv_conf *conf)
{
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct osw_hostap *hostap = m->hostap;
    struct rq *q = &m->q_request_config;

    rq_stop(q);
    rq_kill(q);
    rq_resume(q);

    CALL_HOOKS(m, pre_request_config_fn, conf);

    osw_drv_nl80211_request_config_base(drv, conf);

    if (hostap != NULL) {
        struct rq_task *config_task = osw_hostap_set_conf(hostap, conf);
        config_task->completed_fn = osw_drv_nl80211_request_config_hostap_done_cb;
        config_task->priv = m;
        rq_add_task(q, config_task);
        LOGN(LOG_PREFIX("hostap: scheduling configuration task"));
    }
    else {
        /* It's not really expected to get here, but
         * hypothetically osw_hostap module could be
         * unavailable and the intention is to only report
         * states and disallow configurations.
         */
        LOGD(LOG_PREFIX("hostap: not configuring because "
                        "it is unavailable"));
    }

    CALL_HOOKS(m, post_request_config_fn, conf, q);

    /* FIXME: This should live through to completion so that
     * post_request_config_fn hook can be implemented.
     */
    osw_drv_conf_free(conf);
}

static enum osw_drv_nl80211_phy_group_impl_type
osw_drv_nl80211_get_phy_group_impl_type_from_cstr(const char *override)
{
    if (strcmp(override, "OSW_DRV_NL80211_PHY_GROUP_IMPL_NONE") == 0)
        return OSW_DRV_NL80211_PHY_GROUP_IMPL_NONE;
    else if (strcmp(override, "OSW_DRV_NL80211_PHY_GROUP_IMPL_PHY") == 0)
        return OSW_DRV_NL80211_PHY_GROUP_IMPL_PHY;
    WARN_ON(1);
    return OSW_DRV_NL80211_PHY_GROUP_IMPL_NONE;
}

enum osw_drv_nl80211_phy_group_impl_type
osw_drv_nl80211_get_phy_group_impl_type(const char *phy_name) {
    enum osw_drv_nl80211_phy_group_impl_type impl_type;
    const char *override = osw_etc_get(strfmta("OSW_DRV_NL80211_PHY_GROUP_IMPL_PHY_%s", phy_name));
    if (override != NULL)
        impl_type = osw_drv_nl80211_get_phy_group_impl_type_from_cstr(override);
    else
        impl_type = OSW_DRV_NL80211_PHY_GROUP_IMPL_NONE;
    LOGI(LOG_PREFIX_PHY(phy_name, "get Phy Group impl type=%d", impl_type));
    return impl_type;
}

static enum osw_drv_nl80211_csa_impl_type
osw_drv_nl80211_get_csa_impl_type_from_cstr(const char *override)
{
    if (strcmp(override, "OSW_DRV_NL80211_CSA_IMPL_NONE") == 0)
        return OSW_DRV_NL80211_CSA_IMPL_NONE;
    else if (strcmp(override, "OSW_DRV_NL80211_CSA_IMPL_HOSTAP") == 0)
        return OSW_DRV_NL80211_CSA_IMPL_HOSTAP;
    WARN_ON(1);
    return OSW_DRV_NL80211_CSA_IMPL_NONE;
}

enum osw_drv_nl80211_csa_impl_type
osw_drv_nl80211_get_csa_impl_type(const char *phy_name) {
    enum osw_drv_nl80211_csa_impl_type impl_type;
    const char *override = osw_etc_get(strfmta("OSW_DRV_NL80211_CSA_IMPL_PHY_%s", phy_name));
    if (override != NULL)
        impl_type = osw_drv_nl80211_get_csa_impl_type_from_cstr(override);
    else
        impl_type = OSW_DRV_NL80211_CSA_IMPL_NONE;
    LOGI(LOG_PREFIX_PHY(phy_name, "get CSA impl type=%d", impl_type));
    return impl_type;
}

static enum osw_drv_nl80211_acl_impl_type
osw_drv_nl80211_get_acl_impl_type_from_cstr(const char *override)
{
    if (strcmp(override, "OSW_DRV_NL80211_ACL_IMPL_NONE") == 0)
        return OSW_DRV_NL80211_ACL_IMPL_NONE;
    else if (strcmp(override, "OSW_DRV_NL80211_ACL_IMPL_HOSTAP") == 0)
        return OSW_DRV_NL80211_ACL_IMPL_HOSTAP;
    WARN_ON(1);
    return OSW_DRV_NL80211_ACL_IMPL_NONE;
}

enum osw_drv_nl80211_acl_impl_type
osw_drv_nl80211_get_acl_impl_type(const char *phy_name) {
    enum osw_drv_nl80211_acl_impl_type impl_type;
    const char *override = osw_etc_get(strfmta("OSW_DRV_NL80211_ACL_IMPL_PHY_%s", phy_name));
    if (override != NULL)
        impl_type = osw_drv_nl80211_get_acl_impl_type_from_cstr(override);
    else
        impl_type = OSW_DRV_NL80211_ACL_IMPL_NONE;
    LOGI(LOG_PREFIX_PHY(phy_name, "get ACL impl type=%d", impl_type));
    return impl_type;
}

static enum osw_drv_nl80211_dump_survey_impl_type
osw_drv_nl80211_get_dump_survey_impl_type_from_cstr(const char *override)
{
    if (strcmp(override, "OSW_DRV_NL80211_DUMP_SURVEY_IMPL_NONE") == 0)
        return OSW_DRV_NL80211_DUMP_SURVEY_IMPL_NONE;
    else if (strcmp(override, "OSW_DRV_NL80211_DUMP_SURVEY_IMPL_DELTA") == 0)
        return OSW_DRV_NL80211_DUMP_SURVEY_IMPL_DELTA;
    else if (strcmp(override, "OSW_DRV_NL80211_DUMP_SURVEY_IMPL_ABSOLUTE") == 0)
        return OSW_DRV_NL80211_DUMP_SURVEY_IMPL_ABSOLUTE;
    else if (strcmp(override, "OSW_DRV_NL80211_DUMP_SURVEY_IMPL_ONCHAN_ABSOLUTE_OFFCHAN_DELTA") == 0)
        return OSW_DRV_NL80211_DUMP_SURVEY_IMPL_ONCHAN_ABSOLUTE_OFFCHAN_DELTA;
    WARN_ON(1);
    return OSW_DRV_NL80211_DUMP_SURVEY_IMPL_NONE;
}

static enum osw_drv_nl80211_dump_survey_impl_type
osw_drv_nl80211_get_dump_survey_impl_type(const char *phy_name) {
    enum osw_drv_nl80211_dump_survey_impl_type impl_type;
    const char *override = osw_etc_get(strfmta("OSW_DRV_NL80211_DUMP_SURVEY_IMPL_PHY_%s", phy_name));
    if (override != NULL)
        impl_type = osw_drv_nl80211_get_dump_survey_impl_type_from_cstr(override);
    else
        impl_type = OSW_DRV_NL80211_DUMP_SURVEY_IMPL_NONE;
    LOGI(LOG_PREFIX_PHY(phy_name, "get dump survey impl type=%d", impl_type));
    return impl_type;
}

static enum osw_drv_nl80211_dump_sta_impl_type
osw_drv_nl80211_get_dump_sta_impl_type_from_cstr(const char *override) {
    if (strcmp(override, "OSW_DRV_NL80211_DUMP_STA_IMPL_NONE") == 0)
        return OSW_DRV_NL80211_DUMP_STA_IMPL_NONE;
    else if (strcmp(override, "OSW_DRV_NL80211_DUMP_STA_IMPL_DEFAULT") == 0)
        return OSW_DRV_NL80211_DUMP_STA_IMPL_DEFAULT;
    WARN_ON(1);
    return OSW_DRV_NL80211_DUMP_STA_IMPL_NONE;
}

static enum osw_drv_nl80211_dump_sta_impl_type
osw_drv_nl80211_get_dump_sta_impl_type(const char *phy_name) {
    enum osw_drv_nl80211_dump_sta_impl_type impl_type;
    const char *override = osw_etc_get(strfmta("OSW_DRV_NL80211_DUMP_STA_IMPL_PHY_%s", phy_name));
    if (override != NULL)
        impl_type = osw_drv_nl80211_get_dump_sta_impl_type_from_cstr(override);
    else
        impl_type = OSW_DRV_NL80211_DUMP_STA_IMPL_NONE;
    LOGI(LOG_PREFIX_PHY(phy_name, "get dump sta impl type=%d", impl_type));
    return impl_type;
}

static void
osw_drv_nl80211_request_stats_bss_vif(struct osw_drv_nl80211_vif *vif,
                                      unsigned int stats_mask)
{
    if (vif == NULL) return;

    const char *vif_name = vif->vif_name;
    if (osw_drv_nl80211_vif_is_ignored(vif_name)) return;

    struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (phy == NULL) return;

    struct rq *q = &vif->q_stats;
    struct rq_task *dump_scan = &vif->task_nl_dump_scan_stats.task;

    if (stats_mask & (1 << OSW_STATS_BSS_SCAN)) {
        rq_task_kill(dump_scan);
        rq_add_task(q, dump_scan);
    }

    if (phy->dump_survey_impl_type != OSW_DRV_NL80211_DUMP_SURVEY_IMPL_NONE) {
        if (stats_mask & (1 << OSW_STATS_CHAN)) {
            struct rq_task *dump_survey = &vif->task_nl_dump_survey_stats.task;
            rq_task_kill(dump_survey);
            rq_add_task(q, dump_survey);
        }
    }

    if (phy->dump_sta_impl_type != OSW_DRV_NL80211_DUMP_STA_IMPL_NONE) {
        if (stats_mask & (1 << OSW_STATS_STA)) {
            struct rq_task *dump_sta = &vif->task_nl_dump_sta_stats.task;
            rq_task_kill(dump_sta);
            rq_add_task(q, dump_sta);
        }
    }
}

static void
osw_drv_nl80211_request_stats_cb(struct osw_drv *drv,
                                 unsigned int stats_mask)
{
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);

    CALL_HOOKS(m, pre_request_stats_fn, stats_mask);

    struct osw_drv_nl80211_vif *vif;
    ds_tree_foreach(&m->vifs_by_name, vif) {
        osw_drv_nl80211_request_stats_bss_vif(vif, stats_mask);
    }

    // needs detections/variants:
    //  - accumulating (ath9k)
    //  - overflowing (ath10k)
    //  - read-to-read (rt2800usb?)
}

static void
osw_drv_nl80211_scan_done_cb(struct nl_cmd *cmd,
                             void *priv)
{
    if (nl_cmd_is_failed(cmd)) {
        struct osw_drv_nl80211_vif *vif = priv;
        osw_drv_nl80211_scan_complete(vif, OSW_DRV_SCAN_FAILED);
        return;
    }
}

static struct nl_msg *
osw_drv_nl80211_scan_build_msg_roc(struct nl_80211 *nl,
                                   const struct osw_drv_nl80211_vif *vif,
                                   const struct osw_drv_scan_params *params)
{
    /* FIXME: nl80211 advertises support so it
     *        should be factored in.
     */
    const bool roc_supported = true;
    const bool use_roc = roc_supported
                      && params->passive
                      && params->n_channels == 1;
    if (use_roc == false) return NULL;

    const struct nl_80211_vif *vif_info = vif->info;
    const uint32_t ifindex = vif_info->ifindex;
    struct nl_msg *msg = nl_80211_alloc_roc(nl, ifindex);
    int err = 0;
    WARN_ON(msg == NULL);
    if (msg == NULL) return NULL;

    const struct osw_channel *c = &params->channels[0];
    const uint32_t freq = c->control_freq_mhz;
    const uint32_t duration = params->dwell_time_msec;

    err |= nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ, freq);
    err |= nla_put_u32(msg, NL80211_ATTR_DURATION, duration);

    const char *vif_name = vif->vif_name;
    LOGT(LOG_PREFIX_VIF("", vif_name, "using remain on channel for scan"));
    WARN_ON(err != 0);
    return msg;
}

static struct nl_msg *
osw_drv_nl80211_scan_build_msg(struct nl_80211 *nl,
                               const struct osw_drv_nl80211_vif *vif,
                               const struct osw_drv_scan_params *params)
{
    struct nl_msg *roc = osw_drv_nl80211_scan_build_msg_roc(nl, vif, params);
    if (roc != NULL) {
        return roc;
    }

    /* The following code is mostly dead if roc_supported is
     * true and single-freq passive scans are requested.
     * Keeping the code around because it will eventually be
     * necessary, eg. for custom IE scanning.
     */

    const struct nl_80211_vif *vif_info = vif->info;
    const uint32_t ifindex = vif_info->ifindex;
    struct nl_msg *msg = nl_80211_alloc_trigger_scan(nl, ifindex);
    int err = 0;
    WARN_ON(msg == NULL);
    if (msg == NULL) return NULL;

    if (params->n_channels > 0) {
        struct nl_msg *freqs = nlmsg_alloc();
        WARN_ON(freqs == NULL);

        if (freqs != NULL) {
            size_t i;
            for (i = 0; i < params->n_channels; i++) {
                const struct osw_channel *c = &params->channels[i];
                err |= nla_put_u32(freqs, i, c->control_freq_mhz);
            }
            err |= nla_put_nested(msg, NL80211_ATTR_SCAN_FREQUENCIES, freqs);
            nlmsg_free(freqs);
        }
    }

    /* FIXME This requires a recent-enough nl80211.h. */
    const uint16_t dur_tu = msec_to_tu(params->dwell_time_msec);
    if (dur_tu > 0) {
        err |= nla_put_u16(msg, NL80211_ATTR_MEASUREMENT_DURATION, dur_tu);
    }

    const uint32_t flags = 0
                         | NL80211_SCAN_FLAG_AP;
    err |= nla_put_u32(msg, NL80211_ATTR_SCAN_FLAGS, flags);

    if (params->passive) {
        /* nop */
    }
    else {
        struct nl_msg *ssids = nlmsg_alloc();

        if (ssids != NULL) {
            /* This current adds a wildcard SSID entry. If
             * `params` gets extended, the ssid list
             * generation would go here.
             */
            nla_put(ssids, 1, 0, "");
            err |= nla_put_nested(msg, NL80211_ATTR_SCAN_SSIDS, ssids);
            nlmsg_free(ssids);
        }
    }

    WARN_ON(err != 0);
    return msg;
}

static void
osw_drv_nl80211_scan_timeout_cb(struct osw_timer *timer)
{
    struct osw_drv_nl80211_vif *vif = container_of(timer,
                                                   struct osw_drv_nl80211_vif,
                                                   scan_timeout);
    osw_drv_nl80211_scan_complete(vif, OSW_DRV_SCAN_TIMED_OUT);
}

static void
osw_drv_nl80211_scan_setup(struct osw_drv_nl80211_vif *vif,
                           const struct osw_drv_scan_params *params)
{
    assert(params != NULL);
    assert(vif->scan_cmd == NULL);

    struct osw_timer *timer = &vif->scan_timeout;
    const uint64_t timeout_at = osw_time_mono_clk() + OSW_TIME_SEC(5);
    osw_timer_init(timer, osw_drv_nl80211_scan_timeout_cb);
    osw_timer_arm_at_nsec(timer, timeout_at);

    struct osw_drv_nl80211 *m = vif->m;
    struct nl_conn *conn = m->nl_conn;
    struct nl_80211 *nl = m->nl_80211;
    struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
    struct nl_msg *msg = osw_drv_nl80211_scan_build_msg(nl, vif, params);
    nl_cmd_completed_fn_t *done_cb = osw_drv_nl80211_scan_done_cb;
    nl_cmd_set_completed_fn(cmd, done_cb, vif);

    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    const char *phy_name = phy ? phy->phy_name : NULL;
    const char *vif_name = vif->vif_name;
    nl_cmd_set_name(cmd, strfmta(LOG_PREFIX_VIF(phy_name ?: "", vif_name, "scan")));

    vif->scan_cmd = cmd;
    nl_cmd_set_msg(cmd, msg);
}

static void
osw_drv_nl80211_request_scan_cb(struct osw_drv *drv,
                                const char *phy_name,
                                const char *vif_name,
                                const struct osw_drv_scan_params *params)
{
    if (WARN_ON(drv == NULL)) return;
    if (WARN_ON(phy_name == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    if (WARN_ON(params == NULL)) return;

    struct osw_drv_nl80211_vif *vif = osw_drv_nl80211_vif_lookup(drv, vif_name);
    if (vif == NULL) {
        osw_drv_report_scan_completed(drv, phy_name, vif_name, OSW_DRV_SCAN_FAILED);
        return;
    }

    osw_drv_nl80211_scan_setup(vif, params);
}

static void
osw_drv_nl80211_push_frame_tx_complete(struct osw_drv_nl80211_vif *vif,
                                       const bool timed_out)
{
    struct osw_drv_nl80211 *m = vif->m;
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    const char *phy_name = phy ? phy->phy_name : "";
    const char *vif_name = vif->vif_name;

    struct nl_cmd *cmd = vif->push_frame_tx_cmd;
    vif->push_frame_tx_cmd = NULL;

    WARN_ON(timed_out && cmd == NULL);
    if (cmd == NULL) return;

    const bool completed = nl_cmd_is_completed(cmd);
    const bool failed = nl_cmd_is_failed(cmd);

    WARN_ON(phy == NULL);
    LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "push_frame_tx: done"));

    struct osw_drv *drv = m->drv;

    if (completed) {
        WARN_ON(timed_out);
        osw_drv_report_frame_tx_state_submitted(drv);
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "push_frame_tx: submitted"));
    }
    else if (failed) {
        WARN_ON(timed_out);
        osw_drv_report_frame_tx_state_failed(drv);
        LOGN(LOG_PREFIX_VIF(phy_name, vif_name, "push_frame_tx: failed"));
    }
    else if (timed_out) {
        osw_drv_report_frame_tx_state_failed(drv);
        LOGN(LOG_PREFIX_VIF(phy_name, vif_name, "push_frame_tx: timed out"));
    }
    else {
        osw_drv_report_frame_tx_state_failed(drv);
        LOGN(LOG_PREFIX_VIF(phy_name, vif_name, "push_frame_tx: aborted"));
    }

    struct osw_timer *timer = &vif->push_frame_tx_timer;
    osw_timer_disarm(timer);

    nl_cmd_free(cmd);
}

static void
osw_drv_nl80211_push_frame_tx_done_cb(struct nl_cmd *cmd,
                                      void *priv)
{
    struct osw_drv_nl80211_vif *vif = priv;
    if (vif->push_frame_tx_cmd == NULL) return;
    if (WARN_ON(cmd != vif->push_frame_tx_cmd)) return;
    osw_drv_nl80211_push_frame_tx_complete(vif, false);
}

static void
osw_drv_nl80211_push_frame_tx_timer_cb(struct osw_timer *timer)
{
    struct osw_drv_nl80211_vif *vif = container_of(timer,
                                                   struct osw_drv_nl80211_vif,
                                                   push_frame_tx_timer);
    osw_drv_nl80211_push_frame_tx_complete(vif, true);
}

static void
osw_drv_nl80211_push_frame_tx_abort(struct osw_drv_nl80211_vif *vif)
{
    osw_drv_nl80211_push_frame_tx_complete(vif, false);
}

static void
osw_drv_nl80211_push_frame_tx_cb(struct osw_drv *drv,
                                 const char *phy_name,
                                 const char *vif_name,
                                 struct osw_drv_frame_tx_desc *desc)
{
    const uint8_t *frame = osw_drv_frame_tx_desc_get_frame(desc);
    const size_t frame_len = osw_drv_frame_tx_desc_get_frame_len(desc);
    const size_t dot11_header_size = sizeof(struct osw_drv_dot11_frame_header);

    if (WARN_ON(phy_name == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    if (WARN_ON(desc == NULL)) return;
    if (WARN_ON(frame_len < dot11_header_size)) return;

    struct osw_drv_nl80211_vif *vif = osw_drv_nl80211_vif_lookup(drv, vif_name);

    LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "push_frame_tx: requesting"));

    osw_drv_nl80211_push_frame_tx_abort(vif);

    struct nl_msg *msg = nlmsg_alloc();
    if (WARN_ON(msg == NULL)) return;

    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct nl_80211 *nl = m->nl_80211;
    const struct nl_80211_vif *info = vif->info;
    const uint32_t ifindex = info->ifindex;

    nl_80211_put_cmd(nl, msg, 0, NL80211_CMD_FRAME);
    WARN_ON(nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) != 0);
    WARN_ON(nla_put(msg, NL80211_ATTR_FRAME, frame_len, frame) != 0);

    const bool has_channel = osw_drv_frame_tx_desc_has_channel(desc);
    if (has_channel == true) {
        const struct osw_channel *channel = osw_drv_frame_tx_desc_get_channel(desc);
        if (channel == NULL) {
            LOGT(LOG_PREFIX_VIF(phy_name, vif_name, "push_frame_tx: home-channel"));
        }
        else {
            LOGT(LOG_PREFIX_VIF(phy_name, vif_name, "push_frame_tx: off-channel: "OSW_CHANNEL_FMT,
                                OSW_CHANNEL_ARG(channel)));
            WARN_ON(nla_put_flag(msg, NL80211_ATTR_OFFCHANNEL_TX_OK) != 0);
            WARN_ON(nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ, channel->control_freq_mhz) != 0);
        }
    }

    nl_cmd_completed_fn_t *done_cb = osw_drv_nl80211_push_frame_tx_done_cb;
    struct nl_cmd *cmd = nl_conn_alloc_cmd(m->nl_conn);
    nl_cmd_set_completed_fn(cmd, done_cb, vif);
    nl_cmd_set_msg(cmd, msg);
    WARN_ON(vif->push_frame_tx_cmd != NULL);
    vif->push_frame_tx_cmd = cmd;

    nl_cmd_set_name(cmd, strfmta(LOG_PREFIX_VIF(phy_name, vif_name, "frame")));

    struct osw_timer *timer = &vif->push_frame_tx_timer;
    const uint64_t timeout_at = osw_time_mono_clk() + OSW_TIME_SEC(1);
    osw_timer_arm_at_nsec(timer, timeout_at);
}

static void
osw_drv_nl80211_request_sta_delete_cb(struct osw_drv *drv,
                                      const char *phy_name,
                                      const char *vif_name,
                                      const struct osw_hwaddr *sta_addr)
{
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct osw_drv_nl80211_vif *vif = osw_drv_nl80211_vif_lookup(drv, vif_name);
    struct osw_drv_nl80211_sta *sta = osw_drv_nl80211_sta_lookup(m, phy_name, vif_name, sta_addr);
    if (vif == NULL) return;
    if (sta == NULL) return;

    struct osw_timer *expiry = &sta->delete_expiry;
    const uint64_t now = osw_time_mono_clk();
    const uint64_t at = now + OSW_TIME_SEC(OSW_DRV_NL80211_STA_DELETE_EXPIRY_SEC);
    osw_timer_arm_at_nsec(expiry, at);

    struct osw_hostap_bss *bss = vif->hostap_bss;
    struct rq *q = &sta->q_delete;
    struct rq_task *t = osw_hostap_bss_prep_sta_deauth_no_tx_task(bss, sta_addr);

    rq_stop(q);
    rq_kill(q);
    rq_resume(q);

    bool hook_result = false;
    CALL_RETURN_HOOKS(m, delete_sta_fn, hook_result, phy_name, vif_name, sta_addr);

    if (!hook_result) {
        if (t != NULL) rq_add_task(q, t);
    }
}

static void
osw_drv_nl80211_request_sta_deauth_cb(struct osw_drv *drv,
                                      const char *phy_name,
                                      const char *vif_name,
                                      const struct osw_hwaddr *sta_addr,
                                      int dot11_reason_code)
{
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct osw_drv_nl80211_vif *vif = osw_drv_nl80211_vif_lookup(drv, vif_name);
    struct osw_drv_nl80211_sta *sta = osw_drv_nl80211_sta_lookup(m, phy_name, vif_name, sta_addr);
    if (vif == NULL) return;
    if (sta == NULL) return;

    osw_timer_disarm(&sta->delete_expiry);

    struct osw_hostap_bss *bss = vif->hostap_bss;
    struct rq *q = &sta->q_deauth;
    struct rq_task *t = osw_hostap_bss_prep_sta_deauth_task(bss, sta_addr, dot11_reason_code);

    rq_stop(q);
    rq_kill(q);
    rq_resume(q);

    if (t != NULL) rq_add_task(q, t);
}

static void
osw_drv_nl80211_sta_state_get_sta_resp_cb(struct nl_cmd *cmd,
                                          struct nl_msg *msg,
                                          void *priv)
{
    struct osw_drv_nl80211_sta *sta = priv;
    struct osw_drv_sta_state *state = &sta->state;
    const struct nl_80211_sta *info = sta->info;
    const uint32_t ifindex = info->ifindex;
    const void *mac = info->addr.addr;

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, NULL);
    if (err) return;

    if (nla_ifindex_equal(tb, ifindex) == false) return;
    if (nla_mac_equal(tb, mac) == false) return;

    struct nlattr *nla = tb[NL80211_ATTR_STA_INFO];
    if (nla) {
        const struct nl80211_sta_flag_update *flags;
        struct nla_policy policy[NL80211_STA_INFO_MAX + 1] = {
            [NL80211_STA_INFO_CONNECTED_TIME] = { .type = NLA_U32 },
            [NL80211_STA_INFO_STA_FLAGS] = { .minlen = sizeof(*flags) },
        };
        struct nlattr *tb_info[NL80211_STA_INFO_MAX + 1];
        const int err = nla_parse_nested(tb_info, NL80211_STA_INFO_MAX, nla, policy);
        const bool ok = (err == 0);
        if (ok) {
            state->connected_duration_seconds = nla_get_u32_or(tb_info, NL80211_STA_INFO_CONNECTED_TIME, 0);
            if (tb_info[NL80211_STA_INFO_STA_FLAGS]) {
                flags = nla_data(tb_info[NL80211_STA_INFO_STA_FLAGS]);
                const uint32_t bit = (1 << NL80211_STA_FLAG_AUTHORIZED);
                sta->authorized_valid = (flags->mask & bit);
                sta->authorized_set = (flags->set & bit);
            }
        }
    }

    state->connected = true;
}

/* For every STA on this driver that points at `mld_addr` as its MLD
* address, schedule a state re-report. Used by the AP-MLD's hostapd
* ctrl callbacks to fan out invalidations to the per-link sibling
* records that inherit hostap-derived fields (pmf/akm/cipher/key_id)
* from the MLD record's hostap_info during their state report.
*
* The MLD record's hostap_info must already reflect the latest event
* by the time this is called — sibling structs re-read it asynchronously
* when the scheduled state report runs.
*/
static void
osw_drv_nl80211_invalidate_sta_mld_links(struct osw_drv_nl80211 *m,
                                         const struct osw_hwaddr *mld_addr)
{
    struct osw_drv_nl80211_sta *iter;
    ds_tree_foreach(&m->stas, iter) {
        /* Invalidate when link matches with MLD and sta has no hostap data */
        if (iter->hostap == true) continue;
        const bool known_link = osw_hwaddr_is_equal(&iter->mld_addr, mld_addr);
        if (known_link) {
            osw_drv_report_sta_changed(m->drv, iter->id.phy_name.buf, iter->id.vif_name.buf, &iter->id.sta_addr);
        }
    }
}

static void
osw_drv_nl80211_sta_set_mld_addr(struct osw_drv_nl80211_sta *sta,
                                 const struct osw_hwaddr *mld_addr)
{
    const bool changed = (osw_hwaddr_is_equal(&sta->mld_addr, mld_addr) == false);
    if (!changed) return;

    struct osw_hwaddr old_mld_addr = sta->mld_addr;
    sta->mld_addr = *mld_addr;
    osw_drv_nl80211_invalidate_sta_mld_links(sta->m, &old_mld_addr);
    osw_drv_nl80211_invalidate_sta_mld_links(sta->m, &sta->mld_addr);
}

static void
osw_drv_nl80211_sta_state_report_cb(struct rq *q,
                                    void *priv)
{
    if (q->stopped) return;
    struct osw_drv_nl80211_sta *sta = priv;
    struct osw_drv_nl80211 *m = sta->m;
    struct osw_drv *drv = m->drv;
    struct osw_drv_sta_state *state = &sta->state;
    const char *phy_name = sta->id.phy_name.buf;
    const char *vif_name = sta->id.vif_name.buf;
    const struct osw_hwaddr *sta_addr = &sta->id.sta_addr;

    LOGD(LOG_PREFIX_STA(phy_name, vif_name, sta_addr, "reporting state"));

    if (sta->hostap) {
        state->key_id = sta->hostap_info.key_id;
        state->pmf = sta->hostap_info.pmf;
        state->akm = sta->hostap_info.akm;
        state->pairwise_cipher = sta->hostap_info.pairwise_cipher;
        /* FIXME:assoc time */
    }

    state->connected = sta->hostap_info.authorized
                    || (sta->authorized_valid && sta->authorized_set);

    if (osw_timer_is_armed(&sta->delete_expiry)) {
        state->connected = true;
    }

    CALL_HOOKS(m, fix_sta_state_fn, phy_name, vif_name, sta_addr, state);

    /* Inherit security fields if hostap events are delivered only on
     * AP-MLD's primary VAP
     */
    const bool is_mlo = (osw_hwaddr_is_zero(&state->mld_addr) == false);
    if (sta->hostap == false && is_mlo) {
        struct osw_drv_nl80211_sta *iter;
        ds_tree_foreach(&m->stas, iter) {
            if (iter == sta) continue;
            if (iter->hostap == false) continue;
            if (osw_hwaddr_is_equal(&iter->id.sta_addr, &state->mld_addr) == false) continue;
            state->pmf             = iter->hostap_info.pmf;
            state->akm             = iter->hostap_info.akm;
            state->pairwise_cipher = iter->hostap_info.pairwise_cipher;
            state->key_id          = iter->hostap_info.key_id;
            break;
        }
    }

    osw_drv_nl80211_sta_set_mld_addr(sta, &state->mld_addr);

    if (drv != NULL) osw_drv_report_sta_state(drv, phy_name, vif_name, sta_addr, state);
    osw_drv_sta_state_report_free(state);
}

static void
osw_drv_nl80211_sta_init_nl(struct osw_drv_nl80211_sta *sta,
                            const struct nl_80211_sta *info)
{
    sta->info = info;

    struct osw_drv_nl80211 *m = sta->m;
    const uint32_t ifindex = info->ifindex;
    const void *mac = info->addr.addr;
    struct nl_conn *conn = m->nl_conn;
    struct nl_80211 *nl = m->nl_80211;
    struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
    struct nl_msg *msg = nl_80211_alloc_get_sta(nl, ifindex, mac);
    nl_cmd_response_fn_t *resp_cb = osw_drv_nl80211_sta_state_get_sta_resp_cb;
    nl_cmd_set_response_fn(cmd, resp_cb, sta);
    nl_cmd_task_init(&sta->task_nl_get_sta, cmd, msg);

    const struct osw_hwaddr *sta_addr = mac;
    const struct nl_80211_vif *vif_info = nl_80211_vif_by_ifindex(nl, ifindex);
    const struct osw_drv_nl80211_vif *vif = nl_80211_sub_vif_get_priv(m->nl_80211_sub, vif_info);
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    const char *vif_name = vif ? vif->vif_name : NULL;
    const char *phy_name = phy ? phy->phy_name : "";
    nl_cmd_set_name(cmd, strfmta(LOG_PREFIX_STA(phy_name ?: "", vif_name ?: "", sta_addr, "get station")));
}

static void
osw_drv_nl80211_sta_fini_nl(struct osw_drv_nl80211_sta *sta)
{
    sta->info = NULL;
    nl_cmd_task_fini(&sta->task_nl_get_sta);
}

static void
osw_drv_nl80211_sta_init_hostap(struct osw_drv_nl80211_sta *sta,
                                const struct osw_hostap_bss_sta *info)
{
    sta->hostap = true;
    sta->hostap_info = *info;

    /* assoc_ies is a pointer, we don't use it
     * directly in this driver, making it to NULL + 0
     */
    sta->hostap_info.assoc_ies = NULL;
    sta->hostap_info.assoc_ies_len = 0;
}

static void
osw_drv_nl80211_sta_fini_hostap(struct osw_drv_nl80211_sta *sta)
{
    sta->hostap = false;
}

static void
osw_drv_nl80211_sta_notify_changed(struct osw_drv_nl80211_sta *sta)
{
    struct osw_drv_nl80211 *m = sta->m;
    struct osw_drv *drv = m->drv;
    if (drv == NULL) return;

    const char *phy_name = sta->id.phy_name.buf;
    const char *vif_name = sta->id.vif_name.buf;
    const struct osw_hwaddr *sta_addr = &sta->id.sta_addr;

    osw_drv_report_sta_changed(drv, phy_name, vif_name, sta_addr);
}

static void
osw_drv_nl80211_sta_delete_cb(struct rq *q,
                              void *priv)
{
    if (q->stopped) return;
    struct osw_drv_nl80211_sta *sta = priv;
    const char *phy_name = sta->id.phy_name.buf;
    const char *vif_name = sta->id.vif_name.buf;
    const struct osw_hwaddr *sta_addr = &sta->id.sta_addr;

    LOGD(LOG_PREFIX_STA(phy_name, vif_name, sta_addr, "deleted"));
}

static void
osw_drv_nl80211_sta_deauth_cb(struct rq *q,
                              void *priv)
{
    if (q->stopped) return;
    struct osw_drv_nl80211_sta *sta = priv;
    const char *phy_name = sta->id.phy_name.buf;
    const char *vif_name = sta->id.vif_name.buf;
    const struct osw_hwaddr *sta_addr = &sta->id.sta_addr;

    LOGD(LOG_PREFIX_STA(phy_name, vif_name, sta_addr, "deauthed"));
}

static void
osw_drv_nl80211_sta_delete_expiry_cb(struct osw_timer *t)
{
    struct osw_drv_nl80211_sta *sta = container_of(t, struct osw_drv_nl80211_sta, delete_expiry);
    struct osw_drv_nl80211 *m = sta->m;
    struct osw_drv *drv = m->drv;
    const char *phy_name = sta->id.phy_name.buf;
    const char *vif_name = sta->id.vif_name.buf;
    const struct osw_hwaddr *sta_addr = &sta->id.sta_addr;

    LOGN(LOG_PREFIX_STA(phy_name, vif_name, sta_addr, "timed out trying to delete, possible bug"));
    osw_drv_report_sta_changed(drv, phy_name, vif_name, sta_addr);
}

static void
osw_drv_nl80211_sta_init(struct osw_drv_nl80211_sta *sta,
                         struct osw_drv_nl80211 *m)
{
    sta->m = m;

    rq_init(&sta->q_req, EV_DEFAULT);
    sta->q_req.max_running = 1;
    sta->q_req.empty_fn = osw_drv_nl80211_sta_state_report_cb;
    sta->q_req.priv = sta;

    rq_init(&sta->q_delete, EV_DEFAULT);
    sta->q_delete.max_running = 1;
    sta->q_delete.empty_fn = osw_drv_nl80211_sta_delete_cb;
    sta->q_delete.priv = sta;

    rq_init(&sta->q_deauth, EV_DEFAULT);
    sta->q_deauth.max_running = 1;
    sta->q_deauth.empty_fn = osw_drv_nl80211_sta_deauth_cb;
    sta->q_deauth.priv = sta;

    osw_timer_init(&sta->delete_expiry, osw_drv_nl80211_sta_delete_expiry_cb);

    osw_drv_nl80211_sta_notify_changed(sta);
}

static void
osw_drv_nl80211_sta_fini(struct osw_drv_nl80211_sta *sta)
{
    const char *phy_name = sta->id.phy_name.buf;
    const char *vif_name = sta->id.vif_name.buf;
    const struct osw_hwaddr *sta_addr = &sta->id.sta_addr;
    struct osw_drv_nl80211 *m = sta->m;
    struct osw_drv *drv = m->drv;

    osw_drv_nl80211_sta_notify_changed(sta);

    osw_timer_disarm(&sta->delete_expiry);

    rq_stop(&sta->q_req);
    rq_kill(&sta->q_req);
    rq_fini(&sta->q_req);

    rq_stop(&sta->q_delete);
    rq_kill(&sta->q_delete);
    rq_fini(&sta->q_delete);

    rq_stop(&sta->q_deauth);
    rq_kill(&sta->q_deauth);
    rq_fini(&sta->q_deauth);

    MEMZERO(sta->state);
    osw_drv_report_sta_state(drv, phy_name, vif_name, sta_addr, &sta->state);
}

static struct osw_drv_nl80211_sta *
osw_drv_nl80211_sta_alloc(struct osw_drv_nl80211 *m,
                          const char *phy_name,
                          const char *vif_name,
                          const struct osw_hwaddr *sta_addr)
{
    struct osw_drv_nl80211_sta_id id;

    MEMZERO(id);
    STRSCPY_WARN(id.phy_name.buf, phy_name);
    STRSCPY_WARN(id.vif_name.buf, vif_name);
    id.sta_addr = *sta_addr;

    struct ds_tree *tree = &m->stas;
    struct osw_drv_nl80211_sta *sta = CALLOC(1, sizeof(*sta));
    sta->id = id;
    ds_tree_insert(tree, sta, &sta->id);

    return sta;
}

static void
osw_drv_nl80211_sta_free(struct osw_drv_nl80211_sta *sta)
{
    struct osw_drv_nl80211 *m = sta->m;
    struct ds_tree *tree = &m->stas;
    ds_tree_remove(tree, sta);
    FREE(sta);
}

static bool
osw_drv_nl80211_sta_can_free(struct osw_drv_nl80211_sta *sta)
{
    return (sta->info == NULL) && (sta->hostap == false);
}

static void
osw_drv_nl80211_sta_maybe_free(struct osw_drv_nl80211_sta *sta)
{
    if (osw_drv_nl80211_sta_can_free(sta) == false) return;

    const char *phy_name = sta->id.phy_name.buf;
    const char *vif_name = sta->id.vif_name.buf;
    const struct osw_hwaddr *sta_addr = &sta->id.sta_addr;

    LOGI(LOG_PREFIX_STA(phy_name, vif_name, sta_addr, "removed"));

    osw_drv_nl80211_sta_fini(sta);
    osw_drv_nl80211_sta_free(sta);
}

static struct osw_drv_nl80211_sta *
osw_drv_nl80211_sta_create(struct osw_drv_nl80211 *m,
                           const char *phy_name,
                           const char *vif_name,
                           const struct osw_hwaddr *sta_addr)
{
    struct osw_drv_nl80211_sta *sta = osw_drv_nl80211_sta_alloc(m, phy_name, vif_name, sta_addr);

    osw_drv_nl80211_sta_init(sta, m);
    LOGI(LOG_PREFIX_STA(phy_name, vif_name, sta_addr, "added"));
    return sta;
}

static bool
osw_drv_nl80211_guess_rsno_supported(void)
{
    /* There's no dedicated interface to interrogate the
     * wireless stack if RSNO is supported.
     *
     * The best that can be done is to check if hostapd
     * and wpa_supplicant binaries contain the keyword
     * that is strictly tied to RSNO. Both need to
     * support it for RSNO to be usable end-to-end.
     *
     * The driver itself still may need _some_ changes to
     * have this work though. That can't be checked
     * reliably. Checking for internal symbol names is
     * probably not a good idea.
     */
    const char *keyword_hostapd = "rsn_override_key_mgmt_2";
    const char *keyword_wpas = "rsn_overriding";

    const char *hostapd_binary_path = strexa("which", "hostapd");
    if (WARN_ON(hostapd_binary_path == NULL)) return false;
    const char *hostapd_found = strexa("grep", "-qF", keyword_hostapd, hostapd_binary_path);

    const char *wpas_binary_path = strexa("which", "wpa_supplicant");
    if (WARN_ON(wpas_binary_path == NULL)) return false;
    const char *wpas_found = strexa("grep", "-qF", keyword_wpas, wpas_binary_path);

    return (hostapd_found != NULL) && (wpas_found != NULL);
}

static bool
osw_drv_nl80211_within_freq_range(const struct osw_drv_nl80211_freq_range *fr, int freq_mhz)
{
    return (freq_mhz >= fr->start_mhz) && (freq_mhz <= fr->end_mhz);
}

static bool
osw_drv_nl80211_channel_in_range(const int control_freq_mhz,
                                 const struct osw_drv_nl80211_freq_range *fr)
{
    const int left_edge_mhz = control_freq_mhz - 10;
    const int right_edge_mhz = control_freq_mhz + 10;
    return osw_drv_nl80211_within_freq_range(fr, left_edge_mhz)
        || osw_drv_nl80211_within_freq_range(fr, right_edge_mhz);
}

static bool
osw_drv_nl80211_channel_in_phy(const int control_freq_mhz,
                               const struct osw_drv_nl80211_phy *phy)
{
    if (phy->n_freq_ranges == 0) {
        return true;
    }

    size_t i;
    for (i = 0; i < phy->n_freq_ranges; i++) {
        const struct osw_drv_nl80211_freq_range *fr = &phy->freq_ranges[i];
        if (osw_drv_nl80211_channel_in_range(control_freq_mhz, fr)) {
            return true;
        }
    }
    return false;
}

static void
osw_drv_nl80211_phy_remove_invalid_channel_states(struct osw_drv_nl80211_phy *phy)
{
    size_t i;
    for (i = 0; i < phy->state.n_channel_states; i++) {
        struct osw_channel_state *cs = &phy->state.channel_states[i];
        const struct osw_channel *c = &cs->channel;
        const int control_freq_mhz = c->control_freq_mhz;
        const bool valid = osw_drv_nl80211_channel_in_phy(control_freq_mhz, phy);
        const bool remove_it = !valid;
        if (remove_it) {
            const size_t remaining_entries = phy->state.n_channel_states - i - 1;
            memmove(cs, cs + 1, remaining_entries * sizeof(*cs));
            phy->state.n_channel_states--;
            i--;
        }
    }
}

static void
osw_drv_nl80211_phy_dump_freq_ranges(struct osw_drv_nl80211_phy *phy)
{
    size_t i;
    for (i = 0; i < phy->n_freq_ranges; i++) {
        const struct osw_drv_nl80211_freq_range *fr = &phy->freq_ranges[i];
        LOGT(LOG_PREFIX_PHY(phy->phy_name, "frequency range: %d MHz - %d MHz"), fr->start_mhz, fr->end_mhz);
    }
}

static void
osw_drv_nl80211_phy_state_report_cb(struct rq *q,
                                    void *priv)
{
    if (q->stopped) return;
    struct osw_drv_nl80211_phy *phy = priv;
    struct osw_drv_nl80211 *m = phy->m;
    struct osw_drv *drv = m->drv;
    struct osw_drv_phy_state *state = &phy->state;
    const char *phy_name = phy->phy_name;
    const char *wiphy_name = phy->info->name;

    state->enabled = rfkill_get_phy_enabled(wiphy_name);

    /* Some drivers don't fill up cfg80211
     * structures properly and in consequence end
     * up not advertising NL80211_ATTR_MAC over
     * NL80211_CMD_GET_WIPHY. However sysfs allows
     * reading the (incomplete) data.
     */
    if (osw_hwaddr_is_zero(&state->mac_addr)) {
        char addr_path[1024];
        snprintf(addr_path, sizeof(addr_path), "/sys/class/ieee80211/%s/addresses", phy_name);
        char *mac_str = file_get(addr_path);
        struct osw_hwaddr mac_addr = {0};
        if (mac_str != NULL) {
            const bool valid = osw_hwaddr_from_cstr(mac_str, &mac_addr);
            if (valid) {
                state->mac_addr = mac_addr;
            }
        }
        FREE(mac_str);
    }

    state->rsno_supported = m->rsno_supported;

    osw_drv_nl80211_phy_dump_freq_ranges(phy);
    osw_drv_nl80211_phy_remove_invalid_channel_states(phy);

    CALL_HOOKS(m, fix_phy_state_fn, phy_name, state);

    LOGD(LOG_PREFIX_PHY(phy_name, "reporting state"));
    if (drv != NULL) osw_drv_report_phy_state(drv, phy_name, state);
    osw_drv_phy_state_report_free(state);
}

static void
osw_drv_nl80211_phy_state_get_wiphy_resp_cb(struct nl_cmd *cmd,
                                            struct nl_msg *msg,
                                            void *priv)
{
    struct osw_drv_nl80211_phy *phy = priv;
    struct osw_drv_phy_state *state = &phy->state;
    const struct nl_80211_phy *info = phy->info;
    struct osw_drv_nl80211_phy_sub *sub = nl_80211_sub_phy_get_priv(phy->m->nl_80211_sub, info);
    const uint32_t wiphy = info->wiphy;

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    /* FIXME: Use policies for deref safety */
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, NULL);
    if (err) return;
    if (nla_wiphy_equal(tb, wiphy) == false) return;

    nla_fill_tx_chainmask(&state->tx_chainmask, &sub->combined_antenna_mask, tb);
    nla_fill_radar_detect(&state->radar, tb);
    state->exists = true;
    nla_mac_to_osw_hwaddr(tb[NL80211_ATTR_MAC], &state->mac_addr);
    nla_band_to_osw_chan_states(&state->channel_states,
                                &state->n_channel_states,
                                tb[NL80211_ATTR_WIPHY_BANDS]);
    nla_radios_get_info(tb[NL80211_ATTR_WIPHY_RADIOS],
                        phy->radio_index,
                        &phy->freq_ranges,
                        &phy->n_freq_ranges,
                        &phy->antenna_mask);

    if (phy->antenna_mask != 0) {
        state->tx_chainmask = phy->antenna_mask;
    }
}

static void
osw_drv_nl80211_phy_state_get_reg_resp_cb(struct nl_cmd *cmd,
                                            struct nl_msg *msg,
                                            void *priv)
{
    struct osw_drv_nl80211_phy *phy = priv;
    struct osw_drv_phy_state *state = &phy->state;
    const struct nl_80211_phy *info = phy->info;
    const uint32_t wiphy = info->wiphy;

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, NULL);
    if (err) return;

    nla_fill_reg_domain(state->reg_domain.ccode, tb, wiphy);
}

static char *
osw_drv_nl80211_phy_name_get(const struct nl_80211_phy *info, int radio_index)
{
    if (radio_index == 0) {
        return STRDUP(info->name);
    }

    char *wiphy_name_base = strdupa(info->name);
    char *first_digit = strpbrk(wiphy_name_base, "0123456789");
    const int wiphy_base_number = atoi(first_digit ?: "0");
    if (first_digit != NULL) {
        *first_digit = '\0';
    }

    const int phy_index = wiphy_base_number + radio_index;
    char *phy_name = strfmt("%s%d", wiphy_name_base, phy_index);
    return phy_name;
}

static struct osw_drv_nl80211_phy *
osw_drv_nl80211_alloc(struct osw_drv_nl80211 *m,
                      const struct nl_80211_phy *info,
                      const int radio_index)
{
    const char *wiphy_name = info->name;
    struct osw_drv_nl80211_phy_sub *sub = nl_80211_sub_phy_get_priv(m->nl_80211_sub, info);
    struct osw_drv_nl80211_phy *phy = CALLOC(1, sizeof(*phy));
    phy->phy_name = osw_drv_nl80211_phy_name_get(info, radio_index);
    phy->radio_index = radio_index;
    phy->info = info;
    phy->m = m;

    LOGI(LOG_PREFIX_WIPHY(wiphy_name, "derived %s for radio index %d"), phy->phy_name, radio_index);

    /* This can really only happen if there's o single-wiphy multi-radio
     * systems where multiple wiphys exist leading to indexing clash.
     *
     * The solution to that is at the integrator level: make sure the wiphy
     * names are apart from each other based on their radio count.
     *
     * For example, if phy0 has 2 radios, and phy1 has 3 radios, then:
     *
     *  phy0 stays as phy0
     *    radio_index=0 -> phy0
     *    radio_index=1 -> phy1
     *
     *  phy1 should be renamed to phy2 by the integrator
     *    radio_index=0 -> phy2
     *    radio_index=1 -> phy3
     *    radio_index=2 -> phy4
     */
    const bool phy_name_already_exists = ds_tree_find(&m->phys_by_name, phy->phy_name) != NULL;
    assert(phy_name_already_exists == false);

    const char *phy_name = phy->phy_name;
    phy->phy_group_impl_type = osw_drv_nl80211_get_phy_group_impl_type(phy_name);
    phy->csa_impl_type = osw_drv_nl80211_get_csa_impl_type(phy_name);
    phy->acl_impl_type = osw_drv_nl80211_get_acl_impl_type(phy_name);
    phy->dump_survey_impl_type = osw_drv_nl80211_get_dump_survey_impl_type(phy_name);
    phy->dump_sta_impl_type = osw_drv_nl80211_get_dump_sta_impl_type(phy_name);

    rq_init(&phy->q_req, EV_DEFAULT);
    phy->q_req.max_running = 1;
    phy->q_req.empty_fn = osw_drv_nl80211_phy_state_report_cb;
    phy->q_req.priv = phy;

    {
        const uint32_t wiphy = info->wiphy;
        struct nl_conn *conn = m->nl_conn;
        struct nl_80211 *nl = m->nl_80211;
        struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
        struct nl_msg *msg = nl_80211_alloc_get_phy(nl, wiphy);
        nl_cmd_response_fn_t *resp_cb = osw_drv_nl80211_phy_state_get_wiphy_resp_cb;
        nl_cmd_task_init(&phy->task_nl_get_wiphy, cmd, msg);
        nl_cmd_set_response_fn(cmd, resp_cb, phy);
        nl_cmd_set_name(cmd, strfmta(LOG_PREFIX_PHY(phy_name, "get wiphy")));
    }

    {
        const uint32_t wiphy = info->wiphy;
        struct nl_conn *conn = m->nl_conn;
        struct nl_80211 *nl = m->nl_80211;
        struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
        struct nl_msg *msg = nl_80211_alloc_get_reg(nl, wiphy);
        nl_cmd_response_fn_t *resp_cb = osw_drv_nl80211_phy_state_get_reg_resp_cb;
        nl_cmd_task_init(&phy->task_nl_get_reg, cmd, msg);
        nl_cmd_set_response_fn(cmd, resp_cb, phy);
        nl_cmd_set_name(cmd, strfmta(LOG_PREFIX_PHY(phy_name, "get reg")));
    }

    LOGI(LOG_PREFIX_PHY(phy_name, "added"));
    ds_tree_insert(&m->phys_by_name, phy, phy->phy_name);
    ds_tree_insert(&sub->phys_by_index, phy, &phy->radio_index);
    return phy;
}

static void
osw_drv_nl80211_phy_added_cb(const struct nl_80211_phy *info,
                             void *priv)
{
    struct osw_drv_nl80211 *m = priv;

    struct osw_drv_nl80211_phy_sub *sub = nl_80211_sub_phy_get_priv(m->nl_80211_sub, info);
    ds_tree_init(&sub->phys_by_index, ds_int_cmp, struct osw_drv_nl80211_phy, node_by_index);
    const char *wiphy_name = info->name;

    LOGI(LOG_PREFIX_WIPHY(wiphy_name, "has %d radio(s)"), info->num_radios);

    /* Even if this is a regular non-single-wiphy we still need at least one
     * osw_drv_nl80211_phy instance to represent the phy itself, hence the
     * default of 1.
     */
    const int num_radios = info->num_radios ?: 1;
    int i;
    for (i = 0; i < num_radios; i++) {
        struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_alloc(m, info, i);
        if (osw_drv_nl80211_phy_is_ignored(phy->phy_name)) continue;
        osw_drv_report_phy_changed(m->drv, phy->phy_name);
    }
}

static void
osw_drv_nl80211_phy_drop(struct osw_drv_nl80211_phy *phy)
{
    if (phy == NULL) return;

    const char *phy_name = phy->phy_name;
    struct osw_drv_nl80211 *m = phy->m;
    struct osw_drv_nl80211_phy_sub *sub = nl_80211_sub_phy_get_priv(m->nl_80211_sub, phy->info);
    struct osw_drv *drv = m->drv;

    nl_cmd_task_fini(&phy->task_nl_set_antenna);

    rq_stop(&phy->q_req);
    rq_kill(&phy->q_req);
    rq_fini(&phy->q_req);
    nl_cmd_task_fini(&phy->task_nl_get_wiphy);
    nl_cmd_task_fini(&phy->task_nl_get_reg);

    if (!osw_drv_nl80211_phy_is_ignored(phy_name)) {
        osw_drv_phy_state_report_free(&phy->state);
        osw_drv_report_phy_state(drv, phy_name, &phy->state);
    }

    LOGI(LOG_PREFIX_PHY(phy_name, "removed"));

    ds_tree_remove(&m->phys_by_name, phy);
    ds_tree_remove(&sub->phys_by_index, phy);

    FREE(phy->freq_ranges);
    FREE(phy->phy_name);
    FREE(phy);
}

static void
osw_drv_nl80211_phy_removed_cb(const struct nl_80211_phy *info,
                               void *priv)
{
    struct osw_drv_nl80211 *m = priv;
    struct osw_drv_nl80211_phy_sub *sub = nl_80211_sub_phy_get_priv(m->nl_80211_sub, info);
    struct osw_drv_nl80211_phy *phy;

    while ((phy = ds_tree_head(&sub->phys_by_index)) != NULL) {
        osw_drv_nl80211_phy_drop(phy);
    }
}

static void
osw_drv_nl80211_phy_renamed_cb(const struct nl_80211_phy *info,
                               const char *old_name,
                               const char *new_name,
                               void *priv)
{
    LOGI(LOG_PREFIX_PHY(old_name, "renamed to %s", new_name));

    osw_drv_nl80211_phy_removed_cb(info, priv);
    osw_drv_nl80211_phy_added_cb(info, priv);
}

static void
osw_drv_nl80211_vif_state_get_intf_resp_cb(struct nl_cmd *cmd,
                                           struct nl_msg *msg,
                                           void *priv)
{
    struct osw_drv_nl80211_vif *vif = priv;
    struct osw_drv_vif_state *state = &vif->state;
    const struct nl_80211_vif *info = vif->info;
    const uint32_t ifindex = info->ifindex;

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    /* FIXME: Use policies for deref safety */
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, NULL);
    if (err) return;

    if (nla_ifindex_equal(tb, ifindex) == false) return;

    nla_mac_to_osw_hwaddr(tb[NL80211_ATTR_MAC], &state->mac_addr);

    const uint32_t mbm = nla_get_u32_or(tb, NL80211_ATTR_WIPHY_TX_POWER_LEVEL, 0);
    state->tx_power_dbm = mbm / 100;

    const enum nl80211_iftype iftype = nla_get_iftype(tb);
    state->vif_type = nla_iftype_to_osw_vif_type(iftype);
    switch (state->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            nla_vif_to_osw_vif_state_ap(tb, &state->u.ap);
            osw_vif_status_set(&state->status, ((tb[NL80211_ATTR_SSID] == NULL)
                                                ? OSW_VIF_DISABLED
                                                : OSW_VIF_ENABLED));
            break;
        case OSW_VIF_AP_VLAN:
            nla_vif_to_osw_vif_state_ap_vlan(tb, &state->u.ap_vlan);
            break;
        case OSW_VIF_STA:
            nla_vif_to_osw_vif_state_sta(tb, &state->u.sta);
            break;
    }
}

static void
osw_drv_nl80211_vif_state_dump_scan_resp_cb(struct nl_cmd *cmd,
                                            struct nl_msg *msg,
                                            void *priv)
{
    struct osw_drv_nl80211_vif *vif = priv;
    bool *connected = &vif->state_bits.connected;
    struct osw_hwaddr *bssid = &vif->state_bits.bssid;

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    /* FIXME: Use policies for deref safety */
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, NULL);
    if (err) return;

    struct nlattr *nla = nla_get_bssid(tb);
    *connected = (nla != NULL);
    nla_mac_to_osw_hwaddr(nla, bssid);
}

static void
osw_drv_nl80211_vif_state_dump_scan_stats_resp_cb(struct nl_cmd *cmd,
                                                  struct nl_msg *msg,
                                                  void *priv)
{
    struct osw_drv_nl80211_vif *vif = priv;
    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, NULL);
    if (err) return;

    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (phy == NULL) return;

    struct nlattr *bss = tb[NL80211_ATTR_BSS];
    if (bss == NULL) return;

    static struct nla_policy bss_policy[NL80211_BSS_MAX + 1] = {
        [NL80211_BSS_FREQUENCY] = { .type = NLA_U32 },
        [NL80211_BSS_SIGNAL_MBM] = { .type = NLA_U32 },
    };
    struct nlattr *tb_bss[NL80211_BSS_MAX + 1];
    const int bss_err = nla_parse_nested(tb_bss, NL80211_BSS_MAX, bss, bss_policy);
    if (bss_err) return;

    struct nlattr *nla_bssid = tb_bss[NL80211_BSS_BSSID];
    struct nlattr *nla_freq = tb_bss[NL80211_BSS_FREQUENCY];
    struct nlattr *nla_signal = tb_bss[NL80211_BSS_SIGNAL_MBM];
    struct nlattr *nla_ies = tb_bss[NL80211_BSS_INFORMATION_ELEMENTS];
    const char *phy_name = phy->phy_name;
    if (nla_bssid == NULL) {
        LOGW(LOG_PREFIX_PHY(phy_name, "stats: driver reported bss with empty bssid"));
        return;
    }

    const struct osw_hwaddr *bssid = nla_bssid ? nla_data(nla_bssid) : NULL;
    const uint32_t freq_mhz = nla_freq ? nla_get_u32(nla_freq) : 0;
    const int32_t mdbm = nla_signal ? nla_get_u32(nla_signal) : 0;
    const int32_t rssi_dbm = mdbm / 100;
    const int32_t noise_dbm = osw_channel_nf_20mhz_fixup(0);
    const uint32_t snr_db = (rssi_dbm >= noise_dbm ? (rssi_dbm - noise_dbm) : 0);

    /* Single-wiphy multi-radio systems will report _all_ bands' BSSes because
     * there's only a single wiphy (rdev) under the hood.
     *
     * Opensync was designed around phy=radio=band mapping so to keep things
     * backward compatible and work smoothly weed out scan results that don't
     * belong to the phy's radios.
     *
     * The frequency ranges are only present for single-wiphy multi-radio
     * systems, hence a direct check via osw_drv_nl80211_channel_in_phy().
     *
     * FIXME: The logic will now still run scan dumps per "radio", but all of
     * them will report the same BSS set. This could be optimized.
     */
    const bool other_radio = !osw_drv_nl80211_channel_in_phy(freq_mhz, phy);
    if (other_radio) {
        LOGT(LOG_PREFIX_PHY(phy_name, "stats: bss: skipping scan result on other radio %uMHz"), freq_mhz);
        return;
    }

    struct osw_tlv t;
    MEMZERO(t);

    const size_t off = osw_tlv_put_nested(&t, OSW_STATS_BSS_SCAN);
    osw_tlv_put_string(&t, OSW_STATS_BSS_SCAN_PHY_NAME, phy_name);
    osw_tlv_put_hwaddr(&t, OSW_STATS_BSS_SCAN_MAC_ADDRESS, bssid);
    osw_tlv_put_u32(&t, OSW_STATS_BSS_SCAN_FREQ_MHZ, freq_mhz);
    osw_tlv_put_u32(&t, OSW_STATS_BSS_SCAN_SNR_DB, snr_db);
    if (nla_ies != NULL) {
        osw_tlv_put_buf(&t, OSW_STATS_BSS_SCAN_IES, nla_data(nla_ies), nla_len(nla_ies));
    }
    osw_tlv_end_nested(&t, off);

    /* FIXME: This could be coalesced with other
     * reports to reduce ping-pong and improve
     * timings.
     */
    osw_drv_report_stats(drv, &t);
    osw_tlv_fini(&t);

    LOGT(LOG_PREFIX_PHY(phy_name, "stats: bss: "OSW_HWADDR_FMT" %uMHz %udB",
                        OSW_HWADDR_ARG(bssid),
                        freq_mhz,
                        snr_db));
}

static void
osw_drv_nl80211_vif_state_dump_survey_stats_resp_cb(struct nl_cmd *cmd,
                                                   struct nl_msg *msg,
                                                   void *priv)
{
    struct osw_drv_nl80211_vif *drv_nl80211_vif = priv;
    struct osw_drv_nl80211 *m = drv_nl80211_vif->m;
    struct osw_drv *drv = m->drv;

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, NULL);
    if (err) return;

    struct osw_drv_nl80211_phy *drv_nl80211_phy = osw_drv_nl80211_phy_from_vif(drv_nl80211_vif);
    if (drv_nl80211_phy == NULL) return;
    WARN_ON(drv_nl80211_phy->dump_survey_impl_type == OSW_DRV_NL80211_DUMP_SURVEY_IMPL_NONE);

    struct nlattr *survey = tb[NL80211_ATTR_SURVEY_INFO];
    if (survey == NULL) return;

    const char *phy_name = drv_nl80211_phy->phy_name;
    const char *vif_name = drv_nl80211_vif->vif_name;
    LOGT(LOG_PREFIX_VIF(phy_name, vif_name, "stats: survey"));

    static struct nla_policy survey_policy[NL80211_SURVEY_INFO_MAX + 1] = {
        [NL80211_SURVEY_INFO_FREQUENCY] = { .type = NLA_U32 },
        [NL80211_SURVEY_INFO_NOISE] = { .type = NLA_U8 },
        [NL80211_SURVEY_INFO_IN_USE] = { .type = NLA_FLAG },
        [NL80211_SURVEY_INFO_TIME] = { .type = NLA_U64 },
        [NL80211_SURVEY_INFO_TIME_TX] = { .type = NLA_U64 },
        [NL80211_SURVEY_INFO_TIME_RX] = { .type = NLA_U64 },
        [NL80211_SURVEY_INFO_TIME_BSS_RX] = { .type = NLA_U64 },
        [NL80211_SURVEY_INFO_TIME_BUSY] = { .type = NLA_U64 },
    };
    struct nlattr *tb_survey[NL80211_SURVEY_INFO_MAX + 1];
    const int survey_err = nla_parse_nested(tb_survey, NL80211_SURVEY_INFO_MAX, survey, survey_policy);
    if (WARN_ON(survey_err)) return;

    struct nlattr *nla_freq = tb_survey[NL80211_SURVEY_INFO_FREQUENCY];
    struct nlattr *nla_noise = tb_survey[NL80211_SURVEY_INFO_NOISE];
    struct nlattr *nla_in_use = tb_survey[NL80211_SURVEY_INFO_IN_USE];
    struct nlattr *nla_time = tb_survey[NL80211_SURVEY_INFO_TIME];
    struct nlattr *nla_time_tx = tb_survey[NL80211_SURVEY_INFO_TIME_TX];
    struct nlattr *nla_time_rx = tb_survey[NL80211_SURVEY_INFO_TIME_RX];
    struct nlattr *nla_time_bss_rx = tb_survey[NL80211_SURVEY_INFO_TIME_BSS_RX];
    struct nlattr *nla_time_busy = tb_survey[NL80211_SURVEY_INFO_TIME_BUSY];

    struct osw_tlv t;
    MEMZERO(t);

    enum osw_drv_nl80211_dump_survey_impl_type survey_type = drv_nl80211_phy->dump_survey_impl_type;

    if (survey_type == OSW_DRV_NL80211_DUMP_SURVEY_IMPL_ONCHAN_ABSOLUTE_OFFCHAN_DELTA) {
        const bool is_home_chan = nla_in_use ? nla_get_flag(nla_in_use) : 0;

        if (is_home_chan)
            survey_type = OSW_DRV_NL80211_DUMP_SURVEY_IMPL_ABSOLUTE;
        else
            survey_type = OSW_DRV_NL80211_DUMP_SURVEY_IMPL_DELTA;
    }

    size_t start_chan = osw_tlv_put_nested(&t, OSW_STATS_CHAN);
    {
        if (phy_name != NULL) osw_tlv_put_string(&t, OSW_STATS_CHAN_PHY_NAME, phy_name);
        if (nla_freq != NULL) osw_tlv_put_u32(&t, OSW_STATS_CHAN_FREQ_MHZ, nla_get_u32(nla_freq));
        if (nla_noise != NULL) osw_tlv_put_float(&t, OSW_STATS_CHAN_NOISE_FLOOR_DBM, (float)(int32_t)(int8_t)nla_get_u8(nla_noise));
        if (nla_time != NULL) {
            if (survey_type == OSW_DRV_NL80211_DUMP_SURVEY_IMPL_DELTA)
                osw_tlv_put_u32_delta(&t, OSW_STATS_CHAN_ACTIVE_MSEC, (uint32_t)nla_get_u64(nla_time));
            else if (survey_type == OSW_DRV_NL80211_DUMP_SURVEY_IMPL_ABSOLUTE)
                osw_tlv_put_u32(&t, OSW_STATS_CHAN_ACTIVE_MSEC, (uint32_t)nla_get_u64(nla_time));
        }

        size_t start_cnt = osw_tlv_put_nested(&t, OSW_STATS_CHAN_CNT_MSEC);
        {
            switch (survey_type) {
                case OSW_DRV_NL80211_DUMP_SURVEY_IMPL_NONE:
                    WARN_ON(1);
                    break;
                case OSW_DRV_NL80211_DUMP_SURVEY_IMPL_DELTA:
                    if (nla_time_tx != NULL) osw_tlv_put_u32_delta(&t, OSW_STATS_CHAN_CNT_TX, (uint32_t)nla_get_u64(nla_time_tx));
                    if (nla_time_rx != NULL) osw_tlv_put_u32_delta(&t, OSW_STATS_CHAN_CNT_RX, (uint32_t)nla_get_u64(nla_time_rx));
                    if (nla_time_bss_rx != NULL) osw_tlv_put_u32_delta(&t, OSW_STATS_CHAN_CNT_RX_INBSS, (uint32_t)nla_get_u64(nla_time_bss_rx));
                    if (nla_time_busy != NULL) osw_tlv_put_u32_delta(&t, OSW_STATS_CHAN_CNT_BUSY, (uint32_t)nla_get_u64(nla_time_busy));
                    break;
                case OSW_DRV_NL80211_DUMP_SURVEY_IMPL_ABSOLUTE:
                    if (nla_time_tx != NULL) osw_tlv_put_u32(&t, OSW_STATS_CHAN_CNT_TX, (uint32_t)nla_get_u64(nla_time_tx));
                    if (nla_time_rx != NULL) osw_tlv_put_u32(&t, OSW_STATS_CHAN_CNT_RX, (uint32_t)nla_get_u64(nla_time_rx));
                    if (nla_time_bss_rx != NULL) osw_tlv_put_u32(&t, OSW_STATS_CHAN_CNT_RX_INBSS, (uint32_t)nla_get_u64(nla_time_bss_rx));
                    if (nla_time_busy != NULL) osw_tlv_put_u32(&t, OSW_STATS_CHAN_CNT_BUSY, (uint32_t)nla_get_u64(nla_time_busy));
                    break;
                case OSW_DRV_NL80211_DUMP_SURVEY_IMPL_ONCHAN_ABSOLUTE_OFFCHAN_DELTA:
                    WARN_ON(1);
                    break;
            }
            osw_tlv_end_nested(&t, start_cnt);
        }
        osw_tlv_end_nested(&t, start_chan);
    }

    osw_drv_report_stats(drv, &t);
    osw_tlv_fini(&t);
}

static void
osw_drv_nl80211_vif_state_dump_sta_stats_resp_cb(struct nl_cmd *cmd,
                                                 struct nl_msg *msg,
                                                 void *priv)
{
    struct osw_drv_nl80211_vif *drv_nl80211_vif = priv;
    struct osw_drv_nl80211 *m = drv_nl80211_vif->m;
    struct osw_drv *drv = m->drv;

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, NULL);
    if (err) return;

    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(drv_nl80211_vif);
    if (phy == NULL) return;
    const char *phy_name = phy->phy_name;
    const char *vif_name = drv_nl80211_vif->vif_name;

    LOGT(LOG_PREFIX_VIF(phy_name, vif_name, "stats: sta"));

    struct osw_drv_nl80211_phy *drv_nl80211_phy = osw_drv_nl80211_phy_from_vif(drv_nl80211_vif);
    WARN_ON(drv_nl80211_phy->dump_sta_impl_type == OSW_DRV_NL80211_DUMP_STA_IMPL_NONE);

    struct nlattr *sta = tb[NL80211_ATTR_STA_INFO];
    if (sta == NULL) return;

    struct nlattr *nla_mac = tb[NL80211_ATTR_MAC];
    if (WARN_ON(nla_mac == NULL)) return;
    if (WARN_ON(nla_len(nla_mac) != ETH_ALEN)) return;
    struct osw_hwaddr osw_mac;
    memcpy(osw_mac.octet, nla_data(nla_mac), ETH_ALEN);

    static struct nla_policy sta_policy[NL80211_STA_INFO_MAX + 1] = {
        [NL80211_STA_INFO_SIGNAL] = { .type = NLA_U8 },
        [NL80211_STA_INFO_TX_BYTES] = { .type = NLA_U32 },
        [NL80211_STA_INFO_RX_BYTES] = { .type = NLA_U32 },
        [NL80211_STA_INFO_RX_BYTES64] = { .type = NLA_U64 },
        [NL80211_STA_INFO_TX_BYTES64] = { .type = NLA_U64 },
        [NL80211_STA_INFO_TX_PACKETS] = { .type = NLA_U32 },
        [NL80211_STA_INFO_RX_PACKETS] = { .type = NLA_U32 },
        [NL80211_STA_INFO_RX_BITRATE] = { .type = NLA_NESTED },
        [NL80211_STA_INFO_TX_BITRATE] = { .type = NLA_NESTED },
        [NL80211_STA_INFO_TX_RETRIES] = { .type = NLA_U32 },
        [NL80211_STA_INFO_TX_FAILED] = { .type = NLA_U32 },
        [NL80211_STA_INFO_RX_DROP_MISC] = { .type = NLA_U64 },
    };

    struct nlattr *tb_sta[NL80211_STA_INFO_MAX + 1];
    const int sta_err = nla_parse_nested(tb_sta, NL80211_STA_INFO_MAX, sta, sta_policy);
    if (WARN_ON(sta_err)) return;

    struct nlattr *nla_signal = tb_sta[NL80211_STA_INFO_SIGNAL];
    struct nlattr *nla_tx_bytes = tb_sta[NL80211_STA_INFO_TX_BYTES];
    struct nlattr *nla_rx_bytes = tb_sta[NL80211_STA_INFO_RX_BYTES];
    struct nlattr *nla_tx_bytes64 = tb_sta[NL80211_STA_INFO_TX_BYTES64];
    struct nlattr *nla_rx_bytes64 = tb_sta[NL80211_STA_INFO_RX_BYTES64];
    struct nlattr *nla_tx_packets = tb_sta[NL80211_STA_INFO_TX_PACKETS];
    struct nlattr *nla_rx_packets = tb_sta[NL80211_STA_INFO_RX_PACKETS];
    struct nlattr *nla_tx_retries = tb_sta[NL80211_STA_INFO_TX_RETRIES];
    struct nlattr *nla_tx_failed = tb_sta[NL80211_STA_INFO_TX_FAILED];
    struct nlattr *nla_rx_drop_misc = tb_sta[NL80211_STA_INFO_RX_DROP_MISC];

    /* Rate Info */
    static struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1] = {
        [NL80211_RATE_INFO_BITRATE] = { .type = NLA_U16 },
        [NL80211_RATE_INFO_BITRATE32] = { .type = NLA_U32 },
    };

    struct nlattr *rate_tb[NL80211_RATE_INFO_MAX + 1];
    struct nlattr *nla_sta_rate_tx = tb_sta[NL80211_STA_INFO_TX_BITRATE];
    struct nlattr *nla_sta_rate_rx = tb_sta[NL80211_STA_INFO_RX_BITRATE];
    struct nlattr *nla_rate32 = NULL;
    struct nlattr *nla_rate16 = NULL;
    uint32_t rate_tx = 0;
    uint32_t rate_rx = 0;

    if (nla_sta_rate_tx &&
        !nla_parse_nested(rate_tb,
                          NL80211_RATE_INFO_MAX,
                          nla_sta_rate_tx,
                          rate_policy)) {
        if ((nla_rate32 = rate_tb[NL80211_RATE_INFO_BITRATE32]))
            rate_tx = nla_get_u32(nla_rate32) / 10;
        if ((nla_rate16 = rate_tb[NL80211_RATE_INFO_BITRATE]))
            rate_tx = (uint32_t)nla_get_u16(nla_rate16) / 10;
    }

    if (nla_sta_rate_rx &&
        !nla_parse_nested(rate_tb,
                          NL80211_RATE_INFO_MAX,
                          nla_sta_rate_rx,
                          rate_policy)) {
        nla_rate32 = NULL;
        nla_rate16 = NULL;

        if ((nla_rate32 = rate_tb[NL80211_RATE_INFO_BITRATE32]))
            rate_rx = nla_get_u32(nla_rate32) / 10;
        if ((nla_rate16 = rate_tb[NL80211_RATE_INFO_BITRATE]))
            rate_rx = (uint32_t)nla_get_u16(nla_rate16) / 10;
    }

    struct osw_tlv t;
    MEMZERO(t);

    size_t start_sta = osw_tlv_put_nested(&t, OSW_STATS_STA);
    {
        if (phy_name != NULL) osw_tlv_put_string(&t, OSW_STATS_STA_PHY_NAME, phy_name);
        if (vif_name != NULL) osw_tlv_put_string(&t, OSW_STATS_STA_VIF_NAME, vif_name);
        if (nla_mac != NULL) osw_tlv_put_hwaddr(&t, OSW_STATS_STA_MAC_ADDRESS, &osw_mac);

        const int32_t rssi_dbm = nla_signal ? (int32_t)(int8_t)nla_get_u8(nla_signal) : 0;
        const int32_t noise_dbm = osw_channel_nf_20mhz_fixup(0);
        const uint32_t snr_db = (rssi_dbm >= noise_dbm ? (rssi_dbm - noise_dbm) : 0);
        osw_tlv_put_u32(&t, OSW_STATS_STA_SNR_DB, snr_db);

        switch (drv_nl80211_phy->dump_sta_impl_type) {
            case OSW_DRV_NL80211_DUMP_STA_IMPL_NONE:
                WARN_ON(1);
                break;
            case OSW_DRV_NL80211_DUMP_STA_IMPL_DEFAULT:
                if (nla_tx_bytes != NULL) osw_tlv_put_u32(&t, OSW_STATS_STA_TX_BYTES, nla_get_u32(nla_tx_bytes));
                if (nla_rx_bytes != NULL) osw_tlv_put_u32(&t, OSW_STATS_STA_RX_BYTES, nla_get_u32(nla_rx_bytes));
                if (nla_tx_bytes64 != NULL) osw_tlv_put_u64(&t, OSW_STATS_STA_TX_BYTES_64, nla_get_u64(nla_tx_bytes64));
                if (nla_rx_bytes64 != NULL) osw_tlv_put_u64(&t, OSW_STATS_STA_RX_BYTES_64, nla_get_u64(nla_rx_bytes64));
                if (nla_tx_packets != NULL) osw_tlv_put_u32(&t, OSW_STATS_STA_TX_FRAMES, nla_get_u32(nla_tx_packets));
                if (nla_rx_packets != NULL) osw_tlv_put_u32(&t, OSW_STATS_STA_RX_FRAMES, nla_get_u32(nla_rx_packets));
                if (rate_tx) osw_tlv_put_u32(&t, OSW_STATS_STA_TX_RATE_MBPS, rate_tx);
                if (rate_rx) osw_tlv_put_u32(&t, OSW_STATS_STA_RX_RATE_MBPS, rate_rx);
                if (nla_tx_retries != NULL) osw_tlv_put_u32(&t, OSW_STATS_STA_TX_RETRIES, nla_get_u32(nla_tx_retries));
                if (nla_tx_failed != NULL) osw_tlv_put_u32(&t, OSW_STATS_STA_TX_ERRORS, nla_get_u32(nla_tx_failed));
                if (nla_rx_drop_misc != NULL) osw_tlv_put_u32(&t, OSW_STATS_STA_RX_ERRORS, (uint32_t)nla_get_u64(nla_rx_drop_misc));
                break;
        }
        osw_tlv_end_nested(&t, start_sta);
    }

    osw_drv_report_stats(drv, &t);
    osw_tlv_fini(&t);
}

static void
osw_drv_nl80211_vif_mark_enabled(const char *vif_name,
                                 struct osw_drv_vif_state *state)
{
    const enum osw_vif_type type = state->vif_type;

    /* FIXME: This probably needs a bit more thought and
     * nl80211 should be used to infer that? Enabled !=
     * running (admin-UP vs carrier-UP)
     */
    switch (type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
        case OSW_VIF_AP_VLAN:
            {
                bool enabled = false;
                os_nif_is_running((char *)vif_name, &enabled);
                if (enabled) {
                    osw_vif_status_set(&state->status, OSW_VIF_ENABLED);
                }
                else {
                    osw_vif_status_set(&state->status, OSW_VIF_DISABLED);
                }
                break;
            }
        case OSW_VIF_STA:
            break;
    }
}

static void
osw_drv_nl80211_vif_sta_conn_failure_report(struct osw_drv_nl80211_vif *vif,
                                            enum osw_drv_vif_sta_conn_failure_kind kind,
                                            uint16_t code,
                                            bool local,
                                            const char *detail)
{
    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (drv == NULL) return;
    if (WARN_ON(phy == NULL)) return;

    struct osw_drv_vif_sta_conn_failure failure;
    MEMZERO(failure);
    failure.kind = kind;
    failure.code = code;
    failure.local = local;
    if (detail != NULL) STRSCPY(failure.detail, detail);

    osw_drv_report_vif_sta_conn_failure(drv, phy->phy_name, vif->vif_name, &failure);
}

static void
osw_drv_nl80211_vif_state_report_finalize(struct osw_drv_nl80211_vif *vif)
{
    const char *vif_name = vif->vif_name;
    struct osw_drv_vif_state *state = &vif->state;
    struct osw_drv_vif_state_sta_link *link = &state->u.sta.link;
    const bool *connected = &vif->state_bits.connected;
    const struct osw_hwaddr *bssid = &vif->state_bits.bssid;

    if (state->vif_type == OSW_VIF_STA) {
        if (*connected) {
            link->bssid = *bssid;
            link->status = OSW_DRV_VIF_STATE_STA_LINK_CONNECTED;
        }
        else {
            link->status = OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED;
        }
    }

    os_nif_exists((char *)vif_name, &state->exists);
    osw_drv_nl80211_vif_mark_enabled(vif_name, state);
    osw_hostap_bss_fill_state(vif->hostap_bss, state);

    if (state->vif_type == OSW_VIF_STA) {
        /* Keep conn_status consistent when nl80211 knows the link is
         * up but the wpa_supplicant STATUS poll was unavailable.
         */
        if (link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED) {
            link->conn_status = OSW_DRV_VIF_STATE_STA_CONN_CONNECTED;
        }
    }

    /* percent==100 requests a "no power limit" and is applied via the driver's
     * automatic tx power selection. The netlink tx power level
     * then reflects the hardware maximum, which may not be equal to
     * the per-channel regulatory maximum.
     * Report that the vif is running unrestricted so confsync can settle on that intent
     * rather than on an exact dBm value.
     */
    state->tx_power_percent = vif->tx_power_auto ? 100 : 0;
}

static void
osw_drv_nl80211_vif_state_sanitize_tx_power_dbm(const char *phy_name,
                                                const char *vif_name,
                                                struct osw_drv_vif_state *state)
{
    if (state->status != OSW_VIF_ENABLED && state->tx_power_dbm != 0) {
        LOGD(LOG_PREFIX_VIF(phy_name, vif_name,
                            "state: tx_power_dbm override %d -> to 0: vif is disabled",
                            state->tx_power_dbm));
        state->tx_power_dbm = 0;
        state->tx_power_percent = 0;
        state->tx_power_db_limit = 0;
        state->tx_power_db_limit_valid = false;
    }

    const int max = 50;
    if (state->tx_power_dbm >= max) {
        LOGD(LOG_PREFIX_VIF(phy_name, vif_name,
                            "state: tx_power_dbm override %d -> to 0: reported >= %d",
                            state->tx_power_dbm, max));
        state->tx_power_dbm = 0;
        state->tx_power_percent = 0;
        state->tx_power_db_limit = 0;
        state->tx_power_db_limit_valid = false;
    }
}

static void
osw_drv_nl80211_vif_state_report_cb(struct rq *q,
                                    void *priv)
{
    if (q->stopped) return;
    struct osw_drv_nl80211_vif *vif = priv;
    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;
    struct osw_drv_vif_state *state = &vif->state;
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (WARN_ON(phy == NULL)) return;
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    osw_drv_nl80211_vif_state_report_finalize(vif);

    CALL_HOOKS(m, fix_vif_state_fn, phy_name, vif_name, state);

    osw_drv_nl80211_vif_state_sanitize_tx_power_dbm(phy_name, vif_name, state);

    LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "reporting state"));
    if (drv != NULL) osw_drv_report_vif_state(drv, phy_name, vif_name, state);
    osw_drv_vif_state_report_free(state);
}

static void
osw_drv_nl80211_vif_hostap_event_cb(const char *msg,
                                    size_t msg_len,
                                    void *priv)
{
    struct osw_drv_nl80211_vif *vif = priv;
    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;
    if (drv == NULL) return;

    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (WARN_ON(phy == NULL)) return;
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    char buf[1024];
    STRSCPY_WARN(buf, msg);

    char *p = buf;
    char *event_name = strsep(&p, " ");

    if (strcmp(event_name, "AP-ENABLED") == 0) {
        if (drv == NULL) return;
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "AP-DISABLED") == 0) {
        if (drv == NULL) return;
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "INTERFACE-DISABLED") == 0) {
        if (drv == NULL) return;
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "INTERFACE-ENABLED") == 0) {
        if (drv == NULL) return;
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "AP-STA-CONNECTED") == 0) {
        if (drv == NULL) return;

        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
        /* AP-STA-CONNECTED c0:9f:51:8f:7a:2c auth_alg=sae assoc_ies=00002400f09f518 */
        char *token;
        struct osw_hwaddr sta_addr = {0};

        osw_hwaddr_from_cstr(strsep(&p, " "), &sta_addr);

        while ((token = strsep(&p, " ")) != NULL) {
            const char *k = strsep(&token, "=");
            const char *v = strsep(&token, " ");

            if (strcmp(k, "assoc_ies") == 0) {
                size_t assoc_ies_len = strlen(v)/2;
                uint8_t *assoc_ies = MALLOC(sizeof(uint8_t) * assoc_ies_len);
                const bool decoded = (hex2bin(v, strlen(v), assoc_ies, assoc_ies_len) != -1);
                const bool valid_addr = (osw_hwaddr_is_zero(&sta_addr) != true);

                if (decoded && valid_addr)
                {
                    osw_drv_report_sta_assoc_ies(drv, phy_name, vif_name, &sta_addr, assoc_ies, assoc_ies_len);
                }
                FREE(assoc_ies);
                break;
            }
        }
    }
    else if (strcmp(event_name, "WPS-OVERLAP-DETECTED") == 0) {
        if (drv == NULL) return;
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "wps: overlapped"));
        osw_drv_report_vif_wps_overlap(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "WPS-SUCCESS") == 0) {
        if (drv == NULL) return;
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "wps: succeeded"));
        osw_drv_report_vif_wps_success(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "WPS-FAIL") == 0) {
        if (drv == NULL) return;
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "wps: failed"));
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "WPS-TIMEOUT") == 0) {
        if (drv == NULL) return;
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "wps: timed out"));
        osw_drv_report_vif_wps_pbc_timeout(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "WPS-CANCEL") == 0) {
        if (drv == NULL) return;
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "wps: cancelled"));
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "WPS-PBC-ACTIVE") == 0) {
        if (drv == NULL) return;
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "wps: pbc activated"));
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "WPS-PBC-DISABLE") == 0) {
        if (drv == NULL) return;
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "wps: pbc disabled"));
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "RX-PROBE-REQUEST") == 0) {
        struct osw_drv_report_vif_probe_req preq;
        MEMZERO(preq);

        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));

        /* RX-PROBE-REQUEST sa=e4:26:86:64:7b:54 signal=-7 ssid=wildcard freq=2462 */
        char *token;
        while ((token = strsep(&p, " ")) != NULL) {
            const char *k = strsep(&token, "=");
            const char *v = strsep(&token, " ");

            if (strcmp(k, "sa") == 0) {
                osw_hwaddr_from_cstr(v, &preq.sta_addr);
            }
            else if (strcmp(k, "signal") == 0) {
                const int rssi = atoi(v);
                const int nf = osw_channel_nf_20mhz_fixup(0);
                const int snr = rssi - nf;

                preq.snr = snr;
            }
        }

        const bool report = (osw_hwaddr_is_zero(&preq.sta_addr) == false);

        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "preq: sa="OSW_HWADDR_FMT" snr=%u report=%d",
                            OSW_HWADDR_ARG(&preq.sta_addr),
                            preq.snr,
                            report));

        if (report) {
            if (drv == NULL) return;
            osw_drv_report_vif_probe_req(drv, phy_name, vif_name, &preq);
        }
    }
    else if (strcmp(event_name, "WPS-ENROLLEE-SEEN") == 0) {
        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
    }
    else if (strcmp(event_name, "CTRL-EVENT-CHANNEL-SWITCH") == 0) {
        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
    }
    else if (strcmp(event_name, "CTRL-EVENT-CONNECTED") == 0) {
        if (drv == NULL) return;
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "CTRL-EVENT-DISCONNECTED") == 0) {
        /* CTRL-EVENT-DISCONNECTED bssid=xx:xx:xx:xx:xx:xx reason=15 locally_generated=1 */
        uint16_t code = 0;
        bool local = false;
        char *token;
        while ((token = strsep(&p, " ")) != NULL) {
            const char *k = strsep(&token, "=");
            if (k == NULL) continue;
            const char *v = strsep(&token, " ");
            if (v == NULL) continue;

            if (strcmp(k, "reason") == 0) {
                code = atoi(v);
            }
            else if (strcmp(k, "locally_generated") == 0) {
                local = (atoi(v) == 1);
            }
        }
        osw_drv_nl80211_vif_sta_conn_failure_report(vif, OSW_DRV_VIF_STA_CONN_FAILURE_DISCONNECTED, code, local, NULL);
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "CTRL-EVENT-NETWORK-NOT-FOUND") == 0) {
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
        osw_drv_nl80211_vif_sta_conn_failure_report(vif, OSW_DRV_VIF_STA_CONN_FAILURE_SSID_NOT_FOUND, 0, false, NULL);
        if (drv == NULL) return;
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "CTRL-EVENT-SSID-TEMP-DISABLED") == 0) {
        /* CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid="foo" auth_failures=1 duration=10 reason=WRONG_KEY */

        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
        const char *reason = "";
        char *token;
        while ((token = strsep(&p, " ")) != NULL) {
            const char *k = strsep(&token, "=");
            if (k == NULL) continue;
            const char *v = strsep(&token, " ");
            if (v == NULL) continue;

            if (strcmp(k, "reason") == 0) {
                reason = v;
            }
        }

        if ((strcmp(reason, "WRONG_KEY") == 0) ||
            (strcmp(reason, "AUTH_FAILED") == 0)) {
            osw_drv_nl80211_vif_sta_conn_failure_report(vif, OSW_DRV_VIF_STA_CONN_FAILURE_WRONG_KEY, 0, false, NULL);
        }
        else {
            const char *detail = (strcmp(reason, "NO_PSK_AVAILABLE") == 0) ? "no_psk"
                               : (strcmp(reason, "CONN_FAILED") == 0) ? "conn_failed"
                               : reason;
            osw_drv_nl80211_vif_sta_conn_failure_report(vif, OSW_DRV_VIF_STA_CONN_FAILURE_GENERAL_ERR, 0, false, detail);
        }
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "CTRL-EVENT-ASSOC-REJECT") == 0) {
        /* CTRL-EVENT-ASSOC-REJECT bssid=xx:xx:xx:xx:xx:xx status_code=17 */
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
        uint16_t code = 0;
        char *token;
        while ((token = strsep(&p, " ")) != NULL) {
            const char *k = strsep(&token, "=");
            if (k == NULL) continue;
            const char *v = strsep(&token, " ");
            if (v == NULL) continue;

            if (strcmp(k, "status_code") == 0) {
                code = atoi(v);
            }
        }
        osw_drv_nl80211_vif_sta_conn_failure_report(vif, OSW_DRV_VIF_STA_CONN_FAILURE_ASSOC_REJECT, code, false, NULL);
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "CTRL-EVENT-AUTH-REJECT") == 0) {
        /* CTRL-EVENT-AUTH-REJECT xx:xx:xx:xx:xx:xx auth_type=3 auth_transaction=2 status_code=1 */
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
        uint16_t code = 0;
        int auth_type = -1;
        char *token;
        while ((token = strsep(&p, " ")) != NULL) {
            const char *k = strsep(&token, "=");
            if (k == NULL) continue;
            const char *v = strsep(&token, " ");
            if (v == NULL) continue;

            if (strcmp(k, "status_code") == 0) {
                code = atoi(v);
            }
            else if (strcmp(k, "auth_type") == 0) {
                auth_type = atoi(v);
            }
        }

        /* An AP rejecting SAE authentication: in practice,
         * that means the password did not match. */
        const bool sae = (auth_type == 3);
        osw_drv_nl80211_vif_sta_conn_failure_report(vif,
                                                    sae ? OSW_DRV_VIF_STA_CONN_FAILURE_WRONG_KEY
                                                        : OSW_DRV_VIF_STA_CONN_FAILURE_AUTH_REJECT,
                                                    code, false, NULL);

        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "CTRL-EVENT-SCAN-FAILED") == 0) {
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
        osw_drv_nl80211_vif_sta_conn_failure_report(vif, OSW_DRV_VIF_STA_CONN_FAILURE_GENERAL_ERR, 0, false, "scan_failed");
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "WPS-AP-AVAILABLE") == 0) {
        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
    }
    else if (strcmp(event_name, "CTRL-EVENT-SCAN-STARTED") == 0) {
        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
        if (drv == NULL) return;
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "CTRL-EVENT-SCAN-RESULTS") == 0) {
        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
        if (drv == NULL) return;
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "CTRL-EVENT-BSS-ADDED") == 0) {
        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
    }
    else if (strcmp(event_name, "CTRL-EVENT-BSS-REMOVED") == 0) {
        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
    }
    else if ((strcmp(event_name, "DFS-RADAR-DETECTED") == 0) ||
             (strcmp(event_name, "DFS-NEW-CHANNEL") == 0) ||
             (strcmp(event_name, "DFS-CAC-START") == 0) ||
             (strcmp(event_name, "DFS-CAC-COMPLETED") == 0) ||
             (strcmp(event_name, "DFS-DFS-PRE-CAC-EXPIRED") == 0) ||
             (strcmp(event_name, "DFS-DFS-NOP-FINISHED") == 0)) {
        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
        osw_drv_report_phy_changed(drv, phy_name);
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
    else if (strcmp(event_name, "NL80211-CMD-FRAME") == 0) {
        /* This isn't a standard hostapd event, but one that comes from some
         * patches on some platforms. It's useful to log them in debug, but
         * definitely not in info because they can spam a lot.
         */
        LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
    }
    else {
        LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "hostap event: %s (len=%zu)", msg, msg_len));
    }
}

static void
osw_drv_nl80211_vif_hostap_bss_changed_cb(void *priv)
{
    struct osw_drv_nl80211_vif *vif = priv;
    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (WARN_ON(phy == NULL)) return;
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap: bss changed"));
    if (drv == NULL) return;

    osw_drv_report_vif_changed(drv, phy_name, vif_name);
}

static void
osw_drv_nl80211_vif_hostap_config_applied_cb(void *priv)
{
    struct osw_drv_nl80211_vif *vif = priv;
    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (WARN_ON(phy == NULL)) return;
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "hostap config completed"));
    if (drv == NULL) return;

    osw_drv_report_vif_changed(drv, phy_name, vif_name);

    /* .. and for good measure assume PHY attributes changed implicitly too: */
    osw_drv_report_phy_changed(drv, phy_name);
}

static void
osw_drv_nl80211_vif_hostap_sta_connected_cb(const struct osw_hostap_bss_sta *info,
                                            void *priv)
{
    const struct osw_hwaddr *sta_addr = &info->addr;

    struct osw_drv_nl80211_vif *vif = priv;
    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (WARN_ON(phy == NULL)) return;
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    struct osw_drv_nl80211_sta *sta = osw_drv_nl80211_sta_lookup(m, phy_name, vif_name, sta_addr)
                                   ?: osw_drv_nl80211_sta_create(m, phy_name, vif_name, sta_addr);

    osw_drv_nl80211_sta_init_hostap(sta, info);

    if (info->assoc_ies_len) {
        osw_drv_report_sta_assoc_ies(drv, phy_name, vif_name, sta_addr, info->assoc_ies, info->assoc_ies_len);
    }

    osw_drv_report_sta_changed(drv, phy_name, vif_name, sta_addr);
    osw_drv_nl80211_invalidate_sta_mld_links(m, sta_addr);
}

static void
osw_drv_nl80211_vif_hostap_sta_changed_cb(const struct osw_hostap_bss_sta *info,
                                          void *priv)
{
    const struct osw_hwaddr *sta_addr = &info->addr;

    struct osw_drv_nl80211_vif *vif = priv;
    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (WARN_ON(phy == NULL)) return;
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    struct osw_drv_nl80211_sta *sta = osw_drv_nl80211_sta_lookup(m, phy_name, vif_name, sta_addr);
    if (WARN_ON(sta == NULL)) return;

    sta->hostap_info = *info;
    sta->hostap_info.assoc_ies = NULL;
    sta->hostap_info.assoc_ies_len = 0;

    if (info->assoc_ies_len) {
        osw_drv_report_sta_assoc_ies(drv, phy_name, vif_name, sta_addr, info->assoc_ies, info->assoc_ies_len);
    }

    osw_drv_report_sta_changed(drv, phy_name, vif_name, sta_addr);
    osw_drv_nl80211_invalidate_sta_mld_links(m, sta_addr);
}

static void
osw_drv_nl80211_vif_hostap_sta_disconnected_cb(const struct osw_hostap_bss_sta *info,
                                               void *priv)
{
    const struct osw_hwaddr *sta_addr = &info->addr;

    struct osw_drv_nl80211_vif *vif = priv;
    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (WARN_ON(phy == NULL)) return;
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    struct osw_drv_nl80211_sta *sta = osw_drv_nl80211_sta_lookup(m, phy_name, vif_name, sta_addr);
    if (WARN_ON(sta == NULL)) return;

    osw_drv_nl80211_sta_fini_hostap(sta);
    osw_drv_nl80211_sta_maybe_free(sta);
    osw_drv_report_sta_changed(drv, phy_name, vif_name, sta_addr);
    osw_drv_nl80211_invalidate_sta_mld_links(m, sta_addr);
}

static void
osw_drv_nl80211_vif_netif_cb(osn_netif_t *netif,
                             struct osn_netif_status *status)
{
    struct osw_drv_nl80211_vif *vif = osn_netif_data_get(netif);
    struct osw_drv_nl80211 *m = vif->m;
    struct osw_drv *drv = m->drv;
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (phy == NULL) return;
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    osw_drv_report_vif_changed(drv, phy_name, vif_name);
}

static char *
osw_drv_nl80211_vif_name_get(const struct nl_80211_vif *info)
{
    return STRDUP(info->name);
}

static void
osw_drv_nl80211_vif_added_cb(const struct nl_80211_vif *info,
                             void *priv)
{
    struct osw_drv_nl80211 *m = priv;
    struct osw_drv *drv = m->drv;
    struct osw_drv_nl80211_vif *vif = nl_80211_sub_vif_get_priv(m->nl_80211_sub, info);
    FREE(vif->vif_name);
    vif->vif_name = osw_drv_nl80211_vif_name_get(info);
    vif->info = info;
    vif->m = m;

    if (osw_drv_nl80211_vif_is_ignored(vif->vif_name)) return;

    /* Sanity check. There's a bunch of assumptions in the code that vif_name
     * is a unique identifier. Assert and abort if this is not upheld.
     */
    const bool vif_name_already_exists = ds_tree_find(&m->vifs_by_name, vif->vif_name) != NULL;
    assert(vif_name_already_exists == false);

    ds_tree_insert(&m->vifs_by_name, vif, vif->vif_name);

    struct osw_hostap *hostap = m->hostap;
    struct osw_drv_nl80211_phy *drv_nl80211_phy = osw_drv_nl80211_phy_from_vif(vif);
    assert(drv_nl80211_phy != NULL);

    /* WARNING
     *
     * Single-wiphy multi-radio systems are currently limited to non-MLO
     * operation, and are expected to have their interface arranged with
     * radio_mask in a way, that radio_mask is always non-zero and contains a
     * single non-zero bit set to.
     *
     * Failing to adhere to that (at integration level) would result in
     * undefined behavior. As such, bail out as quickly. The integration (iw
     * interface add) needs to be adjusted.
     */
    const bool wiphy_is_single_radio = drv_nl80211_phy->info->num_radios <= 1;
    const bool wiphy_is_multi_radio = drv_nl80211_phy->info->num_radios > 1;
    const int vif_num_radio_bits = __builtin_popcount(info->radio_mask);
    const bool vif_is_single_radio = (vif_num_radio_bits == 1);
    assert((wiphy_is_single_radio) ||
           (wiphy_is_multi_radio && vif_is_single_radio));

    const char *vif_name = vif->vif_name;
    const char *phy_name = drv_nl80211_phy->phy_name;

    vif->netif = osn_netif_new(vif_name);
    osn_netif_data_set(vif->netif, vif);
    osn_netif_status_notify(vif->netif, osw_drv_nl80211_vif_netif_cb);

    {
        const uint32_t ifindex = info->ifindex;
        struct nl_conn *conn = m->nl_conn;
        struct nl_80211 *nl = m->nl_80211;
        struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
        struct nl_msg *msg = nl_80211_alloc_disconnect(nl, ifindex);
        nl_cmd_set_name(cmd, strfmta(LOG_PREFIX_VIF(phy_name, vif_name, "disconnect")));
        nl_cmd_task_init(&vif->task_nl_disconnect, cmd, msg);
    }

    rq_init(&vif->q_req, EV_DEFAULT);
    vif->q_req.max_running = 1;
    vif->q_req.empty_fn = osw_drv_nl80211_vif_state_report_cb;
    vif->q_req.priv = vif;

    {
        const uint32_t ifindex = info->ifindex;
        struct nl_conn *conn = m->nl_conn;
        struct nl_80211 *nl = m->nl_80211;
        struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
        struct nl_msg *msg = nl_80211_alloc_get_interface(nl, ifindex);
        nl_cmd_response_fn_t *resp_cb = osw_drv_nl80211_vif_state_get_intf_resp_cb;
        nl_cmd_set_response_fn(cmd, resp_cb, vif);
        nl_cmd_set_name(cmd, strfmta(LOG_PREFIX_VIF(phy_name, vif_name, "get interface")));
        nl_cmd_task_init(&vif->task_nl_get_intf, cmd, msg);
    }

    {
        const uint32_t ifindex = info->ifindex;
        struct nl_conn *conn = m->nl_conn;
        struct nl_80211 *nl = m->nl_80211;
        struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
        struct nl_msg *msg = nl_80211_alloc_dump_scan(nl, ifindex);
        nl_cmd_response_fn_t *resp_cb = osw_drv_nl80211_vif_state_dump_scan_resp_cb;
        nl_cmd_set_response_fn(cmd, resp_cb, vif);
        nl_cmd_set_name(cmd, strfmta(LOG_PREFIX_VIF(phy_name, vif_name, "scan: vif")));
        nl_cmd_task_init(&vif->task_nl_dump_scan, cmd, msg);
    }

    rq_init(&vif->q_stats, EV_DEFAULT);
    vif->q_stats.max_running = 1;
    vif->q_stats.priv = vif;

    {
        const uint32_t ifindex = info->ifindex;
        struct nl_conn *conn = m->nl_conn;
        struct nl_80211 *nl = m->nl_80211;
        struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
        struct nl_msg *msg = nl_80211_alloc_dump_scan(nl, ifindex);
        nl_cmd_response_fn_t *resp_cb = osw_drv_nl80211_vif_state_dump_scan_stats_resp_cb;
        nl_cmd_set_response_fn(cmd, resp_cb, vif);
        nl_cmd_set_name(cmd, strfmta(LOG_PREFIX_VIF(phy_name, vif_name, "scan: stats")));
        nl_cmd_task_init(&vif->task_nl_dump_scan_stats, cmd, msg);
    }

    if (drv_nl80211_phy->dump_survey_impl_type != OSW_DRV_NL80211_DUMP_SURVEY_IMPL_NONE) {
        const uint32_t ifindex = info->ifindex;
        struct nl_conn *conn = m->nl_conn;
        struct nl_80211 *nl = m->nl_80211;
        struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
        struct nl_msg *msg = nl_80211_alloc_dump_survey(nl, ifindex);
        nl_cmd_response_fn_t *resp_cb = osw_drv_nl80211_vif_state_dump_survey_stats_resp_cb;
        nl_cmd_set_response_fn(cmd, resp_cb, vif);
        nl_cmd_task_init(&vif->task_nl_dump_survey_stats, cmd, msg);
    }

    if (drv_nl80211_phy->dump_sta_impl_type != OSW_DRV_NL80211_DUMP_STA_IMPL_NONE) {
        const uint32_t ifindex = info->ifindex;
        struct nl_conn *conn = m->nl_conn;
        struct nl_80211 *nl = m->nl_80211;
        struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
        struct nl_msg *msg = nl_80211_alloc_dump_sta(nl, ifindex);
        nl_cmd_response_fn_t *resp_cb = osw_drv_nl80211_vif_state_dump_sta_stats_resp_cb;
        nl_cmd_set_response_fn(cmd, resp_cb, vif);
        nl_cmd_task_init(&vif->task_nl_dump_sta_stats, cmd, msg);
    }

    // FIXME: if phy_name is empty, it could lead to problems */
    static const struct osw_hostap_bss_ops hostap_bss_ops = {
        .event_fn = osw_drv_nl80211_vif_hostap_event_cb,
        .bss_changed_fn = osw_drv_nl80211_vif_hostap_bss_changed_cb,
        .config_applied_fn = osw_drv_nl80211_vif_hostap_config_applied_cb,
        .sta_connected_fn = osw_drv_nl80211_vif_hostap_sta_connected_cb,
        .sta_changed_fn = osw_drv_nl80211_vif_hostap_sta_changed_cb,
        .sta_disconnected_fn = osw_drv_nl80211_vif_hostap_sta_disconnected_cb,
    };
    vif->hostap_bss = osw_hostap_bss_alloc(hostap,
                                           phy_name,
                                           vif_name,
                                           &hostap_bss_ops,
                                           vif);

    if (drv_nl80211_phy->csa_impl_type == OSW_DRV_NL80211_CSA_IMPL_HOSTAP)
        osw_hostap_bss_fill_csa_by_hostap(vif->hostap_bss, true);
    if (drv_nl80211_phy->phy_group_impl_type == OSW_DRV_NL80211_PHY_GROUP_IMPL_PHY)
        osw_hostap_bss_fill_group_by_phy(vif->hostap_bss, true);
    if (drv_nl80211_phy->acl_impl_type == OSW_DRV_NL80211_ACL_IMPL_HOSTAP)
        osw_hostap_bss_fill_acl_by_hostap(vif->hostap_bss, true);
    struct osw_timer *timer = &vif->push_frame_tx_timer;
    osw_timer_init(timer, osw_drv_nl80211_push_frame_tx_timer_cb);

    LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "added"));
    osw_drv_report_vif_changed(drv, phy_name, vif_name);
}

static void
osw_drv_nl80211_vif_removed_cb(const struct nl_80211_vif *info,
                               void *priv)
{
    struct osw_drv_nl80211 *m = priv;
    struct osw_drv *drv = m->drv;
    struct osw_drv_nl80211_vif *vif = nl_80211_sub_vif_get_priv(m->nl_80211_sub, info);
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    const char *phy_name = phy ? phy->phy_name : "unknown";
    const char *vif_name = vif->vif_name;

    if (osw_drv_nl80211_vif_is_ignored(vif->vif_name)) {
        FREE(vif->vif_name);
        vif->vif_name = NULL;
        return;
    }

    ds_tree_remove(&m->vifs_by_name, vif);

    osw_drv_nl80211_scan_complete(vif, OSW_DRV_SCAN_ABORTED);

    osw_hostap_bss_free(vif->hostap_bss);
    vif->hostap_bss = NULL;

    nl_cmd_task_fini(&vif->task_nl_disconnect);
    nl_cmd_task_fini(&vif->task_nl_set_power);

    rq_stop(&vif->q_req);
    rq_kill(&vif->q_req);
    rq_fini(&vif->q_req);
    nl_cmd_task_fini(&vif->task_nl_get_intf);
    nl_cmd_task_fini(&vif->task_nl_dump_scan);

    rq_stop(&vif->q_stats);
    rq_kill(&vif->q_stats);
    rq_fini(&vif->q_stats);
    nl_cmd_task_fini(&vif->task_nl_dump_scan_stats);
    nl_cmd_task_fini(&vif->task_nl_dump_survey_stats);
    nl_cmd_task_fini(&vif->task_nl_dump_sta_stats);

    osw_drv_nl80211_push_frame_tx_abort(vif);
    osw_drv_vif_state_report_free(&vif->state);
    osw_drv_report_vif_state(drv, phy_name, vif_name, &vif->state);

    osn_netif_del(vif->netif);

    LOGI(LOG_PREFIX_VIF(phy_name, vif_name, "removed"));
    FREE(vif->vif_name);
    vif->vif_name = NULL;
}

static void
osw_drv_nl80211_sta_added_cb(const struct nl_80211_sta *info,
                             void *priv)
{
    struct osw_drv_nl80211 *m = priv;
    struct nl_80211 *nl = m->nl_80211;

    const struct osw_hwaddr *sta_addr = (const void *)info->addr.addr;
    const uint32_t ifindex = info->ifindex;
    const struct nl_80211_vif *vif_info = nl_80211_vif_by_ifindex(nl, ifindex);
    const struct osw_drv_nl80211_vif *vif = nl_80211_sub_vif_get_priv(m->nl_80211_sub, vif_info);
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (WARN_ON(vif == NULL)) return;
    if (WARN_ON(phy == NULL)) return;
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    struct osw_drv_nl80211_sta *sta = osw_drv_nl80211_sta_lookup(m, phy_name, vif_name, sta_addr)
                                   ?: osw_drv_nl80211_sta_create(m, phy_name, vif_name, sta_addr);

    osw_drv_nl80211_sta_init_nl(sta, info);
}

static void
osw_drv_nl80211_sta_removed_cb(const struct nl_80211_sta *info,
                               void *priv)
{
    struct osw_drv_nl80211 *m = priv;
    struct nl_80211 *nl = m->nl_80211;

    const struct osw_hwaddr *sta_addr = (const void *)info->addr.addr;
    const uint32_t ifindex = info->ifindex;
    const struct nl_80211_vif *vif_info = nl_80211_vif_by_ifindex(nl, ifindex);
    const struct osw_drv_nl80211_vif *vif = nl_80211_sub_vif_get_priv(m->nl_80211_sub, vif_info);
    const struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (WARN_ON(vif == NULL)) return;
    if (WARN_ON(phy == NULL)) return;
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    struct osw_drv_nl80211_sta *sta = osw_drv_nl80211_sta_lookup(m, phy_name, vif_name, sta_addr);
    if (WARN_ON(sta == NULL)) return;

    osw_drv_sta_state_report_free(&sta->state);
    osw_drv_nl80211_sta_fini_nl(sta);
    osw_drv_nl80211_sta_maybe_free(sta);
}

static void
osw_drv_nl80211_nl_ready_cb(struct nl_80211 *nl_80211,
                            void *priv)
{
    struct osw_drv_nl80211 *m = priv;
    struct osw_drv *drv = m->drv;
    const bool already_registered = (drv != NULL);

    LOGI("osw: drv: nl80211: ready");

    if (WARN_ON(already_registered)) return;

    osw_drv_register_ops(&m->drv_ops);
}

static void
osw_drv_nl80211_conn_event_cb(struct nl_conn_subscription *sub,
                              struct nl_msg *msg,
                              void *priv)
{
    struct osw_drv_nl80211 *m = priv;
    osw_drv_nl80211_event_process(m, msg);
}

static void
osw_drv_nl80211_conn_started_cb(struct nl_conn_subscription *sub,
                                void *priv)
{
}

static void
osw_drv_nl80211_conn_stopped_cb(struct nl_conn_subscription *sub,
                                void *priv)
{
}

static void
osw_drv_nl80211_conn_overrun_cb(struct nl_conn_subscription *sub,
                                void *priv)
{
    struct osw_drv_nl80211 *m = priv;
    struct osw_drv *drv = m->drv;
    if (drv == NULL) return;

    osw_drv_report_overrun(drv);
}

static struct nl_80211 *
osw_drv_nl80211_op_get_nl_80211_cb(struct osw_drv_nl80211_ops *ops)
{
    struct osw_drv_nl80211 *m = ops_to_mod(ops);
    return m->nl_80211;
}

static const struct nl_80211_phy *
osw_drv_nl80211_op_get_phy_info_fn(struct osw_drv_nl80211_ops *ops,
                                  const char *phy_name,
                                  int *radio_index)
{
    struct osw_drv_nl80211 *m = ops_to_mod(ops);
    struct osw_drv_nl80211_phy *phy = ds_tree_find(&m->phys_by_name, phy_name);
    if (phy == NULL) return NULL;

    const struct nl_80211_phy *info = phy->info;

    if (radio_index != NULL) {
        *radio_index = -1;
        if (info->num_radios > 1) {
            *radio_index = phy->radio_index;
        }
    }

    return info;
}

static const char *
osw_drv_nl80211_op_get_phy_name_from_vif_name_fn(struct osw_drv_nl80211_ops *ops,
                                                 const char *vif_name)
{
    struct osw_drv_nl80211 *m = ops_to_mod(ops);
    struct osw_drv_nl80211_vif *vif = ds_tree_find(&m->vifs_by_name, vif_name);
    if (vif == NULL) return NULL;
    struct osw_drv_nl80211_phy *phy = osw_drv_nl80211_phy_from_vif(vif);
    if (phy == NULL) return NULL;
    return phy->phy_name;
}

static struct osw_drv_nl80211_hook *
osw_drv_nl80211_op_add_hook_ops_cb(struct osw_drv_nl80211_ops *ops,
                                   const struct osw_drv_nl80211_hook_ops *hook_ops,
                                   void *priv)
{
    if (WARN_ON(ops == NULL)) return NULL;
    if (WARN_ON(hook_ops == NULL)) return NULL;

    struct osw_drv_nl80211 *m = ops_to_mod(ops);
    struct ds_dlist *hooks = &m->hooks;
    struct osw_drv_nl80211_hook *hook = CALLOC(1, sizeof(*hook));
    hook->owner = m;
    hook->ops = hook_ops;
    hook->priv = priv;
    ds_dlist_insert_tail(hooks, hook);

    return hook;
}

static void
osw_drv_nl80211_op_del_hook_cb(struct osw_drv_nl80211_ops *ops,
                               struct osw_drv_nl80211_hook *hook)
{
    if (WARN_ON(ops == NULL)) return;
    if (hook == NULL) return;
    if (hook->owner == NULL) return;

    struct osw_drv_nl80211 *m = ops_to_mod(ops);
    if (WARN_ON(hook->owner != m)) return;

    struct ds_dlist *hooks = &hook->owner->hooks;
    hook->owner = NULL;
    ds_dlist_remove(hooks, hook);
    FREE(hook);
}

static int
osw_drv_nl80211_sta_id_cmp(const void *a,
                           const void *b)
{
    const struct osw_drv_nl80211_sta_id *x = a;
    const struct osw_drv_nl80211_sta_id *y = b;
    return memcmp(x, y, sizeof(*x));
}

static void
osw_drv_nl80211_init(struct osw_drv_nl80211 *m)
{
    const struct osw_drv_ops drv_ops = {
        .name = "nl80211",
        .init_fn = osw_drv_nl80211_init_cb,
        .get_phy_list_fn = osw_drv_nl80211_get_phy_list_cb,
        .get_vif_list_fn = osw_drv_nl80211_get_vif_list_cb,
        .get_sta_list_fn = osw_drv_nl80211_get_sta_list_cb,
        .request_phy_state_fn = osw_drv_nl80211_request_phy_state_cb,
        .request_vif_state_fn = osw_drv_nl80211_request_vif_state_cb,
        .request_sta_state_fn = osw_drv_nl80211_request_sta_state_cb,
        .request_config_fn = osw_drv_nl80211_request_config_cb,
        .request_sta_deauth_fn = osw_drv_nl80211_request_sta_deauth_cb,
        .request_sta_delete_fn = osw_drv_nl80211_request_sta_delete_cb,
        .request_stats_fn = osw_drv_nl80211_request_stats_cb,
        .request_scan_fn = osw_drv_nl80211_request_scan_cb,
        .push_frame_tx_fn = osw_drv_nl80211_push_frame_tx_cb,
    };
    const struct osw_drv_nl80211_ops mod_ops = {
        .get_nl_80211_fn = osw_drv_nl80211_op_get_nl_80211_cb,
        .get_phy_info_fn = osw_drv_nl80211_op_get_phy_info_fn,
        .get_phy_name_from_vif_name_fn = osw_drv_nl80211_op_get_phy_name_from_vif_name_fn,
        .add_hook_ops_fn = osw_drv_nl80211_op_add_hook_ops_cb,
        .del_hook_fn = osw_drv_nl80211_op_del_hook_cb,
    };
    static const struct nl_80211_sub_ops sub_ops = {
        .phy_added_fn = osw_drv_nl80211_phy_added_cb,
        .phy_renamed_fn = osw_drv_nl80211_phy_renamed_cb,
        .phy_removed_fn = osw_drv_nl80211_phy_removed_cb,
        .vif_added_fn = osw_drv_nl80211_vif_added_cb,
        .vif_removed_fn = osw_drv_nl80211_vif_removed_cb,
        .sta_added_fn = osw_drv_nl80211_sta_added_cb,
        .sta_removed_fn = osw_drv_nl80211_sta_removed_cb,
        .priv_phy_size = sizeof(struct osw_drv_nl80211_phy_sub),
        .priv_vif_size = sizeof(struct osw_drv_nl80211_vif),
        .priv_sta_size = sizeof(struct osw_drv_nl80211_sta),
    };

    ds_tree_init(&m->stas, osw_drv_nl80211_sta_id_cmp, struct osw_drv_nl80211_sta, node);
    ds_tree_init(&m->phys_by_name, ds_str_cmp, struct osw_drv_nl80211_phy, node_by_name);
    ds_tree_init(&m->vifs_by_name, ds_str_cmp, struct osw_drv_nl80211_vif, node_by_name);
    ds_dlist_init(&m->hooks, struct osw_drv_nl80211_hook, node);
    rq_init(&m->q_request_config, EV_DEFAULT);

    m->drv_ops = drv_ops;
    m->mod_ops = mod_ops;
    m->q_request_config.max_running = 1;

    m->nl_conn = nl_conn_alloc();
    m->nl_conn_sub = nl_conn_subscription_alloc();
    m->nl_ev = nl_ev_alloc();
    m->nl_80211 = nl_80211_alloc();
    m->nl_80211_sub = nl_80211_alloc_sub(m->nl_80211, &sub_ops, m);

    nl_conn_subscription_set_event_fn(m->nl_conn_sub, osw_drv_nl80211_conn_event_cb, m);
    nl_conn_subscription_set_started_fn(m->nl_conn_sub, osw_drv_nl80211_conn_started_cb, m);
    nl_conn_subscription_set_stopped_fn(m->nl_conn_sub, osw_drv_nl80211_conn_stopped_cb, m);
    nl_conn_subscription_set_overrun_fn(m->nl_conn_sub, osw_drv_nl80211_conn_overrun_cb, m);
    nl_conn_subscription_start(m->nl_conn_sub, m->nl_conn);

    nl_ev_set_loop(m->nl_ev, EV_DEFAULT);
    nl_ev_set_conn(m->nl_ev, m->nl_conn);
    nl_80211_set_ready_fn(m->nl_80211, osw_drv_nl80211_nl_ready_cb, m);
    nl_80211_set_conn(m->nl_80211, m->nl_conn);

    m->rsno_supported = osw_drv_nl80211_guess_rsno_supported();
    LOGD(LOG_PREFIX("rsno_supported: %d", m->rsno_supported));

    /* FIXME: ADd _fini() and _stop() */
}

static void
osw_drv_nl80211_start(struct osw_drv_nl80211 *m)
{
    m->hostap = OSW_MODULE_LOAD(osw_hostap);
    nl_conn_start(m->nl_conn);
}

static bool
is_enabled(void)
{
    const bool skip = (osw_etc_get("OSW_DRV_NL80211_DISABLE") != NULL);
    const bool enabled = !skip;
    return enabled;
}

OSW_MODULE(osw_drv_nl80211)
{
    static struct osw_drv_nl80211 m;
    if (is_enabled()) {
        osw_drv_nl80211_init(&m);
        osw_drv_nl80211_start(&m);
    }
    return &m.mod_ops;
}
