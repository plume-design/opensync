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

/* std libc */
#include <stdlib.h>

/* opensync */
#include "os.h"
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "schema_consts.h"
#include "log.h"
#include "ds.h"
#include "ds_dlist.h"
#include "target.h"
#include "wm2.h"
#include "wm2_target.h"

/* spec mandates 30s chirp interval; scanning for AP
 * discovery and freq hopping may take a bit, so this seems
 * like a good bet to avoid add+del back2back.
 */
#define WM2_DPP_ANNOUNCEMENT_CLEANUP_SECONDS 120

#define WM2_DPP_RECALC_SECONDS atoi(getenv("WM2_DPP_RECALC_SECONDS") ?: "1")
#define WM2_DPP_TRIES atoi(getenv("WM2_DPP_TRIES") ?: "3")
#define WM2_DPP_ONBOARD_TIMEOUT_SECONDS atoi(getenv("WM2_DPP_ONBOARD_TIMEOUT_SECONDS") ?: "30")
#define WM2_DPP_SSID_LEN 32
#define WM2_DPP_PSK_LEN 64

/**
 * There may be multiple jobs in a list. However at any
 * given time only 0 or 1 can be in_progress. The process of
 * scheduling them takes place in wm2_dpp_recalc().
 */
struct wm2_dpp_job {
    struct ds_dlist_node list;
    struct schema_DPP_Config config;
    bool gone;
    bool interrupted;
    int tries_left;
};

struct wm2_dpp_announcement {
    struct ds_dlist_node list;
    struct schema_DPP_Announcement row;
    bool gone;
    ev_timer expiry;
};

/**
 * This is used to maintain list of interfaces that were
 * enabled for DPP. This is used as a dependency before
 * given wm_dpp_job is considered for execution.
 *
 * The current implementation isn't expected to check for
 * Config == State condition because it never is true during
 * leaf onboarding.
 */
struct wm2_dpp_ifname {
    struct ds_dlist_node list;
    char ifname[16 + 1];
};

static ovsdb_table_t table_DPP_Config;
static ovsdb_table_t table_DPP_Announcement;
static ovsdb_table_t table_DPP_Oftag;
static ev_timer g_wm2_dpp_timeout_timer;
static ev_timer g_wm2_dpp_recalc_timer;
static struct ds_dlist g_wm2_dpp_ifnames = DS_DLIST_INIT(struct wm2_dpp_ifname, list);
static struct ds_dlist g_wm2_dpp_jobs = DS_DLIST_INIT(struct wm2_dpp_job, list);
static struct ds_dlist g_wm2_dpp_announcements = DS_DLIST_INIT(struct wm2_dpp_announcement, list);
static bool g_wm2_dpp_supported;

static struct wm2_dpp_ifname *
wm2_dpp_ifname_get(const char *ifname)
{
    struct wm2_dpp_ifname *i;

    ds_dlist_foreach(&g_wm2_dpp_ifnames, i)
        if (!strcmp(i->ifname, ifname))
            return i;

    return NULL;
}

static struct wm2_dpp_ifname *
wm2_dpp_ifname_add(const char *ifname)
{
    struct wm2_dpp_ifname *i;

    LOGD("dpp: %s: adding", ifname);

    i = calloc(1, sizeof(*i));
    if (!i)
        return NULL;

    STRSCPY_WARN(i->ifname, ifname);
    ds_dlist_insert_tail(&g_wm2_dpp_ifnames, i);
    return i;
}

static void
wm2_dpp_ifname_del(struct wm2_dpp_ifname *i)
{
    LOGD("dpp: %s: removing", i->ifname);

    ds_dlist_remove(&g_wm2_dpp_ifnames, i);
    free(i);
}

static void
wm2_dpp_recalc_schedule(void)
{
    LOGD("dpp: scheduling recalc");

    ev_timer_stop(EV_DEFAULT_ &g_wm2_dpp_recalc_timer);
    ev_timer_set(&g_wm2_dpp_recalc_timer, WM2_DPP_RECALC_SECONDS, 0);
    ev_timer_start(EV_DEFAULT_ &g_wm2_dpp_recalc_timer);
}

static void
wm2_dpp_jobs_del(struct wm2_dpp_job *job)
{
    LOGD("dpp: %s: removing", job->config._uuid.uuid);

    ds_dlist_remove(&g_wm2_dpp_jobs, job);
    free(job);
}

