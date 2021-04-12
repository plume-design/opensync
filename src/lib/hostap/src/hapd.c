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
#include <opensync-hapd.h>

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

#define CONFIG_HAPD_DRIVER "nl80211" // FIXME: kconfig
#define CONFIG_HAPD_MAX_BSS 48 // FIXME: kconfig
#define CONFIG_HAPD_RELOAD_STOP_START 1

#define HAPD_SOCK_PATH(dphy, dvif) F("/var/run/hostapd-%s/%s", dphy, dvif)
#define HAPD_SOCK_DIR(dphy) F("/var/run/hostapd-%s", dphy)
#define HAPD_CONF_PATH(dvif) F("/var/run/hostapd-%s.config", dvif)
#define HAPD_PSKS_PATH(dvif) F("/var/run/hostapd-%s.pskfile", dvif)
#define HAPD_GLOB_CLI(...) E(CMD_TIMEOUT("wpa_cli", "-p", "", "-g", "/var/run/hostapd/global", ## __VA_ARGS__))
#define HAPD_CLI(hapd, ...) E(CMD_TIMEOUT("hostapd_cli", "-p", hapd->ctrl.sockdir, "-i", hapd->ctrl.bss, ## __VA_ARGS__))
#define EV(x) strchomp(strdupa(x), " ")

#define MODULE_ID LOG_MODULE_ID_HAPD

#ifndef DPP_CLI_UNSUPPORTED
#define DPP_CLI_UNSUPPORTED "not supported"
#endif
#ifndef DPP_EVENT_CHIRP_RX
#define DPP_EVENT_CHIRP_RX DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_AUTH_SUCCESS
#define DPP_EVENT_AUTH_SUCCESS DPP_CLI_UNSUPPORTED
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
#ifndef DPP_EVENT_REQ_RX
#define DPP_EVENT_REQ_RX DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_CONF_SENT
#define DPP_EVENT_CONF_SENT DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_AUTH_PK_HASH
#define DPP_EVENT_AUTH_PK_HASH DPP_CLI_UNSUPPORTED
#endif

static struct hapd g_hapd[CONFIG_HAPD_MAX_BSS];

struct hapd *
hapd_lookup(const char *bss)
{
    size_t i;
    for (i = 0; i < ARRAY_SIZE(g_hapd); i++)
        if (strcmp(bss, g_hapd[i].ctrl.bss) == 0)
            return &g_hapd[i];
    return NULL;
}

