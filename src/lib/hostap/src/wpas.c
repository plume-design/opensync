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

#define _GNU_SOURCE

/* libc */
#include <errno.h>
#include <string.h>
#include <net/if.h>
#include <linux/un.h>
#include <linux/limits.h>
#include <assert.h>

/* other */
#include <ev.h>
#include <wpa_ctrl.h>

/* opensync */
#include <util.h>
#include <const.h>
#include <log.h>
#include <kconfig.h>
#include <opensync-ctrl.h>
#include <opensync-wpas.h>

/* local */
#define F(...) strfmta(__VA_ARGS__)
#define E(...) strexa(__VA_ARGS__)
#define R(...) file_geta(__VA_ARGS__)
#define W(...) file_put(__VA_ARGS__)
#if CONFIG_HOSTAP_TIMEOUT_T_SWITCH
#define CMD_TIMEOUT(...) "timeout", "-s", "KILL", "-t", "3", ## __VA_ARGS__
#else
#define CMD_TIMEOUT(...) "timeout", "-s", "KILL", "3", ## __VA_ARGS__
#endif

#define CONFIG_WPAS_DRIVER "nl80211" // FIXME: kconfig
#define CONFIG_WPAS_MAX_BSS 8 // FIXME: kconfig

#define WPAS_SOCK_PATH(dphy, dvif) F("/var/run/wpa_supplicant-%s/%s", dphy, dvif)
#define WPAS_SOCK_DIR(dphy) F("/var/run/wpa_supplicant-%s", dphy)
#define WPAS_CONF_PATH(dvif) F("/var/run/wpa_supplicant-%s.config", dvif)
#define WPAS_GLOB_CLI(...) E(CMD_TIMEOUT("wpa_cli", "-g", "/var/run/wpa_supplicantglobal", ## __VA_ARGS__))
#define WPAS_CLI(wpas, ...) E(CMD_TIMEOUT("wpa_cli", "-p", wpas->ctrl.sockdir, "-i", wpas->ctrl.bss, ## __VA_ARGS__))
#define EV(x) strchomp(strdupa(x), " ")

#define MODULE_ID LOG_MODULE_ID_WPAS

static struct wpas g_wpas[CONFIG_WPAS_MAX_BSS];

static int
wpas_supports_disallow_dfs(void)
{
    return system("which wpa_supplicant | xargs grep -q disallow_dfs") == 0;
}

struct wpas *
wpas_lookup(const char *bss)
{
    size_t i;
    for (i = 0; i < ARRAY_SIZE(g_wpas); i++)
        if (strcmp(bss, g_wpas[i].ctrl.bss) == 0)
            return &g_wpas[i];
    return NULL;
}

static struct wpas *
wpas_lookup_unused(void)
{
    size_t i;
    for (i = 0; i < ARRAY_SIZE(g_wpas); i++)
        if (strlen(g_wpas[i].ctrl.bss) == 0)
            return &g_wpas[i];
    LOGE("out of memory");
    assert(0);
    return NULL;
}