static struct wm2_dpp_job *
wm2_dpp_jobs_add(const struct schema_DPP_Config *config)
{
    struct wm2_dpp_job *job;

    LOGD("dpp: %s: adding", config->_uuid.uuid);

    job = calloc(1, sizeof(*job));
    if (!job)
        return NULL;

    job->tries_left = WM2_DPP_TRIES;
    memcpy(&job->config, config, sizeof(job->config));
    ds_dlist_insert_tail(&g_wm2_dpp_jobs, job);
    return job;
}

static struct wm2_dpp_job *
wm2_dpp_jobs_get(const char *uuid)
{
    struct wm2_dpp_job *job;

    ds_dlist_foreach(&g_wm2_dpp_jobs, job)
        if (!strcmp(job->config._uuid.uuid, uuid))
            return job;

    return NULL;
}

static struct wm2_dpp_job *
wm2_dpp_jobs_get_current(void)
{
    struct wm2_dpp_job *job;

    ds_dlist_foreach(&g_wm2_dpp_jobs, job)
        if (!strcmp(job->config.status, SCHEMA_CONSTS_DPP_IN_PROGRESS))
            return job;

    return NULL;
}

static void
wm2_dpp_jobs_mark_gone(void)
{
    struct wm2_dpp_job *job;

    ds_dlist_foreach(&g_wm2_dpp_jobs, job)
        job->gone = true;
}

static void
wm2_dpp_jobs_flush_gone(void)
{
    struct wm2_dpp_job *job;

    /* ds_dlist_foreach_iter() doesn't work with
     * ds_dlist_remove(), so there's no "safe" way to
     * iterate and remove without forcing abstractions to
     * work with iterators instead of objects.
     *
     * Restart loop whenever list is modified instead.
     */
    for (;;) {
        ds_dlist_foreach(&g_wm2_dpp_jobs, job)
            if (job->gone)
                break;

        if (!job)
            break;

        if (!strcmp(job->config.status, SCHEMA_CONSTS_DPP_IN_PROGRESS))
            LOGI("dpp: %s: cancelling due to removal", job->config._uuid.uuid);

        wm2_dpp_jobs_del(job);
    }
}

static void
wm2_dpp_jobs_pull(void)
{
    struct schema_DPP_Config *configs;
    struct schema_DPP_Config *config;
    struct wm2_dpp_job *job;
    int n;

    wm2_dpp_jobs_mark_gone();
    configs = ovsdb_table_select_where(&table_DPP_Config, json_array(), &n);

    for (config = configs; config && n; config++, n--) {
        LOGD("dpp: %s: processing", config->_uuid.uuid);

        job = wm2_dpp_jobs_get(config->_uuid.uuid);
        if (!job) job = wm2_dpp_jobs_add(config);
        if (job) job->gone = false;

        WARN_ON(!job);
    }

    free(configs);
    wm2_dpp_jobs_flush_gone();
}

static bool
wm2_dpp_jobs_ifnames_ready(const struct wm2_dpp_job *job)
{
    int i;

    if (WARN_ON(job->config.ifnames_len == 0))
        return false;

    for (i = 0; i < job->config.ifnames_len; i++)
        if (!wm2_dpp_ifname_get(job->config.ifnames[i]))
            return false;

    return true;
}

static bool
wm2_dpp_jobs_allowed(void)
{
    /* This could be extended to wait, eg. for
     * config/state to get in sync before DPP
     * proceeds with anything.
     */
    return true;
}

static struct wm2_dpp_job *
wm2_dpp_jobs_get_next(void)
{
    struct wm2_dpp_job *job;

    if (!wm2_dpp_jobs_allowed())
        return NULL;

    ds_dlist_foreach(&g_wm2_dpp_jobs, job)
        if (!strcmp(job->config.status, SCHEMA_CONSTS_DPP_REQUESTED))
            if (wm2_dpp_jobs_ifnames_ready(job))
                return job;

    return NULL;
}

static void
wm2_dpp_jobs_interrupt(void)
{
    struct wm2_dpp_job *job;

    job = wm2_dpp_jobs_get_current();
    if (!job)
        return;

    LOGI("dpp: %s: interrupting, will restart soon", job->config._uuid.uuid);

    ev_timer_stop(EV_DEFAULT_ &g_wm2_dpp_timeout_timer);
    job->interrupted = true;
    ovsdb_table_update(&table_DPP_Config, &job->config);
}