static struct hapd *
hapd_lookup_unused(void)
{
    size_t i;
    for (i = 0; i < ARRAY_SIZE(g_hapd); i++)
        if (strlen(g_hapd[i].ctrl.bss) == 0)
            return &g_hapd[i];
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

void hapd_dpp_conf_received_callback(struct hapd *hapd)
{
    if (hapd->dpp_conf_received)
        // Aggregate multiple CLI messages here
        if (strlen(hapd->dpp_enrollee_conf_ssid_hex) > 1
            && strlen(hapd->dpp_enrollee_conf_connector) > 1
            && strlen(hapd->dpp_enrollee_conf_netaccesskey_hex) > 1
            && strlen(hapd->dpp_enrollee_conf_csign_hex) > 1)
            {
                struct target_dpp_conf_network dpp_enrollee_conf = {
                    .ifname = hapd->ctrl.bss,
                    .ssid_hex = hapd->dpp_enrollee_conf_ssid_hex,
                    .dpp_connector = hapd->dpp_enrollee_conf_connector,
                    .dpp_netaccesskey_hex = hapd->dpp_enrollee_conf_netaccesskey_hex,
                    .dpp_csign_hex = hapd->dpp_enrollee_conf_csign_hex,
                    .akm = wpas_dpp_akm_str2enum(hapd->dpp_enrollee_conf_akm),
                    .psk_hex = hapd->dpp_enrollee_conf_psk_hex,
                };
                hapd->dpp_conf_received(&dpp_enrollee_conf);

                // Clear out hapd buffers
                memset(hapd->dpp_enrollee_conf_ssid_hex, 0, sizeof(hapd->dpp_enrollee_conf_ssid_hex));
                memset(hapd->dpp_enrollee_conf_connector, 0, sizeof(hapd->dpp_enrollee_conf_connector));
                memset(hapd->dpp_enrollee_conf_netaccesskey_hex, 0, sizeof(hapd->dpp_enrollee_conf_netaccesskey_hex));
                memset(hapd->dpp_enrollee_conf_csign_hex, 0, sizeof(hapd->dpp_enrollee_conf_csign_hex));
                memset(hapd->dpp_enrollee_conf_psk_hex, 0, sizeof(hapd->dpp_enrollee_conf_psk_hex));
                memset(hapd->dpp_enrollee_conf_akm, 0, sizeof(hapd->dpp_enrollee_conf_akm));
                return;
        }
}

static void
hapd_ctrl_cb(struct ctrl *ctrl, int level, const char *buf, size_t len)
{
    struct hapd *hapd = container_of(ctrl, struct hapd, ctrl);
    const char *keyid = NULL;
    const char *sha256_hash = NULL;
    const char *pkhash = NULL;
    const char *event;
    const char *mac = NULL;
    const char *k;
    const char *v;
    char *args = strdupa(buf);
    char *kv;

    /* AP-STA-CONNECTED 60:b4:f7:f0:0a:19
     * AP-STA-CONNECTED 60:b4:f7:f0:0a:19 keyid=xxx
     * AP-STA-DISCONNECTED 60:b4:f7:f0:0a:19
     * AP-ENABLED
     * AP-DISABLED
     */
    event = strsep(&args, " ") ?: "_nope_";

    if (!strcmp(event, EV(AP_STA_CONNECTED))) {
        mac = strsep(&args, " ") ?: "";

        while ((kv = strsep(&args, " "))) {
            if ((k = strsep(&kv, "=")) && (v = strsep(&kv, ""))) {
                if (!strcmp(k, "keyid"))
                    keyid = v;
                if (!strcmp(k, "dpp_pkhash"))
                    pkhash = v;
            }
        }

        LOGI("%s: %s: connected keyid=%s pkhash=%s", hapd->ctrl.bss, mac, keyid ?: "", pkhash ?: "");
        if (hapd->sta_connected)
            hapd->sta_connected(hapd, mac, keyid);

        return;
    }

    //DPP Events for Configurator/Responder
    if (!strcmp(event, EV(DPP_EVENT_CHIRP_RX))) {
        while ((kv = strsep(&args, " "))) {
            if ((k = strsep(&kv, "=")) && (v = strsep(&kv, ""))) {
                if (!strcmp(k, "src"))
                    mac = v;
                if (!strcmp(k, "hash"))
                    sha256_hash = v;
            }
        }

        LOGI("%s: dpp chirp received from: %s, hash: %s", hapd->ctrl.bss, mac, sha256_hash ?: "");
        if (hapd->dpp_chirp_received) {
            const struct target_dpp_chirp_obj chirp = {
                .ifname = hapd->ctrl.bss,
                .mac_addr = mac,
                .sha256_hex = sha256_hash
            };
            hapd->dpp_chirp_received(&chirp);
        }
        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_AUTH_SUCCESS))) {
        LOGI("%s: dpp auth success received", hapd->ctrl.bss);
        hapd->dpp_pending_auth_success = 1;

        return;
    }

    //DPP Events for Enrollee
    if (!strcmp(event, EV(DPP_EVENT_CONFOBJ_SSID))) {
        if (ascii2hex(args, hapd->dpp_enrollee_conf_ssid_hex, sizeof(hapd->dpp_enrollee_conf_ssid_hex))) {
            LOGI("%s: dpp conf received ssid: %s, hex: %s", hapd->ctrl.bss, args, hapd->dpp_enrollee_conf_ssid_hex);
            hapd_dpp_conf_received_callback(hapd);
        }
        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONNECTOR))) {
        LOGI("%s: dpp connector received: %s", hapd->ctrl.bss, args);
        STRSCPY_WARN(hapd->dpp_enrollee_conf_connector, args);
        hapd_dpp_conf_received_callback(hapd);

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_C_SIGN_KEY))) {
        LOGI("%s: dpp c-sign key recieved: %s", hapd->ctrl.bss, args);
        STRSCPY_WARN(hapd->dpp_enrollee_conf_csign_hex, args);
        hapd_dpp_conf_received_callback(hapd);

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_NET_ACCESS_KEY))) {
        LOGI("%s: dpp net access key recieved: %s", hapd->ctrl.bss, args);
        STRSCPY_WARN(hapd->dpp_enrollee_conf_netaccesskey_hex, args);
        hapd_dpp_conf_received_callback(hapd);

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONFOBJ_PASS))) {
        LOGI("%s: dpp pass recieved: %s", hapd->ctrl.bss, args);
        STRSCPY_WARN(hapd->dpp_enrollee_conf_psk_hex, args);
        hapd_dpp_conf_received_callback(hapd);

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONFOBJ_AKM))) {
        LOGI("%s: dpp akm recieved: %s", hapd->ctrl.bss, args);
        STRSCPY_WARN(hapd->dpp_enrollee_conf_akm, args);
        hapd_dpp_conf_received_callback(hapd);

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONF_RECEIVED))) {
        LOGI("%s: dpp conf received", hapd->ctrl.bss);
        if (hapd->dpp_pending_auth_success && hapd->dpp_conf_received)
            hapd->dpp_pending_auth_success = 0;

        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONF_REQ_RX))) {
        LOGI("%s: dpp conf request recieved", hapd->ctrl.bss);
        if (!hapd->dpp_pending_conf) {
            hapd->dpp_pending_conf = 1;
            while ((kv = strsep(&args, " "))) {
                if ((k = strsep(&kv, "=")) && (v = strsep(&kv, ""))) {
                    if (!strcmp(k, "src"))
                        mac = v;
                }
            }
            STRSCPY_WARN(hapd->dpp_pending_conf_sta, mac);
            LOGI("%s: dpp conf request recieved from: %s", hapd->ctrl.bss, hapd->dpp_pending_conf_sta);
        }
        else {
            LOGW("%s: dpp conf already in progress, igorning", hapd->ctrl.bss);
        }
        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_AUTH_PK_HASH))) {
        v = strsep(&args, " ") ?: "";
        LOGI("%s: dpp auth pk hash: %s", hapd->ctrl.bss, v);
        STRSCPY_WARN(hapd->dpp_pending_conf_pk_hash, v);
        return;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONF_SENT))) {
        LOGI("%s: dpp conf sent event received", hapd->ctrl.bss);
        //check for STA MAC that asked for the conf
        if (hapd->dpp_conf_sent && hapd->dpp_pending_conf) {
            const struct target_dpp_conf_enrollee enrollee = {
                .ifname = hapd->ctrl.bss,
                .sta_mac_addr = hapd->dpp_pending_conf_sta,
                .sta_netaccesskey_sha256_hex = strlen(hapd->dpp_pending_conf_pk_hash) > 0
                                             ? hapd->dpp_pending_conf_pk_hash
                                             : "0000000000000000000000000000000000000000000000000000000000000000"
            };
            hapd->dpp_conf_sent(&enrollee);
            LOGI("%s: dpp conf sent to: %s", hapd->ctrl.bss, hapd->dpp_pending_conf_sta);
            memset(hapd->dpp_pending_conf_sta, 0, sizeof(hapd->dpp_pending_conf_sta));
            memset(hapd->dpp_pending_conf_pk_hash, 0, sizeof(hapd->dpp_pending_conf_pk_hash));
            hapd->dpp_pending_conf = 0;
        }
        return;
    }

    if (!strcmp(event, EV(AP_STA_DISCONNECTED))) {
        mac = strsep(&args, " ") ?: "";

        LOGI("%s: %s: disconnected", hapd->ctrl.bss, mac);
        if (hapd->sta_disconnected)
            hapd->sta_disconnected(hapd, mac);

        return;
    }

    if (!strcmp(event, EV(AP_EVENT_ENABLED))) {
        LOGI("%s: ap enabled", hapd->ctrl.bss);
        if (hapd->ap_enabled)
            hapd->ap_enabled(hapd);

        return;
    }

    if (!strcmp(event, EV(AP_EVENT_DISABLED))) {
        LOGI("%s: ap disabled", hapd->ctrl.bss);
        if (hapd->ap_disabled)
            hapd->ap_disabled(hapd);

        return;
    }

    if (!strcmp(event, EV(WPS_EVENT_ACTIVE))) {
        LOGI("%s: wps active", hapd->ctrl.bss);
        if (hapd->wps_active)
            hapd->wps_active(hapd);

        return;
    }

    if (!strcmp(event, EV(WPS_EVENT_TIMEOUT))) {
        LOGI("%s: wps timeout", hapd->ctrl.bss);
        if (hapd->wps_timeout)
            hapd->wps_timeout(hapd);

        return;
    }

    if (!strcmp(event, EV(WPS_EVENT_SUCCESS))) {
        LOGI("%s: wps success", hapd->ctrl.bss);
        if (hapd->wps_success)
            hapd->wps_success(hapd);

        return;
    }

    if (!strcmp(event, EV(WPS_EVENT_DISABLE))) {
        LOGI("%s: wps disable", hapd->ctrl.bss);
        if (hapd->wps_disable)
            hapd->wps_disable(hapd);

        return;
    }

    if (!strcmp(event, EV(AP_STA_POSSIBLE_PSK_MISMATCH))) {
        mac = strsep(&args, " ") ?: "";

        LOGI("%s: wpa key mismatch", hapd->ctrl.bss);
        if (hapd->wpa_key_mismatch)
            hapd->wpa_key_mismatch(hapd, mac);

        return;
    }

    if (!strcmp(event, EV(WPS_EVENT_ENROLLEE_SEEN))) {
        LOGD("%s: event: <%d> %s", ctrl->bss, level, buf);
        return;
    }

    LOGI("%s: event: <%d> %s", ctrl->bss, level, buf);
}

static struct hapd *
hapd_init(struct hapd *hapd, const char *phy, const char *bss)
{
    LOGI("%s: initializing", bss);
    memset(hapd, 0, sizeof(*hapd));
    STRSCPY_WARN(hapd->driver, CONFIG_HAPD_DRIVER);
    STRSCPY_WARN(hapd->ctrl.bss, bss);
    STRSCPY_WARN(hapd->ctrl.sockdir, HAPD_SOCK_DIR(phy));
    STRSCPY_WARN(hapd->ctrl.sockpath, HAPD_SOCK_PATH(phy, bss));
    STRSCPY_WARN(hapd->confpath, HAPD_CONF_PATH(bss));
    STRSCPY_WARN(hapd->pskspath, HAPD_PSKS_PATH(bss));
    STRSCPY_WARN(hapd->phy, phy);
    hapd->ctrl.cb = hapd_ctrl_cb;
    return hapd;
}

struct hapd *
hapd_new(const char *phy, const char *bss)
{
    struct hapd *hapd = hapd_lookup_unused();
    if (hapd) hapd_init(hapd, phy, bss);
    return hapd;
}

static const char *
hapd_util_get_mode_str(int mode)
{
    switch (mode) {
        case 1: return "1";
        case 2: return "2";
        case 3: return "mixed";
    }
    LOGW("%s: unknown mode number: %d", __func__, mode);
    return NULL;
}

static int
hapd_util_get_mode(const char *mode)
{
    if (!strcmp(mode, "mixed")) return 3;
    else if (!strcmp(mode, "2")) return 2;
    else if (!strcmp(mode, "1")) return 1;
    else if (strlen(mode) == 0) return 3;

    LOGW("%s: unknown mode string: '%s'", __func__, mode);
    return 3;
}

