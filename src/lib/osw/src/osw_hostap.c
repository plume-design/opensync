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
#include <hostap_conn.h>
#include <hostap_rq_task.h>
#include <util.h>

/* unit */
#include <osw_module.h>
#include <osw_ut.h>
#include <osw_hostap.h>
#include <osw_hostap_conf.h>
#include <osw_wpas_conf.h> // FIXME
#include <osw_etc.h>

struct osw_hostap {
    struct ds_tree bsses;
    struct ds_dlist hooks;
    struct hostap_ev_ctrl ghapd;
    struct hostap_ev_ctrl gwpas;
    struct rq_nested q_config;
    struct ev_loop *loop;
};

struct osw_hostap_bss_hapd_sta {
    struct osw_hwaddr addr;
    struct ds_tree_node node;
    struct hostap_rq_task task_deauth_no_tx;
    struct hostap_rq_task task_deauth;
};

struct osw_hostap_bss_hapd {
    struct hostap_ev_ctrl ctrl;

    struct osw_hostap_conf_ap_config conf;
    char path_config[4096];
    char path_psk_file[4096];
    char path_rxkh[4096];

    struct ds_tree stas;

    struct rq_nested q_config;
    struct hostap_rq_task task_add;
    struct hostap_rq_task task_remove;
    struct hostap_rq_task task_log_level;
    struct hostap_rq_task task_reload_psk;
    struct hostap_rq_task task_init_bssid;
    struct hostap_rq_task task_init_neigh;
    struct hostap_rq_task *task_neigh_add;
    struct hostap_rq_task *task_neigh_mod;
    struct hostap_rq_task *task_neigh_del;
    struct hostap_rq_task *task_neigh_fill;
    size_t n_task_neigh_add;
    size_t n_task_neigh_mod;
    size_t n_task_neigh_del;
    size_t n_task_neigh_fill;
    struct hostap_rq_task task_reload_rxkh;
    struct hostap_rq_task task_wps_pbc;
    struct hostap_rq_task task_wps_cancel;

    struct rq_nested q_state;
    struct hostap_rq_task task_get_config;
    struct hostap_rq_task task_get_status;
    struct hostap_rq_task task_get_mib;
    struct hostap_rq_task task_get_wps_status;
    struct hostap_rq_task task_get_neigh;
    struct hostap_rq_task task_get_rxkhs;
    bool   csa_by_hostap;
    struct hostap_rq_task task_csa;

    bool   group_by_phy;
    struct hostap_rq_task task_clear_accept_acl;
    struct hostap_rq_task task_clear_deny_acl;
    struct hostap_rq_task task_set_accept_acl_policy;
    struct hostap_rq_task task_set_deny_acl_policy;
    struct hostap_rq_task *task_acl_mac;
    struct hostap_rq_task *task_acl_add_mac;
    struct hostap_rq_task *task_acl_del_mac;
    struct hostap_rq_task task_get_accept_acl_list;
    struct hostap_rq_task task_get_deny_acl_list;
    bool   acl_by_hostap;
    size_t n_task_acl_mac;
    size_t n_task_acl_add_mac;
    size_t n_task_acl_del_mac;

    /* FIXME: sta listing? hostap_sta supports it somewhat */
};

struct osw_hostap_bss_wpas {
    struct hostap_ev_ctrl ctrl;

    struct osw_hostap_conf_sta_config conf;
    char path_config[4096];

    /* When reconfiguring networks the network intended to
     * be remain to be used may be on the new list, but the
     * config file may end up being re-generated in a way,
     * where retrieving the psk may no longer be guaranteed
     * to be correct or possible.
     *
     * The code could issue a reassoc command to wpa_s
     * whenever config file contents change, but that would
     * have a side-effect of disconnect+connect of the link
     * causing unnecessary delays to onboarding.
     */
    struct osw_psk psk;
    bool psk_valid;

    struct osw_ifname bridge_if_name;
    struct osw_ifname bridge_if_name_pending;

    struct rq_nested q_config;
    struct hostap_rq_task task_add;
    struct hostap_rq_task task_remove;
    struct hostap_rq_task task_log_level;
    struct hostap_rq_task task_reconfigure;
    struct hostap_rq_task task_reassociate;
    struct hostap_rq_task task_disconnect;

    struct rq_nested q_state;
    struct hostap_rq_task task_get_status;
    struct hostap_rq_task task_get_networks;
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
    LOG_PREFIX("task: %s: %s: " fmt, \
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
#define OSW_HOSTAP_CS_COUNT 15

/* FIXME: This should probably be moved to osw_hostap_conf
 * since it's involved in translating to/from OSW,
 * especially since its responsible for assembling key id
 * strings.
 */
static enum osw_akm
osw_hostap_akm_selector_to_osw(const char *s)
{
    const uint32_t suite = osw_suite_from_dash_str(s);
    const enum osw_akm akm = osw_suite_into_akm(suite);
    return akm;
}

static enum osw_cipher
osw_hostap_cipher_selector_to_osw(const char *s)
{
    const uint32_t suite = osw_suite_from_dash_str(s);
    const enum osw_cipher cipher = osw_suite_into_cipher(suite);
    return cipher;
}

static void
osw_hostap_bss_sta_parse(const char *buf,
                         struct osw_hostap_bss_sta *sta)
{
    if (buf == NULL) return;
    if (WARN_ON(sta == NULL)) return;

    char *copy = STRDUP(buf);
    char *lines = copy;
    char *addr;
    char *line;

    addr = strsep(&lines, "\n");
    WARN_ON(addr == NULL);
    if (addr != NULL) {
        const bool ok = osw_hwaddr_from_cstr(addr, &sta->addr);
        const bool failed = (ok == false);
        WARN_ON(failed);
    }

    while (lines != NULL && (line = strsep(&lines, "\n")) != NULL) {
        const char *k = strsep(&line, "=");
        if (k == NULL) continue;

        const char *v = strsep(&line, "\n");
        if (v == NULL) continue;

        if (strcmp(k, "flags") == 0) {
            sta->authenticated = (strstr(v, "[AUTH]") != NULL);
            sta->associated = (strstr(v, "[ASSOC]") != NULL);
            sta->authorized = (strstr(v, "[AUTHORIZED]") != NULL);
            sta->pmf = (strstr(v, "[MFP]") != NULL);
        }
        if (strcmp(k, "keyid") == 0) {
            const char *key0 = "key";
            const char *prefix = "key-";

            if (strcmp(v, key0) == 0) {
                sta->key_id_known = true;
                sta->key_id = 0;
            }
            else if (strstr(v, prefix) == v) {
                sta->key_id_known = true;
                sta->key_id = atoi(v + strlen(prefix));
            }
        }
        else if (strcmp(k, "AKMSuiteSelector") == 0) {
            sta->akm = osw_hostap_akm_selector_to_osw(v);
        }
        else if (strcmp(k, "dot11RSNAStatsSelectedPairwiseCipher") == 0) {
            sta->pairwise_cipher = osw_hostap_cipher_selector_to_osw(v);
        }
        else if (strcmp(k, "assoc_ies") == 0) {
            if (sta->assoc_ies_len)
                FREE(sta->assoc_ies);

            sta->assoc_ies_len = strlen(v)/2;
            sta->assoc_ies = MALLOC(sizeof(*sta->assoc_ies) * sta->assoc_ies_len);

            const bool decoded = (hex2bin(v, strlen(v), sta->assoc_ies, sta->assoc_ies_len) != -1);

            if (!decoded)
            {
                sta->assoc_ies_len = 0;
                FREE(sta->assoc_ies);
            }
        }
    }

    FREE(copy);
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
                                    osw_etc_get("OSW_HOSTAP_HAPD_PATH"));
}

static char *
osw_hostap_get_wpas_path(const char *phy_name,
                         const char *vif_name)
{
    return osw_hostap_get_tmpl_path(phy_name,
                                    vif_name,
                                    OSW_HOSTAP_WPAS_PATH,
                                    osw_etc_get("OSW_HOSTAP_WPAS_PATH"));
}

static const char *
osw_hostap_get_global_hapd_path(void)
{
    const char *path = osw_etc_get("OSW_HOSTAP_GLOBAL_HAPD_PATH");
    if (path) return path;
    return "/var/run/hostapd/global";
}

static const char *
osw_hostap_get_global_wpas_path(void)
{
    const char *path = osw_etc_get("OSW_HOSTAP_GLOBAL_WPAS_PATH");
    if (path) return path;
    return "/var/run/wpa_supplicant/global";
}

/* helpers */
static void
osw_hostap_add_task_array(struct rq *q,
                          struct hostap_rq_task *tasks,
                          size_t n_tasks)
{
    size_t i;
    for (i = 0; i < n_tasks; i++) {
        rq_add_task(q, &tasks[i].task);
    }
}

static void
osw_hostap_fini_task_array(struct hostap_rq_task *tasks,
                           size_t n_tasks)
{
    if (tasks == NULL) return;
    while (n_tasks > 0) {
        hostap_rq_task_fini(tasks);
        tasks++;
        n_tasks--;
    }
}

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

static void
osw_hostap_bss_cmd_warn_on_err(struct rq_task *task,
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
            LOGD(LOG_PREFIX_TASK(t, "failed"));
        }
        else {
            LOGD(LOG_PREFIX_TASK(t, "ok"));
        }
    }
}