static void
wpas_ctrl_cb(struct ctrl *ctrl, int level, const char *buf, size_t len)
{
    struct wpas *wpas = container_of(ctrl, struct wpas, ctrl);
    const char *id_str = NULL;
    const char *bssid = NULL;
    const char *event;
    const char *k;
    const char *v;
    char *str = strdupa(buf);
    char *kv;
    int reason = 0;
    int local = 0;
    int id = 0;
    int ret = 0;
    int retry = 0;

    /* CTRL-EVENT-CONNECTED - Connection to 00:1d:73:73:88:ea completed [id=0 id_str=]
     * CTRL-EVENT-DISCONNECTED bssid=00:1d:73:73:88:ea reason=3 locally_generated=1
     */
    event = strsep(&str, " ");

    if (!strcmp(event, EV(WPA_EVENT_CONNECTED))) {
        strsep(&str, " "); /* "-" */
        strsep(&str, " "); /* "Connection" */
        strsep(&str, " "); /* "to" */
        bssid = strsep(&str, " ");
        strsep(&str, " ["); // completed
        strchomp(str, "]");

        while ((kv = strsep(&str, " "))) {
            if ((k = strsep(&kv, "=")) &&
                (v = strsep(&kv, ""))) {
                if (!strcmp(k, "id"))
                    id = atoi(v);
                else if (!strcmp(k, "id_str"))
                    id_str = v;
            }
        }

        LOGI("%s: %s: connected id=%d id_str=%s", wpas->ctrl.bss, bssid, id, id_str);
        if (wpas->connected)
            wpas->connected(wpas, bssid, id, id_str);

        return;
    }

    if (!strcmp(event, EV(WPA_EVENT_DISCONNECTED))) {
        while ((kv = strsep(&str, " "))) {
            if ((k = strsep(&kv, "=")) &&
                (v = strsep(&kv, ""))) {
                if (!strcmp(k, "bssid"))
                    bssid = v;
                else if (!strcmp(k, "reason"))
                    reason = atoi(v);
                else if (!strcmp(k, "locally_generated"))
                    local = atoi(v);
            }
        }

        LOGI("%s: %s: disconnected reason=%d local=%d", wpas->ctrl.bss, bssid, reason, local);
        if (wpas->disconnected)
            wpas->disconnected(wpas, bssid, reason, local);

        return;
    }

    if (!strcmp(event, EV(WPA_EVENT_SCAN_RESULTS))) {
        LOGI("%s: scan results available", wpas->ctrl.bss);
        if (wpas->scan_results)
            wpas->scan_results(wpas);

        return;
    }

    if (!strcmp(event, EV(WPA_EVENT_SCAN_FAILED))) {
        while ((kv = strsep(&str, " "))) {
            if ((k = strsep(&kv, "=")) &&
                (v = strsep(&kv, ""))) {
                if (!strcmp(k, "ret"))
                    ret = atoi(v);
                else if (!strcmp(k, "retry"))
                    retry = atoi(v);
            }
        }

        LOGI("%s: scan failed ret=%d retry=%d", wpas->ctrl.bss, ret, retry);
        if (wpas->scan_failed)
            wpas->scan_failed(wpas, ret);

        return;
    }

    if (!strcmp(event, EV(WPA_EVENT_BSS_ADDED))) {
        LOGD("%s: event: <%d> %s", ctrl->bss, level, buf);
        return;
    }

    if (!strcmp(event, EV(WPA_EVENT_BSS_REMOVED))) {
        LOGD("%s: event: <%d> %s", ctrl->bss, level, buf);
        return;
    }

    LOGI("%s: event: <%d> %s", ctrl->bss, level, buf);
}

static struct wpas *
wpas_init(struct wpas *wpas, const char *phy, const char *bss)
{
    LOGI("%s: initializing", bss);
    memset(wpas, 0, sizeof(*wpas));
    STRSCPY_WARN(wpas->driver, CONFIG_WPAS_DRIVER);
    STRSCPY_WARN(wpas->ctrl.bss, bss);
    STRSCPY_WARN(wpas->ctrl.sockdir, WPAS_SOCK_DIR(phy));
    STRSCPY_WARN(wpas->ctrl.sockpath, WPAS_SOCK_PATH(phy, bss));
    STRSCPY_WARN(wpas->confpath, WPAS_CONF_PATH(bss));
    STRSCPY_WARN(wpas->phy, phy);
    wpas->ctrl.cb = wpas_ctrl_cb;
    return wpas;
}

struct wpas *
wpas_new(const char *phy, const char *bss)
{
    struct wpas *wpas = wpas_lookup_unused();
    if (wpas) wpas_init(wpas, phy, bss);
    return wpas;
}

static const char *
wpas_util_get_proto(int wpa)
{
    switch (wpa) {
        case 1: return "WPA";
        case 2: return "RSN";
        case 3: return "WPA RSN";
    }
    return "";
}

static int
wpas_util_get_mode(const char *mode)
{
    if (!strcmp(mode, "mixed")) return 3;
    else if (!strcmp(mode, "2")) return 2;
    else if (!strcmp(mode, "1")) return 1;
    else if (strlen(mode) == 0) return 2;

    LOGW("%s: unknown mode string: '%s'", __func__, mode);
    return 2;
}

