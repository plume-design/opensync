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

/* libc */
#include <libgen.h>

/* 3rd party */
#include <ev.h>

/* opensync */
#include <const.h>
#include <log.h>
#include <os.h>
#include <memutil.h>
#include <ds_tree.h>
#include <ds_dlist.h>
#include <rq.h>
#include <rq_nested.h>
#include <hostap_ev_ctrl.h>
#include <hostap_sock.h>
#include <hostap_rq_task.h>
#include <util.h>

/* unit */
#include <osw_module.h>
#include <osw_hostap.h>
#include <osw_hostap_conf.h>

struct osw_hostap {
    struct ds_tree bsses;
    struct ds_dlist hooks;
    struct hostap_ev_ctrl ghapd;
    struct hostap_ev_ctrl gwpas;
    struct rq_nested q_config;
    struct ev_loop *loop;
};

struct osw_hostap_bss_hapd {
    struct hostap_ev_ctrl ctrl;

    struct osw_hostap_conf_ap_config conf;
    char path_config[4096];
    char path_psk_file[4096];

    struct rq_nested q_config;
    struct hostap_rq_task task_add;
    struct hostap_rq_task task_remove;
    struct hostap_rq_task task_log_level;
    struct hostap_rq_task task_reload_psk;
    //struct hostap_rq_task task_reload_rxkh;
    //struct hostap_rq_task task_wps_pbc;
    //struct hostap_rq_task task_wps_cancel;
    //struct rq_task task_reload_neigh; /* ADD/DEL_NEIGH */

    struct rq_nested q_state;
    struct hostap_rq_task task_get_config;
    struct hostap_rq_task task_get_status;
    struct hostap_rq_task task_get_mib;
    struct hostap_rq_task task_get_wps_status;
    //struct hostap_rq_task task_get_rxkhs;
    //struct hostap_rq_task task_get_neigh;

    /* FIXME: sta listing? hostap_sta supports it somewhat */
};

struct osw_hostap_bss_wpas {
    struct hostap_ev_ctrl ctrl;

    struct rq_nested q_config;
    //struct hostap_rq_task task_add;
    //struct hostap_rq_task task_remove;
    //struct hostap_rq_task task_reassociate;

    struct rq_nested q_state;
    struct hostap_rq_task task_get_status;
};

struct osw_hostap_bss {
    struct osw_hostap *owner;
    struct ds_tree_node node; /* keyed by vif_name */
    const struct osw_hostap_bss_ops *ops;
    void *ops_priv;
    char *phy_name;
    char *vif_name;
    struct rq_nested q_config;
    struct rq_nested q_state;
    struct osw_hostap_bss_hapd hapd;
    struct osw_hostap_bss_wpas wpas;
};

struct osw_hostap_hook {
    struct osw_hostap *owner;
    struct ds_dlist_node node;
    const struct osw_hostap_hook_ops *ops;
    void *ops_priv;
};

#define LOG_PREFIX(fmt, ...) \
    "osw: hostap: " fmt, \
    ##__VA_ARGS__