static void
osw_hostap_bss_cmd_reconfigure_done(struct rq_task *task,
                                    void *priv)
{
    struct hostap_rq_task *t = container_of(task, struct hostap_rq_task, task);
    struct osw_hostap_bss_wpas *wpas = priv;

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
            return;
        }
    }

    /* If reconfigure failed it means that whatever was in
     * the config file did _not_ apply. This needs to be
     * reflected in subsequent state reports until this
     * resolves itself.
     */
    WARN_ON(file_put(wpas->path_config, "") != 0);
}

static void
osw_hostap_bss_remove_done_cb(struct rq_task *task,
                              void *priv)
{
    osw_hostap_bss_cmd_warn_on_fail(task, priv);

    struct hostap_rq_task *t = container_of(task, struct hostap_rq_task, task);
    if (strstr(t->reply ?: "", "OK") == t->reply) {
        hostap_conn_reset(priv);
    }
}

/* hapd */
static void
osw_hostap_bss_hapd_update_add_task(struct osw_hostap_bss_hapd *hapd,
                                    struct osw_hostap *hostap,
                                    const char *phy_name,
                                    const char *vif_name)
{
    char cmd_add[8192];
    const char *phy_group = hapd->group_by_phy ? phy_name : vif_name;

    snprintf(cmd_add, sizeof(cmd_add),
             "ADD bss_config=%s:%s",
             phy_group,
             hapd->path_config);
    hostap_rq_task_fini(&hapd->task_add);
    hostap_rq_task_init(&hapd->task_add, hostap->ghapd.txq, cmd_add);
}

static void
osw_hostap_bss_hapd_neigh_fill_free(struct osw_hostap_bss_hapd *hapd)
{
    osw_hostap_fini_task_array(hapd->task_neigh_fill, hapd->n_task_neigh_fill);
    FREE(hapd->task_neigh_fill);
    hapd->task_neigh_fill = NULL;
    hapd->n_task_neigh_fill = 0;
}

static void
osw_hostap_bss_hapd_neigh_free(struct osw_hostap_bss_hapd *hapd)
{
    osw_hostap_fini_task_array(hapd->task_neigh_add, hapd->n_task_neigh_add);
    osw_hostap_fini_task_array(hapd->task_neigh_mod, hapd->n_task_neigh_mod);
    osw_hostap_fini_task_array(hapd->task_neigh_del, hapd->n_task_neigh_del);
    FREE(hapd->task_neigh_add);
    FREE(hapd->task_neigh_mod);
    FREE(hapd->task_neigh_del);
    hapd->task_neigh_add = NULL;
    hapd->task_neigh_mod = NULL;
    hapd->task_neigh_del = NULL;
    hapd->n_task_neigh_add = 0;
    hapd->n_task_neigh_mod = 0;
    hapd->n_task_neigh_del = 0;
}

static void
osw_hostap_bss_hapd_neigh_prep_set(char *buf,
                                   size_t buf_size,
                                   const struct osw_ssid *ssid,
                                   const struct osw_neigh *n)
{
    struct osw_ssid_hex hexbuf;
    const char *hex = osw_ssid_into_hex(&hexbuf, ssid);
    LOGT(LOG_PREFIX("converting "OSW_SSID_FMT, OSW_SSID_ARG(ssid)));
    WARN_ON(hex == NULL);
    WARN_ON(strlen(hex) == 0);
    if (hex == NULL) hex = "";

    /* SET_NEIGHBOR either adds a new entry, or re-uses an
     * existing one. There's no need to worry about
     * inserting duplicate BSSIDs.
    */
    snprintf(buf, buf_size,
             "SET_NEIGHBOR "OSW_HWADDR_FMT" ssid=%s nr="
             "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx" /* bssid */
             "%02hhx%02hhx%02hhx%02hhx"             /* info */
             "%02hhx"                               /* opclass */
             "%02hhx"                               /* channel */
             "%02hhx",                              /* phymode */
             OSW_HWADDR_ARG(&n->bssid),
             hex,
             n->bssid.octet[0],
             n->bssid.octet[1],
             n->bssid.octet[2],
             n->bssid.octet[3],
             n->bssid.octet[4],
             n->bssid.octet[5],
             (unsigned char)((n->bssid_info >>  0) & 0xff),
             (unsigned char)((n->bssid_info >>  8) & 0xff),
             (unsigned char)((n->bssid_info >> 16) & 0xff),
             (unsigned char)((n->bssid_info >> 24) & 0xff),
             n->op_class,
             n->channel,
             n->phy_type);
}

static void
osw_hostap_bss_hapd_neigh_prep_del(char *buf,
                                   size_t buf_size,
                                   const struct osw_neigh *n)
{
    snprintf(buf, buf_size,
             "REMOVE_NEIGHBOR "OSW_HWADDR_FMT,
             OSW_HWADDR_ARG(&n->bssid));
}

static void osw_hostap_bss_hapd_neigh_fill_prep(struct osw_hostap_bss_hapd *hapd,
                                                struct osw_drv_vif_config_ap *ap)
{
    const struct osw_ssid *ssid = &ap->ssid;
    const size_t n_add = ap->neigh_list.count;
    struct hostap_rq_task *add = CALLOC(n_add, sizeof(*add));
    size_t i;

    for (i = 0; i < n_add; i++) {
        const struct osw_neigh *n = &ap->neigh_list.list[i];
        char cmd[1024];

        osw_hostap_bss_hapd_neigh_prep_set(cmd, sizeof(cmd), ssid, n);
        hostap_rq_task_init(&add[i], hapd->ctrl.txq, cmd);
        add[i].task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    }

    hapd->n_task_neigh_fill = n_add;
    hapd->task_neigh_fill = add;
}

static void
osw_hostap_bss_hapd_neigh_prep(struct osw_hostap_bss_hapd *hapd,
                               struct osw_drv_vif_config_ap *ap)
{
    const struct osw_ssid *ssid = &ap->ssid;
    const size_t n_add = ap->neigh_add_list.count;
    const size_t n_mod = ap->neigh_mod_list.count;
    const size_t n_del = ap->neigh_del_list.count;
    struct hostap_rq_task *add = CALLOC(n_add, sizeof(*add));
    struct hostap_rq_task *mod = CALLOC(n_mod, sizeof(*mod));
    struct hostap_rq_task *del = CALLOC(n_del, sizeof(*del));
    size_t i;

    for (i = 0; i < n_add; i++) {
        const struct osw_neigh *n = &ap->neigh_add_list.list[i];
        char cmd[1024];

        osw_hostap_bss_hapd_neigh_prep_set(cmd, sizeof(cmd), ssid, n);
        hostap_rq_task_init(&add[i], hapd->ctrl.txq, cmd);
        add[i].task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    }

    for (i = 0; i < n_mod; i++) {
        const struct osw_neigh *n = &ap->neigh_mod_list.list[i];
        char cmd[1024];

        osw_hostap_bss_hapd_neigh_prep_set(cmd, sizeof(cmd), ssid, n);
        hostap_rq_task_init(&mod[i], hapd->ctrl.txq, cmd);
        mod[i].task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    }

    for (i = 0; i < n_del; i++) {
        const struct osw_neigh *n = &ap->neigh_del_list.list[i];
        char cmd[1024];

        osw_hostap_bss_hapd_neigh_prep_del(cmd, sizeof(cmd), n);
        hostap_rq_task_init(&del[i], hapd->ctrl.txq, cmd);
        del[i].task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    }

    hapd->n_task_neigh_add = n_add;
    hapd->n_task_neigh_mod = n_mod;
    hapd->n_task_neigh_del = n_del;

    hapd->task_neigh_add = add;
    hapd->task_neigh_mod = mod;
    hapd->task_neigh_del = del;
}

static void
osw_hostap_bss_hapd_neigh_add(struct osw_hostap_bss_hapd *hapd,
                              struct rq *q)
{
    osw_hostap_add_task_array(q, hapd->task_neigh_add, hapd->n_task_neigh_add);
    osw_hostap_add_task_array(q, hapd->task_neigh_mod, hapd->n_task_neigh_mod);
    osw_hostap_add_task_array(q, hapd->task_neigh_del, hapd->n_task_neigh_del);
}