static const char *
wm2_dpp_keytype_to_str(enum target_dpp_key_type type)
{
    switch (type) {
        case TARGET_DPP_KEY_PRIME256V1: return SCHEMA_CONSTS_DPP_PRIME256V1;
        case TARGET_DPP_KEY_SECP384R1: return SCHEMA_CONSTS_DPP_SECP384R1;
        case TARGET_DPP_KEY_SECP512R1: return SCHEMA_CONSTS_DPP_SECP521R1;
        case TARGET_DPP_KEY_BRAINPOOLP256R1: return SCHEMA_CONSTS_DPP_BRAINPOOLP256R1;
        case TARGET_DPP_KEY_BRAINPOOLP384R1: return SCHEMA_CONSTS_DPP_BRAINPOOLP384R1;
        case TARGET_DPP_KEY_BRAINPOOLP512R1: return SCHEMA_CONSTS_DPP_BRAINPOOLP512R1;
    }
    WARN_ON(1);
    return "";
}

static const char *
wm2_dpp_akm_to_str(const enum target_dpp_conf_akm akm)
{
    switch (akm) {
        case TARGET_DPP_CONF_UNKNOWN: return SCHEMA_CONSTS_DPP_AKM_UNKNOWN;
        case TARGET_DPP_CONF_PSK: return SCHEMA_CONSTS_DPP_AKM_PSK;
        case TARGET_DPP_CONF_SAE: return SCHEMA_CONSTS_DPP_AKM_SAE;
        case TARGET_DPP_CONF_PSK_SAE: return SCHEMA_CONSTS_DPP_AKM_PSK_SAE;
        case TARGET_DPP_CONF_DPP: return SCHEMA_CONSTS_DPP_AKM_DPP;
        case TARGET_DPP_CONF_DPP_SAE: return SCHEMA_CONSTS_DPP_AKM_DPP_SAE;
        case TARGET_DPP_CONF_DPP_PSK_SAE: return SCHEMA_CONSTS_DPP_AKM_DPP_PSK_SAE;
    }
    return "unknown";
}

static bool
wm2_dpp_akm_is_dpp(const enum target_dpp_conf_akm akm)
{
    return akm == TARGET_DPP_CONF_DPP ||
           akm == TARGET_DPP_CONF_DPP_SAE ||
           akm == TARGET_DPP_CONF_DPP_PSK_SAE;
}

static bool
wm2_dpp_akm_is_psk(const enum target_dpp_conf_akm akm)
{
    return akm == TARGET_DPP_CONF_PSK ||
           akm == TARGET_DPP_CONF_PSK_SAE ||
           akm == TARGET_DPP_CONF_DPP_PSK_SAE;
}

static bool
wm2_dpp_akm_is_sae(const enum target_dpp_conf_akm akm)
{
    return akm == TARGET_DPP_CONF_SAE ||
           akm == TARGET_DPP_CONF_DPP_SAE;
}

static bool
wm2_dpp_onboard_each_psk_sae(struct schema_Wifi_VIF_Config *vconf,
                             const struct target_dpp_conf_network *c,
                             const char *wpa_key_mgmt)

{
    const char *hex;
    char psk[WM2_DPP_PSK_LEN + 1] = {0};
    int i;

    if (WARN_ON(strlen(c->psk_hex) > (2 * WM2_DPP_PSK_LEN)))
        return false;
    if (WARN_ON((strlen(c->psk_hex) % 2) != 0))
        return false;

    for (i = 0, hex = c->psk_hex; *hex; i++, hex += 2)
        if (WARN_ON(sscanf(hex, "%2hhx", &psk[i]) != 1))
            return false;

    SCHEMA_VAL_APPEND(vconf->wpa_key_mgmt, wpa_key_mgmt);
    SCHEMA_VAL_APPEND(vconf->wpa_psks, psk);
    return true;
}

static bool
wm2_dpp_onboard_each_dpp(struct schema_Wifi_VIF_Config *vconf,
                         const struct target_dpp_conf_network *c)
{
    SCHEMA_VAL_APPEND(vconf->wpa_key_mgmt, SCHEMA_CONSTS_KEY_DPP);
    SCHEMA_SET_STR(vconf->dpp_connector, c->dpp_connector);
    SCHEMA_SET_STR(vconf->dpp_netaccesskey_hex, c->dpp_netaccesskey_hex);
    SCHEMA_SET_STR(vconf->dpp_csign_hex, c->dpp_csign_hex);
    return true;
}