static const char *
hapd_util_get_pairwise(int wpa)
{
    switch (wpa) {
        case 1: return "TKIP";
        case 2: return "CCMP";
        case 3: return "CCMP TKIP";
    }
    LOGW("%s: unhandled wpa mode %d", __func__, wpa);
    return "";
}

static unsigned short int
hapd_util_fletcher16(const char *data, int count)
{
    unsigned short int sum1 = 0;
    unsigned short int sum2 = 0;
    int index;

    for( index = 0; index < count; ++index )
    {
       sum1 = (sum1 + data[index]) % 255;
       sum2 = (sum2 + sum1) % 255;
    }

    return (sum2 << 8) | sum1;
}

static const char *
hapd_util_ft_nas_id(void)
{
    /* This is connected with radius server. This is
     * required when configure 802.11R even we only using
     * FT-PSK today. For FT-PSK and ft_psk_generate_local=1
     * is not used. Currently we can't skip this while
     * hostapd will not start and we could see such message:
     * FT (IEEE 802.11r) requires nas_identifier to be
     * configured as a 1..48 octet string
     */
    return "plumewifi";
}

static int
hapd_util_ft_reassoc_deadline_tu(void)
{
    return 5000;
}

static unsigned short
hapd_util_ft_md(const struct schema_Wifi_VIF_Config *vconf)
{
    return (vconf->ft_mobility_domain
            ? vconf->ft_mobility_domain
            : hapd_util_fletcher16(vconf->ssid, strlen(vconf->ssid)));
}

static int
hapd_util_get_wpa(const struct schema_Wifi_VIF_Config *vconf)
{
    int wpa = 0;

    if (!vconf->wpa)
        return 0;

    if (util_vif_wpa_key_mgmt_match(vconf, "wpa-"))
        wpa |= 1;
    if (util_vif_wpa_key_mgmt_match(vconf, "wpa2-") || util_vif_wpa_key_mgmt_match(vconf, "sae"))
        wpa |= 2;

    return wpa;
}

static int
hapd_util_get_sae_require_mfp(const struct schema_Wifi_VIF_Config *vconf)
{
    if (!vconf->wpa)
        return 0;

    return util_vif_wpa_key_mgmt_match(vconf, "sae") ? 1 : 0;
}

static int
hapd_util_get_ieee8021x(const struct schema_Wifi_VIF_Config *vconf)
{
    if (!vconf->wpa)
        return 0;

    return util_vif_wpa_key_mgmt_match(vconf, "wpa2-eap") ? 1 : 0;
}

static const char*
hapd_util_get_wpa_psk_oftag_by_keyid(const struct schema_Wifi_VIF_Config *vconf,
                                     const char *keyid)
{
    int i = 0;

    if (!vconf->wpa_oftags_len)
        return NULL;

    for (i = 0; i < vconf->wpa_oftags_len; i++) {
        if (!strcmp(vconf->wpa_oftags_keys[i], keyid))
            return vconf->wpa_oftags[i];
    }

    return NULL;
}

static int
hapd_map_str2int(const struct schema_Wifi_VIF_Config *vconf)
{
    if (!vconf->multi_ap_exists) return 0;
    if (!strcmp(vconf->multi_ap, "none")) return 0;
    if (!strcmp(vconf->multi_ap, "fronthaul_bss")) return 2;
    if (!strcmp(vconf->multi_ap, "backhaul_bss")) return 1;
    if (!strcmp(vconf->multi_ap, "fronthaul_backhaul_bss")) return 3;
    LOGW("%s: unknown multi_ap: '%s'", vconf->if_name, vconf->multi_ap);
    return 0;
}

static const char *
hapd_map_int2str(int i)
{
    switch (i) {
        case 0: return "none";
        case 1: return "backhaul_bss";
        case 2: return "fronthaul_bss";
        case 3: return "fronthaul_backhaul_bss";
    }
    LOGW("unknown map arg %d", i);
    return "none";
}

static int
hapd_conf_gen_psk(struct hapd *hapd,
                  const struct schema_Wifi_VIF_Config *vconf)
{
    const char *oftag;
    const char *psk;
    size_t len;
    char *buf = hapd->psks;
    int i;
    int j;
    int wps;

    len = sizeof(hapd->psks);
    memset(buf, 0, len);

    for (i = 0; i < vconf->security_len; i++) {
        /* FIXME This is workaround on inconsistent naming convention used by
         * cloud. The very first key has ID 'key' and oftag 'oftag'. Following
         * keys use 'key-N' and'oftag-N'.
         */
        const char *oftag_key;

        if (strstr(vconf->security_keys[i], "key") != vconf->security_keys[i])
            continue;

        LOGT("%s: parsing vconf: key '%s'",
             vconf->if_name, vconf->security_keys[i]);

        j = 1;
        sscanf(vconf->security_keys[i], "key-%d", &j);

        if (strcmp(vconf->security_keys[i], "key") == 0)
            oftag_key = strfmta("oftag");
        else
            oftag_key = strfmta("oftag-%s", vconf->security_keys[i]);

        oftag = SCHEMA_KEY_VAL(vconf->security, oftag_key);
        psk = SCHEMA_KEY_VAL(vconf->security, vconf->security_keys[i]);
        wps = strcmp(vconf->security_keys[i], vconf->wps_pbc_key_id) == 0 ? 1 : 0;

        LOGT("%s: parsing vconf: key '%s': key=%d oftag='%s' psk='%s' wps='%d'",
             vconf->if_name, vconf->security_keys[i], j, oftag, psk, wps);

        if (strlen(psk) == 0)
            continue;

        csnprintf(&buf, &len, "#oftag=%s\n", oftag);
        if (kconfig_enabled(CONFIG_HOSTAP_PSK_FILE_WPS))
            csnprintf(&buf, &len, "wps=%d ", wps); /* no \n intentionally, continued below */

        csnprintf(&buf, &len, "keyid=%s 00:00:00:00:00:00 %s\n", vconf->security_keys[i], psk);
    }

    WARN_ON(len == 1); /* likely buf was truncated */
    return 0;
}

static int
hapd_11n_enabled(const struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf->hw_mode_exists) return 0;
    if ((!strcmp(rconf->hw_mode, "11ax"))
        || (!strcmp(rconf->hw_mode, "11ac"))
        || (!strcmp(rconf->hw_mode, "11n")))
        return 1;
    return 0;
}

static int
hapd_11ac_enabled(const struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf->hw_mode_exists) return 0;
    if ((!strcmp(rconf->hw_mode, "11ax"))
        || (!strcmp(rconf->hw_mode, "11ac")))
        return 1;
    return 0;
}

static int
hapd_11ax_enabled(const struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf->hw_mode_exists) return 0;
    if (!strcmp(rconf->hw_mode, "11ax"))
        return 1;
    return 0;
}

static const char *
hapd_ht_caps(const struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf->ht_mode_exists) return "";
    if (!strcmp(rconf->ht_mode, "HT20")) {
        return "[HT20]";
    } else if (!strcmp(rconf->ht_mode, "HT40") ||
               !strcmp(rconf->ht_mode, "HT80") ||
               !strcmp(rconf->ht_mode, "HT160")) {
        switch (rconf->channel) {
            case 0:
                return "[HT40+] [HT40-]";
            case 1 ... 7:
            case 36:
            case 44:
            case 52:
            case 60:
            case 100:
            case 108:
            case 116:
            case 124:
            case 132:
            case 140:
            case 149:
            case 157:
                return "[HT40+]";
            case 8 ... 13:
            case 40:
            case 48:
            case 56:
            case 64:
            case 104:
            case 112:
            case 120:
            case 128:
            case 136:
            case 144:
            case 153:
            case 161:
                return "[HT40-]";
            default:
                LOG(TRACE,
                    "%s: %d is not a valid channel",
                    rconf->if_name, rconf->channel);
                return "";
        }
    }

    LOGT("%s: %s is incorrect htmode", __func__, rconf->ht_mode);
    return "";
}