static void
osw_hostap_bss_hapd_csa(struct osw_hostap_bss_hapd *hapd,
                        struct osw_drv_vif_config_ap *ap,
                        struct rq *q)
{
    char cmd[1024];
    char mode[64] = {0};

    if (ap->mode.ht_enabled)    STRSCAT(mode, "ht ");
    if (ap->mode.vht_enabled)   STRSCAT(mode, "vht ");
    if (ap->mode.he_enabled)    STRSCAT(mode, "he ");
    if (ap->mode.eht_enabled)   STRSCAT(mode, "eht ");

    snprintf(cmd, sizeof(cmd),
             "CHAN_SWITCH %d %d center_freq1=%d sec_channel_offset=%d bandwidth=%s %s",
             OSW_HOSTAP_CS_COUNT,
             ap->channel.control_freq_mhz,
             ap->channel.center_freq0_mhz,
             osw_channel_ht40_offset(&ap->channel),
             osw_channel_width_to_str(ap->channel.width),
             mode);

    hostap_rq_task_fini(&hapd->task_csa);
    hostap_rq_task_init(&hapd->task_csa, hapd->ctrl.txq, cmd);
    rq_add_task(q, &hapd->task_csa.task);
}

static void
osw_hostap_bss_hapd_acl_free(struct osw_hostap_bss_hapd *hapd)
{
    osw_hostap_fini_task_array(hapd->task_acl_mac, hapd->n_task_acl_mac);
    osw_hostap_fini_task_array(hapd->task_acl_add_mac, hapd->n_task_acl_add_mac);
    osw_hostap_fini_task_array(hapd->task_acl_del_mac, hapd->n_task_acl_del_mac);
    FREE(hapd->task_acl_mac);
    FREE(hapd->task_acl_add_mac);
    FREE(hapd->task_acl_del_mac);
    hapd->task_acl_mac = NULL;
    hapd->task_acl_add_mac = NULL;
    hapd->task_acl_del_mac = NULL;
    hapd->n_task_acl_mac = 0;
    hapd->n_task_acl_add_mac = 0;
    hapd->n_task_acl_del_mac = 0;
}

static void
osw_hostap_bss_hapd_acl_list_prep_add_mac(char *buf,
                                   size_t buf_size,
                                   const char* hostapd_cli_acl_list,
                                   const struct osw_hwaddr *addr)
{
    struct osw_hwaddr_str hwaddr_str;
    const char *str = osw_hwaddr2str(addr, &hwaddr_str);

    if (WARN_ON(str == NULL)) return;

    snprintf(buf, buf_size,
            "%s ADD_MAC %s%s",
            hostapd_cli_acl_list,
            str,
            " disassoc=0");
}

static void
osw_hostap_bss_hapd_acl_list_prep_del_mac(char *buf,
                                   size_t buf_size,
                                   const char* hostapd_cli_acl_list,
                                   const struct osw_hwaddr *addr)
{
    struct osw_hwaddr_str hwaddr_str;
    const char *str = osw_hwaddr2str(addr, &hwaddr_str);

    if (WARN_ON(str == NULL)) return;

    snprintf(buf, buf_size,
             "%s DEL_MAC %s",
             hostapd_cli_acl_list,
             str);
}

static void
osw_hostap_bss_hapd_acl_list_all_prep(struct osw_hostap_bss_hapd *hapd,
                               struct osw_drv_vif_config_ap *ap,
                               const char *hostapd_cli_acl_list)
{
    struct osw_hwaddr_list *acl_list = &ap->acl;
    const size_t n_acl_list = ap->acl.count;
    struct hostap_rq_task *acl_mac = CALLOC(n_acl_list, sizeof(*acl_mac));
    size_t i;

    for (i = 0; i < n_acl_list; i++) {
        const struct osw_hwaddr *addr = &acl_list->list[i];
        char cmd[1024];

        osw_hostap_bss_hapd_acl_list_prep_add_mac(cmd,
                                                  sizeof(cmd),
                                                  hostapd_cli_acl_list,
                                                  addr);
        hostap_rq_task_init(&acl_mac[i], hapd->ctrl.txq, cmd);
        acl_mac[i].task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    }

    hapd->n_task_acl_mac = n_acl_list;
    hapd->task_acl_mac = acl_mac;
}

static void
osw_hostap_bss_hapd_acl_list_all_add(struct osw_hostap_bss_hapd *hapd,
                              struct rq *q)
{
    osw_hostap_add_task_array(q, hapd->task_acl_mac, hapd->n_task_acl_mac);
}

static void
osw_hostap_bss_hapd_acl_list_prep(struct osw_hostap_bss_hapd *hapd,
                               struct osw_drv_vif_config_ap *ap,
                               const char *hostapd_cli_acl_list)
{
    struct osw_hwaddr_list *acl_add_list = &ap->acl_add;
    struct osw_hwaddr_list *acl_del_list = &ap->acl_del;
    const size_t n_acl_add_list = ap->acl_add.count;
    const size_t n_acl_del_list = ap->acl_del.count;
    struct hostap_rq_task *acl_add_mac = CALLOC(n_acl_add_list, sizeof(*acl_add_mac));
    struct hostap_rq_task *acl_del_mac = CALLOC(n_acl_del_list, sizeof(*acl_del_mac));
    size_t i;

    for (i = 0; i < n_acl_add_list; i++) {
        const struct osw_hwaddr *addr = &acl_add_list->list[i];
        char cmd[1024];

        osw_hostap_bss_hapd_acl_list_prep_add_mac(cmd,
                                                  sizeof(cmd),
                                                  hostapd_cli_acl_list,
                                                  addr);
        hostap_rq_task_init(&acl_add_mac[i], hapd->ctrl.txq, cmd);
        acl_add_mac[i].task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    }

    for (i = 0; i < n_acl_del_list; i++) {
        const struct osw_hwaddr *addr = &acl_del_list->list[i];
        char cmd[1024];

        osw_hostap_bss_hapd_acl_list_prep_del_mac(cmd,
                                                  sizeof(cmd),
                                                  hostapd_cli_acl_list,
                                                  addr);
        hostap_rq_task_init(&acl_del_mac[i], hapd->ctrl.txq, cmd);
        acl_del_mac[i].task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    }

    hapd->n_task_acl_add_mac = n_acl_add_list;
    hapd->task_acl_add_mac = acl_add_mac;
    hapd->n_task_acl_del_mac = n_acl_del_list;
    hapd->task_acl_del_mac = acl_del_mac;
}

static void
osw_hostap_bss_hapd_acl_list_add(struct osw_hostap_bss_hapd *hapd,
                              struct rq *q)
{
    osw_hostap_add_task_array(q, hapd->task_acl_add_mac, hapd->n_task_acl_add_mac);
    osw_hostap_add_task_array(q, hapd->task_acl_del_mac, hapd->n_task_acl_del_mac);
}

static void
osw_hostap_bss_hapd_acl_update(struct osw_hostap_bss_hapd *hapd,
                                    struct osw_drv_vif_config_ap *ap,
                                    struct rq *q)
{
    char hostapd_cli_acl_list[64] = {0};

    if (ap->acl_policy == OSW_ACL_ALLOW_LIST)
        STRSCPY(hostapd_cli_acl_list, "ACCEPT_ACL");
    else if (ap->acl_policy == OSW_ACL_DENY_LIST)
        STRSCPY(hostapd_cli_acl_list, "DENY_ACL");

    if (ap->acl_policy_changed) {
        rq_add_task(q, &hapd->task_clear_accept_acl.task);
        rq_add_task(q, &hapd->task_clear_deny_acl.task);
        switch (ap->acl_policy) {
            case OSW_ACL_ALLOW_LIST:
                rq_add_task(q, &hapd->task_set_accept_acl_policy.task);
                osw_hostap_bss_hapd_acl_list_all_prep(hapd, ap, hostapd_cli_acl_list);
                osw_hostap_bss_hapd_acl_list_all_add(hapd, q);
                break;
            case OSW_ACL_DENY_LIST:
                rq_add_task(q, &hapd->task_set_deny_acl_policy.task);
                osw_hostap_bss_hapd_acl_list_all_prep(hapd, ap, hostapd_cli_acl_list);
                osw_hostap_bss_hapd_acl_list_all_add(hapd, q);
                break;
            case OSW_ACL_NONE:
                rq_add_task(q, &hapd->task_set_deny_acl_policy.task);
                break;
        }
    } else if (ap->acl_changed) {
        osw_hostap_bss_hapd_acl_list_prep(hapd, ap, hostapd_cli_acl_list);
        osw_hostap_bss_hapd_acl_list_add(hapd, q);
    }
}