static void
wm2_dpp_onboard_each(const char *ifname, const struct target_dpp_conf_network *c)
{
    struct schema_Wifi_VIF_Config vconf = {0};
    const char *hex;
    char ssid[WM2_DPP_SSID_LEN + 1] = {0};
    bool ok = false;
    int i;

    if (WARN_ON(strlen(c->ssid_hex) > (2 * WM2_DPP_SSID_LEN)))
        return;
    if (WARN_ON((strlen(c->ssid_hex) % 2) != 0))
        return;

    for (i = 0, hex = c->ssid_hex; *hex; i++, hex += 2)
        if (WARN_ON(sscanf(hex, "%2hhx", &ssid[i]) != 1))
            return;

    LOGD("dpp: %s: decoded ssid %s -> %s", ifname, c->ssid_hex, ssid);
    LOGI("dpp: %s: setting onboard network config", ifname);

    vconf._partial_update = true;
    vconf.security_len = 0;
    vconf.wpa_psks_len = 0;

    SCHEMA_SET_STR(vconf.if_name, ifname);
    SCHEMA_SET_INT(vconf.wpa, true);
    SCHEMA_SET_STR(vconf.ssid, ssid);

    /* TODO: Arguably this could just push data blindly to
     * Wifi_VIF_Config and let the supplicant do the job at
     * picking the key mgmt. This isn't handled / tested yet
     * and the following should be good enough for now.
     */
    switch (c->akm) {
    case TARGET_DPP_CONF_UNKNOWN:
        WARN_ON(1);
        return;
    case TARGET_DPP_CONF_SAE:
        ok = wm2_dpp_onboard_each_psk_sae(&vconf, c, SCHEMA_CONSTS_KEY_SAE);
        break;
    case TARGET_DPP_CONF_PSK_SAE:
        ok = wm2_dpp_onboard_each_psk_sae(&vconf, c, SCHEMA_CONSTS_KEY_SAE);
        break;
    case TARGET_DPP_CONF_PSK:
        ok = wm2_dpp_onboard_each_psk_sae(&vconf, c, SCHEMA_CONSTS_KEY_WPA2_PSK);
        break;
    case TARGET_DPP_CONF_DPP:
        ok = wm2_dpp_onboard_each_dpp(&vconf, c);
        break;
    case TARGET_DPP_CONF_DPP_SAE:
        ok = wm2_dpp_onboard_each_dpp(&vconf, c);
        break;
    case TARGET_DPP_CONF_DPP_PSK_SAE:
        ok = wm2_dpp_onboard_each_dpp(&vconf, c);
        break;
    }

    if (WARN_ON(!ok))
        return;

    ok = ovsdb_table_update(&table_Wifi_VIF_Config, &vconf);
    WARN_ON(!ok);
}

static int
wm2_dpp_get_vif_num_non_sta(void)
{
    struct schema_Wifi_VIF_Config *vconfs;
    const char *column;
    json_t *where;
    int n;

    column = SCHEMA_COLUMN(Wifi_VIF_Config, mode);
    where = ovsdb_tran_cond(OCLM_STR, column, OFUNC_NEQ, "sta");
    vconfs = ovsdb_table_select_where(&table_Wifi_VIF_Config, where, &n);
    free(vconfs);

    return n;
}

static void
wm2_dpp_onboard(struct wm2_dpp_job *job, const struct target_dpp_conf_network *c)
{
    const bool is_chirp = !strcmp(job->config.auth, SCHEMA_CONSTS_DPP_CHIRP);
    int i;

    if (!is_chirp)
        return;

    if (wm2_dpp_get_vif_num_non_sta() > 0) {
        LOGI("dpp: %s: non-sta vifs found, ignoring", job->config._uuid.uuid);
        return;
    }

    for (i = 0; i < job->config.ifnames_len; i++)
        wm2_dpp_onboard_each(job->config.ifnames[i], c);
}

static bool
wm2_dpp_onboard_completed(void)
{
    struct wm2_dpp_job *job;
    ds_dlist_foreach(&g_wm2_dpp_jobs, job) {
        if (!strcmp(job->config.auth, SCHEMA_CONSTS_DPP_CHIRP) &&
            (!strcmp(job->config.status, SCHEMA_CONSTS_DPP_FAILED) ||
             !strcmp(job->config.status, SCHEMA_CONSTS_DPP_TIMED_OUT) ||
             !strcmp(job->config.status, SCHEMA_CONSTS_DPP_SUCCEEDED)))
            return true;
    }
    return false;
}