static int
hapd_vht_oper_chwidth(const struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf->ht_mode_exists) return 0;
    if (!strcmp(rconf->ht_mode, "HT20") ||
            !strcmp(rconf->ht_mode, "HT40"))
        return 0;
    else if (!strcmp(rconf->ht_mode, "HT80"))
        return 1;
    else if (!strcmp(rconf->ht_mode, "HT160"))
        return 2;

    LOGT("%s: %s is incorrect htmode", __func__, rconf->ht_mode);
    return 0;
}

static int
hapd_vht_oper_centr_freq_idx(const struct schema_Wifi_Radio_Config *rconf)
{
    const int width = atoi(strlen(rconf->ht_mode) > 2 ? rconf->ht_mode + 2 : "20");
    const int *chans;
    int sum = 0;
    int cnt = 0;

    if (!strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_6G))
        chans = unii_6g_chan2list(rconf->channel, width);
    else
        chans = unii_5g_chan2list(rconf->channel, width);

    while (*chans) {
        sum += *chans;
        cnt++;
        chans++;
    }
    return sum / cnt;
}

int
hapd_conf_gen_wpa_psks(struct hapd *hapd,
                       const struct schema_Wifi_VIF_Config *vconf)
{
    const char *oftag;
    const char *psk;
    const char *keyid;
    size_t len = sizeof(hapd->psks);
    char *buf = hapd->psks;
    int i;
    int wps;
    int j;

    memset(buf, 0, len);

    for (i = 0; i < vconf->wpa_psks_len; i++) {
        keyid = vconf->wpa_psks_keys[i];

        LOGT("%s: parsing vconf: key '%s'", vconf->if_name, keyid);

        sscanf(keyid, "key-%d", &j);

        oftag = hapd_util_get_wpa_psk_oftag_by_keyid(vconf, keyid);
        psk = vconf->wpa_psks[i];
        wps = strcmp(keyid, vconf->wps_pbc_key_id) == 0 ? 1 : 0;

        if (!oftag) {
            LOGW("%s: No oftag found for keyid='%s'", vconf->if_name, keyid);
            continue;
        }

        LOGT("%s: parsing vconf: key '%s': key=%d oftag='%s' psk='%s' wps='%d'",
             vconf->if_name, keyid, j, oftag, psk, wps);

        if (strlen(psk) == 0)
            continue;

        csnprintf(&buf, &len, "#oftag=%s\n", oftag);
        if (kconfig_enabled(CONFIG_HOSTAP_PSK_FILE_WPS))
            csnprintf(&buf, &len, "wps=%d ", wps); /* no \n intentionally, continued below */

        csnprintf(&buf, &len, "keyid=%s 00:00:00:00:00:00 %s\n", keyid, psk);
    }

    WARN_ON(len == 1); /* likely buf was truncated */
    return 0;
}

static int
hapd_conf_gen_security_legacy(struct hapd *hapd,
                              char *buf,
                              size_t *len,
                              const struct schema_Wifi_VIF_Config *vconf)
{
    const char *wpa_pairwise;
    const char *wpa_key_mgmt;
    const char *radius_server_ip;
    const char *radius_server_port;
    const char *radius_server_secret;
    bool suppress_wps = false;
    char keys[32];
    int wpa;

    wpa = hapd_util_get_mode(SCHEMA_KEY_VAL(vconf->security, "mode"));
    wpa_pairwise = hapd_util_get_pairwise(wpa);
    wpa_key_mgmt = SCHEMA_KEY_VAL(vconf->security, "encryption");

    if (!strcmp(wpa_key_mgmt, "WPA-PSK")) {
        snprintf(keys, sizeof(keys), "%s%s",
                 vconf->ft_psk_exists && vconf->ft_psk ? "FT-PSK " : "",
                 wpa_key_mgmt);

        csnprintf(&buf, len, "auth_algs=1\n");
        csnprintf(&buf, len, "wpa_key_mgmt=%s\n", keys);
        csnprintf(&buf, len, "wpa_psk_file=%s\n", hapd->pskspath);
        csnprintf(&buf, len, "wpa=%d\n", wpa);
        csnprintf(&buf, len, "wpa_pairwise=%s\n", wpa_pairwise);
    } else if (!strcmp(wpa_key_mgmt, "WPA-EAP")) {
        csnprintf(&buf, len, "auth_algs=1\n");
        csnprintf(&buf, len, "ieee8021x=1\n");
        csnprintf(&buf, len, "own_ip_addr=127.0.0.1\n");
        csnprintf(&buf, len, "wpa_key_mgmt=WPA-EAP\n");
        /* WPA-EAP supports solely WPA2 */
        csnprintf(&buf, len, "wpa=2\n");
        csnprintf(&buf, len, "wpa_pairwise=CCMP\n");

        radius_server_ip = SCHEMA_KEY_VAL(vconf->security, "radius_server_ip");
        radius_server_port = SCHEMA_KEY_VAL(vconf->security, "radius_server_port");
        radius_server_secret = SCHEMA_KEY_VAL(vconf->security, "radius_server_secret");

        csnprintf(&buf, len, "auth_server_addr=%s\n", radius_server_ip);
        csnprintf(&buf, len, "auth_server_port=%s\n", radius_server_port);
        csnprintf(&buf, len, "auth_server_shared_secret=%s\n", radius_server_secret);
    } else if (!strcmp(wpa_key_mgmt, "OPEN")) {
        csnprintf(&buf, len, "wpa=0\n");
        csnprintf(&buf, len, "auth_algs=1\n");
    } else {
        LOGW("%s: (legacy) key mgmt '%s' not supported", vconf->if_name, wpa_key_mgmt);
        errno = ENOTSUP;
        return -1;
    }

    if (vconf->ft_psk_exists) {
        csnprintf(&buf, len, "#ft_psk=%d\n", vconf->ft_psk); // FIXME: rely on FT-PSK only

        if (vconf->ft_psk) {
            csnprintf(&buf, len, "nas_identifier=%s\n", hapd_util_ft_nas_id());
            csnprintf(&buf, len, "reassociation_deadline=%d\n", hapd_util_ft_reassoc_deadline_tu());
            csnprintf(&buf, len, "mobility_domain=%04x\n", hapd_util_ft_md(vconf));
            csnprintf(&buf, len, "ft_over_ds=0\n");
            csnprintf(&buf, len, "ft_psk_generate_local=1\n");
        }
    }

    /*
     * WPA-EAP + remote RADIUS excludes WPS on single VAP (hostapd doesn't support
     * such configuration).
     */
    if (!strcmp(wpa_key_mgmt, "WPA-EAP") && vconf->wps) {
        suppress_wps |= true;
        LOGW("%s: (legacy) Disabling WPS because WPA-EAP is enabled", vconf->if_name);
    }

    /*
     * OPEN mode and WPS cannot be configured on single VAP.
     */
    if (!strcmp(wpa_key_mgmt, "OPEN") && vconf->wps) {
        suppress_wps |= true;
        LOGW("%s: (legacy) Disabling WPS because OPEN mode is enabled", vconf->if_name);
    }

    if (kconfig_enabled(CONFIG_HOSTAP_PSK_FILE_WPS) && !suppress_wps) {
        csnprintf(&buf, len, "wps_state=%d\n", vconf->wps ? 2 : 0);
        csnprintf(&buf, len, "eap_server=%d\n", vconf->wps ? 1 : 0);
        csnprintf(&buf, len, "config_methods=virtual_push_button\n");
        csnprintf(&buf, len, "pbc_in_m1=1\n");
    }

    WARN_ON(*len == 1); /* likely buf was truncated */

    return hapd_conf_gen_psk(hapd, vconf);
}