static const char *
wpas_util_get_pairwise(int wpa)
{
    switch (wpa) {
        case 1: return "TKIP";
        case 2: return "CCMP";
        case 3: return "CCMP TKIP";
    }
    LOGW("%s: unhandled wpa mode %d", __func__, wpa);
    return "";
}

static char *
wpas_conf_gen_freqlist(struct wpas *wpas, char *freqs, size_t len)
{
    int onboard[] = {
        /* the list excludes dfs channels to avoid inheriting cac */
        2412, 2417, 2422, 2427, 2432, 2437, 2442,
        2447, 2452, 2457, 2462, 2467, 2472, 2484,
        5180, 5200, 5220, 5240, 5745, 5765, 5785, 5805,
    };
    size_t i;
    size_t j;

    memset(freqs, 0, len);

    for (i = 0; i < ARRAY_SIZE(wpas->freqlist); i++)
        for (j = 0; j < ARRAY_SIZE(onboard); j++)
            if (wpas->freqlist[i] && wpas->freqlist[i] == onboard[j])
                csnprintf(&freqs, &len, "%d ", wpas->freqlist[i]);

    return strchomp(freqs, " ");
}

static int
wpas_map_str2int(const struct schema_Wifi_VIF_Config *vconf)
{
    if (!vconf->multi_ap_exists) return 0;
    if (!strcmp(vconf->multi_ap, "none")) return 0;
    if (!strcmp(vconf->multi_ap, "backhaul_sta")) return 1;
    WARN_ON(1);
    return 0;
}

static const char *
wpas_map_int2str(int i)
{
    switch (i) {
        case 0: return "none";
        case 1: return "backhaul_sta";
    }
    WARN_ON(1);
    return "none";
}