static bool
wm2_dpp_job_has_ifname(const struct wm2_dpp_job *job, const char *ifname)
{
    int i;
    for (i = 0; i < job->config.ifnames_len; i++)
        if (!strcmp(job->config.ifnames[i], ifname))
            return true;
    return false;
}

static struct wm2_dpp_job *
wm2_dpp_onboard_get_current_job(void)
{
    struct wm2_dpp_job *job;
    ds_dlist_foreach(&g_wm2_dpp_jobs, job) {
        if (!strcmp(job->config.auth, SCHEMA_CONSTS_DPP_CHIRP) &&
            (!strcmp(job->config.status, SCHEMA_CONSTS_DPP_REQUESTED) ||
             !strcmp(job->config.status, SCHEMA_CONSTS_DPP_IN_PROGRESS)))
            return job;
    }
    return NULL;
}

static void
wm2_dpp_onboard_clear(void)
{
    const char *column = SCHEMA_COLUMN(DPP_Config, auth);
    const char *value = SCHEMA_CONSTS_DPP_CHIRP;

    ovsdb_table_delete_simple(&table_DPP_Config, column, value);
}

static void
wm2_dpp_recalc_onboard(void)
{
    struct schema_DPP_Config c = {0};
    struct target_dpp_key key = {0};
    struct wm2_dpp_job *job = wm2_dpp_onboard_get_current_job();
    const char *vif;
    char buf[4096] = {0};
    char *vifs = buf;
    size_t len = sizeof(buf);
    bool changed = false;
    bool ok;

    if (!g_wm2_dpp_supported)
        return;

    ok = wm2_target_dpp_key_get(&key);
    if (!ok)
        return;

    if (wm2_dpp_onboard_completed())
        return;

    if (!wm2_radio_onboard_vifs(buf, len))
        return;

    c._partial_update = true;
    SCHEMA_SET_INT(c.timeout_seconds, WM2_DPP_ONBOARD_TIMEOUT_SECONDS);
    SCHEMA_SET_STR(c.own_bi_key_curve, wm2_dpp_keytype_to_str(key.type));
    SCHEMA_SET_STR(c.own_bi_key_hex, key.hex);
    SCHEMA_SET_STR(c.auth, SCHEMA_CONSTS_DPP_CHIRP);
    SCHEMA_SET_STR(c.status, SCHEMA_CONSTS_DPP_REQUESTED);

    while ((vif = strsep(&vifs, " "))) {
        if (strlen(vif) == 0)
            continue;

        SCHEMA_VAL_APPEND(c.ifnames, vif);

        if (job && !wm2_dpp_job_has_ifname(job, vif))
            changed = true;
    }

    if (job && !changed)
        return;

    LOGI("dpp: preparing onboarding");
    wm2_dpp_onboard_clear();

    ok = ovsdb_table_insert(&table_DPP_Config, &c);
    WARN_ON(!ok);
}

static void
wm2_dpp_recalc(void)
{
    struct wm2_dpp_job *job;
    bool ok;

    LOGD("dpp: recalculating");
    wm2_dpp_jobs_pull();
    wm2_dpp_recalc_onboard();

    job = wm2_dpp_jobs_get_current();

    if (job && job->interrupted) {
        SCHEMA_SET_STR(job->config.status, SCHEMA_CONSTS_DPP_REQUESTED);
        job->interrupted = false;
        job = NULL; /* force new job selection */
    }

    if (job) {
        WARN_ON(!ev_is_active(&g_wm2_dpp_timeout_timer));
        LOGI("dpp: %s: still running (timeout in %.2lf seconds)",
             job->config._uuid.uuid,
             ev_timer_remaining(EV_DEFAULT_ &g_wm2_dpp_timeout_timer));
        return;
    }

    ev_timer_stop(EV_DEFAULT_ &g_wm2_dpp_timeout_timer);

    job = wm2_dpp_jobs_get_next();
    if (job)
        LOGD("dpp: %s: starting", job->config._uuid.uuid);
    else
        LOGD("dpp: stopping");

    ok = wm2_target_dpp_config_set(job ? &job->config : NULL);
    if (WARN_ON(!ok)) {
        if (job && job->tries_left > 0) {
            LOGI("dpp: %s: failed to start, will retry", job->config._uuid.uuid);
            job->tries_left--;
        } else if (job) {
            LOGE("dpp: %s: failed to start too many times, giving up", job->config._uuid.uuid);
            SCHEMA_SET_STR(job->config.status, SCHEMA_CONSTS_DPP_FAILED);
            ok = ovsdb_table_update(&table_DPP_Config, &job->config);
            WARN_ON(!ok);
        } else {
            LOGE("dpp: failed to stop, will retry");
        }
        wm2_dpp_recalc_schedule();
        return;
    }

    if (!job)
        return;

    ev_timer_set(&g_wm2_dpp_timeout_timer, job->config.timeout_seconds, 0);
    ev_timer_start(EV_DEFAULT_ &g_wm2_dpp_timeout_timer);

    LOGI("dpp: %s: started %s (timeout in %.2lf seconds)",
         job->config._uuid.uuid,
         job->config.auth,
         ev_timer_remaining(EV_DEFAULT_ &g_wm2_dpp_timeout_timer));

    SCHEMA_SET_STR(job->config.status, SCHEMA_CONSTS_DPP_IN_PROGRESS);
    ok = ovsdb_table_update(&table_DPP_Config, &job->config);
    WARN_ON(!ok);
}