static int
hapd_conf_gen_security(struct hapd *hapd,
                       char *buf,
                       size_t *len,
                       const struct schema_Wifi_VIF_Config *vconf)
{
    const char *main_psk_key_id = "key";
    const char *sae_psk = "";
    const char *sae_opt_present = "#";
    const char *ft_opt_present = "#";
    const char *wps_opt_present = "#";
    const char *ieee80211w_present = "#";
    char wpa_key_mgmt[64];
    char wpa_pairwise[64];
    char ieee80211w[4];
    bool suppress_wps = false;

    util_vif_get_wpa_key_mgmt(vconf, wpa_key_mgmt, sizeof(wpa_key_mgmt));
    util_vif_get_wpa_pairwise(vconf, wpa_pairwise, sizeof(wpa_pairwise));
    util_vif_get_ieee80211w(vconf, ieee80211w, sizeof(ieee80211w));

    /*
     * Set main WPA password as default SAE password.
     */
    if (util_vif_wpa_key_mgmt_match(vconf, "sae")) {
        sae_psk = SCHEMA_KEY_VAL(vconf->wpa_psks, main_psk_key_id);
        if (strlen(sae_psk) == 0) {
            LOGW("%s: Failed to set SAE psk, no psk with keid: '%s' was found "
                 "in wpa_psks", vconf->if_name, main_psk_key_id);
        }

        if (vconf->wpa_psks_len > 1) {
            LOGD("%s: wpa_psks contains multiple psks, password with keyid: '%s' "
                 "is set as SAE psk", vconf->if_name, main_psk_key_id);
        }
    }

    /*
     * WPA-EAP + remote RADIUS excludes WPS on single VAP (hostapd doesn't support
     * such configuration).
     */
    if (util_vif_wpa_key_mgmt_match(vconf, "eap") && vconf->wps) {
        suppress_wps |= true;
        LOGW("%s: Disabling WPS because WPA-EAP is enabled", vconf->if_name);
    }

    /*
     * OPEN mode and WPS cannot be configured on single VAP.
     */
    if (!vconf->wpa && vconf->wps) {
        suppress_wps |= true;
        LOGW("%s: Disabling WPS because OPEN mode is enabled", vconf->if_name);
    }

    sae_opt_present = util_vif_wpa_key_mgmt_match(vconf, "sae") ? "" : "#";
    wps_opt_present = kconfig_enabled(CONFIG_HOSTAP_PSK_FILE_WPS) && !suppress_wps ? "" : "#";
    ft_opt_present = util_vif_wpa_key_mgmt_match(vconf, "ft-") ? "" : "#";
    ieee80211w_present = strlen(ieee80211w) != 0 ? "" : "#";

    csnprintf(&buf, len, "wpa=%d\n", hapd_util_get_wpa(vconf));
    csnprintf(&buf, len, "%s", strlen(wpa_key_mgmt) > 0 ? "" : "#");
    csnprintf(&buf, len, "wpa_key_mgmt=%s\n", strlen(wpa_key_mgmt) > 0 ? wpa_key_mgmt : "");
    csnprintf(&buf, len, "auth_algs=1\n");
    csnprintf(&buf, len, "ieee8021x=%d\n", hapd_util_get_ieee8021x(vconf));
    csnprintf(&buf, len, "%s", strlen(wpa_pairwise) > 0 ? "" : "#");
    csnprintf(&buf, len, "wpa_pairwise=%s\n", strlen(wpa_pairwise) > 0 ? wpa_pairwise : "");
    csnprintf(&buf, len, "%s", strlen(hapd->pskspath) ? "" : "#");
    csnprintf(&buf, len, "wpa_psk_file=%s\n", strlen(hapd->pskspath) ? hapd->pskspath : "");
    csnprintf(&buf, len, "%s", util_vif_wpa_key_mgmt_match(vconf, "eap") ? "" : "#");
    csnprintf(&buf, len, "own_ip_addr=127.0.0.1\n");
    csnprintf(&buf, len, "%s", vconf->radius_srv_addr_exists ? "" : "#");
    csnprintf(&buf, len, "auth_server_addr=%s\n",
              vconf->radius_srv_addr_exists ? vconf->radius_srv_addr : "");
    csnprintf(&buf, len, "%s", vconf->radius_srv_port_exists ? "" : "#");
    csnprintf(&buf, len, "auth_server_port=%d\n", vconf->radius_srv_port);
    csnprintf(&buf, len, "%s", vconf->radius_srv_secret_exists ? "" : "#");
    csnprintf(&buf, len, "auth_server_shared_secret=%s\n",
              vconf->radius_srv_secret_exists ? vconf->radius_srv_secret : "");
    csnprintf(&buf, len, "%sieee80211w=%s\n", ieee80211w_present, ieee80211w);
    csnprintf(&buf, len, "%snas_identifier=%s\n", ft_opt_present, hapd_util_ft_nas_id());
    csnprintf(&buf, len, "%sreassociation_deadline=%d\n", ft_opt_present,
              hapd_util_ft_reassoc_deadline_tu());
    csnprintf(&buf, len, "%smobility_domain=%04x\n", ft_opt_present, hapd_util_ft_md(vconf));
    csnprintf(&buf, len, "%sft_over_ds=0\n", ft_opt_present);
    csnprintf(&buf, len, "%sft_psk_generate_local=1\n", ft_opt_present);
    csnprintf(&buf, len, "%ssae_password=%s\n", sae_opt_present, sae_psk);
    csnprintf(&buf, len, "%ssae_require_mfp=%d\n", sae_opt_present,
              hapd_util_get_sae_require_mfp(vconf));
    csnprintf(&buf, len, "%swps_state=%d\n", wps_opt_present, vconf->wps ? 2 : 0);
    csnprintf(&buf, len, "%seap_server=%d\n", wps_opt_present, vconf->wps ? 1 : 0);
    csnprintf(&buf, len, "%sconfig_methods=virtual_push_button\n", wps_opt_present);
    csnprintf(&buf, len, "%spbc_in_m1=1\n", wps_opt_present);

    if (vconf->dpp_connector_exists)
        csnprintf(&buf, len, "dpp_connector=%s\n", vconf->dpp_connector);
    if (vconf->dpp_csign_hex_exists)
        csnprintf(&buf, len, "dpp_csign=%s\n", vconf->dpp_csign_hex);
    if (vconf->dpp_netaccesskey_hex_exists)
        csnprintf(&buf, len, "dpp_netaccesskey=%s\n", vconf->dpp_netaccesskey_hex);

    WARN_ON(*len == 1); /* likely buf was truncated */

    return hapd_conf_gen_wpa_psks(hapd, vconf);
}

static const char *
hapd_band2hwmode(const char *band)
{
    if (!strcmp(band, SCHEMA_CONSTS_RADIO_TYPE_STR_2G))
        return "g";
    else if (!strcmp(band, SCHEMA_CONSTS_RADIO_TYPE_STR_5G) ||
             !strcmp(band, SCHEMA_CONSTS_RADIO_TYPE_STR_5GL) ||
             !strcmp(band, SCHEMA_CONSTS_RADIO_TYPE_STR_5GU) ||
             !strcmp(band, SCHEMA_CONSTS_RADIO_TYPE_STR_6G))
        return "a";
    else
        return "?";
}

