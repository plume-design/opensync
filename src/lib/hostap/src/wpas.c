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
#include "internal-util.h"

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
#ifndef DPP_CLI_UNSUPPORTED
#define DPP_CLI_UNSUPPORTED "not supported"
#ifndef DPP_EVENT_AUTH_SUCCESS
#define DPP_EVENT_AUTH_SUCCESS DPP_CLI_UNSUPPORTED
#endif
#endif
#ifndef DPP_EVENT_CONFOBJ_SSID
#define DPP_EVENT_CONFOBJ_SSID DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_CONNECTOR
#define DPP_EVENT_CONNECTOR DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_C_SIGN_KEY
#define DPP_EVENT_C_SIGN_KEY DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_NET_ACCESS_KEY
#define DPP_EVENT_NET_ACCESS_KEY DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_CONF_REQ_RX
#define DPP_EVENT_CONF_REQ_RX DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_CONF_RECEIVED
#define DPP_EVENT_CONF_RECEIVED DPP_CLI_UNSUPPORTED
#endif

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

static enum target_dpp_conf_akm
wpas_dpp_akm_str2enum(const char *s)
{
    if (!strcmp(s, "dpp")) return TARGET_DPP_CONF_DPP;
    if (!strcmp(s, "psk")) return TARGET_DPP_CONF_PSK;
    if (!strcmp(s, "sae")) return TARGET_DPP_CONF_SAE;
    if (!strcmp(s, "psk+sae")) return TARGET_DPP_CONF_PSK_SAE;
    if (!strcmp(s, "dpp+sae")) return TARGET_DPP_CONF_DPP_SAE;
    if (!strcmp(s, "dpp+psk+sae")) return TARGET_DPP_CONF_DPP_PSK_SAE;
    if (!strcmp(s, "dot1x")) return TARGET_DPP_CONF_UNKNOWN;
    if (!strcmp(s, "??")) return TARGET_DPP_CONF_UNKNOWN;
    return TARGET_DPP_CONF_UNKNOWN;
}