static void
wm2_dpp_recalc_cb(EV_P_ ev_timer *t, int revents)
{
    wm2_dpp_recalc();
}

static void
wm2_dpp_timeout_cb(EV_P_ ev_timer *timer, int revents)
{
    struct wm2_dpp_job *job;
    bool ok;

    job = wm2_dpp_jobs_get_current();
    if (WARN_ON(!job))
        return;

    LOGI("dpp: %s: timed out", job->config._uuid.uuid);

    SCHEMA_SET_STR(job->config.status, SCHEMA_CONSTS_DPP_TIMED_OUT);
    ok = ovsdb_table_update(&table_DPP_Config, &job->config);
    WARN_ON(!ok);
    wm2_dpp_recalc_schedule();
}

static void
callback_DPP_Config(ovsdb_update_monitor_t *mon,
                    struct schema_DPP_Config *old,
                    struct schema_DPP_Config *row)
{
    if (!g_wm2_dpp_supported)
        return;

    wm2_dpp_recalc_schedule();
}

bool
wm2_dpp_in_progress(const char *ifname)
{
    struct wm2_dpp_job *job;
    int i;

    job = wm2_dpp_jobs_get_current();
    if (!job)
        return false;

    if (ifname == NULL)
        return true;

    for (i = 0; i < job->config.ifnames_len; i++)
        if (!strcmp(job->config.ifnames[i], ifname))
            return true;

    return false;
}

bool
wm2_dpp_is_chirping(void)
{
    struct wm2_dpp_job *job;

    job = wm2_dpp_jobs_get_current();
    return job && !strcmp(job->config.auth, SCHEMA_CONSTS_DPP_CHIRP);
}

void
wm2_dpp_interrupt(void)
{
    if (!g_wm2_dpp_supported)
        return;

    wm2_dpp_jobs_interrupt();
    wm2_dpp_recalc_schedule();
}

void
wm2_dpp_ifname_set(const char *ifname, bool enabled)
{
    struct wm2_dpp_ifname *dpp_ifname;

    if (!g_wm2_dpp_supported)
        return;

    dpp_ifname = wm2_dpp_ifname_get(ifname);

    if (!dpp_ifname && enabled)
        WARN_ON(!(dpp_ifname = wm2_dpp_ifname_add(ifname)));
    if (!enabled && dpp_ifname)
        wm2_dpp_ifname_del(dpp_ifname);

    LOGI("dpp: %s: setting %s", ifname, enabled ? "true" : "false");
}

bool
wm2_dpp_key_to_oftag(const char *key, char *oftag, int size)
{
    struct schema_DPP_Oftag row;
    const char *column;
    bool ok;

    column = SCHEMA_COLUMN(DPP_Oftag, sta_netaccesskey_sha256_hex);
    ok = ovsdb_table_select_one(&table_DPP_Oftag, column, key, &row);
    if (!ok)
        return false;

    strscpy(oftag, row.oftag, size);
    return true;
}

static void
wm2_dpp_announcement_del(struct wm2_dpp_announcement *a)
{
    LOGI("dpp: announcement: %s: aging out", a->row._uuid.uuid);
    ovsdb_table_delete(&table_DPP_Announcement, &a->row);
    ds_dlist_remove(&g_wm2_dpp_announcements, a);
    free(a);
}

static void
wm2_dpp_announcement_cleanup_cb(EV_P_ ev_timer *t, int revents)
{
    struct wm2_dpp_announcement *a;
    a = container_of(t, struct wm2_dpp_announcement, expiry);
    ev_timer_stop(EV_A_ t);
    wm2_dpp_announcement_del(a);
}