int
hapd_conf_gen(struct hapd *hapd,
              const struct schema_Wifi_Radio_Config *rconf,
              const struct schema_Wifi_VIF_Config *vconf)
{
    size_t len = sizeof(hapd->conf);
    char *buf = hapd->conf;
    int closed;

    memset(buf, 0, len);
    memset(hapd->psks, 0, sizeof(hapd->psks));

    if (!vconf->enabled)
        return 0;

    closed = !strcmp(vconf->ssid_broadcast, "enabled") ? 0 :
             !strcmp(vconf->ssid_broadcast, "disabled") ? 1 : 0;

    csnprintf(&buf, &len, "driver=%s\n", hapd->driver);
    csnprintf(&buf, &len, "interface=%s\n", vconf->if_name);
    csnprintf(&buf, &len, "ctrl_interface=%s\n", hapd->ctrl.sockdir);
    csnprintf(&buf, &len, "logger_syslog=-1\n");
    csnprintf(&buf, &len, "logger_syslog_level=3\n");
    csnprintf(&buf, &len, "ssid=%s\n", vconf->ssid);
    csnprintf(&buf, &len, "hw_mode=%s\n", hapd_band2hwmode(rconf->freq_band));
    csnprintf(&buf, &len, "channel=%d\n", rconf->channel);
    csnprintf(&buf, &len, "ignore_broadcast_ssid=%d\n", closed);
    csnprintf(&buf, &len, "wmm_enabled=1\n");
    csnprintf(&buf, &len, "%s", hapd->respect_multi_ap ? "" : "#");
    csnprintf(&buf, &len, "multi_ap=%d\n", hapd_map_str2int(vconf));
    csnprintf(&buf, &len, "send_probe_response=%d\n", hapd->skip_probe_response ? 0 : 1);
    csnprintf(&buf, &len, "%s", hapd->ieee80211n ? "" : "#");
    csnprintf(&buf, &len, "ieee80211n=%d\n", hapd_11n_enabled(rconf));
    csnprintf(&buf, &len, "%s", hapd->ieee80211ac ? "" : "#");
    csnprintf(&buf, &len, "ieee80211ac=%d\n", hapd_11ac_enabled(rconf));
    csnprintf(&buf, &len, "%s", hapd->ieee80211ax ? "" : "#");
    csnprintf(&buf, &len, "ieee80211ax=%d\n", hapd_11ax_enabled(rconf));
    csnprintf(&buf, &len, "%s", vconf->dpp_cc_exists ? "" : "#");
    csnprintf(&buf, &len, "dpp_configurator_connectivity=%d\n", vconf->dpp_cc ? 1 : 0);

    if (hapd->ieee80211ac && hapd_11ac_enabled(rconf)) {
        if (strcmp(rconf->freq_band, "2.4G")) {
            csnprintf(&buf, &len, "vht_oper_chwidth=%d\n",
                      hapd_vht_oper_chwidth(rconf));
            csnprintf(&buf, &len, "vht_oper_centr_freq_seg0_idx=%d\n",
                      hapd_vht_oper_centr_freq_idx(rconf));
        }
    }

    if (!strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_6G))
        csnprintf(&buf, &len, "op_class=131\n");

    if (hapd->ieee80211n)
        csnprintf(&buf, &len, "ht_capab=%s %s\n",
                  hapd->htcaps, hapd_ht_caps(rconf));

    if (hapd->ieee80211ac)
        csnprintf(&buf, &len, "vht_capab=%s\n", hapd->vhtcaps);

    if (strlen(hapd->country) > 0) {
        csnprintf(&buf, &len, "country_code=%s\n", hapd->country);
        csnprintf(&buf, &len, "ieee80211d=1\n");
        csnprintf(&buf, &len, "ieee80211h=1\n");
    }

    /* FIXME: this should be able to implement min_hw_mode on setups where
     * hostapd's driver respects things like: supported_rates, basic_rates,
     * beacon_rate,  etc.
     */

    if (vconf->btm_exists)
        csnprintf(&buf, &len, "bss_transition=%d\n", !!vconf->btm);

    if (vconf->rrm_exists)
        csnprintf(&buf, &len, "rrm_neighbor_report=%d\n", !!vconf->rrm);

    if (strlen(vconf->bridge) > 0)
        csnprintf(&buf, &len, "bridge=%s\n", vconf->bridge);

    if (vconf->group_rekey_exists && vconf->group_rekey >= 0)
        csnprintf(&buf, &len, "wpa_group_rekey=%d\n", vconf->group_rekey);

    if (vconf->wpa_exists)
        return hapd_conf_gen_security(hapd, buf, &len, vconf);
    else
        return hapd_conf_gen_security_legacy(hapd, buf, &len, vconf);
}

static void
hapd_bss_get_security_legacy(struct schema_Wifi_VIF_State *vstate,
                             const char *conf,
                             const char *status)
{
    const char *keys = ini_geta(status, "key_mgmt") ?: "";

    if (strstr(keys, "WPA-PSK")) {
        const char *wpa = ini_geta(status, "wpa");
        const char *mode = hapd_util_get_mode_str(atoi(wpa ?: ""));

        /* `keys` can be also: 'FT-PSK WPA-PSK' */
        SCHEMA_KEY_VAL_APPEND(vstate->security, "encryption", "WPA-PSK");
        SCHEMA_KEY_VAL_APPEND(vstate->security, "mode", mode ?: "no_mode");
    }
    else if (strcmp(keys, "WPA-EAP") == 0) {
        const char *auth_server_addr = ini_geta(conf, "auth_server_addr");
        const char *auth_server_port = ini_geta(conf, "auth_server_port");
        const char *auth_server_shared_secret = ini_geta(conf, "auth_server_shared_secret");

        SCHEMA_KEY_VAL_APPEND(vstate->security, "encryption", "WPA-EAP");
        SCHEMA_KEY_VAL_APPEND(vstate->security, "radius_server_ip", auth_server_addr);
        SCHEMA_KEY_VAL_APPEND(vstate->security, "radius_server_port", auth_server_port);
        SCHEMA_KEY_VAL_APPEND(vstate->security, "radius_server_secret", auth_server_shared_secret);
    }
    else if (strcmp(keys, "") == 0) {
        /* OPEN mode */
        SCHEMA_KEY_VAL_APPEND(vstate->security, "encryption", "OPEN");
    }

    /* WPA3 is not configurable with 'security' column */
}

static void
hapd_bss_get_security(struct schema_Wifi_VIF_State *vstate,
                      const char *conf,
                      const char *status)
{
    const char *conf_keys = ini_geta(status, "key_mgmt") ?: "";
    const char *conf_radius_srv_addr = ini_geta(status, "auth_server_addr");
    const char *conf_radius_srv_port = ini_geta(status, "auth_server_port");
    const char *conf_radius_srv_secret = ini_geta(status, "auth_server_shared_secret");
    const int conf_wpa = atoi(ini_geta(status, "wpa") ?: "0");
    const char *p;
    bool wpa_psk;
    bool wpa2_psk;
    bool wpa2_eap;
    bool sae;
    bool ft_wpa2_psk;
    bool dpp;

    wpa_psk = (strstr(conf_keys, "WPA-PSK") && (conf_wpa & 1));
    wpa2_psk = (strstr(conf_keys, "WPA-PSK") && (conf_wpa & 2));
    wpa2_eap = (strstr(conf_keys, "WPA-EAP"));
    sae = (strstr(conf_keys, "SAE"));
    ft_wpa2_psk = (strstr(conf_keys, "FT-PSK") && (conf_wpa & 2));
    dpp = (strstr(conf_keys, "DPP"));

    SCHEMA_SET_INT(vstate->wpa, wpa_psk || wpa2_psk || wpa2_eap || sae || ft_wpa2_psk);
    if (wpa_psk) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "wpa-psk");
    if (wpa2_psk) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "wpa2-psk");
    if (wpa2_eap) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "wpa2-eap");
    if (sae) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "sae");
    if (ft_wpa2_psk) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "ft-wpa2-psk");
    if (dpp) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "dpp");
    if (conf_radius_srv_addr) SCHEMA_SET_STR(vstate->radius_srv_addr, conf_radius_srv_addr);
    if (conf_radius_srv_port) SCHEMA_SET_INT(vstate->radius_srv_port, atoi(conf_radius_srv_port));
    if (conf_radius_srv_secret) SCHEMA_SET_STR(vstate->radius_srv_secret, conf_radius_srv_secret);
    if ((p = ini_geta(conf, "dpp_connector"))) SCHEMA_SET_STR(vstate->dpp_connector, p);
    if ((p = ini_geta(conf, "dpp_csign"))) SCHEMA_SET_STR(vstate->dpp_csign_hex, p);
    if ((p = ini_geta(conf, "dpp_netaccesskey"))) SCHEMA_SET_STR(vstate->dpp_netaccesskey_hex, p);
}