static void
osw_hostap_bss_hapd_flush_state_replies(struct osw_hostap_bss_hapd *hapd)
{
    hapd->task_get_config.reply = NULL;
    hapd->task_get_status.reply = NULL;
    hapd->task_get_mib.reply = NULL;
    hapd->task_get_wps_status.reply = NULL;
    hapd->task_get_neigh.reply = NULL;
    hapd->task_get_rxkhs.reply = NULL;
    hapd->task_get_accept_acl_list.reply = NULL;
    hapd->task_get_deny_acl_list.reply = NULL;
}

static void
osw_hostap_bss_hapd_prep_state_task(struct osw_hostap_bss_hapd *hapd)
{
    osw_hostap_bss_hapd_flush_state_replies(hapd);

    if (hapd->ctrl.opened == false) return;

    struct rq *q = &hapd->q_state.q;
    rq_resume(q);
    rq_add_task(q, &hapd->task_get_config.task);
    rq_add_task(q, &hapd->task_get_status.task);
    rq_add_task(q, &hapd->task_get_mib.task);
    rq_add_task(q, &hapd->task_get_wps_status.task);
    rq_add_task(q, &hapd->task_get_neigh.task);
    rq_add_task(q, &hapd->task_get_rxkhs.task);
    rq_add_task(q, &hapd->task_get_accept_acl_list.task);
    rq_add_task(q, &hapd->task_get_deny_acl_list.task);
    rq_stop(q);
}

static void
osw_hostap_bss_hapd_fill_state(struct osw_hostap_bss_hapd *hapd,
                               struct osw_drv_vif_state *state)
{
    char *config = file_get(hapd->path_config);
    char *psk_file = file_get(hapd->path_psk_file);
    char *rxkh_file = NULL;
    if (osw_wpa_is_ft(&state->u.ap.wpa)) {
        rxkh_file = file_get(hapd->path_rxkh);
    }

    struct osw_hostap_conf_ap_state_bufs bufs = {
        .config = config ?: "",
        .wpa_psk_file = psk_file ?: "",
        .rxkh_file = rxkh_file ?: "",
        .get_config = hapd->task_get_config.reply ?: "",
        .status = hapd->task_get_status.reply ?: "",
        .mib = hapd->task_get_mib.reply ?: "",
        .wps_get_status = hapd->task_get_wps_status.reply ?: "",
        .show_neighbor = hapd->task_get_neigh.reply ?: "",
        .show_accept_acl =  hapd->task_get_accept_acl_list.reply ?: "",
        .show_deny_acl = hapd->task_get_deny_acl_list.reply ?: "",
        .show_rxkhs = hapd->task_get_rxkhs.reply ?: "",
    };

    osw_hostap_conf_fill_ap_state(&bufs, state);

    FREE(config);
    FREE(psk_file);
    FREE(rxkh_file);
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
        CALL_HOOKS(bss->owner, event_fn, bss->phy_name, bss->vif_name, msg, msg_len);
    }
}