static struct wm2_dpp_announcement *
wm2_dpp_announcement_get(const char *mac_addr)
{
    struct wm2_dpp_announcement *a;

    ds_dlist_foreach(&g_wm2_dpp_announcements, a)
        if (!strcmp(a->row.sta_mac_addr, mac_addr))
            return a;

    return NULL;
}

static struct wm2_dpp_announcement *
wm2_dpp_announcement_new(const struct schema_DPP_Announcement *row)
{
    struct wm2_dpp_announcement *a;
    float sec = WM2_DPP_ANNOUNCEMENT_CLEANUP_SECONDS;

    a = malloc(sizeof(*a));
    if (!a) return NULL;

    ds_dlist_insert_tail(&g_wm2_dpp_announcements, a);
    memcpy(&a->row, row, sizeof(*row));
    ev_init(&a->expiry, wm2_dpp_announcement_cleanup_cb);
    ev_timer_set(&a->expiry, sec, sec);
    ev_timer_again(EV_DEFAULT_ &a->expiry);
    LOGD("dpp: announcement: %s: first seen, starting purge timer", row->_uuid.uuid);

    return a;
}

static bool
wm2_dpp_announcement_pull(void)
{
    struct schema_DPP_Announcement *rows;
    struct schema_DPP_Announcement *row;
    struct wm2_dpp_announcement *a;
    int n;

    rows = ovsdb_table_select_where(&table_DPP_Announcement, json_array(), &n);
    for (row = rows; row && n; row++, n--) {
        a = wm2_dpp_announcement_get(row->sta_mac_addr);
        if (!a) a = wm2_dpp_announcement_new(row);
        if (!a) break; /* enomem */
        if (!strcmp(a->row._version.uuid, row->_version.uuid)) continue;

        LOGD("dpp: announcement: %s: seen again (diff version uuid), bumping timer", a->row._uuid.uuid);
        ev_timer_again(EV_DEFAULT_ &a->expiry);
        memcpy(&a->row, row, sizeof(*row));
    }

    free(rows);

    return n == 0;
}

static void
callback_DPP_Announcement(ovsdb_update_monitor_t *mon,
                          struct schema_DPP_Announcement *old,
                          struct schema_DPP_Announcement *row)
{
    if (!g_wm2_dpp_supported)
        return;

    wm2_dpp_announcement_pull();
}

void
wm2_dpp_op_announcement(const struct target_dpp_chirp_obj *c)
{
    struct schema_DPP_Announcement row;
    struct wm2_dpp_announcement *a;
    bool ok;
    int err = 0;

    if (WARN_ON(!g_wm2_dpp_supported))
        return;

    err |= WARN_ON(!c->ifname);
    err |= WARN_ON(!c->mac_addr);
    err |= WARN_ON(!c->sha256_hex);
    if (err)
        return;

    LOGI("dpp: %s: %s: announcement with sha256 %s", c->ifname, c->mac_addr, c->sha256_hex);

    a = wm2_dpp_announcement_get(c->mac_addr);
    if (a) {
        LOGD("dpp: announcement: %s: seen again, bumping timer", a->row._uuid.uuid);
        ev_timer_again(EV_DEFAULT_ &a->expiry);
    }

    memset(&row, 0, sizeof(row));
    SCHEMA_SET_STR(row.sta_mac_addr, c->mac_addr);
    SCHEMA_SET_STR(row.chirp_sha256_hex, c->sha256_hex);
    str_tolower(row.sta_mac_addr);

    ok = ovsdb_table_upsert(&table_DPP_Announcement, &row, false);
    WARN_ON(!ok);
}

void
wm2_dpp_op_conf_enrollee(const struct target_dpp_conf_enrollee *c)
{
    struct wm2_dpp_job *job;
    bool ok;
    int err = 0;

    if (WARN_ON(!g_wm2_dpp_supported))
        return;

    job = wm2_dpp_jobs_get_current();
    if (WARN_ON(!job))
        return;

    err |= WARN_ON(!c->sta_mac_addr);
    err |= WARN_ON(!c->sta_netaccesskey_sha256_hex);

    LOGI("dpp: %s: %s: enrollee configuration handed out", c->ifname, c->sta_mac_addr);

    if (err)
        SCHEMA_SET_STR(job->config.status, SCHEMA_CONSTS_DPP_FAILED);
    else
        SCHEMA_SET_STR(job->config.status, SCHEMA_CONSTS_DPP_SUCCEEDED);

    if (c->sta_mac_addr)
        SCHEMA_SET_STR(job->config.sta_mac_addr, c->sta_mac_addr);
    if (c->sta_netaccesskey_sha256_hex)
        SCHEMA_SET_STR(job->config.sta_netaccesskey_sha256_hex, c->sta_netaccesskey_sha256_hex);

    str_tolower(job->config.sta_mac_addr);

    ok = ovsdb_table_update(&table_DPP_Config, &job->config);
    WARN_ON(!ok);

    wm2_dpp_recalc_schedule();
}