static void
hapd_bss_get_psks_legacy(struct schema_Wifi_VIF_State *vstate,
                         const char *conf)
{
    const char *oftag;
    const char *key_id;
    const char *wps;
    const char *psk;
    const char *mac;
    const char *k;
    const char *v;
    char *oftagline;
    char *pskline;
    char *ptr;
    char *param;

    ptr = strdupa(conf);
    if (WARN_ON(!ptr))
        return;

    LOGT("%s: (legacy) parsing psk entries", vstate->if_name);

    for (;;) {
        const char *oftag_key;

        if (!(oftagline = strsep(&ptr, "\n")))
            break;
        if (!(pskline = strsep(&ptr, "\n")))
            break;

        LOGT("%s: (legacy) parsing pskfile: raw: oftagline='%s' pskline='%s'",
             vstate->if_name, oftagline, pskline);

        if (WARN_ON(!(oftag = strsep(&oftagline, "="))))
            continue;
        if (WARN_ON(strcmp(oftag, "#oftag")))
            continue;
        if (WARN_ON(!(oftag = strsep(&oftagline, ""))))
            continue;

        key_id = NULL;
        wps = NULL;
        while ((param = strsep(&pskline, " "))) {
            if (!strstr(param, "="))
                break;
            if (!(k = strsep(&param, "=")))
                continue;
            if (!(v = strsep(&param, "")))
                continue;

            if (!strcmp(k, "keyid"))
                key_id = v;
            else if (!strcmp(k, "wps"))
                wps = v;
        }

        if (WARN_ON(!(mac = param)))
            continue;
        if (WARN_ON(strcmp(mac, "00:00:00:00:00:00")))
            continue;
        if (WARN_ON(!(psk = strsep(&pskline, ""))))
            continue;

        if (WARN_ON(!key_id))
            continue;

        LOGT("%s: (legacy) parsing pskfile: stripped: key_id='%s' wps='%s' oftag='%s' psk='%s'",
             vstate->if_name, key_id, wps, oftag, psk);

        SCHEMA_KEY_VAL_APPEND(vstate->security, key_id, psk);

        if (strlen(oftag) > 0) {
            if (strcmp(key_id, "key") == 0)
                oftag_key = strfmta("oftag");
            else
                oftag_key = strfmta("oftag-%s", key_id);

            SCHEMA_KEY_VAL_APPEND(vstate->security, oftag_key, oftag);
        }

        if (wps && strcmp(wps, "1") == 0) {
            if (!key_id) {
                LOGW("%s: (legacy) PSK in pskfile with 'wps=1' is required to have valid `keyid` tag", vstate->if_name);
                continue;
            }

            SCHEMA_SET_STR(vstate->wps_pbc_key_id, key_id);
        }
    }
}

static void
hapd_bss_get_psks(struct schema_Wifi_VIF_State *vstate,
                  const char *conf)
{
    const char *key_id;
    const char *wps;
    const char *psk;
    const char *mac;
    const char *k;
    const char *v;
    char *pskline;
    char *ptr;
    char *param;

    ptr = strdupa(conf);
    if (WARN_ON(!ptr))
        return;

    LOGT("%s: parsing psk entries", vstate->if_name);

    for (;;) {
        if (!(pskline = strsep(&ptr, "\n")))
            break;

        if (strlen(pskline) == 0)
            break;

        if (strstr(pskline, "#") == pskline)
            continue;

        LOGT("%s: parsing pskfile: raw: pskline='%s'", vstate->if_name, pskline);

        key_id = NULL;
        wps = NULL;
        while ((param = strsep(&pskline, " "))) {
            if (!strstr(param, "="))
                break;
            if (!(k = strsep(&param, "=")))
                continue;
            if (!(v = strsep(&param, "")))
                continue;

            if (!strcmp(k, "keyid"))
                key_id = v;
            else if (!strcmp(k, "wps"))
                wps = v;
        }

        if (WARN_ON(!(mac = param)))
            continue;
        if (WARN_ON(strcmp(mac, "00:00:00:00:00:00")))
            continue;
        if (WARN_ON(!(psk = strsep(&pskline, ""))))
            continue;

        if (WARN_ON(!key_id))
            continue;

        LOGT("%s: parsing pskfile: stripped: key_id='%s' wps='%s' psk='%s'",
             vstate->if_name, key_id, wps, psk);

        SCHEMA_KEY_VAL_APPEND(vstate->wpa_psks, key_id, psk);

        if (wps && strcmp(wps, "1") == 0) {
            if (!key_id) {
                LOGW("%s: PSK in pskfile with 'wps=1' is required to have valid `keyid` tag", vstate->if_name);
                continue;
            }

            SCHEMA_SET_STR(vstate->wps_pbc_key_id, key_id);
        }
    }
}

static void
hapd_bss_get_wps(struct hapd *hapd,
                 struct schema_Wifi_VIF_State *vstate,
                 const char *status)
{
    const char *pbc_status_tag = "PBC Status: ";
    const char *wps_state = ini_geta(status, "wps_state") ?: "";
    const char *buf = HAPD_CLI(hapd, "wps_get_status");
    const char *pbc_status;
    char *ptr;

    if (!buf)
        return;

    ptr = strdupa(buf);
    if (WARN_ON(!ptr))
        return;

    if (WARN_ON(!(pbc_status = strsep(&ptr, "\n"))))
        return;

    if (WARN_ON(strstr(pbc_status, pbc_status_tag) != pbc_status))
        return;

    pbc_status += strlen(pbc_status_tag);

    vstate->wps_exists = true;
    vstate->wps = strcmp(wps_state, "configured") == 0 ? 1 : 0;
    vstate->wps_pbc_exists = true;
    vstate->wps_pbc = strcmp(pbc_status, "Active") == 0 ? 1 : 0;
}

int
hapd_bss_get(struct hapd *hapd,
             struct schema_Wifi_VIF_State *vstate)
{
    const char *status = HAPD_CLI(hapd, "get_config");
    const char *conf = R(hapd->confpath) ?: "";
    const char *psks = R(hapd->pskspath) ?: "";
    const char *map = ini_geta(conf, "multi_ap");
    char *p;

    /* FIXME: return non-zero if interface doesnt exist? */

    vstate->bridge_exists = true;
    if ((p = ini_geta(conf, "bridge")))
        STRSCPY_WARN(vstate->bridge, p);
    if ((vstate->ssid_exists = (p = ini_geta(conf, "ssid"))))
        STRSCPY_WARN(vstate->ssid, p);
    if ((vstate->group_rekey_exists = (p = ini_geta(conf, "wpa_group_rekey"))))
        vstate->group_rekey = atoi(p);
    if ((vstate->ft_mobility_domain_exists = (p = ini_geta(conf, "mobility_domain"))))
        vstate->ft_mobility_domain = strtol(p, NULL, 16);
    if ((vstate->ft_psk_exists = (p = ini_geta(conf, "#ft_psk")))) // FIXME: check FT-PSK in status
        vstate->ft_psk = atoi(p);
    if ((vstate->btm_exists = (p = ini_geta(conf, "bss_transition"))))
        vstate->btm = atoi(p);

    SCHEMA_SET_STR(vstate->multi_ap, hapd_map_int2str(atoi(map ?: "0")));
    SCHEMA_SET_INT(vstate->dpp_cc, atoi(ini_geta(conf, "dpp_configurator_connectivity") ?: "0"));