#define LOG_PREFIX_BSS(bss, fmt, ...) \
    LOG_PREFIX("bss: %s/%s: " fmt, \
    bss->phy_name, \
    bss->vif_name, \
    ##__VA_ARGS__)

#define LOG_PREFIX_PHY(phy, fmt, ...) \
    LOG_PREFIX("%s: " fmt, \
    phy, \
    ##__VA_ARGS__)

#define LOG_PREFIX_VIF(phy, vif, fmt, ...) \
    LOG_PREFIX_PHY(phy, "%s: " fmt, vif, ##__VA_ARGS__)

#define LOG_PREFIX_HAPD(hapd, fmt, ...) \
    LOG_PREFIX_BSS(container_of(hapd, struct osw_hostap_bss, hapd), "hapd: " fmt, ##__VA_ARGS__)

#define LOG_PREFIX_WPAS(wpas, fmt, ...) \
    LOG_PREFIX_BSS(container_of(wpas, struct osw_hostap_bss, wpas), "wpas: " fmt, ##__VA_ARGS__)

#define LOG_PREFIX_TASK(t, fmt, ...) \
    LOG_PREFIX("task: %s/%s: " fmt, \
    hostap_sock_get_path( \
    hostap_conn_get_sock( \
    hostap_txq_get_conn(t->txq))), \
    t->request ?: "", \
    ##__VA_ARGS__)

#define CALL_HOOKS(hostap, fn, ...) \
    do { \
        struct osw_hostap_hook *hook; \
        ds_dlist_foreach(&(hostap)->hooks, hook) \
            if (hook->ops != NULL && hook->ops->fn != NULL) \
                hook->ops->fn(hook, ##__VA_ARGS__, hook->ops_priv); \
    } while (0)

/* Some systems may want to use, eg. /var/run/hostapd-PHY/VIF */
#define OSW_HOSTAP_HAPD_PATH "/var/run/hostapd/VIF"
#define OSW_HOSTAP_WPAS_PATH "/var/run/wpa_supplicant/VIF"

static char *
str_replace_with(const char *str,
                 const char *from,
                 const char *to)
{
    const size_t from_len = strlen(from);
    const size_t to_len = strlen(to);
    const char *pos = str;
    const char *end = str + strlen(pos);
    char *out = NULL;
    size_t out_size = 1;
    size_t out_len = 0;

    for (;;) {
        const char *found = strstr(pos, from);
        const char *copy_until = found ? found : end;
        const size_t copy_len = (copy_until - pos);

        out_size += copy_len;
        if (found) out_size += to_len;
        out = REALLOC(out, out_size);
        memcpy(out + out_len, pos, copy_len);
        if (found) memcpy(out + out_len + copy_len, to, to_len);
        out_len = out_size - 1;

        if (found == NULL) break;

        pos = found + from_len;
    }

    if (out != NULL) {
        out[out_len] = 0;
    }

    return out;
}

/* path logic */
static char *
osw_hostap_get_tmpl_path(const char *phy_name,
                         const char *vif_name,
                         const char *default_template,
                         const char *runtime_template)
{
    const char *template = runtime_template ?: default_template;
    char *phy_replaced = str_replace_with(template, "PHY", phy_name);
    char *vif_replaced = str_replace_with(phy_replaced, "VIF", vif_name);
    FREE(phy_replaced);
    return vif_replaced;
}

static char *
osw_hostap_get_hapd_path(const char *phy_name,
                         const char *vif_name)
{
    return osw_hostap_get_tmpl_path(phy_name,
                                    vif_name,
                                    OSW_HOSTAP_HAPD_PATH,
                                    getenv("OSW_HOSTAP_HAPD_PATH"));
}

static char *
osw_hostap_get_wpas_path(const char *phy_name,
                         const char *vif_name)
{
    return osw_hostap_get_tmpl_path(phy_name,
                                    vif_name,
                                    OSW_HOSTAP_WPAS_PATH,
                                    getenv("OSW_HOSTAP_WPAS_PATH"));
}

static const char *
osw_hostap_get_global_hapd_path(void)
{
    const char *path = getenv("OSW_HOSTAP_GLOBAL_HAPD_PATH");
    if (path) return path;
    return "/var/run/hostapd/global";
}

static const char *
osw_hostap_get_global_wpas_path(void)
{
    const char *path = getenv("OSW_HOSTAP_GLOBAL_WPAS_PATH");
    if (path) return path;
    return "/var/run/wpa_supplicant/global";
}

/* helpers */
static void
osw_hostap_bss_cmd_warn_on_fail(struct rq_task *task,
                                void *priv)
{
    struct hostap_rq_task *t = container_of(task, struct hostap_rq_task, task);

    if (task->timed_out || task->cancel_timed_out) {
        LOGW(LOG_PREFIX_TASK(t, "timed out"));
    }
    else if (task->cancelled) {
        LOGI(LOG_PREFIX_TASK(t, "cancelled"));
    }
    else if (task->killed) {
        LOGI(LOG_PREFIX_TASK(t, "killed"));
    }
    else {
        if (t->reply == NULL) {
            /* This can happen when commands get cancelled
             * even before they get a chance to be started.
             */
            LOGD(LOG_PREFIX_TASK(t, "no reply"));
        }
        else if (strstr(t->reply, "FAIL") == t->reply) {
            LOGI(LOG_PREFIX_TASK(t, "failed"));
        }
        else {
            LOGD(LOG_PREFIX_TASK(t, "ok"));
        }
    }
}

/* hapd */
static void
osw_hostap_bss_hapd_flush_state_replies(struct osw_hostap_bss_hapd *hapd)
{
    hapd->task_get_config.reply = NULL;
    hapd->task_get_status.reply = NULL;
    hapd->task_get_mib.reply = NULL;
    hapd->task_get_wps_status.reply = NULL;
}

static void
osw_hostap_bss_hapd_prep_state_task(struct osw_hostap_bss_hapd *hapd)
{
    osw_hostap_bss_hapd_flush_state_replies(hapd);

    if (hostap_conn_is_opened(hapd->ctrl.conn) == false) return;

    struct rq *q = &hapd->q_state.q;
    rq_resume(q);
    rq_add_task(q, &hapd->task_get_config.task);
    rq_add_task(q, &hapd->task_get_status.task);
    rq_add_task(q, &hapd->task_get_mib.task);
    rq_add_task(q, &hapd->task_get_wps_status.task);
    rq_stop(q);
}

static void
osw_hostap_bss_hapd_fill_state(struct osw_hostap_bss_hapd *hapd,
                               struct osw_drv_vif_state *state)
{
    char *config = file_get(hapd->path_config);
    char *psk_file = file_get(hapd->path_psk_file);

    struct osw_hostap_conf_ap_state_bufs bufs = {
        .config = config ?: "",
        .wpa_psk_file = psk_file ?: "",
        .get_config = hapd->task_get_config.reply ?: "",
        .status = hapd->task_get_status.reply ?: "",
        .mib = hapd->task_get_mib.reply ?: "",
        .wps_get_status = hapd->task_get_wps_status.reply ?: "",
    };

    osw_hostap_conf_fill_ap_state(&bufs, state);

    FREE(config);
    FREE(psk_file);
}

static void
osw_hostap_bss_hapd_msg_cb(struct hostap_conn_ref *ref,
                           const void *msg,
                           size_t msg_len,
                           bool is_event,
                           void *priv)
{
    struct osw_hostap_bss_hapd *hapd = priv;
    struct osw_hostap_bss *bss = container_of(hapd, struct osw_hostap_bss, hapd);

    if (is_event == false) {
        return;
    }

    const char *skip = "<.>";
    const size_t skip_len = strlen(skip);
    if (msg_len >= skip_len) {
        msg += skip_len;
        msg_len -= skip_len;
    }

    if (bss->ops->event_fn != NULL) {
        bss->ops->event_fn(msg, msg_len, bss->ops_priv);
    }
}

static void
osw_hostap_bss_hapd_opened_cb(struct hostap_conn_ref *ref,
                              void *priv)
{
    struct osw_hostap_bss_hapd *hapd = priv;
    struct osw_hostap_bss *bss = container_of(hapd, struct osw_hostap_bss, hapd);
    const struct osw_hostap_bss_ops *ops = bss->ops;
    void *ops_priv = bss->ops_priv;

    if (ops->bss_changed_fn != NULL) {
        ops->bss_changed_fn(ops_priv);
    }
}

static void
osw_hostap_bss_hapd_closed_cb(struct hostap_conn_ref *ref,
                              void *priv)
{
    struct osw_hostap_bss_hapd *hapd = priv;
    struct osw_hostap_bss *bss = container_of(hapd, struct osw_hostap_bss, hapd);
    const struct osw_hostap_bss_ops *ops = bss->ops;
    void *ops_priv = bss->ops_priv;

    if (ops->bss_changed_fn != NULL) {
        ops->bss_changed_fn(ops_priv);
    }
}

static void
osw_hostap_bss_hapd_config_complete_cb(struct rq_task *task,
                                       void *priv)
{
    struct osw_hostap_bss_hapd *hapd = priv;
    struct osw_hostap_bss *bss = container_of(hapd, struct osw_hostap_bss, hapd);
    const struct osw_hostap_bss_ops *ops = bss->ops;
    void *ops_priv = bss->ops_priv;

    LOGD(LOG_PREFIX_HAPD(hapd, "config complete"));

    if (ops->config_applied_fn != NULL) {
        ops->config_applied_fn(ops_priv);
    }
}

static void
osw_hostap_bss_hapd_sta_connected_cb(struct hostap_sta_ref *ref,
                                     const struct hostap_sta_info *info,
                                     void *priv)
{
    struct osw_hostap_bss_hapd *hapd = priv;
    struct osw_hostap_bss *bss = container_of(hapd, struct osw_hostap_bss, hapd);
    const struct osw_hostap_bss_ops *ops = bss->ops;
    void *ops_priv = bss->ops_priv;
    struct osw_hostap_bss_sta sta;
    const size_t addr_len = sizeof(sta.addr.octet);

    MEMZERO(sta);
    memcpy(sta.addr.octet, info->addr.addr, addr_len);
    /* FIXME key_id */

    if (ops->sta_connected_fn != NULL) {
        ops->sta_connected_fn(&sta, ops_priv);
    }
}

static void
osw_hostap_bss_hapd_sta_changed_cb(struct hostap_sta_ref *ref,
                                   const struct hostap_sta_info *info,
                                   const char *old_buf,
                                   void *priv)
{
    struct osw_hostap_bss_hapd *hapd = priv;
    struct osw_hostap_bss *bss = container_of(hapd, struct osw_hostap_bss, hapd);
    const struct osw_hostap_bss_ops *ops = bss->ops;
    void *ops_priv = bss->ops_priv;
    struct osw_hostap_bss_sta sta;
    const size_t addr_len = sizeof(sta.addr.octet);

    MEMZERO(sta);
    memcpy(sta.addr.octet, info->addr.addr, addr_len);
    /* FIXME key_id */

    if (ops->sta_changed_fn != NULL) {
        ops->sta_changed_fn(&sta, ops_priv);
    }
}

static void
osw_hostap_bss_hapd_sta_disconnected_cb(struct hostap_sta_ref *ref,
                                        const struct hostap_sta_info *info,
                                        void *priv)
{
    struct osw_hostap_bss_hapd *hapd = priv;
    struct osw_hostap_bss *bss = container_of(hapd, struct osw_hostap_bss, hapd);
    const struct osw_hostap_bss_ops *ops = bss->ops;
    void *ops_priv = bss->ops_priv;
    struct osw_hostap_bss_sta sta;
    const size_t addr_len = sizeof(sta.addr.octet);

    MEMZERO(sta);
    memcpy(sta.addr.octet, info->addr.addr, addr_len);

    if (ops->sta_disconnected_fn != NULL) {
        ops->sta_disconnected_fn(&sta, ops_priv);
    }
}

static void
osw_hostap_bss_hapd_init(struct hostap_ev_ctrl *ghapd,
                         struct osw_hostap_bss_hapd *hapd,
                         const char *phy_name,
                         const char *vif_name,
                         struct ev_loop *loop)
{
    static const struct hostap_conn_ref_ops hapd_ops = {
        .msg_fn = osw_hostap_bss_hapd_msg_cb,
        .opened_fn = osw_hostap_bss_hapd_opened_cb,
        .closed_fn = osw_hostap_bss_hapd_closed_cb,
    };
    static const struct hostap_sta_ops sta_ops = {
        .connected_fn = osw_hostap_bss_hapd_sta_connected_cb,
        .changed_fn = osw_hostap_bss_hapd_sta_changed_cb,
        .disconnected_fn = osw_hostap_bss_hapd_sta_disconnected_cb,
    };

    char *path = osw_hostap_get_hapd_path(phy_name, vif_name);
    hostap_ev_ctrl_init(&hapd->ctrl, loop, path, &hapd_ops, &sta_ops, hapd);
    FREE(path);

    rq_nested_init(&hapd->q_config, loop);
    rq_nested_init(&hapd->q_state, loop);
    hapd->q_config.q.max_running = 1;
    hapd->q_state.q.max_running = 1;
    hapd->q_config.task.completed_fn = osw_hostap_bss_hapd_config_complete_cb;
    hapd->q_config.task.priv = hapd;

    char cmd_add[8192];
    char cmd_remove[1024];

    /* FIXME: This should be probably selectable to group,
     * or not to group, VIFs per PHY. Grouping is required
     * for CSA to work out-of-the-box for hostapd, but some
     * vendor drivers don't really need it, or care about
     * it.
     */
    const char *phy_group = vif_name;

    snprintf(hapd->path_config, sizeof(hapd->path_config),
             "/var/run/hostapd-%s.config",
             vif_name);

    snprintf(hapd->path_psk_file, sizeof(hapd->path_config),
             "/var/run/hostapd-%s.pskfile",
             vif_name);

    snprintf(cmd_add, sizeof(cmd_add),
             "ADD bss_config=%s:%s",
             phy_group,
             hapd->path_config);

    snprintf(cmd_remove, sizeof(cmd_remove),
             "REMOVE %s",
             vif_name);

    hostap_rq_task_init(&hapd->task_add, ghapd->txq, cmd_add);
    hostap_rq_task_init(&hapd->task_remove, ghapd->txq, cmd_remove);
    hostap_rq_task_init(&hapd->task_log_level, hapd->ctrl.txq, "LOG_LEVEL DEBUG");
    hostap_rq_task_init(&hapd->task_reload_psk, hapd->ctrl.txq, "RELOAD_WPA_PSK");
    hostap_rq_task_init(&hapd->task_get_config, hapd->ctrl.txq, "GET_CONFIG");
    hostap_rq_task_init(&hapd->task_get_status, hapd->ctrl.txq, "STATUS");
    hostap_rq_task_init(&hapd->task_get_mib, hapd->ctrl.txq, "MIB");
    hostap_rq_task_init(&hapd->task_get_wps_status, hapd->ctrl.txq, "WPS_GET_STATUS");

    hapd->task_add.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_remove.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_log_level.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_reload_psk.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_get_config.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_get_status.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_get_mib.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_get_wps_status.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
}

static void
osw_hostap_bss_hapd_fini(struct osw_hostap_bss_hapd *hapd)
{
    rq_task_kill(&hapd->q_config.task);
    rq_task_kill(&hapd->q_state.task);

    hostap_rq_task_fini(&hapd->task_add);
    hostap_rq_task_fini(&hapd->task_remove);
    hostap_rq_task_fini(&hapd->task_log_level);
    hostap_rq_task_fini(&hapd->task_reload_psk);
    hostap_rq_task_fini(&hapd->task_get_config);
    hostap_rq_task_fini(&hapd->task_get_status);
    hostap_rq_task_fini(&hapd->task_get_mib);
    hostap_rq_task_fini(&hapd->task_get_wps_status);
}

/* wpas */
static void
osw_hostap_bss_wpas_flush_state_replies(struct osw_hostap_bss_wpas *wpas)
{
    wpas->task_get_status.reply = NULL;
}

static void
osw_hostap_bss_wpas_prep_state_task(struct osw_hostap_bss_wpas *wpas)
{
    osw_hostap_bss_wpas_flush_state_replies(wpas);

    if (hostap_conn_is_opened(wpas->ctrl.conn) == false) return;

    struct rq *q = &wpas->q_state.q;
    rq_resume(q);
    rq_add_task(q, &wpas->task_get_status.task);
    rq_stop(q);
}

static void
osw_hostap_bss_wpas_fill_state(struct osw_hostap_bss_wpas *wpas,
                               struct osw_drv_vif_state *state)
{
    if (wpas->task_get_status.reply != NULL) {
        // FIXME
    }
}

static void
osw_hostap_bss_wpas_msg_cb(struct hostap_conn_ref *ref,
                           const void *msg,
                           size_t msg_len,
                           bool is_event,
                           void *priv)
{
    struct osw_hostap_bss_wpas *wpas = priv;
    struct osw_hostap_bss *bss = container_of(wpas, struct osw_hostap_bss, wpas);

    if (is_event == false) {
        return;
    }

    const char *skip = "<.>";
    const size_t skip_len = strlen(skip);
    if (msg_len >= skip_len) {
        msg += skip_len;
        msg_len -= skip_len;
    }

    if (bss->ops->event_fn != NULL) {
        bss->ops->event_fn(msg, msg_len, bss->ops_priv);
    }
}

static void
osw_hostap_bss_wpas_opened_cb(struct hostap_conn_ref *ref,
                              void *priv)
{
    struct osw_hostap_bss_wpas *wpas = priv;
    (void)wpas;
    // FIXME
}

static void
osw_hostap_bss_wpas_closed_cb(struct hostap_conn_ref *ref,
                              void *priv)
{
    struct osw_hostap_bss_wpas *wpas = priv;
    (void)wpas;
    // FIXME
}

static void
osw_hostap_bss_wpas_init(struct hostap_ev_ctrl *gwpas,
                         struct osw_hostap_bss_wpas *wpas,
                         const char *phy_name,
                         const char *vif_name,
                         struct ev_loop *loop)
{
    static const struct hostap_conn_ref_ops wpas_ops = {
        .msg_fn = osw_hostap_bss_wpas_msg_cb,
        .opened_fn = osw_hostap_bss_wpas_opened_cb,
        .closed_fn = osw_hostap_bss_wpas_closed_cb,
    };

    char *path = osw_hostap_get_wpas_path(phy_name, vif_name);
    hostap_ev_ctrl_init(&wpas->ctrl, loop, path, &wpas_ops, NULL, wpas);
    FREE(path);

    rq_nested_init(&wpas->q_config, loop);
    rq_nested_init(&wpas->q_state, loop);
    wpas->q_config.q.max_running = 1;
    wpas->q_state.q.max_running = 1;
    hostap_rq_task_init(&wpas->task_get_status, wpas->ctrl.txq, "STATUS");

    wpas->task_get_status.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
}

static void
osw_hostap_bss_wpas_fini(struct osw_hostap_bss_wpas *wpas)
{
    hostap_rq_task_fini(&wpas->task_get_status);
}

/* bss */
struct osw_hostap_bss *
osw_hostap_bss_alloc(struct osw_hostap *hostap,
                     const char *phy_name,
                     const char *vif_name,
                     const struct osw_hostap_bss_ops *ops,
                     void *ops_priv)
{
    struct hostap_ev_ctrl *ghapd = &hostap->ghapd;
    struct hostap_ev_ctrl *gwpas = &hostap->gwpas;
    struct ev_loop *loop = hostap->loop;
    struct osw_hostap_bss *bss = CALLOC(1, sizeof(*bss));
    struct ds_tree *bsses = &hostap->bsses;

    const bool already_added = ds_tree_find(bsses, vif_name);
    if (WARN_ON(already_added)) return NULL;

    bss->owner = hostap;
    bss->phy_name = STRDUP(phy_name);
    bss->vif_name = STRDUP(vif_name);
    bss->ops = ops;
    bss->ops_priv = ops_priv;
    rq_nested_init(&bss->q_config, loop);
    rq_nested_init(&bss->q_state, loop);
    bss->q_config.q.max_running = 1;
    bss->q_state.q.max_running = 1;
    osw_hostap_bss_hapd_init(ghapd, &bss->hapd, phy_name, vif_name, loop);
    osw_hostap_bss_wpas_init(gwpas, &bss->wpas, phy_name, vif_name, loop);
    ds_tree_insert(bsses, bss, bss->vif_name);

    LOGD(LOG_PREFIX_BSS(bss, "added"));

    return bss;
}

void
osw_hostap_bss_free(struct osw_hostap_bss *bss)
{
    if (bss == NULL) return;
    if (bss->owner == NULL) return;

    LOGD(LOG_PREFIX_BSS(bss, "removed"));

    struct ds_tree *bsses = &bss->owner->bsses;
    bss->owner = NULL;

    osw_hostap_bss_hapd_fini(&bss->hapd);
    osw_hostap_bss_wpas_fini(&bss->wpas);
    ds_tree_remove(bsses, bss);
    FREE(bss->phy_name);
    FREE(bss->vif_name);
    FREE(bss);
}

void
osw_hostap_bss_fill_state(struct osw_hostap_bss *bss,
                          struct osw_drv_vif_state *state)
{
    switch (state->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            osw_hostap_bss_hapd_fill_state(&bss->hapd, state);
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            osw_hostap_bss_wpas_fill_state(&bss->wpas, state);
            break;
    }
}

struct rq_task *
osw_hostap_bss_prep_state_task(struct osw_hostap_bss *bss)
{
    struct rq_task *t = &bss->q_state.task;
    struct rq *q = &bss->q_state.q;

    rq_task_kill(t);
    osw_hostap_bss_hapd_prep_state_task(&bss->hapd);
    osw_hostap_bss_wpas_prep_state_task(&bss->wpas);
    rq_resume(q);
    rq_add_task(q, &bss->hapd.q_state.task);
    rq_add_task(q, &bss->wpas.q_state.task);
    rq_stop(q);

    return t;
}

static void
osw_hostap_set_conf_ap_write(const struct osw_hostap_bss_hapd *hapd)
{
    const struct osw_hostap_conf_ap_config *conf = &hapd->conf;

    /* FIXME: This could be staged to allow differentiating
     * old/new/pending configs.
     */

    file_put(hapd->path_config, conf->conf_buf);
    file_put(hapd->path_psk_file, conf->psks_buf);
    /* FIXME: rxkh */
}

static struct rq_task *
osw_hostap_set_conf_ap(struct osw_hostap *hostap,
                       struct osw_drv_conf *drv_conf,
                       struct osw_drv_phy_config *dphy,
                       struct osw_drv_vif_config *dvif,
                       const char *phy_name,
                       const char *vif_name,
                       struct osw_hostap_bss_hapd *hapd)
{
    struct rq *q = &hapd->q_config.q;
    struct rq_task *t = &hapd->q_config.task;

    rq_task_kill(t);

    struct osw_hostap_conf_ap_config *conf = &hapd->conf;
    const bool ok = osw_hostap_conf_fill_ap_config(drv_conf,
                                                   phy_name,
                                                   vif_name,
                                                   conf);
    const bool failed = !ok;
    WARN_ON(failed);


    OSW_HOSTAP_CONF_SET_BUF(conf->wpa_psk_file, hapd->path_psk_file);
    /* FIXME: rxkh */

    const struct hostap_sock *sock = hostap_conn_get_sock(hapd->ctrl.conn);
    const char *path_sock = hostap_sock_get_path(sock);
    WARN_ON(path_sock == NULL);
    char *path_ctrl = STRDUP(path_sock ?: "");
    dirname(path_ctrl);
    OSW_HOSTAP_CONF_SET_BUF(conf->ctrl_interface, path_ctrl);
    FREE(path_ctrl);

    CALL_HOOKS(hostap, ap_conf_mutate_fn, phy_name, vif_name, drv_conf, conf);

    osw_hostap_conf_generate_ap_config_bufs(conf);
    osw_hostap_set_conf_ap_write(hapd);

    /* FIXME: This could/should rely on hostapd
     * file content comparison, probably at least
     * partially.
     */

    const bool is_running = hostap_conn_is_opened(hapd->ctrl.conn);
    const bool want_ap = (dvif->vif_type == OSW_VIF_AP);
    const bool want_running = dvif->enabled && want_ap;
    const bool invalidated = dvif->enabled_changed
                          || (dvif->u.ap.channel_changed == true &&
                              dvif->u.ap.csa_required == false)
                          || dvif->u.ap.bridge_if_name_changed
                          || dvif->u.ap.beacon_interval_tu_changed
                          || dvif->u.ap.ssid_hidden_changed
                          || dvif->u.ap.isolated_changed
                          || dvif->u.ap.mcast2ucast_changed
                          || dvif->u.ap.mode_changed
                          || dvif->u.ap.ssid_changed
                          || dvif->u.ap.wpa_changed
                          || dphy->reg_domain_changed;

    const bool do_remove = (is_running && !want_running)
                        || (is_running && want_running && invalidated);
    const bool do_add = (!is_running && want_running)
                     || (is_running && want_running && invalidated);
    const bool do_reload_psk = (want_running && dvif->u.ap.psk_list_changed);

    rq_resume(q);

    if (do_remove) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do remove"));
        rq_add_task(q, &hapd->task_remove.task);
    }

    if (do_add) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do add"));
        rq_add_task(q, &hapd->task_add.task);
        rq_add_task(q, &hapd->task_log_level.task);
    }

    if (do_reload_psk) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do reload psk"));
        rq_add_task(q, &hapd->task_reload_psk.task);
    }

    rq_stop(q);
    return t;
}

static struct rq_task *
osw_hostap_set_conf_sta(struct osw_hostap *hostap,
                       struct osw_drv_conf *drv_conf,
                       struct osw_drv_phy_config *dphy,
                       struct osw_drv_vif_config *dvif,
                       const char *phy_name,
                       const char *vif_name,
                       struct osw_hostap_bss_wpas *wpas)
{
    struct rq *q = &wpas->q_config.q;
    struct rq_task *t = &wpas->q_config.task;

    rq_task_kill(t);

    LOGD(LOG_PREFIX_WPAS(wpas, "nop; fixme"));
    (void)q;
    /* FIXME: generate config, write config, apply config */

    rq_stop(q);
    return t;
}

static void
osw_hostap_bss_config_complete_cb(struct rq_task *task,
                                  void *priv)
{
    struct osw_hostap_bss *bss = priv;
    const struct osw_hostap_bss_ops *ops = bss->ops;
    void *ops_priv = bss->ops_priv;

    LOGD(LOG_PREFIX_BSS(bss, "config complete"));

    if (ops->bss_changed_fn != NULL) {
        ops->bss_changed_fn(ops_priv);
    }
}

static struct rq_task *
osw_hostap_set_conf_bss(struct osw_hostap *hostap,
                        struct osw_drv_conf *drv_conf,
                        struct osw_drv_phy_config *dphy,
                        struct osw_drv_vif_config *dvif)
{
    struct ds_tree *bsses = &hostap->bsses;
    const char *phy_name = dphy->phy_name;
    const char *vif_name = dvif->vif_name;

    LOGD(LOG_PREFIX_VIF(phy_name, vif_name, "maybe configuring"));

    struct osw_hostap_bss *bss = ds_tree_find(bsses, vif_name);
    if (bss == NULL) return NULL;

    LOGD(LOG_PREFIX_BSS(bss, "configuring"));

    struct rq *q = &bss->q_config.q;
    struct rq_task *t = &bss->q_config.task;

    rq_task_kill(t);

    t->completed_fn = osw_hostap_bss_config_complete_cb;
    t->priv = bss;

    /* All variants are applied because, in theory, vif_type
     * can change meaning wpas/hapd roles swap and _both_
     * need to apply some configs.
     */

    struct rq_task *sta_t = osw_hostap_set_conf_sta(hostap,
                                                    drv_conf,
                                                    dphy,
                                                    dvif,
                                                    phy_name,
                                                    vif_name,
                                                    &bss->wpas);

    struct rq_task *ap_t = osw_hostap_set_conf_ap(hostap,
                                                  drv_conf,
                                                  dphy,
                                                  dvif,
                                                  phy_name,
                                                  vif_name,
                                                  &bss->hapd);
    rq_resume(q);
    rq_add_task(q, sta_t);
    rq_add_task(q, ap_t);
    rq_stop(q);
    return t;
}

struct rq_task *
osw_hostap_set_conf(struct osw_hostap *hostap,
                    struct osw_drv_conf *drv_conf)
{
    struct rq *q = &hostap->q_config.q;
    struct rq_task *t = &hostap->q_config.task;

    rq_task_kill(t);
    rq_resume(q);

    size_t i;
    for (i = 0; i < drv_conf->n_phy_list; i++) {
        struct osw_drv_phy_config *dphy = &drv_conf->phy_list[i];
        size_t j;
        for (j = 0; j < dphy->vif_list.count; j++) {
            struct osw_drv_vif_config *dvif = &dphy->vif_list.list[j];
            struct rq_task *bss_t = osw_hostap_set_conf_bss(hostap,
                                                            drv_conf,
                                                            dphy,
                                                            dvif);
            if (bss_t != NULL) {
                rq_add_task(q, bss_t);
            }
        }
    }

    rq_stop(q);
    return t;
}

static void
osw_hostap_hook_attach(struct osw_hostap_hook *hook,
                       struct osw_hostap *hostap)
{
    assert(hook->owner == NULL);
    hook->owner = hostap;
    ds_dlist_insert_tail(&hostap->hooks, hook);
}

static void
osw_hostap_hook_detach(struct osw_hostap_hook *hook)
{
    if (hook->owner == NULL) return;
    ds_dlist_remove(&hook->owner->hooks, hook);
    hook->owner = NULL;
}

struct osw_hostap_hook *
osw_hostap_hook_alloc(struct osw_hostap *hostap,
                      const struct osw_hostap_hook_ops *ops,
                      void *ops_priv)
{
    if (hostap == NULL) return NULL;

    struct osw_hostap_hook *hook = CALLOC(1, sizeof(*hook));
    hook->ops = ops;
    hook->ops_priv = ops_priv;
    osw_hostap_hook_attach(hook, hostap);
    return hook;
}

void
osw_hostap_hook_free(struct osw_hostap_hook *hook)
{
    if (hook == NULL) return;
    osw_hostap_hook_detach(hook);
    FREE(hook);
}

static void
osw_hostap_init(struct osw_hostap *m)
{
    ds_tree_init(&m->bsses, ds_str_cmp, struct osw_hostap_bss, node);
    ds_dlist_init(&m->hooks, struct osw_hostap_hook, node);
    m->loop = EV_DEFAULT; /* FIXME: hardcode bad */
    rq_nested_init(&m->q_config, m->loop);
}

static void
osw_hostap_bss_ghapd_opened_cb(struct hostap_conn_ref *ref,
                               void *priv)
{
    struct osw_hostap *m = priv;
    (void)m;
    LOGI(LOG_PREFIX("global hostap: opened"));
}

static void
osw_hostap_bss_ghapd_closed_cb(struct hostap_conn_ref *ref,
                               void *priv)
{
    struct osw_hostap *m = priv;
    (void)m;
    LOGI(LOG_PREFIX("global hostap: closed"));
}

static void
osw_hostap_bss_gwpas_opened_cb(struct hostap_conn_ref *ref,
                               void *priv)
{
    struct osw_hostap *m = priv;
    (void)m;
    LOGI(LOG_PREFIX("global wpa_supplicant: opened"));
}

static void
osw_hostap_bss_gwpas_closed_cb(struct hostap_conn_ref *ref,
                               void *priv)
{
    struct osw_hostap *m = priv;
    (void)m;
    LOGI(LOG_PREFIX("global wpa_supplicant: closed"));
}

static void
osw_hostap_start(struct osw_hostap *m)
{
    const char *path_hapd = osw_hostap_get_global_hapd_path();
    const char *path_wpas = osw_hostap_get_global_wpas_path();

    static const struct hostap_conn_ref_ops ghapd_ops = {
        .opened_fn = osw_hostap_bss_ghapd_opened_cb,
        .closed_fn = osw_hostap_bss_ghapd_closed_cb,
    };
    static const struct hostap_conn_ref_ops gwpas_ops = {
        .opened_fn = osw_hostap_bss_gwpas_opened_cb,
        .closed_fn = osw_hostap_bss_gwpas_closed_cb,
    };

    hostap_ev_ctrl_init(&m->ghapd, m->loop, path_hapd, &ghapd_ops, NULL, m);
    hostap_ev_ctrl_init(&m->gwpas, m->loop, path_wpas, &gwpas_ops, NULL, m);
}

OSW_MODULE(osw_hostap)
{
    struct osw_hostap *m = CALLOC(1, sizeof(*m));
    osw_hostap_init(m);
    osw_hostap_start(m);
    return m;
}