void
wm2_dpp_op_conf_network(const struct target_dpp_conf_network *c)
{
    struct wm2_dpp_job *job;
    bool ok;
    int err = 0;

    if (WARN_ON(!g_wm2_dpp_supported))
        return;

    job = wm2_dpp_jobs_get_current();
    if (WARN_ON(!job))
        return;

    LOGI("dpp: %s: network configuration obtained", c->ifname);

    SCHEMA_SET_STR(job->config.akm, wm2_dpp_akm_to_str(c->akm));

    if (wm2_dpp_akm_is_psk(c->akm) || wm2_dpp_akm_is_sae(c->akm)) {
        err |= WARN_ON(!c->ssid_hex);
        err |= WARN_ON(!c->psk_hex);
        err |= WARN_ON(!c->psk_hex && !c->pmk_hex);
    }

    if (wm2_dpp_akm_is_dpp(c->akm)) {
        err |= WARN_ON(!c->ssid_hex);
        err |= WARN_ON(!c->dpp_netaccesskey_hex);
        err |= WARN_ON(!c->dpp_connector);
        err |= WARN_ON(!c->dpp_csign_hex);
    }

    if (err)
        SCHEMA_SET_STR(job->config.status, SCHEMA_CONSTS_DPP_FAILED);
    else
        SCHEMA_SET_STR(job->config.status, SCHEMA_CONSTS_DPP_SUCCEEDED);

    if (c->ssid_hex)
        SCHEMA_SET_STR(job->config.ssid_hex, c->ssid_hex);
    if (c->psk_hex)
        SCHEMA_SET_STR(job->config.psk_hex, c->psk_hex);
    if (c->pmk_hex)
        SCHEMA_SET_STR(job->config.pmk_hex, c->pmk_hex);
    if (c->dpp_netaccesskey_hex)
        SCHEMA_SET_STR(job->config.dpp_netaccesskey_hex, c->dpp_netaccesskey_hex);
    if (c->dpp_connector)
        SCHEMA_SET_STR(job->config.dpp_connector, c->dpp_connector);
    if (c->dpp_csign_hex)
        SCHEMA_SET_STR(job->config.dpp_csign_hex, c->dpp_csign_hex);

    ok = ovsdb_table_update(&table_DPP_Config, &job->config);
    WARN_ON(!ok);

    if (err == 0)
        wm2_dpp_onboard(job, c);

    wm2_dpp_recalc_schedule();
}

void
wm2_dpp_op_conf_failed(void)
{
    struct wm2_dpp_job *job;
    bool ok;

    if (WARN_ON(!g_wm2_dpp_supported))
        return;

    job = wm2_dpp_jobs_get_current();
    if (WARN_ON(!job))
        return;

    LOGI("dpp: configuration failed");

    SCHEMA_SET_STR(job->config.status, SCHEMA_CONSTS_DPP_FAILED);

    ok = ovsdb_table_update(&table_DPP_Config, &job->config);
    WARN_ON(!ok);

    wm2_dpp_recalc_schedule();
}

void
wm2_dpp_init(void)
{
    LOGD("dpp: initializing");

    OVSDB_TABLE_INIT(DPP_Config, _uuid);
    OVSDB_TABLE_INIT(DPP_Announcement, sta_mac_addr);
    OVSDB_TABLE_INIT(DPP_Oftag, sta_netaccesskey_sha256_hex);
    OVSDB_TABLE_MONITOR_F(DPP_Config, C_VPACK("-", "status"));
    OVSDB_TABLE_MONITOR(DPP_Announcement, false);

    ev_init(&g_wm2_dpp_recalc_timer, wm2_dpp_recalc_cb);
    ev_init(&g_wm2_dpp_timeout_timer, wm2_dpp_timeout_cb);

    g_wm2_dpp_supported = wm2_target_dpp_supported();

    if (g_wm2_dpp_supported)
        LOGI("dpp: supported");
}