void wpas_dpp_conf_received_callback(struct wpas *wpas)
{
    if (wpas->dpp_conf_received)
        // Aggregate multiple CLI Messages here
        if (strlen(wpas->dpp_enrollee_conf_ssid_hex) > 1
            && strlen(wpas->dpp_enrollee_conf_connector) > 1
            && strlen(wpas->dpp_enrollee_conf_netaccesskey_hex) > 1
            && strlen(wpas->dpp_enrollee_conf_csign_hex) > 1)
            {
                struct target_dpp_conf_network dpp_enrollee_conf = {
                    .ifname = wpas->ctrl.bss,
                    .ssid_hex = wpas->dpp_enrollee_conf_ssid_hex,
                    .dpp_connector = wpas->dpp_enrollee_conf_connector,
                    .dpp_netaccesskey_hex = wpas->dpp_enrollee_conf_netaccesskey_hex,
                    .dpp_csign_hex = wpas->dpp_enrollee_conf_csign_hex,
                    .akm = wpas_dpp_akm_str2enum(wpas->dpp_enrollee_conf_akm),
                    .psk_hex = wpas->dpp_enrollee_conf_psk_hex,
                };
                wpas->dpp_conf_received(&dpp_enrollee_conf);

                // Clear out wpas buffers
                memset(wpas->dpp_enrollee_conf_ssid_hex, 0, sizeof(wpas->dpp_enrollee_conf_ssid_hex));
                memset(wpas->dpp_enrollee_conf_connector, 0, sizeof(wpas->dpp_enrollee_conf_connector));
                memset(wpas->dpp_enrollee_conf_netaccesskey_hex, 0, sizeof(wpas->dpp_enrollee_conf_netaccesskey_hex));
                memset(wpas->dpp_enrollee_conf_csign_hex, 0, sizeof(wpas->dpp_enrollee_conf_csign_hex));
                memset(wpas->dpp_enrollee_conf_psk_hex, 0, sizeof(wpas->dpp_enrollee_conf_psk_hex));
                memset(wpas->dpp_enrollee_conf_akm, 0, sizeof(wpas->dpp_enrollee_conf_akm));
                return;
        }
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

    //DPP Events for Enrollee
    if (!strcmp(event, EV(DPP_EVENT_CONFOBJ_SSID))) {
        if (ascii2hex(str, wpas->dpp_enrollee_conf_ssid_hex, sizeof(wpas->dpp_enrollee_conf_ssid_hex))) {
            LOGI("%s: dpp conf received ssid: %s, hex: %s", wpas->ctrl.bss, str, wpas->dpp_enrollee_conf_ssid_hex);
            wpas_dpp_conf_received_callback(wpas);
        }
        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONNECTOR))) {
        LOGI("%s: dpp connector received: %s", wpas->ctrl.bss, str);
        STRSCPY_WARN(wpas->dpp_enrollee_conf_connector, str);
        wpas_dpp_conf_received_callback(wpas);

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_C_SIGN_KEY))) {
        LOGI("%s: dpp c-sign key recieved: %s", wpas->ctrl.bss, str);
        STRSCPY_WARN(wpas->dpp_enrollee_conf_csign_hex, str);
        wpas_dpp_conf_received_callback(wpas);

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_NET_ACCESS_KEY))) {
        LOGI("%s: dpp net access key recieved: %s", wpas->ctrl.bss, str);
        STRSCPY_WARN(wpas->dpp_enrollee_conf_netaccesskey_hex, str);
        wpas_dpp_conf_received_callback(wpas);

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONFOBJ_PASS))) {
        LOGI("%s: dpp pass recieved: %s", wpas->ctrl.bss, str);
        STRSCPY_WARN(wpas->dpp_enrollee_conf_psk_hex, str);
        wpas_dpp_conf_received_callback(wpas);

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONFOBJ_AKM))) {
        LOGI("%s: dpp akm recieved: %s", wpas->ctrl.bss, str);
        STRSCPY_WARN(wpas->dpp_enrollee_conf_akm, str);
        wpas_dpp_conf_received_callback(wpas);

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONF_RECEIVED))) {
        LOGI("%s: dpp conf received", wpas->ctrl.bss);
        if (wpas->dpp_pending_auth_success && wpas->dpp_conf_received)
            wpas->dpp_pending_auth_success = 0;

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_AUTH_SUCCESS))) {
        LOGI("%s: dpp auth success received", wpas->ctrl.bss);
        wpas->dpp_pending_auth_success = 1;

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
wpas_util_get_proto_legacy(int wpa)
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

static void
wpas_util_get_wpa_proto(const struct schema_Wifi_VIF_Config *vconf,
                        char *buf,
                        size_t len)
{
    memset(buf, 0, len);

    if (!vconf->wpa)
        return;

    csnprintf(&buf, &len, "RSN");

    WARN_ON(len == 1); /* likely buf was truncated */
}

static bool
wpas_util_validate_key_mgmt(const struct schema_Wifi_VIF_Config *vconf)
{
    /* At the moment STA supports solely WPA2-PSK and/or SAE */

    const char *valid_key_mgmts[] = { "wpa2-psk", "sae", "dpp" };
    size_t j;
    int i;

    if (vconf->wpa_key_mgmt_len == 0)
        return false;

    for (i = 0; i < vconf->wpa_key_mgmt_len; i++) {
        for (j = 0; j < ARRAY_SIZE(valid_key_mgmts); j++) {
            if (strcmp(vconf->wpa_key_mgmt[i], valid_key_mgmts[j]) == 0)
                break;
        }

        if (j == ARRAY_SIZE(valid_key_mgmts))
            return false;
    }

    return true;
}

bool
wpas_conf_gen_vif_network(struct wpas *wpas,
                          const struct schema_Wifi_VIF_Config *vconf,
                          char **buf,
                          size_t *len)
{
    const char *wpa_passphrase;
    char wpa_pairwise[64];
    char wpa_key_mgmt[64];
    char ieee80211w[64];
    char wpa_proto[64];
    bool dfs_allowed;
    char freqlist[512];
    bool psk;

    if (!vconf->wpa) {
        LOGW("%s: open security mode is not supported for sta", wpas->ctrl.bss);
        return false;
    }

    if (!wpas_util_validate_key_mgmt(vconf)) {
        LOGW("%s: unsupported security modes (Wifi_VIF_Config::wpa_key_mgmt) configured for sta",
             wpas->ctrl.bss);
        return false;
    }

    if (vconf->wpa_psks_len > 1)
        LOGW("%s: Too many PSKs were configured for STA, first PSK will be used", wpas->ctrl.bss);

    wpas_util_get_wpa_proto(vconf, ARRAY_AND_SIZE(wpa_proto));
    util_vif_get_wpa_pairwise(vconf, ARRAY_AND_SIZE(wpa_pairwise));
    util_vif_get_wpa_key_mgmt(vconf, ARRAY_AND_SIZE(wpa_key_mgmt));
    util_vif_get_ieee80211w(vconf, ARRAY_AND_SIZE(ieee80211w));
    wpa_passphrase = vconf->wpa_psks[0];
    wpas_conf_gen_freqlist(wpas, freqlist, sizeof(freqlist));
    dfs_allowed = (vconf->parent_exists && strlen(vconf->parent) > 0);
    psk = strstr(wpa_key_mgmt, "-PSK") || strstr(wpa_key_mgmt, "SAE");

    csnprintf(buf, len, "network={\n");
    csnprintf(buf, len, "\tscan_ssid=1\n");
    csnprintf(buf, len, "\tbgscan=\"\"\n");
    csnprintf(buf, len, "\tscan_freq=%s\n", dfs_allowed ? "" : freqlist);
    csnprintf(buf, len, "\tfreq_list=%s\n", dfs_allowed ? "" : freqlist);
    csnprintf(buf, len, "\tssid=\"%s\"\n", vconf->ssid);
    csnprintf(buf, len, "\t%s", psk ? "" : "#");
    csnprintf(buf, len, "psk=\"%s\"\n", wpa_passphrase);
    csnprintf(buf, len, "\tkey_mgmt=%s\n", wpa_key_mgmt);
    csnprintf(buf, len, "\tpairwise=%s\n", wpa_pairwise);
    csnprintf(buf, len, "\tproto=%s\n", wpa_proto);
    csnprintf(buf, len, "\t%s", wpas->respect_multi_ap ? "" : "#");
    csnprintf(buf, len, "multi_ap_backhaul_sta=%d\n", wpas_map_str2int(vconf));
    csnprintf(buf, len, "\t%s", strlen(vconf->parent) > 0 ? "" : "#");
    csnprintf(buf, len, "bssid=%s\n", vconf->parent);
    csnprintf(buf, len, "\t%s", strlen(ieee80211w) > 0 ? "" : "#");
    csnprintf(buf, len, "ieee80211w=%s\n", ieee80211w);
    csnprintf(buf, len, "\t%s", vconf->dpp_connector_exists ? "" : "#");
    csnprintf(buf, len, "dpp_connector=\"%s\"\n", vconf->dpp_connector);
    csnprintf(buf, len, "\t%s", vconf->dpp_netaccesskey_hex_exists ? "" : "#");
    csnprintf(buf, len, "dpp_netaccesskey=%s\n", vconf->dpp_netaccesskey_hex);
    csnprintf(buf, len, "\t%s", vconf->dpp_csign_hex_exists ? "" : "#");
    csnprintf(buf, len, "dpp_csign=%s\n", vconf->dpp_csign_hex);

    csnprintf(buf, len, "}\n");

    WARN_ON(*len == 1); /* likely buf was truncated */

    return true;
}

bool
wpas_conf_gen_vif_network_legacy(struct wpas *wpas,
                                 const struct schema_Wifi_VIF_Config *vconf,
                                 char **buf,
                                 size_t *len)
{
    const char *wpa_passphrase;
    const char *wpa_pairwise;
    const char *wpa_key_mgmt;
    const char *wpa_proto;
    bool dfs_allowed;
    char freqlist[512];
    int wpa;

    wpa = wpas_util_get_mode(SCHEMA_KEY_VAL(vconf->security, "mode"));
    wpa_pairwise = wpas_util_get_pairwise(wpa);
    wpa_proto = wpas_util_get_proto_legacy(wpa);
    wpa_key_mgmt = SCHEMA_KEY_VAL(vconf->security, "encryption");
    wpa_passphrase = SCHEMA_KEY_VAL(vconf->security, "key");
    wpas_conf_gen_freqlist(wpas, freqlist, sizeof(freqlist));
    dfs_allowed = (vconf->parent_exists && strlen(vconf->parent) > 0);

    csnprintf(buf, len, "network={\n");
    csnprintf(buf, len, "\tscan_ssid=1\n");
    csnprintf(buf, len, "\tbgscan=\"\"\n");
    csnprintf(buf, len, "\tscan_freq=%s\n", dfs_allowed ? "" : freqlist);
    csnprintf(buf, len, "\tfreq_list=%s\n", dfs_allowed ? "" : freqlist);
    csnprintf(buf, len, "\tssid=\"%s\"\n", vconf->ssid);
    csnprintf(buf, len, "\tpsk=\"%s\"\n", wpa_passphrase);
    csnprintf(buf, len, "\tkey_mgmt=%s\n", wpa_key_mgmt);
    csnprintf(buf, len, "\tpairwise=%s\n", wpa_pairwise);
    csnprintf(buf, len, "\tproto=%s\n", wpa_proto);
    csnprintf(buf, len, "\t%s", wpas->respect_multi_ap ? "" : "#");
    csnprintf(buf, len, "multi_ap_backhaul_sta=%d\n", wpas_map_str2int(vconf));
    csnprintf(buf, len, "\t%s", strlen(vconf->parent) > 0 ? "" : "#");
    csnprintf(buf, len, "bssid=%s\n", vconf->parent);
    csnprintf(buf, len, "\t#ieee80211w=\n");
    csnprintf(buf, len, "}\n");

    WARN_ON(*len == 1); /* likely buf was truncated */

    return true;
}

bool
wpas_conf_gen_cred_config_networks(struct wpas *wpas,
                                   const struct schema_Wifi_VIF_Config *vconf,
                                   const struct schema_Wifi_Credential_Config *cconfs,
                                   size_t n_cconfs,
                                   char **buf,
                                   size_t *len)
{
    const char *wpa_passphrase;
    const char *wpa_pairwise;
    const char *wpa_key_mgmt;
    const char *wpa_proto;
    char freqlist[512];
    bool dfs_allowed;
    int wpa;

    wpa = wpas_util_get_mode(SCHEMA_KEY_VAL(vconf->security, "mode"));
    wpa_pairwise = wpas_util_get_pairwise(wpa);
    wpa_proto = wpas_util_get_proto_legacy(wpa);
    wpa_key_mgmt = SCHEMA_KEY_VAL(vconf->security, "encryption");
    wpa_passphrase = SCHEMA_KEY_VAL(vconf->security, "key");
    wpas_conf_gen_freqlist(wpas, freqlist, sizeof(freqlist));
    dfs_allowed = (vconf->parent_exists && strlen(vconf->parent) > 0);

    for (; n_cconfs; n_cconfs--, cconfs++) {
        wpa = wpas_util_get_mode(SCHEMA_KEY_VAL(vconf->security, "mode"));
        wpa_pairwise = wpas_util_get_pairwise(wpa);
        wpa_proto = wpas_util_get_proto_legacy(wpa);
        wpa_key_mgmt = SCHEMA_KEY_VAL(cconfs->security, "encryption");
        wpa_passphrase = SCHEMA_KEY_VAL(cconfs->security, "key");

        csnprintf(buf, len, "network={\n");
        csnprintf(buf, len, "\tscan_ssid=1\n");
        csnprintf(buf, len, "\tbgscan=\"\"\n");
        csnprintf(buf, len, "\tscan_freq=%s\n", dfs_allowed ? "" : freqlist);
        csnprintf(buf, len, "\tfreq_list=%s\n", dfs_allowed ? "" : freqlist);
        csnprintf(buf, len, "\tssid=\"%s\"\n", cconfs->ssid);
        csnprintf(buf, len, "\tpsk=\"%s\"\n", wpa_passphrase);
        csnprintf(buf, len, "\tkey_mgmt=%s\n", wpa_key_mgmt);
        csnprintf(buf, len, "\tpairwise=%s\n", wpa_pairwise);
        csnprintf(buf, len, "\tproto=%s\n", wpa_proto);
        csnprintf(buf, len, "\t%s", wpas->respect_multi_ap ? "" : "#");
        csnprintf(buf, len, "multi_ap_backhaul_sta=%d\n", wpas_map_str2int(vconf));
        csnprintf(buf, len, "\t%s", strlen(vconf->parent) > 0 ? "" : "#");
        csnprintf(buf, len, "bssid=%s\n", vconf->parent);
        csnprintf(buf, len, "\t#ieee80211w=\n");
        csnprintf(buf, len, "}\n");
    }

    WARN_ON(*len == 1); /* likely buf was truncated */

    return true;
}

int
wpas_conf_gen(struct wpas *wpas,
              const struct schema_Wifi_Radio_Config *rconf,
              const struct schema_Wifi_VIF_Config *vconf,
              const struct schema_Wifi_Credential_Config *cconfs,
              size_t n_cconfs)
{
    const char *wpa_passphrase = SCHEMA_KEY_VAL(vconf->security, "key");
    size_t len = sizeof(wpas->conf);
    char *buf = wpas->conf;
    bool ok;

    memset(wpas->conf, 0, sizeof(wpas->conf));

    if (!vconf->enabled)
        return 0;

    csnprintf(&buf, &len, "ctrl_interface=%s\n", wpas->ctrl.sockdir);
    csnprintf(&buf, &len, "%s", wpas_supports_disallow_dfs() ? "" : "#");
    csnprintf(&buf, &len, "disallow_dfs=%d\n", !(vconf->parent_exists && strlen(vconf->parent) > 0));
    csnprintf(&buf, &len, "scan_cur_freq=%d\n", vconf->parent_exists && strlen(vconf->parent) > 0);
    csnprintf(&buf, &len, "#bridge=%s\n", vconf->bridge_exists ? vconf->bridge : "");

    /* Credential_Config is supposed to be used only
     * during initial onboarding/bootstrap. After that
     * the cloud is supposed to always provide a single
     * parent to connect to.
     */
    if (vconf->wpa_exists)
        ok = wpas_conf_gen_vif_network(wpas, vconf, &buf, &len);
    else if (vconf->security_len > 0 && vconf->ssid_exists && strlen(wpa_passphrase) > 0)
        ok = wpas_conf_gen_vif_network_legacy(wpas, vconf, &buf, &len);
    else
        ok = wpas_conf_gen_cred_config_networks(wpas, vconf, cconfs, n_cconfs, &buf, &len);

    return ok ? 0 : -1;
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
wpas_bss_get_network_security_legacy(struct schema_Wifi_VIF_State *vstate,
                                     const char *network)
{
    const char *key_mgmt;
    char *psk;

    key_mgmt = ini_geta(network, "key_mgmt") ?: "";
    if (strcmp(key_mgmt, "WPA-PSK") != 0) {
        LOGD("%s: sta configured with non-wpa-psk keys mgmt, " \
             "leaving Wifi_VIF_State:security empty", vstate->if_name);
        return;
    }

    psk = ini_geta(network, "psk") ?: "";

    /* entry in file looks actually like this: psk="passphrase", so remove the: " */
    if (strlen(psk) > 0) psk++;
    if (strlen(psk) > 0) psk[strlen(psk)-1] = 0;

    SCHEMA_KEY_VAL_APPEND(vstate->security, "encryption", "WPA-PSK");
    SCHEMA_KEY_VAL_APPEND(vstate->security, "key", psk);
}

static void
wpas_bss_get_network_security(struct schema_Wifi_VIF_State *vstate,
                              const char *network)
{
    const char *key_mgmt = ini_geta(network, "key_mgmt") ?: "";
    const bool wpa2_psk = strstr(key_mgmt, "WPA-PSK") != NULL;
    const bool sae = strstr(key_mgmt, "SAE") != NULL;
    const bool dpp = strstr(key_mgmt, "DPP") != NULL;
    char *psk = ini_geta(network, "psk") ?: "";
    char *dpp_conn = ini_geta(network, "dpp_connector") ?: "";
    char *dpp_key = ini_geta(network, "dpp_netaccesskey") ?: "";
    char *dpp_csign = ini_geta(network, "dpp_csign") ?: "";
    int wpa;

    if (!sae && !wpa2_psk && !dpp) {
        LOGW("%s: Cannot fill Wifi_VIF_State, unknown sta key mgmt: %s",
             vstate->if_name, key_mgmt);
        return;
    }

    if (psk) {
        /* entry in file looks actually like this: psk="passphrase", so remove the: " */
        if (strlen(psk) > 0) psk++;
        if (strlen(psk) > 0) psk[strlen(psk)-1] = 0;
    }

    if (dpp_conn) {
        /* entry in file looks actually like this: dpp_connector="...", so remove the: " */
        if (strlen(dpp_conn) > 0) dpp_conn++;
        if (strlen(dpp_conn) > 0) dpp_conn[strlen(dpp_conn)-1] = 0;
    }

    wpa = wpa2_psk || sae || dpp;

    SCHEMA_SET_INT(vstate->wpa, wpa);
    if (wpa2_psk) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "wpa2-psk");
    if (sae) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "sae");
    if (dpp) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "dpp");
    if (psk) SCHEMA_VAL_APPEND(vstate->wpa_psks, psk);
    if (dpp_conn) SCHEMA_SET_STR(vstate->dpp_connector, dpp_conn);
    if (dpp_key) SCHEMA_SET_STR(vstate->dpp_netaccesskey_hex, dpp_key);
    if (dpp_csign) SCHEMA_SET_STR(vstate->dpp_csign_hex, dpp_csign);
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
    char *network = strdupa(conf);
    int n = atoi(id) + 1;

    vstate->ssid_present = true;
    vstate->parent_present = true;

    if (strcmp(state, "COMPLETED"))
        return;

    while (network && n-- > 0)
        if ((network = strstr(network, "network={")))
            network += strlen("network={");

    if (!network)
        network = strdupa("");

    map = ini_geta(network, "multi_ap_backhaul_sta");

    if ((vstate->parent_exists = (bssid != NULL)))
        SCHEMA_SET_STR(vstate->parent, bssid);
    if ((vstate->ssid_exists = (ssid != NULL)))
        SCHEMA_SET_STR(vstate->ssid, ssid);

    SCHEMA_SET_STR(vstate->multi_ap, wpas_map_int2str(atoi(map ?: "0")));
    SCHEMA_SET_STR(vstate->bridge, bridge);

    wpas_bss_get_network_security_legacy(vstate, network);
    wpas_bss_get_network_security(vstate, network);
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

int
wpas_each(int (*iter)(struct ctrl *ctrl, void *ptr), void *ptr)
{
    size_t i;
    int err = 0;

    for (i = 0; i < ARRAY_SIZE(g_wpas); i++)
        if (strlen(g_wpas[i].ctrl.bss) > 0)
            err |= iter(&g_wpas[i].ctrl, ptr);

    return err;
}