    if (status) {
        hapd_bss_get_security_legacy(vstate, conf, status);
        hapd_bss_get_security(vstate, conf, status);
        hapd_bss_get_wps(hapd, vstate, status);
        hapd_bss_get_psks_legacy(vstate, psks);
        hapd_bss_get_psks(vstate, psks);
    }

    /* FIXME: rely on "status" command, eg state=ENABLED, ssid[0]=XXX */

    return 0;
}

int
hapd_sta_get(struct hapd *hapd,
             const char *mac,
             struct schema_Wifi_Associated_Clients *client)
{
    const char *sta = HAPD_CLI(hapd, "sta", mac) ?: "";
    const char *keyid = NULL;
    const char *dpp_pkhash = NULL;
    const char *k;
    const char *v;
    char *lines = strdupa(sta);
    char *kv;

    while ((kv = strsep(&lines, "\r\n")))
        if ((k = strsep(&kv, "=")) &&
            (v = strsep(&kv, ""))) {
            if (!strcmp(k, "keyid"))
                keyid = v;
            else if (!strcmp(k, "dpp_pkhash"))
                dpp_pkhash = v;
        }

    SCHEMA_SET_STR(client->key_id, keyid ?: "");
    SCHEMA_SET_STR(client->mac, mac);
    SCHEMA_SET_STR(client->state, "active");
    if (dpp_pkhash) SCHEMA_SET_STR(client->dpp_netaccesskey_sha256_hex, dpp_pkhash);

    if (!strcmp(sta, "FAIL"))
        return -1;
    if (!strstr(sta, "flags=[AUTH][ASSOC][AUTHORIZED]"))
        return -1;

    return 0;
}

int
hapd_sta_deauth(struct hapd *hapd, const char *mac)
{
    LOGI("%s: deauthing %s", hapd->ctrl.bss, mac);
    return strcmp("OK", HAPD_CLI(hapd, "deauth", mac) ?: "");
}

void
hapd_sta_iter(struct hapd *hapd,
              void (*cb)(struct hapd *hapd, const char *mac, void *data),
              void *data)
{
    const char *mac;
    char *list = HAPD_CLI(hapd, "list_sta");

    while (list && (mac = strsep(&list, " \r\n")))
        if (strlen(mac) > 0)
            cb(hapd, mac, data);
}

static int
hapd_ctrl_add(struct hapd *hapd)
{
    const char *arg = F("bss_config=%s:%s", hapd->ctrl.bss, hapd->confpath);
    int err = 0;
    /* FIXME: check if I can use hapd->phy instead od hapd->bss above on qca */
    LOGI("%s: adding", hapd->ctrl.bss);
    err |= strcmp("OK", HAPD_GLOB_CLI("raw", "ADD", arg) ?: "");
    err |= strcmp("OK", HAPD_CLI(hapd, "log_level", "DEBUG") ?: "");
    return err;
}

static int
hapd_ctrl_remove(struct hapd *hapd)
{
    LOGI("%s: removing", hapd->ctrl.bss);
    return strcmp("OK", HAPD_GLOB_CLI("raw", "REMOVE", hapd->ctrl.bss) ?: "");
}

static int
hapd_ctrl_reload_psk(struct hapd *hapd)
{
    LOGI("%s: reloading psk", hapd->ctrl.bss);
    return strcmp("OK", HAPD_CLI(hapd, "reload_wpa_psk") ?: "");
}

static int
hapd_ctrl_reload(struct hapd *hapd)
{
    int err = 0;
    if (kconfig_enabled(CONFIG_HAPD_RELOAD_STOP_START)) {
        err |= WARN_ON(hapd_ctrl_remove(hapd));
        err |= WARN_ON(hapd_ctrl_add(hapd));
    } else {
        LOGI("%s: reloading", hapd->ctrl.bss);
        err |= strcmp("OK", HAPD_CLI(hapd, "reload") ?: "");
    }
    return err;
}

static int
hapd_configured(struct hapd *hapd)
{
    return strlen(hapd->conf) > 0;
}

static char *
hapd_conf_strip(char *buf, bool running)
{
    const size_t buf_size = strlen(buf) + 1;
    size_t len = buf_size;
    char *lines = buf;
    char tmp[buf_size];
    char *ptr = tmp;
    char *line;
    *tmp = 0;

    /* Fundamentally channel= is a phy property, not bss.
     * Bss parameters may not change but channel can via
     * csa. If this happens bss should not be considered to
     * have changed and therefore should not be reloaded.
     *
     * If hostapd isn't running yet the channel is
     * important. It could be an invalid or unusable
     * channel. As such it should be able to recalc a
     * subsequent channel fixup and retry adding hostapd
     * instance.
     */
    while ((line = strsep(&lines, "\n")))
        if (!running || (strstr(line, "channel=") != line
                && strstr(line, "ht_capab=") != line
                && strstr(line, "vht_oper_chwidth=") != line
                && strstr(line, "vht_oper_centr_freq_seg0_idx=") != line))
            csnprintf(&ptr, &len, "%s\n", line);
    strscpy(buf, tmp, buf_size); /* its guaranteed to fit */
    return buf;
}

static int
hapd_conf_changed(const char *a, const char *b, bool running)
{
    const char *x = hapd_conf_strip(strdupa(a), running);
    const char *y = hapd_conf_strip(strdupa(b), running);
    return strcmp(x, y);
}

int
hapd_conf_apply(struct hapd *hapd)
{
    const char *oldconf = R(hapd->confpath) ?: "";
    const char *oldpsks = R(hapd->pskspath) ?: "";
    bool running = ctrl_running(&hapd->ctrl);
    int changed_conf = hapd_conf_changed(oldconf, hapd->conf, running);
    int changed_psks = strcmp(oldpsks, hapd->psks);
    int add = !ctrl_running(&hapd->ctrl) && hapd_configured(hapd);
    int reload = ctrl_running(&hapd->ctrl) && hapd_configured(hapd);
    int reload_all = reload && changed_conf;
    int reload_psk = reload && changed_psks && !reload_all;
    int del = ctrl_running(&hapd->ctrl) && !hapd_configured(hapd);
    int err = 0;

    if (changed_conf) WARN_ON(W(hapd->confpath, hapd->conf) < 0);
    if (changed_psks) WARN_ON(W(hapd->pskspath, hapd->psks) < 0);
    if (add) err |= WARN_ON(W(hapd->pskspath, hapd->psks) < 0);
    if (add) err |= WARN_ON(hapd_ctrl_add(hapd));
    if (reload_psk) err |= WARN_ON(hapd_ctrl_reload_psk(hapd));
    if (reload_all) err |= WARN_ON(hapd_ctrl_reload(hapd));
    if (del) err |= WARN_ON(hapd_ctrl_remove(hapd));

    return err;
}

void
hapd_destroy(struct hapd *hapd)
{
    hapd_ctrl_remove(hapd);
    ctrl_disable(&hapd->ctrl);
    memset(hapd, 0, sizeof(*hapd));
}

int hapd_wps_activate(struct hapd *hapd)
{
    LOGI("%s: activating WPS session", hapd->ctrl.bss);
    return (strcmp("OK", HAPD_CLI(hapd, "wps_pbc") ?: "") == 0) ? 0 : -1;
}

int hapd_wps_cancel(struct hapd *hapd)
{
    LOGI("%s: cancelling WPS session", hapd->ctrl.bss);
    return (strcmp("OK", HAPD_CLI(hapd, "wps_cancel") ?: "") == 0) ? 0 : -1;
}

int
hapd_each(int (*iter)(struct ctrl *ctrl, void *ptr), void *ptr)
{
    size_t i;
    int err = 0;

    for (i = 0; i < ARRAY_SIZE(g_hapd); i++)
        if (strlen(g_hapd[i].ctrl.bss) > 0)
            err |= iter(&g_hapd[i].ctrl, ptr);

    return err;
}