int
wpas_conf_gen(struct wpas *wpas,
              const struct schema_Wifi_Radio_Config *rconf,
              const struct schema_Wifi_VIF_Config *vconf,
              const struct schema_Wifi_Credential_Config *cconfs,
              size_t n_cconfs)
{
    const char *wpa_passphrase;
    const char *wpa_pairwise;
    const char *wpa_key_mgmt;
    const char *wpa_proto;
    size_t len = sizeof(wpas->conf);
    char *buf = wpas->conf;
    char freqlist[512];
    bool dfs_allowed = (vconf->parent_exists && strlen(vconf->parent) > 0);
    int wpa;

    memset(wpas->conf, 0, sizeof(wpas->conf));

    if (!vconf->enabled)
        return 0;

    csnprintf(&buf, &len, "ctrl_interface=%s\n", wpas->ctrl.sockdir);
    csnprintf(&buf, &len, "%s", wpas_supports_disallow_dfs() ? "" : "#");
    csnprintf(&buf, &len, "disallow_dfs=%d\n", !(vconf->parent_exists && strlen(vconf->parent) > 0));
    csnprintf(&buf, &len, "scan_cur_freq=%d\n", vconf->parent_exists && strlen(vconf->parent) > 0);
    csnprintf(&buf, &len, "#bridge=%s\n", vconf->bridge_exists ? vconf->bridge : "");

    wpa = wpas_util_get_mode(SCHEMA_KEY_VAL(vconf->security, "mode"));
    wpa_pairwise = wpas_util_get_pairwise(wpa);
    wpa_proto = wpas_util_get_proto(wpa);
    wpa_key_mgmt = SCHEMA_KEY_VAL(vconf->security, "encryption");
    wpa_passphrase = SCHEMA_KEY_VAL(vconf->security, "key");
    wpas_conf_gen_freqlist(wpas, freqlist, sizeof(freqlist));

    if (vconf->security_len > 0 &&
        vconf->ssid_exists &&
        strlen(wpa_passphrase) > 0) {
        /* FIXME: Unify security and creds generation */
        csnprintf(&buf, &len, "network={\n");
        csnprintf(&buf, &len, "\tscan_ssid=1\n");
        csnprintf(&buf, &len, "\tbgscan=\"\"\n");
        csnprintf(&buf, &len, "\tscan_freq=%s\n", dfs_allowed ? "" : freqlist);
        csnprintf(&buf, &len, "\tfreq_list=%s\n", dfs_allowed ? "" : freqlist);
        csnprintf(&buf, &len, "\tssid=\"%s\"\n", vconf->ssid);
        csnprintf(&buf, &len, "\tpsk=\"%s\"\n", wpa_passphrase);
        csnprintf(&buf, &len, "\tkey_mgmt=%s\n", wpa_key_mgmt);
        csnprintf(&buf, &len, "\tpairwise=%s\n", wpa_pairwise);
        csnprintf(&buf, &len, "\tproto=%s\n", wpa_proto);
        csnprintf(&buf, &len, "\t%s", wpas->respect_multi_ap ? "" : "#");
        csnprintf(&buf, &len, "multi_ap_backhaul_sta=%d\n", wpas_map_str2int(vconf));
        csnprintf(&buf, &len, "\t%s", strlen(vconf->parent) > 0 ? "" : "#");
        csnprintf(&buf, &len, "bssid=%s\n", vconf->parent);
        csnprintf(&buf, &len, "}\n");

        /* Credential_Config is supposed to be used only
         * during initial onboarding/bootstrap. After that
         * the cloud is supposed to always provide a single
         * parent to connect to.
         */
        return len > 1;
    }

    for (; n_cconfs; n_cconfs--, cconfs++) {
        wpa = wpas_util_get_mode(SCHEMA_KEY_VAL(vconf->security, "mode"));
        wpa_pairwise = wpas_util_get_pairwise(wpa);
        wpa_proto = wpas_util_get_proto(wpa);
        wpa_key_mgmt = SCHEMA_KEY_VAL(cconfs->security, "encryption");
        wpa_passphrase = SCHEMA_KEY_VAL(cconfs->security, "key");

        csnprintf(&buf, &len, "network={\n");
        csnprintf(&buf, &len, "\tscan_ssid=1\n");
        csnprintf(&buf, &len, "\tbgscan=\"\"\n");
        csnprintf(&buf, &len, "\tscan_freq=%s\n", dfs_allowed ? "" : freqlist);
        csnprintf(&buf, &len, "\tfreq_list=%s\n", dfs_allowed ? "" : freqlist);
        csnprintf(&buf, &len, "\tssid=\"%s\"\n", cconfs->ssid);
        csnprintf(&buf, &len, "\tpsk=\"%s\"\n", wpa_passphrase);
        csnprintf(&buf, &len, "\tkey_mgmt=%s\n", wpa_key_mgmt);
        csnprintf(&buf, &len, "\tpairwise=%s\n", wpa_pairwise);
        csnprintf(&buf, &len, "\tproto=%s\n", wpa_proto);
        csnprintf(&buf, &len, "\t%s", wpas->respect_multi_ap ? "" : "#");
        csnprintf(&buf, &len, "multi_ap_backhaul_sta=%d\n", wpas_map_str2int(vconf));
        csnprintf(&buf, &len, "\t%s", strlen(vconf->parent) > 0 ? "" : "#");
        csnprintf(&buf, &len, "bssid=%s\n", vconf->parent);
        csnprintf(&buf, &len, "}\n");
    }

    return len > 1;
}

static int
wpas_ctrl_add(struct wpas *wpas)
{
    const char *bridge = ini_geta(wpas->conf, "#bridge") ?: "";
    int err = 0;
    LOGI("%s: adding", wpas->ctrl.bss);
    err |= strcmp("OK", WPAS_GLOB_CLI("interface_add", wpas->ctrl.bss, wpas->confpath, wpas->driver, wpas->ctrl.sockdir, "", bridge) ?: "");
    err |= strcmp("OK", WPAS_CLI(wpas, "log_level", "DEBUG") ?: "");
    return err;
}