static void
osw_hostap_bss_hapd_opened_cb(struct hostap_ev_ctrl *ctrl,
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
osw_hostap_bss_hapd_closed_cb(struct hostap_ev_ctrl *ctrl,
                              void *priv)
{
    struct osw_hostap_bss_hapd *hapd = priv;
    struct osw_hostap_bss *bss = container_of(hapd, struct osw_hostap_bss, hapd);
    const struct osw_hostap_bss_ops *ops = bss->ops;
    void *ops_priv = bss->ops_priv;

    rq_task_kill(&bss->q_state.task);
    rq_kill(&bss->q_state.q);

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
osw_hostap_bss_hapd_sta_alloc(struct osw_hostap_bss_hapd *hapd,
                              const struct hostap_sta_info *info)
{
    struct ds_tree *stas = &hapd->stas;
    const struct osw_hwaddr sta_addr = { .octet = { FMT_os_macaddr_t(info->addr) } };
    assert(ds_tree_find(stas, &sta_addr) == NULL);
    struct osw_hostap_bss_hapd_sta *sta = CALLOC(1, sizeof(*sta));

    char deauth_no_tx_cmd[1024];
    snprintf(deauth_no_tx_cmd, sizeof(deauth_no_tx_cmd),
             "DEAUTHENTICATE "OSW_HWADDR_FMT" tx=0",
             OSW_HWADDR_ARG(&sta_addr));

    /* sta->task_deauth is initialized on-demand
     * later because reason code changes
     * dynamically
     */

    hostap_rq_task_init(&sta->task_deauth_no_tx, hapd->ctrl.txq, deauth_no_tx_cmd);
    hostap_rq_task_init(&sta->task_deauth, hapd->ctrl.txq, "");

    sta->addr = sta_addr;
    ds_tree_insert(stas, &sta->addr, sta);
}

static void
osw_hostap_bss_hapd_sta_free(struct osw_hostap_bss_hapd *hapd,
                             const struct hostap_sta_info *info)
{
    struct ds_tree *stas = &hapd->stas;
    const struct osw_hwaddr sta_addr = { .octet = { FMT_os_macaddr_t(info->addr) } };
    struct osw_hostap_bss_hapd_sta *sta = ds_tree_find(stas, &sta_addr);
    assert(sta != NULL);

    rq_task_kill(&sta->task_deauth_no_tx.task);
    rq_task_kill(&sta->task_deauth.task);
    hostap_rq_task_fini(&sta->task_deauth_no_tx);
    hostap_rq_task_fini(&sta->task_deauth);
    ds_tree_remove(stas, sta);
    FREE(sta);
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

    MEMZERO(sta);
    osw_hostap_bss_sta_parse(info->buf, &sta);
    osw_hostap_bss_hapd_sta_alloc(hapd, info);

    if (ops->sta_connected_fn != NULL) {
        ops->sta_connected_fn(&sta, ops_priv);
    }

    if (sta.assoc_ies_len)
    {
        sta.assoc_ies_len = 0;
        FREE(sta.assoc_ies);
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

    MEMZERO(sta);
    osw_hostap_bss_sta_parse(info->buf, &sta);

    if (ops->sta_changed_fn != NULL) {
        ops->sta_changed_fn(&sta, ops_priv);
    }

    if (sta.assoc_ies_len)
    {
        sta.assoc_ies_len = 0;
        FREE(sta.assoc_ies);
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

    osw_hostap_bss_hapd_sta_free(hapd, info);
}

static void
osw_hostap_bss_hapd_init_bssid_cb(struct rq_task *task,
                                  void *priv)
{
    struct hostap_rq_task *t = container_of(task, struct hostap_rq_task, task);
    struct hostap_rq_task *t2 = priv;
    struct osw_hwaddr bssid;

    MEMZERO(bssid);
    if (task->cancelled) return;
    if (task->killed) return;
    if (WARN_ON(t->reply == NULL)) return;

    char *copy = STRDUP(t->reply);
    char *lines = copy;
    char *line;

    while ((line = strsep(&lines, "\n")) != NULL) {
        char *k = strsep(&line, "=");
        char *v = strsep(&line, "\n");

        if (k == NULL) continue;
        if (v == NULL) continue;

        if (strcmp(k, "bssid[0]") == 0) {
            WARN_ON(osw_hwaddr_from_cstr(v, &bssid) == false);
            LOGD(LOG_PREFIX_TASK(t, "bssid: "OSW_HWADDR_FMT,
                                 OSW_HWADDR_ARG(&bssid)));

            char cmd[256];
            const struct osw_neigh n = { .bssid = bssid };
            osw_hostap_bss_hapd_neigh_prep_del(cmd, sizeof(cmd), &n);
            hostap_rq_task_fini(t2);
            hostap_rq_task_init(t2, t2->txq, cmd);
            break;
        }
    }

    FREE(copy);
    WARN_ON(osw_hwaddr_is_zero(&bssid));
}

static void
osw_hostap_bss_hapd_init(struct hostap_ev_ctrl *ghapd,
                         struct osw_hostap_bss_hapd *hapd,
                         const char *phy_name,
                         const char *vif_name,
                         struct ev_loop *loop)
{
    static const struct hostap_ev_ctrl_ops hapd_ops = {
        .msg_fn = osw_hostap_bss_hapd_msg_cb,
        .opened_fn = osw_hostap_bss_hapd_opened_cb,
        .closed_fn = osw_hostap_bss_hapd_closed_cb,
    };
    static const struct hostap_sta_ops sta_ops = {
        .connected_fn = osw_hostap_bss_hapd_sta_connected_cb,
        .changed_fn = osw_hostap_bss_hapd_sta_changed_cb,
        .disconnected_fn = osw_hostap_bss_hapd_sta_disconnected_cb,
    };

    ds_tree_init(&hapd->stas,
                 (ds_key_cmp_t *)osw_hwaddr_cmp,
                 struct osw_hostap_bss_hapd_sta,
                 node);

    char *path = osw_hostap_get_hapd_path(phy_name, vif_name);
    hostap_ev_ctrl_init(&hapd->ctrl, loop, path, &hapd_ops, &sta_ops, hapd);
    FREE(path);

    rq_nested_init(&hapd->q_config, loop);
    rq_nested_init(&hapd->q_state, loop);
    hapd->q_config.q.max_running = 1;
    hapd->q_state.q.max_running = 1;
    hapd->q_config.task.completed_fn = osw_hostap_bss_hapd_config_complete_cb;
    hapd->q_config.task.priv = hapd;

    char cmd_remove[1024];

    snprintf(hapd->path_config, sizeof(hapd->path_config),
             "/var/run/hostapd-%s.config",
             vif_name);

    snprintf(hapd->path_psk_file, sizeof(hapd->path_config),
             "/var/run/hostapd-%s.pskfile",
             vif_name);

    snprintf(hapd->path_rxkh, sizeof(hapd->path_rxkh),
             "/var/run/hostapd-%s.rxkh",
             vif_name);

    snprintf(cmd_remove, sizeof(cmd_remove),
             "REMOVE %s",
             vif_name);

    hostap_rq_task_init(&hapd->task_add, ghapd->txq, ""); /* set in update_add_task */
    hostap_rq_task_init(&hapd->task_remove, ghapd->txq, cmd_remove);
    hostap_rq_task_init(&hapd->task_log_level, hapd->ctrl.txq, "LOG_LEVEL INFO");
    hostap_rq_task_init(&hapd->task_init_bssid, hapd->ctrl.txq, "STATUS");
    hostap_rq_task_init(&hapd->task_init_neigh, hapd->ctrl.txq, ""); /* set in bssid_cb */
    hostap_rq_task_init(&hapd->task_reload_psk, hapd->ctrl.txq, "RELOAD_WPA_PSK");
    hostap_rq_task_init(&hapd->task_wps_pbc, hapd->ctrl.txq, "WPS_PBC");
    hostap_rq_task_init(&hapd->task_wps_cancel, hapd->ctrl.txq, "WPS_CANCEL");
    hostap_rq_task_init(&hapd->task_get_config, hapd->ctrl.txq, "GET_CONFIG");
    hostap_rq_task_init(&hapd->task_get_status, hapd->ctrl.txq, "STATUS");
    hostap_rq_task_init(&hapd->task_get_mib, hapd->ctrl.txq, "MIB");
    hostap_rq_task_init(&hapd->task_get_wps_status, hapd->ctrl.txq, "WPS_GET_STATUS");
    hostap_rq_task_init(&hapd->task_get_neigh, hapd->ctrl.txq, "SHOW_NEIGHBOR");
    hostap_rq_task_init(&hapd->task_csa, hapd->ctrl.txq, ""); /* set in csa prepare */
    hostap_rq_task_init(&hapd->task_clear_accept_acl, hapd->ctrl.txq, "ACCEPT_ACL CLEAR");
    hostap_rq_task_init(&hapd->task_clear_deny_acl, hapd->ctrl.txq, "DENY_ACL CLEAR");
    hostap_rq_task_init(&hapd->task_set_accept_acl_policy, hapd->ctrl.txq, "SET macaddr_acl 1");
    hostap_rq_task_init(&hapd->task_set_deny_acl_policy, hapd->ctrl.txq, "SET macaddr_acl 0");
    hostap_rq_task_init(&hapd->task_get_accept_acl_list, hapd->ctrl.txq, "ACCEPT_ACL SHOW");
    hostap_rq_task_init(&hapd->task_get_deny_acl_list, hapd->ctrl.txq, "DENY_ACL SHOW");
    hostap_rq_task_init(&hapd->task_get_rxkhs, hapd->ctrl.txq, "GET_RXKHS");
    hostap_rq_task_init(&hapd->task_reload_rxkh, hapd->ctrl.txq, "RELOAD_RXKHS");

    hapd->task_add.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_remove.task.completed_fn = osw_hostap_bss_remove_done_cb;
    hapd->task_remove.task.priv = hapd->ctrl.conn;
    hapd->task_log_level.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_init_bssid.task.completed_fn = osw_hostap_bss_hapd_init_bssid_cb;
    hapd->task_init_bssid.task.priv = &hapd->task_init_neigh;
    hapd->task_init_neigh.task.completed_fn = osw_hostap_bss_cmd_warn_on_err;
    hapd->task_reload_psk.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_wps_pbc.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_wps_cancel.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_get_config.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_get_status.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_get_mib.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_get_wps_status.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_get_neigh.task.completed_fn = osw_hostap_bss_cmd_warn_on_err;
    hapd->task_csa.task.completed_fn = osw_hostap_bss_cmd_warn_on_err;
    hapd->task_clear_accept_acl.task.completed_fn = osw_hostap_bss_cmd_warn_on_err;
    hapd->task_clear_deny_acl.task.completed_fn = osw_hostap_bss_cmd_warn_on_err;
    hapd->task_set_accept_acl_policy.task.completed_fn = osw_hostap_bss_cmd_warn_on_err;
    hapd->task_set_deny_acl_policy.task.completed_fn = osw_hostap_bss_cmd_warn_on_err;
    hapd->task_get_accept_acl_list.task.completed_fn = osw_hostap_bss_cmd_warn_on_err;
    hapd->task_get_deny_acl_list.task.completed_fn = osw_hostap_bss_cmd_warn_on_err;
    hapd->task_get_rxkhs.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    hapd->task_reload_rxkh.task.completed_fn = osw_hostap_bss_cmd_warn_on_err;
}

static void
osw_hostap_bss_hapd_fini(struct osw_hostap_bss_hapd *hapd)
{
    rq_task_kill(&hapd->q_config.task);
    rq_task_kill(&hapd->q_state.task);

    hostap_rq_task_fini(&hapd->task_add);
    hostap_rq_task_fini(&hapd->task_remove);
    hostap_rq_task_fini(&hapd->task_log_level);
    hostap_rq_task_fini(&hapd->task_init_bssid);
    hostap_rq_task_fini(&hapd->task_init_neigh);
    hostap_rq_task_fini(&hapd->task_reload_psk);
    hostap_rq_task_fini(&hapd->task_wps_pbc);
    hostap_rq_task_fini(&hapd->task_wps_cancel);
    hostap_rq_task_fini(&hapd->task_get_config);
    hostap_rq_task_fini(&hapd->task_get_status);
    hostap_rq_task_fini(&hapd->task_get_mib);
    hostap_rq_task_fini(&hapd->task_get_wps_status);
    hostap_rq_task_fini(&hapd->task_get_neigh);
    hostap_rq_task_fini(&hapd->task_csa);
    hostap_rq_task_fini(&hapd->task_clear_accept_acl);
    hostap_rq_task_fini(&hapd->task_clear_deny_acl);
    hostap_rq_task_fini(&hapd->task_set_accept_acl_policy);
    hostap_rq_task_fini(&hapd->task_set_deny_acl_policy);
    hostap_rq_task_fini(&hapd->task_get_accept_acl_list);
    hostap_rq_task_fini(&hapd->task_get_deny_acl_list);
    hostap_rq_task_fini(&hapd->task_get_rxkhs);
    hostap_rq_task_fini(&hapd->task_reload_rxkh);
    osw_hostap_bss_hapd_acl_free(hapd);
    osw_hostap_bss_hapd_neigh_free(hapd);
    osw_hostap_bss_hapd_neigh_fill_free(hapd);
    hostap_ev_ctrl_fini(&hapd->ctrl);
    assert(ds_tree_is_empty(&hapd->stas));
}

/* wpas */
static void
osw_hostap_bss_wpas_flush_state_replies(struct osw_hostap_bss_wpas *wpas)
{
    wpas->task_get_status.reply = NULL;
    wpas->task_get_networks.reply = NULL;
}

static void
osw_hostap_bss_wpas_prep_state_task(struct osw_hostap_bss_wpas *wpas)
{
    osw_hostap_bss_wpas_flush_state_replies(wpas);

    if (wpas->ctrl.opened == false) return;

    struct rq *q = &wpas->q_state.q;
    rq_resume(q);
    rq_add_task(q, &wpas->task_get_status.task);
    rq_add_task(q, &wpas->task_get_networks.task);
    rq_stop(q);
}

static void
osw_hostap_bss_wpas_psk_invalidate(struct osw_hostap_bss_wpas *wpas)
{
    if (wpas->psk_valid == false) return;

    wpas->psk_valid = false;
    LOGD(LOG_PREFIX_WPAS(wpas, "psk: invalidating: '%s'",
                         wpas->psk.str));
}

static void
osw_hostap_bss_wpas_psk_set(struct osw_hostap_bss_wpas *wpas,
                            const struct osw_drv_vif_state_sta_link *link)
{
    if (link->status != OSW_DRV_VIF_STATE_STA_LINK_CONNECTED) return;

    const char *psk = link->psk.str;
    const bool changed = (strcmp(psk, wpas->psk.str) != 0);
    const bool same = !changed;

    wpas->psk_valid = true;
    if (same) return;

    STRSCPY_WARN(wpas->psk.str, psk);
}

static void
osw_hostap_bss_wpas_psk_override(const struct osw_hostap_bss_wpas *wpas,
                                 struct osw_drv_vif_state_sta_link *link)
{
    if (wpas->psk_valid == false) return;

    LOGD(LOG_PREFIX_WPAS(wpas, "psk: overriding: '%s' -> '%s'",
                         link->psk.str,
                         wpas->psk.str));
    STRSCPY_WARN(link->psk.str, wpas->psk.str);
}

static void
osw_hostap_bss_wpas_fill_state(struct osw_hostap_bss_wpas *wpas,
                               struct osw_drv_vif_state *state)
{
    char *config = file_get(wpas->path_config);

    struct osw_hostap_conf_sta_state_bufs bufs = {
        .config = config ?: "",
        .status = wpas->task_get_status.reply ?: "",
        .list_networks = wpas->task_get_networks.reply ?: "",
        .bridge_if_name = wpas->bridge_if_name.buf,
        .mib = "",
    };

    struct osw_drv_vif_state_sta_link *link = &state->u.sta.link;
    const struct osw_channel chan = link->channel;

    osw_hostap_conf_fill_sta_state(&bufs, state);

    if (link->status != OSW_DRV_VIF_STATE_STA_LINK_CONNECTED) {
        osw_hostap_bss_wpas_psk_invalidate(wpas);
    }

    osw_hostap_bss_wpas_psk_override(wpas, link);
    osw_hostap_bss_wpas_psk_set(wpas, link);

    if (chan.control_freq_mhz != 0) {
        link->channel = chan;
    }

    FREE(config);
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
osw_hostap_bss_wpas_opened_cb(struct hostap_ev_ctrl *ctrl,
                              void *priv)
{
    struct osw_hostap_bss_wpas *wpas = priv;
    struct osw_hostap_bss *bss = container_of(wpas, struct osw_hostap_bss, wpas);
    const struct osw_hostap_bss_ops *ops = bss->ops;
    void *ops_priv = bss->ops_priv;

    if (ops->bss_changed_fn != NULL) {
        ops->bss_changed_fn(ops_priv);
    }
}

static void
osw_hostap_bss_wpas_closed_cb(struct hostap_ev_ctrl *ctrl,
                              void *priv)
{
    struct osw_hostap_bss_wpas *wpas = priv;
    struct osw_hostap_bss *bss = container_of(wpas, struct osw_hostap_bss, wpas);
    const struct osw_hostap_bss_ops *ops = bss->ops;
    void *ops_priv = bss->ops_priv;

    osw_hostap_bss_wpas_psk_invalidate(wpas);
    rq_task_kill(&bss->q_state.task);
    rq_kill(&bss->q_state.q);

    if (ops->bss_changed_fn != NULL) {
        ops->bss_changed_fn(ops_priv);
    }
}

static void
osw_hostap_bss_cmd_add_completed_update_bridge(struct rq_task *task,
                                               void *priv)
{
    struct hostap_rq_task *t = container_of(task, struct hostap_rq_task, task);
    const char *ok_str = "OK";
    const ssize_t ok_str_len = strlen(ok_str);
    const bool reply_ok = (t->reply != NULL)
                       && (strncmp(t->reply, ok_str, ok_str_len) == 0);
    const bool reply_not_ok = !reply_ok;
    if (reply_not_ok) return;

    struct osw_hostap_bss_wpas *wpas = priv;
    STRSCPY_WARN(wpas->bridge_if_name.buf,
            wpas->bridge_if_name_pending.buf);
}

static void
osw_hostap_bss_cmd_add_completed_cb(struct rq_task *task,
                                    void *priv)
{
    osw_hostap_bss_cmd_warn_on_fail(task, priv);
    osw_hostap_bss_cmd_add_completed_update_bridge(task, priv);
}

static void
osw_hostap_bss_wpas_init_add(struct hostap_ev_ctrl *gwpas,
                             struct osw_hostap_bss_wpas *wpas,
                             const char *phy_name,
                             const char *vif_name)
{
    char *path = osw_hostap_get_wpas_path(phy_name, vif_name);
    char sock_dir_path[1024];
    STRSCPY_WARN(sock_dir_path, path ?: "");
    dirname(sock_dir_path);
    FREE(path);

    const char *drv_name = "nl80211";
    const char *drv_param = "";
    const char *bridge = wpas->bridge_if_name_pending.buf;
    char cmd_add[8192];

    snprintf(cmd_add, sizeof(cmd_add),
             "INTERFACE_ADD %s\t%s\t%s\t%s\t%s\t%s",
             vif_name,
             wpas->path_config,
             drv_name,
             sock_dir_path,
             drv_param,
             bridge);

    hostap_rq_task_fini(&wpas->task_add);
    hostap_rq_task_init(&wpas->task_add, gwpas->txq, cmd_add);
    wpas->task_add.task.completed_fn = osw_hostap_bss_cmd_add_completed_cb;
    wpas->task_add.task.priv = wpas;
}

static void
osw_hostap_bss_wpas_init(struct hostap_ev_ctrl *gwpas,
                         struct osw_hostap_bss_wpas *wpas,
                         const char *phy_name,
                         const char *vif_name,
                         struct ev_loop *loop)
{
    static const struct hostap_ev_ctrl_ops wpas_ops = {
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

    char cmd_remove[1024];

    snprintf(wpas->path_config, sizeof(wpas->path_config),
             "/var/run/supplicant-%s.config",
             vif_name);

    snprintf(cmd_remove, sizeof(cmd_remove),
             "INTERFACE_REMOVE %s",
             vif_name);

    osw_hostap_bss_wpas_init_add(gwpas, wpas, phy_name, vif_name);

    hostap_rq_task_init(&wpas->task_remove, gwpas->txq, cmd_remove);
    hostap_rq_task_init(&wpas->task_log_level, wpas->ctrl.txq, "LOG_LEVEL INFO");
    hostap_rq_task_init(&wpas->task_reconfigure, wpas->ctrl.txq, "RECONFIGURE");
    hostap_rq_task_init(&wpas->task_reassociate, wpas->ctrl.txq, "REASSOCIATE");
    hostap_rq_task_init(&wpas->task_disconnect, wpas->ctrl.txq, "DISCONNECT");
    hostap_rq_task_init(&wpas->task_get_status, wpas->ctrl.txq, "STATUS");
    hostap_rq_task_init(&wpas->task_get_networks, wpas->ctrl.txq, "LIST_NETWORKS");

    wpas->task_remove.task.completed_fn = osw_hostap_bss_remove_done_cb;
    wpas->task_remove.task.priv = wpas->ctrl.conn;
    wpas->task_log_level.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    wpas->task_reconfigure.task.completed_fn = osw_hostap_bss_cmd_reconfigure_done;
    wpas->task_reconfigure.task.priv = wpas;
    wpas->task_reassociate.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    wpas->task_disconnect.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    wpas->task_get_status.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
    wpas->task_get_networks.task.completed_fn = osw_hostap_bss_cmd_warn_on_fail;
}

static void
osw_hostap_bss_wpas_fini(struct osw_hostap_bss_wpas *wpas)
{
    rq_task_kill(&wpas->q_config.task);
    rq_task_kill(&wpas->q_state.task);

    hostap_rq_task_fini(&wpas->task_add);
    hostap_rq_task_fini(&wpas->task_remove);
    hostap_rq_task_fini(&wpas->task_log_level);
    hostap_rq_task_fini(&wpas->task_reconfigure);
    hostap_rq_task_fini(&wpas->task_reassociate);
    hostap_rq_task_fini(&wpas->task_disconnect);
    hostap_rq_task_fini(&wpas->task_get_status);
    hostap_rq_task_fini(&wpas->task_get_networks);
    osw_hostap_conf_free_sta_config(&wpas->conf);
    hostap_ev_ctrl_fini(&wpas->ctrl);
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
    rq_task_kill(&bss->q_config.task);
    rq_task_kill(&bss->q_state.task);
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

void
osw_hostap_bss_fill_csa_by_hostap(struct osw_hostap_bss *bss,
                                  bool csa_by_hostap)
{
    bss->hapd.csa_by_hostap = csa_by_hostap;
    LOGD(LOG_PREFIX_BSS(bss, "csa_by_hostap=%d", csa_by_hostap));
}

void
osw_hostap_bss_fill_group_by_phy(struct osw_hostap_bss *bss,
                                  bool group_by_phy)
{
    bss->hapd.group_by_phy = group_by_phy;
    LOGD(LOG_PREFIX_BSS(bss, "group_by_phy=%d", group_by_phy));
}

void
osw_hostap_bss_fill_acl_by_hostap(struct osw_hostap_bss *bss,
                                  bool acl_by_hostap)
{
    bss->hapd.acl_by_hostap = acl_by_hostap;
    LOGD(LOG_PREFIX_BSS(bss, "acl_by_hostap=%d", acl_by_hostap));
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

struct rq_task *
osw_hostap_bss_prep_sta_deauth_no_tx_task(struct osw_hostap_bss *bss,
                                          const struct osw_hwaddr *sta_addr)
{
    struct osw_hostap_bss_hapd *hapd = &bss->hapd;
    if (hapd->ctrl.opened == false) return NULL;

    struct osw_hostap_bss_hapd_sta *sta = ds_tree_find(&hapd->stas, sta_addr);
    if (sta == NULL) return NULL;

    struct rq_task *t = &sta->task_deauth_no_tx.task;
    rq_task_kill(t);
    return t;
}

struct rq_task *
osw_hostap_bss_prep_sta_deauth_task(struct osw_hostap_bss *bss,
                                    const struct osw_hwaddr *sta_addr,
                                    const int dot11_reason_code)
{
    struct osw_hostap_bss_hapd *hapd = &bss->hapd;
    if (hapd->ctrl.opened == false) return NULL;

    struct osw_hostap_bss_hapd_sta *sta = ds_tree_find(&hapd->stas, sta_addr);
    if (sta == NULL) return NULL;

    char deauth_cmd[1024];
    snprintf(deauth_cmd, sizeof(deauth_cmd),
             "DEAUTHENTICATE "OSW_HWADDR_FMT" reason=%d",
             OSW_HWADDR_ARG(sta_addr),
             dot11_reason_code);

    struct hostap_rq_task *ht = &sta->task_deauth;
    struct rq_task *t = &ht->task;

    rq_task_kill(t);
    hostap_rq_task_fini(ht);
    hostap_rq_task_init(ht, hapd->ctrl.txq, deauth_cmd);

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
}

static void
osw_hostap_set_conf_ap_write_ft(const struct osw_hostap_bss_hapd *hapd)
{
    const struct osw_hostap_conf_ap_config *conf = &hapd->conf;

    file_put(hapd->path_rxkh, conf->rxkh_buf);
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

    const bool is_ft = osw_wpa_is_ft(&dvif->u.ap.wpa);

    OSW_HOSTAP_CONF_SET_BUF(conf->wpa_psk_file, hapd->path_psk_file);
    if (is_ft) OSW_HOSTAP_CONF_SET_BUF(conf->rxkh_file, hapd->path_rxkh);

    const struct hostap_sock *sock = hostap_conn_get_sock(hapd->ctrl.conn);
    const char *path_sock = hostap_sock_get_path(sock);
    WARN_ON(path_sock == NULL);
    char *path_ctrl = STRDUP(path_sock ?: "");
    dirname(path_ctrl);
    OSW_HOSTAP_CONF_SET_BUF(conf->ctrl_interface, path_ctrl);
    FREE(path_ctrl);

    MEMZERO(conf->extra_buf);
    CALL_HOOKS(hostap, ap_conf_mutate_fn, phy_name, vif_name, drv_conf, conf);

    osw_hostap_conf_generate_ap_config_bufs(conf);
    osw_hostap_set_conf_ap_write(hapd);
    if (is_ft) osw_hostap_set_conf_ap_write_ft(hapd);

    osw_hostap_conf_list_free(conf);
    /* FIXME: This could/should rely on hostapd
     * file content comparison, probably at least
     * partially.
     */

    const bool is_running = hostap_conn_is_opened(hapd->ctrl.conn);
    const bool want_ap = (dvif->vif_type == OSW_VIF_AP);
    const bool want_running = dvif->enabled && want_ap;
    const bool sae_psk_changed = dvif->u.ap.psk_list_changed
                              && dvif->u.ap.wpa.akm_sae;
    const bool invalidated = dvif->enabled_changed
                          || sae_psk_changed
                          || (dvif->u.ap.channel_changed == true &&
                              dvif->u.ap.csa_required == false)
                          || dvif->u.ap.bridge_if_name_changed
                          || dvif->u.ap.nas_identifier_changed
                          || dvif->u.ap.beacon_interval_tu_changed
                          || dvif->u.ap.ssid_hidden_changed
                          || dvif->u.ap.isolated_changed
                          || dvif->u.ap.mcast2ucast_changed
                          || dvif->u.ap.mode_changed
                          || dvif->u.ap.ssid_changed
                          || dvif->u.ap.wpa_changed
                          || dvif->u.ap.radius_list_changed
                          || dvif->u.ap.acct_list_changed
                          || dvif->u.ap.passpoint_changed
                          || dvif->u.ap.multi_ap_changed
                          || dvif->u.ap.ft_encr_key_changed
                          || dvif->u.ap.ft_over_ds_changed
                          || dvif->u.ap.ft_pmk_r0_key_lifetime_sec_changed
                          || dvif->u.ap.ft_pmk_r1_max_key_lifetime_sec_changed
                          || dvif->u.ap.ft_pmk_r1_push_changed
                          || dvif->u.ap.ft_psk_generate_local_changed
                          || dphy->reg_domain_changed;
    const bool psk_file_invalidated = (dvif->u.ap.psk_list_changed
                                    || dvif->u.ap.wps_cred_list_changed);

    const bool do_remove = (is_running && !want_running)
                        || (is_running && want_running && invalidated);
    const bool do_add = (!is_running && want_running)
                     || (is_running && want_running && invalidated);
    const bool do_reload_psk = (want_running && psk_file_invalidated);
    const bool do_neigh = (want_running && dvif->u.ap.neigh_list_changed);
    const bool do_wps_pbc = is_running
                         && dvif->u.ap.wps_pbc == true
                         && dvif->u.ap.wps_pbc_changed == true;
    const bool do_wps_cancel = is_running
                            && dvif->u.ap.wps_pbc == false
                            && dvif->u.ap.wps_pbc_changed == true;
    const bool do_csa = is_running && want_running &&
                     dvif->u.ap.channel_changed && dvif->u.ap.csa_required && hapd->csa_by_hostap;

    const bool do_acl = want_running && (dvif->u.ap.acl_changed || dvif->u.ap.acl_policy_changed) &&
                     hapd->acl_by_hostap;

    const bool do_ft = is_running && dvif->u.ap.neigh_ft_list_changed;

    rq_resume(q);

    if (do_remove) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do remove"));
        rq_add_task(q, &hapd->task_remove.task);
    }

    if (do_add) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do add"));
        osw_hostap_bss_hapd_update_add_task(hapd, hostap, phy_name, vif_name);
        rq_add_task(q, &hapd->task_add.task);
        rq_add_task(q, &hapd->task_log_level.task);
        rq_add_task(q, &hapd->task_init_bssid.task);
        rq_add_task(q, &hapd->task_init_neigh.task);

        /* fill neigh on add */
        osw_hostap_bss_hapd_neigh_fill_free(hapd);
        osw_hostap_bss_hapd_neigh_fill_prep(hapd, &dvif->u.ap);
        osw_hostap_add_task_array(q, hapd->task_neigh_fill, hapd->n_task_neigh_fill);
    }

    if (do_reload_psk) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do reload psk"));
        rq_add_task(q, &hapd->task_reload_psk.task);
    }

    if (do_neigh) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do neigh"));
        osw_hostap_bss_hapd_neigh_free(hapd);
        osw_hostap_bss_hapd_neigh_prep(hapd, &dvif->u.ap);
        osw_hostap_bss_hapd_neigh_add(hapd, q);
    }

    if (do_wps_pbc) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do wps pbc"));
        rq_add_task(q, &hapd->task_wps_pbc.task);
    }

    if (do_wps_cancel) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do wps cancel"));
        rq_add_task(q, &hapd->task_wps_cancel.task);
    }

    if (do_csa) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do csa"));
        osw_hostap_bss_hapd_csa(hapd, &dvif->u.ap, q);
    }

    if (do_acl) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do acl"));
        osw_hostap_bss_hapd_acl_free(hapd);
        osw_hostap_bss_hapd_acl_update(hapd, &dvif->u.ap, q);
    }

    if (do_ft) {
        LOGD(LOG_PREFIX_HAPD(hapd, "do ft"));
        rq_add_task(q, &hapd->task_reload_rxkh.task);
    }

    rq_stop(q);
    return t;
}

static void
osw_hostap_set_conf_sta_write(const struct osw_hostap_bss_wpas *wpas)
{
    const struct osw_hostap_conf_sta_config *conf = &wpas->conf;

    /* FIXME: This could be staged to allow differentiating
     * old/new/pending configs.
     */

    file_put(wpas->path_config, conf->conf_buf);
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

    struct osw_hostap_conf_sta_config *conf = &wpas->conf;
    osw_hostap_conf_free_sta_config(conf);
    const bool ok = osw_hostap_conf_fill_sta_config(drv_conf,
                                                    phy_name,
                                                    vif_name,
                                                    conf);
    /* Ignore that result. It's going to fail for non-STA
     * interfaces, but that is intended to be applied for AP
     * too. Subsequent conditions will evaluate what tasks
     * need to be really done.
     */
    (void)ok;

    const struct hostap_sock *sock = hostap_conn_get_sock(wpas->ctrl.conn);
    const char *path_sock = hostap_sock_get_path(sock);
    WARN_ON(path_sock == NULL);
    char *path_ctrl = STRDUP(path_sock ?: "");
    dirname(path_ctrl);
    OSW_HOSTAP_CONF_SET_BUF(conf->global.ctrl_interface, path_ctrl);
    FREE(path_ctrl);

    MEMZERO(conf->extra_buf);
    CALL_HOOKS(hostap, sta_conf_mutate_fn, phy_name, vif_name, drv_conf, conf);

    osw_hostap_conf_generate_sta_config_bufs(conf);
    osw_hostap_set_conf_sta_write(wpas);

    const bool is_running = hostap_conn_is_opened(wpas->ctrl.conn);
    const bool want_sta = (dvif->vif_type == OSW_VIF_STA);
    const bool want_running = dvif->enabled && want_sta;
    const bool invalidated = dvif->enabled_changed
                          || dvif->u.sta.network_changed;
    const bool bridging_changed = (strncmp(conf->bridge_if_name.buf,
                                           wpas->bridge_if_name.buf,
                                           sizeof(wpas->bridge_if_name.buf)) != 0);

    const bool do_remove = (is_running && !want_running)
                        || (is_running && want_running && bridging_changed);
    const bool do_add = (!is_running && want_running)
                     || (want_running && bridging_changed);
    const bool do_reconf = (want_running && invalidated & !bridging_changed);
    const bool do_reassoc = (want_running &&
                             (dvif->u.sta.operation == OSW_DRV_VIF_CONFIG_STA_CONNECT ||
                              dvif->u.sta.operation == OSW_DRV_VIF_CONFIG_STA_RECONNECT));
    const bool do_disc = (want_running && dvif->u.sta.operation == OSW_DRV_VIF_CONFIG_STA_DISCONNECT);

    STRSCPY_WARN(wpas->bridge_if_name_pending.buf,
                 conf->bridge_if_name.buf);

    struct hostap_ev_ctrl *gwpas = &hostap->gwpas;
    osw_hostap_bss_wpas_init_add(gwpas, wpas, phy_name, vif_name);

    rq_resume(q);

    if (invalidated) {
        osw_hostap_bss_wpas_psk_invalidate(wpas);
    }

    if (do_remove) {
        LOGD(LOG_PREFIX_WPAS(wpas, "do remove"));
        rq_add_task(q, &wpas->task_remove.task);
    }

    if (do_add) {
        LOGD(LOG_PREFIX_WPAS(wpas, "do add"));
        rq_add_task(q, &wpas->task_add.task);
        rq_add_task(q, &wpas->task_log_level.task);
    }

    if (do_reconf) {
        LOGD(LOG_PREFIX_WPAS(wpas, "do reconf"));
        rq_add_task(q, &wpas->task_reconfigure.task);
    }

    if (do_reassoc) {
        LOGD(LOG_PREFIX_WPAS(wpas, "do reassoc"));
        rq_add_task(q, &wpas->task_reassociate.task);
    }

    if (do_disc) {
        LOGD(LOG_PREFIX_WPAS(wpas, "do disc"));
        rq_add_task(q, &wpas->task_disconnect.task);
    }

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
osw_hostap_bss_ghapd_opened_cb(struct hostap_ev_ctrl *ctrl,
                               void *priv)
{
    struct osw_hostap *m = priv;
    (void)m;
    LOGI(LOG_PREFIX("global hostap: opened"));
}

static void
osw_hostap_bss_ghapd_closed_cb(struct hostap_ev_ctrl *ctrl,
                               void *priv)
{
    struct osw_hostap *m = priv;
    (void)m;
    LOGI(LOG_PREFIX("global hostap: closed"));
}

static void
osw_hostap_bss_gwpas_opened_cb(struct hostap_ev_ctrl *ctrl,
                               void *priv)
{
    struct osw_hostap *m = priv;
    (void)m;
    LOGI(LOG_PREFIX("global wpa_supplicant: opened"));
}

static void
osw_hostap_bss_gwpas_closed_cb(struct hostap_ev_ctrl *ctrl,
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

    static const struct hostap_ev_ctrl_ops ghapd_ops = {
        .opened_fn = osw_hostap_bss_ghapd_opened_cb,
        .closed_fn = osw_hostap_bss_ghapd_closed_cb,
    };
    static const struct hostap_ev_ctrl_ops gwpas_ops = {
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

OSW_UT(osw_hostap_sta_parse)
{
    struct osw_hostap_bss_sta sta;
    struct osw_hwaddr addr = { .octet = {
        0xe8, 0x84, 0xa5, 0x2c, 0x0e, 0xbb,
    } };
    const char *buf1 = ""
        "e8:84:a5:2c:0e:bb\n"
        "flags=[AUTH][ASSOC][AUTHORIZED][SHORT_PREAMBLE][WMM][HT][HE]\n"
        "aid=1\n"
        "capability=0x431\n"
        "listen_interval=10\n"
        "supported_rates=02 04 0b 16 0c 12 18 24 30 48 60 6c\n"
        "timeout_next=NULLFUNC POLL\n"
        "dot11RSNAStatsSTAAddress=e8:84:a5:2c:0e:bb\n"
        "dot11RSNAStatsVersion=1\n"
        "dot11RSNAStatsSelectedPairwiseCipher=00-0f-ac-4\n"
        "dot11RSNAStatsTKIPLocalMICFailures=0\n"
        "dot11RSNAStatsTKIPRemoteMICFailures=0\n"
        "wpa=2\n"
        "AKMSuiteSelector=00-0f-ac-2\n"
        "hostapdWPAPTKState=11\n"
        "hostapdWPAPTKGroupState=0\n"
        "rx_packets=0\n"
        "tx_packets=0\n"
        "rx_bytes=0\n"
        "tx_bytes=0\n"
        "inactive_msec=0\n"
        "signal=0\n"
        "rx_rate_info=0\n"
        "tx_rate_info=0\n"
        "ht_mcs_bitmask=ffff0000000000000000\n"
        "connected_time=2983\n"
        "supp_op_classes=51515354737475767778797a7b7c7d7e7f\n"
        "ht_caps_info=0x19ef\n"
        "ext_capab=04004a0201400040002120\n"
        "keyid=key--1";
    const char *buf2 = "";
    const char *buf3 = ""
        "e8:84:a5:2c:0e:bb\n"
        "flags=[AUTH]";

    MEMZERO(sta);
    osw_hostap_bss_sta_parse(buf1, &sta);
    assert(memcmp(&sta.addr, &addr, sizeof(addr)) == 0);
    assert(sta.authenticated == true);
    assert(sta.associated == true);
    assert(sta.authorized == true);
    assert(sta.key_id_known == true);
    assert(sta.key_id == -1);

    MEMZERO(sta);
    osw_hostap_bss_sta_parse(buf2, &sta);
    assert(sta.authenticated == false);
    assert(sta.associated == false);
    assert(sta.authorized == false);
    assert(sta.key_id_known == false);
    assert(sta.key_id == 0);

    MEMZERO(sta);
    osw_hostap_bss_sta_parse(buf3, &sta);
    assert(memcmp(&sta.addr, &addr, sizeof(addr)) == 0);
    assert(sta.authenticated == true);
    assert(sta.associated == false);
    assert(sta.authorized == false);
    assert(sta.key_id_known == false);
}