static int
wpas_ctrl_remove(struct wpas *wpas)
{
    LOGI("%s: removing", wpas->ctrl.bss);
    return strcmp("OK", WPAS_GLOB_CLI("interface_remove", wpas->ctrl.bss) ?: "");
}

static int
wpas_ctrl_reload(struct wpas *wpas)
{
    int err = 0;
    LOGI("%s: reloading", wpas->ctrl.bss);
    err |= strcmp("OK", WPAS_CLI(wpas, "reconfigure") ?: "");
    err |= strcmp("OK", WPAS_CLI(wpas, "reassoc") ?: "");
    return err;
}

static int
wpas_configured(struct wpas *wpas)
{
    return strlen(wpas->conf) > 0;
}

int
wpas_conf_apply(struct wpas *wpas)
{
    const char *oldconf = R(wpas->confpath) ?: "";
    const char *oldbr = ini_geta(oldconf, "#bridge") ?: "";
    const char *newbr = ini_geta(wpas->conf, "#bridge") ?: "";
    int changed_br = strcmp(oldbr, newbr);
    int changed = strcmp(oldconf, wpas->conf);
    int add = !ctrl_running(&wpas->ctrl) && wpas_configured(wpas);
    int reload = ctrl_running(&wpas->ctrl) && wpas_configured(wpas) && changed;
    int remove = ctrl_running(&wpas->ctrl) && !wpas_configured(wpas);
    int err = 0;

    if (changed) WARN_ON(W(wpas->confpath, wpas->conf) < 0);
    if (reload && changed_br) err |= WARN_ON(wpas_ctrl_remove(wpas));
    if (add || changed_br) err |= WARN_ON(wpas_ctrl_add(wpas));
    if (reload && !changed_br) err |= WARN_ON(wpas_ctrl_reload(wpas));
    if (remove) err |= WARN_ON(wpas_ctrl_remove(wpas));

    return err;
}

static void
wpas_bss_get_network(struct schema_Wifi_VIF_State *vstate,
                     const char *conf,
                     const char *status)
{
    const char *state = ini_geta(status, "wpa_state") ?: "";
    const char *bridge = ini_geta(conf, "#bridge") ?: "";
    const char *bssid = ini_geta(status, "bssid");
    const char *ssid = ini_geta(status, "ssid");
    const char *id = ini_geta(status, "id") ?: "0";
    const char *map;
    char *psk;
    char *network = strdupa(conf);
    int n = atoi(id) + 1;

    if (strcmp(state, "COMPLETED"))
        return;

    while (network && n-- > 0)
        if ((network = strstr(network, "network={")))
            network += strlen("network={");

    if (!network)
        network = strdupa("");

    map = ini_geta(network, "multi_ap_backhaul_sta");
    psk = ini_geta(network, "psk") ?: "";

    /* entry in file looks actually like this: psk="passphrase", so remove the: " */
    if (strlen(psk) > 0) psk++;
    if (strlen(psk) > 0) psk[strlen(psk)-1] = 0;

    if ((vstate->parent_exists = (bssid != NULL)))
        STRSCPY_WARN(vstate->parent, bssid);
    if ((vstate->ssid_exists = (ssid != NULL)))
        STRSCPY_WARN(vstate->ssid, ssid);

    SCHEMA_KEY_VAL_APPEND(vstate->security, "encryption", "WPA-PSK");
    SCHEMA_KEY_VAL_APPEND(vstate->security, "key", psk);
    SCHEMA_SET_STR(vstate->multi_ap, wpas_map_int2str(atoi(map ?: "0")));
    SCHEMA_SET_STR(vstate->bridge, bridge);
}

int
wpas_bss_get(struct wpas *wpas,
             struct schema_Wifi_VIF_State *vstate)
{
    const char *status = WPAS_CLI(wpas, "status") ?: "";
    const char *conf = R(wpas->confpath) ?: "";

    wpas_bss_get_network(vstate, conf, status);
    return 0;
}

void
wpas_destroy(struct wpas *wpas)
{
    wpas_ctrl_remove(wpas);
    ctrl_disable(&wpas->ctrl);
    memset(wpas, 0, sizeof(*wpas));
}
