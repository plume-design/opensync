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
#include "util.h"
#include "const.h"
#include "log.h"
#include "kconfig.h"
#include "target.h"
#include "opensync-ctrl.h"
#include "opensync-hapd.h"

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

#define HAPD_DEFAULT_FT_KEY "8261b033613b35b373761cbde421250e67cd21d44737fe5e5deed61869b4b397"

#define HAPD_SOCK_PATH(dphy, dvif) F("/var/run/hostapd-%s/%s", dphy, dvif)
#define HAPD_SOCK_DIR(dphy) F("/var/run/hostapd-%s", dphy)
#define HAPD_CONF_PATH(dvif) F("/var/run/hostapd-%s.config", dvif)
#define HAPD_PSKS_PATH(dvif) F("/var/run/hostapd-%s.pskfile", dvif)
#define HAPD_RXKH_PATH(dvif) F("/var/run/hostapd-%s.rxkh", dvif)
#define HAPD_GLOB_CLI(...) E(CMD_TIMEOUT("wpa_cli", "-p", "", "-g", "/var/run/hostapd/global", ## __VA_ARGS__))
#define HAPD_CLI(hapd, ...) E(CMD_TIMEOUT("hostapd_cli", "-p", hapd->ctrl.sockdir, "-i", hapd->ctrl.bss, ## __VA_ARGS__))
#define EV(x) strchomp(strdupa(x), " ")

#define HAPD_CONF_APPEND(buf, len, ...) do { \
    csnprintf(&(buf), len, __VA_ARGS__); \
} while (0)

/* Append line to config file if string 'exists'. Otherwise comment out. */
#define HAPD_CONF_APPEND_STR_IF_EXISTS(buf, len, FMT, val) do { \
    csnprintf(&(buf), len, "%s", (val ## _exists) ? "" : "#"); \
    csnprintf(&(buf), len, FMT,  (val ## _exists) ? val : ""); \
} while (0)

/* Append config option if string is not empty. Otherwise comment out. */
#define HAPD_CONF_APPEND_STR_NE(buf, len, FMT, val) do { \
    csnprintf(&(buf), len, "%s", strlen(val) > 0 ? "" : "#"); \
    csnprintf(&(buf), len, FMT,  strlen(val) > 0 ? val : ""); \
} while (0)

/* Append config option if int 'exists'. Otherwise comment out. */
#define HAPD_CONF_APPEND_INT_IF_EXISTS(buf, len, FMT, val) do { \
    csnprintf(&(buf), len, "%s", (val ## _exists == true) ? "" : "#"); \
    csnprintf(&(buf), len, FMT,  (val ## _exists == true) ? val : 0); \
} while (0)

/* Append config option if int 'exists'. Otherwise comment out. */
#define HAPD_CONF_APPEND_WITH_DEFAULT(buf, len, FMT, val, def) do { \
    csnprintf(&(buf), len, FMT, (val ## _exists == true) ? val : def); \
} while (0)

#define MODULE_ID LOG_MODULE_ID_HAPD

/* Some Operating Systems, including at least some
 * Windows revisions at the time of writing, are known
 * to show up a WPS-PIN entry in the UI instead of
 * WPA-PSK/SAE passphrase entry when selecting a network.
 * This was confusing to users. One way to prevent that
 * is to use a very specific device_type string that gets
 * advertised through WPS IE.
 */
#define WPS_DEVICE_TYPE_STR "6-0050F204-1"

static struct hapd g_hapd[CONFIG_HAPD_MAX_BSS];

struct hapd_sae_keyid_fixup_ctx
{
    char key_id[128];
    unsigned int psks_num;
};

typedef void (*hapd_wpa_psk_handler_fn) (const char *key_id,
                                         const char *psk,
                                         const char *oftag_key,
                                         const char *oftag,
                                         bool is_wps,
                                         void *arg);

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

void
hapd_lookup_radius(struct hapd *hapd,
                   struct schema_RADIUS *radius_list,
                   int max_radius_num,
                   int *num_radius_list)
{
    const char *mib = HAPD_CLI(hapd, "mib");
    const char *token = mib;
    struct schema_RADIUS *ptr = radius_list;

    if (!token)
        return;

    while ((token = strstr(token, "radiusAuthServerAddress"))) {
        const char *ip = ini_geta(token, "radiusAuthServerAddress");
        const int port = atoi(ini_geta(token, "radiusAuthClientServerPortNumber"));

        if ((*num_radius_list) >= max_radius_num) goto trunc;

        STRSCPY_WARN(ptr->ip_addr, ip);
        ptr->port = port;
        STRSCPY_WARN(ptr->type, "AA");

        ptr++; (*num_radius_list)++; token++;
    }

    token = mib;
    while ((token = strstr(token, "radiusAccServerAddress"))) {
        const char *ip = ini_geta(token, "radiusAccServerAddress");
        const int port = atoi(ini_geta(token, "radiusAccClientServerPortNumber"));

        if ((*num_radius_list) >= max_radius_num) goto trunc;

        STRSCPY_WARN(ptr->ip_addr, ip);
        ptr->port = port;
        STRSCPY_WARN(ptr->type, "A");

        ptr++; (*num_radius_list)++; token++;
    }
    return;
trunc:
    LOGW("%s: the list of RADIUS servers truncated. Device supports max %d servers",
            __func__, max_radius_num);
}

void
hapd_lookup_nbors(struct hapd *hapd,
                  struct schema_Wifi_VIF_Neighbors *nbors_list,
                  int max_nbors_num,
                  int *num_nbors_list)
{
    const char *rxkhs = HAPD_CLI(hapd, "get_rxkhs");
    const char *token = rxkhs;
    struct schema_Wifi_VIF_Neighbors *nbor = nbors_list;
    char *bssid;
    char *nas_id;
    char *encr_key;
    *num_nbors_list = 0;

    if (!token)
        return;

    while ((token = strstr(token, "r0kh"))) {
        /* r0kh=AB:CD:EF:12:34:56 plumewifiAB1 862abc...df662 */
        char *line = ini_geta(token, "r0kh");
        bssid = strsep(&line, " ");
        nas_id = strsep(&line, " ");
        encr_key = strsep(&line, "");

        if ((*num_nbors_list) >= max_nbors_num) goto trunc;

        STRSCPY_WARN(nbor->bssid, str_tolower(bssid));
        STRSCPY_WARN(nbor->nas_identifier, nas_id);
        STRSCPY_WARN(nbor->ft_encr_key, encr_key);

        nbor++; (*num_nbors_list)++; token++;
    }
    return;
trunc:
    LOGW("%s: the list of Neighbors reported by hostapd truncated. Max supported: %d",
            __func__, max_nbors_num);
}

static void
hapd_ctrl_cb(struct ctrl *ctrl, int level, const char *buf, size_t len)
{
    struct hapd *hapd = container_of(ctrl, struct hapd, ctrl);
    const char *keyid = NULL;
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

    if (!strcmp(event, EV(DFS_EVENT_CAC_START))) {
        LOGI("%s: dfs event - cac started", hapd->ctrl.bss);
        if (hapd->dfs_event_cac_start)
            hapd->dfs_event_cac_start(hapd, args);

        return;
    }

    if (!strcmp(event, EV(DFS_EVENT_CAC_COMPLETED))) {
        LOGI("%s: dfs event - cac completed", hapd->ctrl.bss);
        if (hapd->dfs_event_cac_completed)
            hapd->dfs_event_cac_completed(hapd, args);

        return;
    }

    if (!strcmp(event, EV(DFS_EVENT_RADAR_DETECTED))) {
        LOGI("%s: dfs event - radar detected", hapd->ctrl.bss);
        if (hapd->dfs_event_radar_detected)
            hapd->dfs_event_radar_detected(hapd, args);

        return;
    }

    if (!strcmp(event, EV(DFS_EVENT_NOP_FINISHED))) {
        LOGI("%s: dfs event - nop finished", hapd->ctrl.bss);
        if (hapd->dfs_event_nop_finished)
            hapd->dfs_event_nop_finished(hapd, args);

        return;
    }

    if (!strcmp(event, EV(DFS_EVENT_PRE_CAC_EXPIRED))) {
        LOGI("%s: dfs event - pre cac expired", hapd->ctrl.bss);
        if (hapd->dfs_event_pre_cac_expired)
            hapd->dfs_event_pre_cac_expired(hapd, args);

        return;
    }

    if (!strcmp(event, EV(AP_CSA_FINISHED))) {
        LOGI("%s: ap csa event", hapd->ctrl.bss);
        if (hapd->ap_csa_finished)
            hapd->ap_csa_finished(hapd, args);

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
    STRSCPY_WARN(hapd->rxkhpath, HAPD_RXKH_PATH(bss));
    STRSCPY_WARN(hapd->phy, phy);
    hapd->ctrl.cb = hapd_ctrl_cb;
    hapd->ctrl.hapd = hapd;
    hapd->legacy_controller = false;
    hapd->group_by_phy_name = false;
    hapd->use_driver_iface_addr = false;
    /* Vendor specific flag to enable loopback socket for passing
     * FT RRB frames. Set by the target layer if hostapd implements
     * this feature. If not set when required, FT Action Frames
     * used in FT over-the-DS won't be handled by the driver and
     * FT over-the-DS will timeout */
    hapd->use_driver_rrb_lo = false;
    /* FIXME in current implementation it is assumed, that
     * target that implements rxkh_file in hostapd will set
     * this feature flag when initializing hapd structure.
     * Much better approach would be to figure it out in
     * runtime. Maybe by analyzing 'hostapd_cli help' output */
    hapd->use_rxkh_file = false;
    /* FIXME same as above */
    hapd->use_reload_rxkhs = false;

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
    /* FIXME Watch out! having the same nas_identifier for
     * many NASes in the ESS will not work as expected. STA
     * will advertise R0KH-ID and the new AP will recognize
     * it as itself. It obvioussly is not the owner of R0 key
     * so the whole exchange will fail.
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

    if (util_vif_wpa_key_mgmt_partial_match(vconf, "wpa-"))
        wpa |= 1;
    if (util_vif_wpa_key_mgmt_partial_match(vconf, "wpa2-") || util_vif_wpa_key_mgmt_partial_match(vconf, "sae"))
        wpa |= 2;

    return wpa;
}

static int
hapd_util_get_sae_require_mfp(const struct schema_Wifi_VIF_Config *vconf)
{
    if (!vconf->wpa)
        return 0;

    return util_vif_wpa_key_mgmt_partial_match(vconf, "sae") ? 1 : 0;
}

static int
hapd_util_get_ieee8021x(const struct schema_Wifi_VIF_Config *vconf)
{
    if (!vconf->wpa)
        return 0;

    return util_vif_wpa_key_mgmt_partial_match(vconf, "wpa2-eap") ? 1 : 0;
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

static void
hapd_vstate_add_wpa_psk_legacy(const char *key_id,
                               const char *psk,
                               const char *oftag_key,
                               const char *oftag,
                               bool is_wps,
                               void *arg)
{
    struct schema_Wifi_VIF_State *vstate = arg;

    SCHEMA_KEY_VAL_APPEND(vstate->security, key_id, psk);
    if (oftag_key)
        SCHEMA_KEY_VAL_APPEND(vstate->security, oftag_key, oftag);
    if (is_wps)
        SCHEMA_SET_STR(vstate->wps_pbc_key_id, key_id);
}

static void
hapd_vstate_add_wpa_psk(const char *key_id,
                        const char *psk,
                        const char *oftag_key,
                        const char *oftag,
                        bool is_wps,
                        void *arg)
{
    struct schema_Wifi_VIF_State *vstate = arg;

    SCHEMA_KEY_VAL_APPEND(vstate->wpa_psks, key_id, psk);
    if (is_wps)
        SCHEMA_SET_STR(vstate->wps_pbc_key_id, key_id);
}

static void
hapd_parse_wpa_psks_file_buf(const char *psks,
                             const char *if_name,
                             hapd_wpa_psk_handler_fn psk_handler,
                             void *psk_handler_arg)
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

    if (WARN_ON(!psk_handler))
        return;

    if (WARN_ON(!psks))
        return;

    ptr = strdupa(psks);
    if (WARN_ON(!ptr))
        return;

    LOGT("%s: parsing psk entries", if_name);

    for (;;) {
        const char *oftag_key;
        bool is_wps;

        if (!(oftagline = strsep(&ptr, "\n")))
            break;
        if (!(pskline = strsep(&ptr, "\n")))
            break;

        LOGT("%s: parsing pskfile: raw: oftagline='%s' pskline='%s'",
             if_name, oftagline, pskline);

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

        LOGT("%s: parsing pskfile: stripped: key_id='%s' wps='%s' oftag='%s' psk='%s'",
             if_name, key_id, wps, oftag, psk);

        oftag_key = NULL;
        if (strlen(oftag) > 0) {
            if (strcmp(key_id, "key") == 0)
                oftag_key = strfmta("oftag");
            else
                oftag_key = strfmta("oftag-%s", key_id);
        }

        is_wps = false;
        if (wps && strcmp(wps, "1") == 0) {
            if (!key_id)
                LOGW("%s: PSK in pskfile with 'wps=1' is required to have valid `keyid` tag",
                     if_name);
            else
                is_wps = true;
        }

        psk_handler(key_id, psk, oftag_key, oftag, is_wps, psk_handler_arg);
    }
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
        || (!strcmp(rconf->hw_mode, "11be"))
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
        || (!strcmp(rconf->hw_mode, "11be"))
        || (!strcmp(rconf->hw_mode, "11ac")))
        return 1;
    return 0;
}

static int
hapd_11ax_enabled(const struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf->hw_mode_exists) return 0;
    if (!strcmp(rconf->hw_mode, "11ax")
        || (!strcmp(rconf->hw_mode, "11be")))
        return 1;
    return 0;
}

static int
hapd_11be_enabled(const struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf->hw_mode_exists) return 0;
    if (!strcmp(rconf->hw_mode, "11be"))
        return 1;
    return 0;
}

static const char *
hapd_ht_caps(const struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf->ht_mode_exists) return "";

    if (!strcmp(rconf->ht_mode, "HT20"))
        return "[HT20]";

    if (strcmp(rconf->ht_mode, "HT40") &&
            strcmp(rconf->ht_mode, "HT80") &&
            strcmp(rconf->ht_mode, "HT160")) {
        LOGT("%s: %s is incorrect htmode", __func__, rconf->ht_mode);
        return "";
    }

    if (!strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_2G)) {
        switch (rconf->channel) {
            case 0:
                return "[HT40+] [HT40-]";
            case 1 ... 7:
                return "[HT40+]";
            case 8 ... 13:
                return "[HT40-]";
            default:
                LOG(TRACE,
                    "%s: %d is not a valid channel",
                    rconf->if_name, rconf->channel);
        }
    } else if (!strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_5G) ||
            !strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_5GL) ||
            !strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_5GU)) {
        switch (rconf->channel) {
            case 0:
                return "[HT40+] [HT40-]";
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
        }
    } else if (!strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_6G)) {
        if (!strcmp(rconf->ht_mode, "HT40")) {
            switch ((rconf->channel / 4) % 2) {
                case 0:
                    return "[HT40+]";
                case 1:
                    return "[HT40-]";
            }
        }
    }
    return "";
}

static int
hapd_op_class_6ghz_from_ht_mode(const char *ht_mode)
{
    int op_code = 0;

    if (!strcmp(ht_mode, "HT20"))
        op_code = 131;
    else if (!strcmp(ht_mode, "HT40"))
        op_code = 132;
    else if (!strcmp(ht_mode, "HT80"))
        op_code = 133;
    else if (!strcmp(ht_mode, "HT160"))
        op_code = 134;
    else if (!strcmp(ht_mode, "HT320"))
        op_code = 137;

    return op_code;
}

/* Get operating channel width for 80211n, 80211ac, 80211ax */
static int
hapd_util_get_oper_chwidth(const struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf->ht_mode_exists) return 0;
    if (!strcmp(rconf->ht_mode, "HT20") ||
            !strcmp(rconf->ht_mode, "HT40"))
        return 0;
    else if (!strcmp(rconf->ht_mode, "HT80"))
        return 1;
    else if (!strcmp(rconf->ht_mode, "HT160") ||
             !strcmp(rconf->ht_mode, "HT320"))
        return 2;

    LOGT("%s: %s is incorrect htmode", __func__, rconf->ht_mode);
    return 0;
}

/* Get operating channel width for 80211be */
static int
hapd_util_get_eht_oper_chwidth(const struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf->ht_mode_exists) return 0;

    if (!strcmp(rconf->ht_mode, "HT320"))
        return 9;
    else
        return hapd_util_get_oper_chwidth(rconf);
}

/* Get center frequency index for 80211n, 80211ac, 80211ax */
static int
hapd_util_get_oper_centr_freq_idx(const struct schema_Wifi_Radio_Config *rconf)
{
    int width = atoi(strlen(rconf->ht_mode) > 2 ? rconf->ht_mode + 2 : "20");
    const int *chans = NULL;

    if (!rconf->freq_band_exists || !rconf->channel_exists)
        return 0;

    /* Downgrade channel width from 320MHz to 160MHz */
    if (width == 320)
        width = 160;

    if (!strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_6G))
        chans = unii_6g_chan2list(rconf->channel, width);

    if ((!strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_5G))
        || (!strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_5GL))
        || (!strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_5GU)))
        chans = unii_5g_chan2list(rconf->channel, width);

    if (WARN_ON(!chans))
        return 0;

    return chanlist_to_center(chans);
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

    memset(buf, 0, len);

    for (i = 0; i < vconf->wpa_psks_len; i++) {
        keyid = vconf->wpa_psks_keys[i];

        LOGT("%s: parsing vconf: key '%s'", vconf->if_name, keyid);

        oftag = hapd_util_get_wpa_psk_oftag_by_keyid(vconf, keyid);
        psk = vconf->wpa_psks[i];
        wps = strcmp(keyid, vconf->wps_pbc_key_id) == 0 ? 1 : 0;

        if (!oftag)
            oftag = vconf->default_oftag_exists ? vconf->default_oftag : NULL;

        if (!oftag)
            oftag = "";

        LOGT("%s: parsing vconf: key '%s': oftag='%s' psk='%s' wps='%d'",
             vconf->if_name, keyid, oftag, psk, wps);

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
        csnprintf(&buf, len, "device_type=%s\n", WPS_DEVICE_TYPE_STR);
        csnprintf(&buf, len, "pbc_in_m1=1\n");
    }

    WARN_ON(*len == 1); /* likely buf was truncated */

    return hapd_conf_gen_psk(hapd, vconf);
}

struct vif_security_config {
    bool wpa_tkip;
    bool wpa_ccmp;
    bool rsn_tkip;
    bool rsn_ccmp;
    bool ft;
    bool eap;
    bool psk;
    bool sae;
    bool dpp;
    bool suiteb;
    bool wps;
    enum hostap_conf_wpa wpa;
    enum hostap_conf_pmf pmf;
    char wpa_pairwise[64];
    char rsn_pairwise[64];
    char wpa_key_mgmt[128];
};

static int
hapd_conf_fill_security(struct hapd *hapd,
                        struct vif_security_config *conf,
                        const struct schema_Wifi_VIF_Config *vconf)
{
    char *p_wpa_key_mgmt = conf->wpa_key_mgmt;
    size_t wpa_key_mgmt_len = sizeof(conf->wpa_key_mgmt);
    const char *key_mgmt;
    bool legacy = false;
    int mode, i;

    for (i = 0; i < vconf->wpa_key_mgmt_len; i++)
    {
        key_mgmt = vconf->wpa_key_mgmt[i];
        mode = util_vif_wpa_key_mgmt_to_enum(key_mgmt);

        /* resolve ambiguity of WPA_PSK in old/new controller */
        if (mode == HOSTAP_CONF_KEY_MGMT_WPA_PSK && !util_vif_pairwise_supported(vconf)) {
            mode = HOSTAP_CONF_KEY_MGMT_WPA_PSK_LEGACY;
        }

        switch (mode) {
            case HOSTAP_CONF_KEY_MGMT_WPA2_PSK:
                /* deprecated */
                legacy = true;
                conf->psk = true;
                conf->rsn_ccmp = true;
                conf->wpa |= HOSTAP_CONF_WPA_RSN;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "WPA-PSK ");
                break;
            case HOSTAP_CONF_KEY_MGMT_WPA_PSK_LEGACY:
                /* deprecated */
                /* This case HOSTAP_CONF_KEY_MGMT_is matched when old controller is detected.
                 * Option 'wpa-psk' used by old controller means WPA1/TKIP */
                legacy = true;
                conf->psk = true;
                conf->wpa_tkip = true;
                conf->wpa |= HOSTAP_CONF_WPA_WPA;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "WPA-PSK ");
                break;
            case HOSTAP_CONF_KEY_MGMT_WPA2_EAP:
                /* deprecated */
                legacy = true;
                conf->eap = true;
                conf->rsn_ccmp = true;
                conf->wpa |= HOSTAP_CONF_WPA_RSN;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "WPA-EAP ");
                break;
            case HOSTAP_CONF_KEY_MGMT_FT_WPA2_PSK:
                /* deprecated */
                legacy = true;
                conf->ft = true;
                conf->psk = true;
                conf->rsn_ccmp = true;
                conf->wpa |= HOSTAP_CONF_WPA_RSN;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "FT-PSK ");
                break;
            case HOSTAP_CONF_KEY_MGMT_WPA_PSK:
                /* This case of WPA_PSK only matches when using 'new controller'
                 * (having fields xxx_pairwise_xxxx) */
                conf->psk = true;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "WPA-PSK ");
                break;
            case HOSTAP_CONF_KEY_MGMT_WPA_PSK_SHA256:
                conf->psk = true;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "WPA-PSK-SHA256 ");
                break;
            case HOSTAP_CONF_KEY_MGMT_WPA_EAP:
                conf->eap = true;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "WPA-EAP ");
                break;
            case HOSTAP_CONF_KEY_MGMT_WPA_EAP_SHA256:
                conf->eap = true;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "WPA-EAP-SHA256 ");
                break;
            case HOSTAP_CONF_KEY_MGMT_WPA_EAP_B_192:
                conf->eap = true;
                conf->suiteb = true;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "WPA-EAP-SUITE-B-192 ");
                break;
            case HOSTAP_CONF_KEY_MGMT_FT_PSK:
                conf->ft = true;
                conf->psk = true;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "FT-PSK ");
                break;
            case HOSTAP_CONF_KEY_MGMT_FT_EAP:
                conf->ft = true;
                conf->eap = true;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "FT-EAP ");
                break;
            case HOSTAP_CONF_KEY_MGMT_FT_EAP_SHA384:
                conf->ft = true;
                conf->eap = true;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "FT-EAP-SHA384 ");
                break;
            case HOSTAP_CONF_KEY_MGMT_FT_SAE:
                conf->ft = true;
                conf->sae = true;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "FT-SAE ");
                break;
            case HOSTAP_CONF_KEY_MGMT_DPP:
                conf->dpp = true;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "DPP ");
                break;
            case HOSTAP_CONF_KEY_MGMT_SAE:
                conf->sae = true;
                csnprintf(&p_wpa_key_mgmt, &wpa_key_mgmt_len, "SAE ");
                break;
            case HOSTAP_CONF_KEY_MGMT_UNKNOWN:
                LOGE("%s: Error while parsing wpa_key_mgmt.", __func__);
                return -1;
        }
    }
    conf->wps = vconf->wps;

    /* Combination of legacy and non-legacy options is invalid.
     * The controller can either be old (using only legacy) or
     * new (using only non-legacy modes) */
    if (legacy) {
        if (conf->sae || conf->dpp) conf->pmf = HOSTAP_CONF_PMF_REQUIRED;
        if (conf->pmf && conf->psk) conf->pmf = HOSTAP_CONF_PMF_OPTIONAL;

        /* To properly report state tables the code needs to store information
         * if legacy controller is used in an object accessible from functions
         * generating also state tables */
        hapd->legacy_controller = true;
        LOGW("%s Configuring legacy mode only!", __func__);
        return 0;
    }

    conf->wpa = util_vif_get_wpa(vconf);

    conf->wpa_tkip = vconf->wpa_pairwise_tkip;
    conf->wpa_ccmp = vconf->wpa_pairwise_ccmp;
    conf->rsn_tkip = vconf->rsn_pairwise_tkip;
    conf->rsn_ccmp = vconf->rsn_pairwise_ccmp;

    conf->pmf = util_vif_pmf_schema_to_enum(vconf->pmf);

    return 0;
}

static void
hapd_conf_validate_config(struct hapd *hapd,
                          struct vif_security_config *conf,
                          const struct schema_Wifi_VIF_Config *vconf)
{
    if (conf->wps && conf->eap) {
        LOGW("%s: Disabling WPS because 802.1x(EAP) is enabled", vconf->if_name);
        conf->wps = false;
    }

    if (!conf->wpa && conf->wps) {
        LOGW("%s: Disabling WPS because OPEN mode is enabled", vconf->if_name);
        conf->wps = false;
    }

    if (conf->wps && !kconfig_enabled(CONFIG_HOSTAP_PSK_FILE_WPS)) {
        LOGW("%s: Disabling WPS. Not supporting PSK file", vconf->if_name);
        conf->wps = false;
    }

    if (conf->sae && !conf->pmf) {
        LOGW("%s: Setting PMF to 'optional' as required by SAE", vconf->if_name);
        conf->pmf = HOSTAP_CONF_PMF_OPTIONAL;
    }

    if (conf->sae && (conf->wpa_tkip || conf->wpa_ccmp || conf->rsn_tkip)) {
        LOGW("%s: SAE cannot coexist with WPA1 and/or TKIP. Disabling WPA1/TKIP", vconf->if_name);
        conf->wpa_tkip = false;
        conf->wpa_ccmp = false;
        conf->rsn_tkip = false;
        conf->wpa = HOSTAP_CONF_WPA_RSN;
    }
}

static int
hapd_conf_gen_security_new(struct hapd *hapd,
                           char *buf,
                           size_t *len,
                           const struct schema_Wifi_VIF_Config *vconf,
                           const struct schema_Wifi_VIF_Neighbors *nbors_list,
                           const struct schema_RADIUS *radius_list,
                           const int num_nbors_list,
                           const int num_radius_list,
                           const char *bssid)
{
    int i;
    char *rxkh_buf = hapd->rxkh;
    size_t rxkh_len = sizeof(hapd->rxkh);;
    struct vif_security_config conf;
    memset(&conf, 0, sizeof(struct vif_security_config));
    memset(rxkh_buf, 0, sizeof(hapd->rxkh));

    hapd_conf_fill_security(hapd, &conf, vconf);
    hapd_conf_validate_config(hapd, &conf, vconf);

    HAPD_CONF_APPEND(buf, len, "wpa=%d\n", conf.wpa);
    HAPD_CONF_APPEND(buf, len, "auth_algs=%d\n", 1);

    if (conf.wpa_tkip) STRSCAT(conf.wpa_pairwise, "TKIP ");
    if (conf.wpa_ccmp) STRSCAT(conf.wpa_pairwise, "CCMP ");
    if (conf.rsn_tkip) STRSCAT(conf.rsn_pairwise, "TKIP ");
    if (conf.rsn_ccmp) STRSCAT(conf.rsn_pairwise, "CCMP ");

    HAPD_CONF_APPEND_STR_NE(buf, len, "wpa_key_mgmt=%s\n", conf.wpa_key_mgmt);
    HAPD_CONF_APPEND_STR_NE(buf, len, "wpa_pairwise=%s\n", conf.wpa_pairwise);
    HAPD_CONF_APPEND_STR_NE(buf, len, "rsn_pairwise=%s\n", conf.rsn_pairwise);

    if (conf.psk || conf.sae) {
        HAPD_CONF_APPEND_STR_NE(buf, len, "wpa_psk_file=%s\n", hapd->pskspath);
    }

    if (conf.eap) {
        HAPD_CONF_APPEND(buf, len, "ieee8021x=%s\n", "1");
        HAPD_CONF_APPEND(buf, len, "own_ip_addr=%s\n", "127.0.0.1");
        for (i = 0; i < num_radius_list; i++)
        {
            if (!strcmp(radius_list->type, "AA")) {
                HAPD_CONF_APPEND(buf, len, "auth_server_addr=%s\n", radius_list->ip_addr);
                HAPD_CONF_APPEND(buf, len, "auth_server_port=%d\n", radius_list->port);
                HAPD_CONF_APPEND(buf, len, "auth_server_shared_secret=%s\n", radius_list->secret);
            }
            if (!strcmp(radius_list->type, "A")) {
                HAPD_CONF_APPEND(buf, len, "acct_server_addr=%s\n", radius_list->ip_addr);
                HAPD_CONF_APPEND(buf, len, "acct_server_port=%d\n", radius_list->port);
                HAPD_CONF_APPEND(buf, len, "acct_server_shared_secret=%s\n", radius_list->secret);
            }
            radius_list++;
        }
        /* Take legacy RADIUS configuration. In case of radius_list
         * being empty this becomes primary server. One of secondary
         * servers otherwise */
        if (vconf->radius_srv_addr_exists &&
            vconf->radius_srv_port_exists &&
            vconf->radius_srv_secret_exists) {
            HAPD_CONF_APPEND(buf, len, "auth_server_addr=%s\n", vconf->radius_srv_addr);
            HAPD_CONF_APPEND(buf, len, "auth_server_port=%d\n", vconf->radius_srv_port);
            HAPD_CONF_APPEND(buf, len, "auth_server_shared_secret=%s\n", vconf->radius_srv_secret);
        }
    }

    if (conf.sae) {
        if (vconf->wpa_psks_len != 1) {
            LOGE("%s: SAE requires wpa_psks to contain exactly one PSK", vconf->if_name);
            goto err;
        }
        HAPD_CONF_APPEND(buf, len, "sae_password=%s\n", vconf->wpa_psks[0]);
        HAPD_CONF_APPEND(buf, len, "sae_require_mfp=%d\n", 1);
    }

    if (conf.ft) {
        HAPD_CONF_APPEND_WITH_DEFAULT(buf, len, "nas_identifier=%s\n", vconf->nas_identifier, bssid);
        HAPD_CONF_APPEND_WITH_DEFAULT(buf, len, "mobility_domain=%04x\n", vconf->ft_mobility_domain, 0xddf7);
        HAPD_CONF_APPEND(buf, len, "ft_psk_generate_local=1\n");
    }

    if (conf.ft && (conf.eap || conf.sae)) {
        HAPD_CONF_APPEND_WITH_DEFAULT(buf, len, "ft_over_ds=%d\n", vconf->ft_over_ds, 1);
        HAPD_CONF_APPEND_WITH_DEFAULT(buf, len, "pmk_r1_push=%d\n", vconf->ft_pmk_r1_push, 0);
        HAPD_CONF_APPEND_WITH_DEFAULT(buf, len, "ft_r0_key_lifetime=%d\n", vconf->ft_pmk_r0_key_lifetime_sec, 1209600);
        HAPD_CONF_APPEND_WITH_DEFAULT(buf, len, "r1_max_key_lifetime=%d\n", vconf->ft_pmk_r1_max_key_lifetime_sec, 86400);

        /* R0KHs */
        for (i = 0; i < num_nbors_list; i++) {
            HAPD_CONF_APPEND(rxkh_buf, &rxkh_len, "r0kh=%s ", nbors_list[i].bssid);
            HAPD_CONF_APPEND_WITH_DEFAULT(rxkh_buf, &rxkh_len, "%s " , nbors_list[i].nas_identifier, nbors_list[i].bssid);
            HAPD_CONF_APPEND_WITH_DEFAULT(rxkh_buf, &rxkh_len, "%s\n", nbors_list[i].ft_encr_key, HAPD_DEFAULT_FT_KEY);
        }
        /* R1KHs */
        for (i = 0; i < num_nbors_list; i++) {
            HAPD_CONF_APPEND(rxkh_buf, &rxkh_len, "r1kh=%s %s ",  nbors_list[i].bssid, nbors_list[i].bssid);
            HAPD_CONF_APPEND_WITH_DEFAULT(rxkh_buf, &rxkh_len, "%s\n", nbors_list[i].ft_encr_key, HAPD_DEFAULT_FT_KEY);
        }

        if (hapd->use_rxkh_file) {
            HAPD_CONF_APPEND(buf, len, "rxkh_file=%s\n", hapd->rxkhpath);
        } else {
            HAPD_CONF_APPEND(buf, len, "%s", rxkh_buf);
        }

        if (hapd->use_driver_rrb_lo)
            HAPD_CONF_APPEND(buf, len, "ft_rrb_lo_sock=1\n");
    }

    if (conf.wps) {
        HAPD_CONF_APPEND(buf, len, "wps_state=%d\n", 2);
        HAPD_CONF_APPEND(buf, len, "eap_server=%d\n", 1);
        HAPD_CONF_APPEND(buf, len, "config_methods=virtual_push_button\n");
        HAPD_CONF_APPEND(buf, len, "device_type=%s\n", WPS_DEVICE_TYPE_STR);
        HAPD_CONF_APPEND(buf, len, "pbc_in_m1=%d\n", 1);
    }

    if (conf.dpp) {
        HAPD_CONF_APPEND_STR_NE(buf, len, "dpp_connector=%s\n", vconf->dpp_connector);
        HAPD_CONF_APPEND_STR_NE(buf, len, "dpp_csign=%s\n", vconf->dpp_csign_hex);
        HAPD_CONF_APPEND_STR_NE(buf, len, "dpp_netaccesskey=%s\n", vconf->dpp_netaccesskey_hex);
    }
    HAPD_CONF_APPEND(buf, len, "ieee80211w=%d\n", conf.pmf);
    WARN_ON(*len == 1);

    return hapd_conf_gen_wpa_psks(hapd, vconf);

err:
    LOGE("%s: Failed to generate hostapd.conf file", vconf->if_name);
    return -1;
}

static int
hapd_conf_gen_security(struct hapd *hapd,
                       char *buf,
                       size_t *len,
                       const struct schema_Wifi_VIF_Config *vconf,
                       const char *bssid)
{
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
    if (util_vif_wpa_key_mgmt_partial_match(vconf, "sae")) {
        if (vconf->wpa_psks_len != 1) {
            LOGE("%s: SAE requires wpa_psks to contain exactly one psk", vconf->if_name);
            return -1;
        }

        sae_psk = vconf->wpa_psks[0];
    }

    /*
     * WPA-EAP + remote RADIUS excludes WPS on single VAP (hostapd doesn't support
     * such configuration).
     */
    if (util_vif_wpa_key_mgmt_partial_match(vconf, "eap") && vconf->wps) {
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

    sae_opt_present = util_vif_wpa_key_mgmt_partial_match(vconf, "sae") ? "" : "#";
    wps_opt_present = kconfig_enabled(CONFIG_HOSTAP_PSK_FILE_WPS) && !suppress_wps ? "" : "#";
    ft_opt_present = util_vif_wpa_key_mgmt_partial_match(vconf, "ft-") ? "" : "#";
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
    csnprintf(&buf, len, "%s", util_vif_wpa_key_mgmt_partial_match(vconf, "eap") ? "" : "#");
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
    csnprintf(&buf, len, "%snas_identifier=%s\n", ft_opt_present, bssid);
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
    csnprintf(&buf, len, "%sdevice_type=%s\n", wps_opt_present, WPS_DEVICE_TYPE_STR);
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
    /* For backwards compatibility resolve hapd_conf_gen
     * as the call to new hapd_conf_gen2 ignoring some
     * parameters for RADIUS configuration
     */
    return hapd_conf_gen2(hapd, rconf, vconf, NULL, NULL, 0, 0, NULL);
}

int
hapd_conf_gen2(struct hapd *hapd,
               const struct schema_Wifi_Radio_Config *rconf,
               const struct schema_Wifi_VIF_Config *vconf,
               const struct schema_Wifi_VIF_Neighbors *nbors_list,
               const struct schema_RADIUS *radius_list,
               const int num_nbors_list,
               const int num_radius_list,
               const char *bssid)
{
    size_t len = sizeof(hapd->conf);
    char *buf = hapd->conf;
    char low_bssid[] = "00:00:00:00:00:00";
    const int ap_isolate = !vconf->ap_bridge;
    int closed;

    memset(buf, 0, len);
    memset(hapd->psks, 0, sizeof(hapd->psks));
    memset(hapd->rxkh, 0, sizeof(hapd->rxkh));

    if (!vconf->enabled)
        return 0;

    if (bssid != NULL)
        STRSCPY_WARN(low_bssid, bssid);

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
    csnprintf(&buf, &len, "ap_isolate=%d\n", ap_isolate);
    csnprintf(&buf, &len, "%s", hapd->respect_multi_ap ? "" : "#");
    csnprintf(&buf, &len, "multi_ap=%d\n", hapd_map_str2int(vconf));
    csnprintf(&buf, &len, "send_probe_response=%d\n", hapd->skip_probe_response ? 0 : 1);
    csnprintf(&buf, &len, "%s", hapd->ieee80211n ? "" : "#");
    csnprintf(&buf, &len, "ieee80211n=%d\n", hapd_11n_enabled(rconf));
    /* Hostapd supports 11ac VHT features on 2.4GHz although it is vendor specific.
     * Depending on driver capabilities, VHT configurations can be enabled for 2.4GHz.
     * ieee80211ac can be disabled in target layer in case of hostapd issues on 2.4GHz.
     */
    csnprintf(&buf, &len, "%s", hapd->ieee80211ac ? "" : "#");
    csnprintf(&buf, &len, "ieee80211ac=%d\n", hapd_11ac_enabled(rconf));
    csnprintf(&buf, &len, "%s", hapd->ieee80211ax ? "" : "#");
    csnprintf(&buf, &len, "ieee80211ax=%d\n", hapd_11ax_enabled(rconf));
    csnprintf(&buf, &len, "%s", hapd->ieee80211be ? "" : "#");
    csnprintf(&buf, &len, "ieee80211be=%d\n", hapd_11be_enabled(rconf));
    if (hapd->use_driver_iface_addr == true) {
        csnprintf(&buf, &len, "use_driver_iface_addr=1\n");
    }
    csnprintf(&buf, &len, "%s", vconf->dpp_cc_exists ? "" : "#");
    csnprintf(&buf, &len, "dpp_configurator_connectivity=%d\n", vconf->dpp_cc ? 1 : 0);

    if (strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_2G)) {
        if (hapd->ieee80211ax && hapd_11ax_enabled(rconf)) {
            csnprintf(&buf, &len, "he_oper_chwidth=%d\n", hapd_util_get_oper_chwidth(rconf));
            csnprintf(&buf, &len, "he_oper_centr_freq_seg0_idx=%d\n",
                      hapd_util_get_oper_centr_freq_idx(rconf));
        }
        if (hapd->ieee80211ac && hapd_11ac_enabled(rconf) &&
            (strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_6G))) {
            csnprintf(&buf, &len, "vht_oper_chwidth=%d\n", hapd_util_get_oper_chwidth(rconf));
            csnprintf(&buf, &len, "vht_oper_centr_freq_seg0_idx=%d\n",
                      hapd_util_get_oper_centr_freq_idx(rconf));
        }
        if (hapd->ieee80211be && hapd_11be_enabled(rconf) && !WARN_ON(!rconf->center_freq0_chan_exists)) {
            csnprintf(&buf, &len, "eht_oper_chwidth=%d\n", hapd_util_get_eht_oper_chwidth(rconf));
            csnprintf(&buf, &len, "eht_oper_centr_freq_seg0_idx=%d\n", rconf->center_freq0_chan);
        }
    }

    if (!strcmp(rconf->freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_6G)) {
        const int op_class = hapd_op_class_6ghz_from_ht_mode(rconf->ht_mode);
        csnprintf(&buf, &len, "op_class=%d\n", op_class);
        csnprintf(&buf, &len, "sae_pwe=2\n");
    }

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

    if (rconf->bcn_int_exists && rconf->bcn_int >= 0)
        csnprintf(&buf, &len, "beacon_int=%d\n", rconf->bcn_int);

    if (vconf->wpa_exists) {
        if (util_vif_pairwise_supported(vconf)) {
            hapd->legacy_controller = false;
            return hapd_conf_gen_security_new(hapd, buf, &len,
                    vconf, nbors_list, radius_list,
                    num_nbors_list, num_radius_list,
                    str_tolower(low_bssid));
        } else {
            hapd->legacy_controller = true;
            return hapd_conf_gen_security(hapd, buf, &len, vconf,
                                          str_tolower(low_bssid));
        }
    } else {
        hapd->legacy_controller = true;
        return hapd_conf_gen_security_legacy(hapd, buf, &len, vconf);
    }
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
                      const char *status,
                      const char *mib)
{
    char *conf_keys = ini_geta(status, "key_mgmt") ?: "";
    const char *mib_radius_srv_addr = ini_geta(mib, "radiusAuthServerAddress");
    const char *mib_radius_srv_port = ini_geta(mib, "radiusAuthClientServerPortNumber");
    const int conf_wpa = atoi(ini_geta(status, "wpa") ?: "0");
    const char *conf_key;
    const char *p;
    bool wpa_psk = false;
    bool wpa2_psk = false;
    bool wpa2_eap = false;
    bool sae = false;
    bool ft_wpa2_psk = false;
    bool ft_sae = false;
    bool dpp = false;

    while ((conf_key = strsep(&conf_keys, " "))) {
        if (strcmp(conf_key, "WPA-PSK") == 0) {
            if (conf_wpa & 1)
                wpa_psk = true;
            if (conf_wpa & 2)
                wpa2_psk = true;
            continue;
        }
        if (strcmp(conf_key, "WPA-EAP") == 0) {
            wpa2_eap = true;
            continue;
        }
        if (strcmp(conf_key, "SAE") == 0) {
            sae = true;
            continue;
        }
        if (strcmp(conf_key, "FT-PSK") == 0 && (conf_wpa & 2)) {
            ft_wpa2_psk = true;
            continue;
        }
        if (strcmp(conf_key, "FT-SAE") == 0) {
            ft_sae = true;
            continue;
        }
        if (strcmp(conf_key, "DPP") == 0) {
            dpp = true;
            continue;
        }
    }

    SCHEMA_SET_INT(vstate->wpa, wpa_psk || wpa2_psk || wpa2_eap || sae || ft_wpa2_psk);
    if (wpa_psk) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "wpa-psk");
    if (wpa2_psk) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "wpa2-psk");
    if (wpa2_eap) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "wpa2-eap");
    if (sae) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "sae");
    if (ft_wpa2_psk) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_WPA2_PSK);
    if (ft_sae) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_SAE);
    if (dpp) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, "dpp");
    if (mib_radius_srv_addr) SCHEMA_SET_STR(vstate->radius_srv_addr, mib_radius_srv_addr);
    if (mib_radius_srv_port) SCHEMA_SET_INT(vstate->radius_srv_port, atoi(mib_radius_srv_port));
    if ((p = ini_geta(conf, "dpp_connector"))) SCHEMA_SET_STR(vstate->dpp_connector, p);
    if ((p = ini_geta(conf, "dpp_csign"))) SCHEMA_SET_STR(vstate->dpp_csign_hex, p);
    if ((p = ini_geta(conf, "dpp_netaccesskey"))) SCHEMA_SET_STR(vstate->dpp_netaccesskey_hex, p);
}

static void
hapd_bss_get_security_new(struct schema_Wifi_VIF_State *vstate,
                          const char *conf,
                          const char *status,
                          const char *mib)
{
    char *conf_keys = ini_geta(status, "key_mgmt") ?: "";
    const char *mib_radius_srv_addr = ini_geta(mib, "radiusAuthServerAddress");
    const char *mib_radius_srv_port = ini_geta(mib, "radiusAuthClientServerPortNumber");
    const char *wpa_pairwise = ini_geta(status, "wpa_pairwise_cipher") ?: "";
    const char *rsn_pairwise = ini_geta(status, "rsn_pairwise_cipher") ?: "";
    const int conf_wpa = atoi(ini_geta(status, "wpa") ?: "0");
    const int pmf = atoi(ini_geta(conf, "ieee80211w") ?: "0");

    /*
     * Hostapd do not expose any status interface to get below data
     * Report what's configured in config file in vstate. Omit key.
     */
    struct hapd *hapd = hapd_lookup(vstate->if_name);
    const int ft_over_ds = atoi(ini_geta(hapd->conf, "ft_over_ds") ?: "0");
    const int ft_pmk_r0_key_lifetime_sec = atoi(ini_geta(hapd->conf, "ft_r0_key_lifetime") ?: "0");
    const int ft_pmk_r1_max_key_lifetime_sec = atoi(ini_geta(hapd->conf, "r1_max_key_lifetime") ?: "0");
    const int ft_pmk_r1_push = atoi(ini_geta(hapd->conf, "pmk_r1_push") ?: "0");
    const int ft_psk_generate_local = atoi(ini_geta(hapd->conf, "ft_psk_generate_local") ?: "0");
    const char *nas_identifier = ini_geta(hapd->conf, "nas_identifier");

    char *conf_key;
    const char *p;

    while ((conf_key = strsep(&conf_keys, " "))) {
        SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, str_tolower(conf_key));
    }

    SCHEMA_SET_BOOL(vstate->wpa, (conf_wpa != 0) ? true : false);
    SCHEMA_SET_BOOL(vstate->wpa_pairwise_tkip, (strstr(wpa_pairwise, "TKIP") != NULL) ? true : false);
    SCHEMA_SET_BOOL(vstate->wpa_pairwise_ccmp, (strstr(wpa_pairwise, "CCMP") != NULL) ? true : false);
    SCHEMA_SET_BOOL(vstate->rsn_pairwise_tkip, (strstr(rsn_pairwise, "TKIP") != NULL) ? true : false);
    SCHEMA_SET_BOOL(vstate->rsn_pairwise_ccmp, (strstr(rsn_pairwise, "CCMP") != NULL) ? true : false);
    SCHEMA_SET_STR(vstate->pmf, util_vif_pmf_enum_to_schema(pmf));

    SCHEMA_SET_BOOL(vstate->ft_over_ds, (ft_over_ds != 0) ? true : false);
    SCHEMA_SET_INT(vstate->ft_pmk_r0_key_lifetime_sec, ft_pmk_r0_key_lifetime_sec);
    SCHEMA_SET_INT(vstate->ft_pmk_r1_max_key_lifetime_sec, ft_pmk_r1_max_key_lifetime_sec);
    SCHEMA_SET_BOOL(vstate->ft_pmk_r1_push, (ft_pmk_r1_push != 0) ? true : false);
    SCHEMA_SET_BOOL(vstate->ft_psk_generate_local, (ft_psk_generate_local != 0) ? true : false);
    if (nas_identifier) SCHEMA_SET_STR(vstate->nas_identifier, nas_identifier);
    if (mib_radius_srv_addr) SCHEMA_SET_STR(vstate->radius_srv_addr, mib_radius_srv_addr);
    if (mib_radius_srv_port) SCHEMA_SET_INT(vstate->radius_srv_port, atoi(mib_radius_srv_port));
    if ((p = ini_geta(conf, "dpp_connector"))) SCHEMA_SET_STR(vstate->dpp_connector, p);
    if ((p = ini_geta(conf, "dpp_csign"))) SCHEMA_SET_STR(vstate->dpp_csign_hex, p);
    if ((p = ini_geta(conf, "dpp_netaccesskey"))) SCHEMA_SET_STR(vstate->dpp_netaccesskey_hex, p);
}

static void
hapd_bss_get_psks_legacy(struct schema_Wifi_VIF_State *vstate,
                         const char *conf)
{
    hapd_parse_wpa_psks_file_buf(conf, vstate->if_name, hapd_vstate_add_wpa_psk_legacy, vstate);
}

static void
hapd_bss_get_psks(struct schema_Wifi_VIF_State *vstate,
                  const char *conf)
{
    hapd_parse_wpa_psks_file_buf(conf, vstate->if_name, hapd_vstate_add_wpa_psk, vstate);
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

static void
hapd_sae_keyid_fixup_handle_wpa_psk(const char *key_id,
                                    const char *psk,
                                    const char *oftag_key,
                                    const char *oftag,
                                    bool is_wps,
                                    void *arg)
{
    struct hapd_sae_keyid_fixup_ctx *ctx = arg;
    STRSCPY_WARN(ctx->key_id, key_id);
    ctx->psks_num++;
}

static void
hapd_sae_keyid_fixup(const struct hapd *hapd,
                     const char *hapd_sta_cmd_output,
                     struct schema_Wifi_Associated_Clients *client)
{
    static const char *sae_akms[] = {
        "00-0f-ac-8", /* SAE */
        "00-0f-ac-9", /* FT-SAE */
        NULL
    };

    struct hapd_sae_keyid_fixup_ctx ctx;
    unsigned int i;
    const char *if_name = hapd->ctrl.bss;
    const char *sta_akm;
    const char *k;
    const char *v;
    char *lines;
    char *kv;
    bool is_sae_akm;

    if (strlen(client->key_id) > 0)
        return; /* key_id is set, skipping */

    sta_akm = NULL;
    lines = strdupa(hapd_sta_cmd_output);
    while ((kv = strsep(&lines, "\r\n"))) {
        if ((k = strsep(&kv, "=")) && (v = strsep(&kv, ""))) {
            if (!strcmp(k, "AKMSuiteSelector"))
                sta_akm = v;
        }
    }

    if (!sta_akm)
        return; /* OPEN security mode, skipping */

    is_sae_akm = false;
    for (i = 0; sae_akms[i]; i++) {
        if (strcmp(sta_akm, sae_akms[i]) != 0)
            is_sae_akm = true;
    }

    if (!is_sae_akm)
        return; /* None-SAE AKM, skipping */

    memset(&ctx, 0, sizeof(ctx));
    hapd_parse_wpa_psks_file_buf(R(hapd->pskspath), if_name, hapd_sae_keyid_fixup_handle_wpa_psk, &ctx);

    if (ctx.psks_num > 1) {
        LOGW("%s: keyid cannot be fixed for AP with SAE AKM using multiple PSKs", if_name);
        return;
    }

    if (ctx.psks_num == 0) {
        LOGW("%s: AP with SAE AKM cannot be configured with none PSKs", if_name);
        return;
    }

    SCHEMA_SET_STR(client->key_id, ctx.key_id);

    LOGD("%s: STA %s connected with SAE but failed to lookup keyid, setting keyid to '%s'",
         if_name, client->mac, client->key_id);
}

int
hapd_bss_get(struct hapd *hapd,
             struct schema_Wifi_VIF_State *vstate)
{
    const char *status = HAPD_CLI(hapd, "get_config");
    const char *mib = HAPD_CLI(hapd, "mib");
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
        if (hapd->legacy_controller) {
            hapd_bss_get_security_legacy(vstate, conf, status);
            hapd_bss_get_security(vstate, conf, status, mib);
        } else {
            hapd_bss_get_security_new(vstate, conf, status, mib);
        }
        hapd_bss_get_wps(hapd, vstate, status);
        hapd_bss_get_psks_legacy(vstate, psks);
        hapd_bss_get_psks(vstate, psks);
    }

    /* FIXME: rely on "status" command, eg state=ENABLED, ssid[0]=XXX */

    return 0;
}

static const char*
hapd_akmsuite_translation(const char* akm_suite)
{
    if (!strcmp(akm_suite, "00-0f-ac-1"))
        return "wpa-eap";
    else if (!strcmp(akm_suite, "00-0f-ac-2"))
        return "wpa-psk";
    else if (!strcmp(akm_suite, "00-0f-ac-3"))
        return "ft-eap";
    else if (!strcmp(akm_suite, "00-0f-ac-4"))
        return "ft-psk";
    else if (!strcmp(akm_suite, "00-0f-ac-5"))
        return "wpa-eap-sha256";
    else if (!strcmp(akm_suite, "00-0f-ac-6"))
        return "wpa-psk-sha256";
    else if (!strcmp(akm_suite, "00-0f-ac-8"))
        return "sae";
    else if (!strcmp(akm_suite, "00-0f-ac-9"))
        return "ft-sae";
    else if (!strcmp(akm_suite, "00-0f-ac-12"))
        return "wpa-eap-suite-b-192";
    else if (!strcmp(akm_suite, "00-0f-ac-13"))
        return "ft-eap-sha384";
    else if (!strcmp(akm_suite, "50-6f-9a-02"))
        return "dpp";

    return NULL;
}

static const char*
hapd_ciphersuite_translation(const char* cipher_suite)
{
    if (!strcmp(cipher_suite, "00-0f-ac-0"))
        return "rsn-none";
    else if (!strcmp(cipher_suite, "00-0f-ac-1"))
        return "wep";
    else if (!strcmp(cipher_suite, "00-0f-ac-2"))
        return "rsn-tkip";
    else if (!strcmp(cipher_suite, "00-0f-ac-4"))
        return "rsn-ccmp";
    else if (!strcmp(cipher_suite, "00-0f-ac-6"))
        return "bip-cmac";
    else if (!strcmp(cipher_suite, "00-50-f2-0"))
        return "wpa-none";
    else if (!strcmp(cipher_suite, "00-50-f2-2"))
        return "wpa-tkip";
    else if (!strcmp(cipher_suite, "00-50-f2-4"))
        return "wpa-ccmp";
    
    return NULL;
}

int
hapd_sta_get(struct hapd *hapd,
             const char *mac,
             struct schema_Wifi_Associated_Clients *client)
{
    const char *sta = HAPD_CLI(hapd, "sta", mac) ?: "";
    const char *keyid = NULL;
    const char *dpp_pkhash = NULL;
    const char *akm_suite = NULL;
    const char *wpa_key_mgmt = NULL;
    const char *pairwise_cipher_suite = NULL;
    const char *pairwise_cipher = NULL;
    const char *k;
    const char *v;
    char *lines = strdupa(sta);
    char *kv;
    bool pmf = false;

    while ((kv = strsep(&lines, "\r\n")))
        if ((k = strsep(&kv, "=")) &&
            (v = strsep(&kv, ""))) {
            if (!strcmp(k, "keyid"))
                keyid = v;
            else if (!strcmp(k, "dpp_pkhash"))
                dpp_pkhash = v;
            else if (!strcmp(k, "AKMSuiteSelector"))
                akm_suite = v;
            else if (!strcmp(k, "dot11RSNAStatsSelectedPairwiseCipher"))
                pairwise_cipher_suite = v;
            else if (!strcmp(k, "flags")) {
                if (strstr(v, "[MFP]"))
                    pmf = true;
            }
        }

    SCHEMA_SET_STR(client->key_id, keyid ?: "");
    SCHEMA_SET_STR(client->mac, mac);
    SCHEMA_SET_STR(client->state, "active");
    SCHEMA_SET_BOOL(client->pmf, pmf);

    if (akm_suite)
        wpa_key_mgmt = hapd_akmsuite_translation(akm_suite);
    if (pairwise_cipher_suite)
        pairwise_cipher = hapd_ciphersuite_translation(pairwise_cipher_suite);

    if (dpp_pkhash) SCHEMA_SET_STR(client->dpp_netaccesskey_sha256_hex, dpp_pkhash);

    if (wpa_key_mgmt) SCHEMA_SET_STR(client->wpa_key_mgmt, wpa_key_mgmt);

    if (pairwise_cipher) SCHEMA_SET_STR(client->pairwise_cipher, pairwise_cipher);

    if (!strcmp(sta, "FAIL"))
        return -1;
    if (!strstr(sta, "flags=[AUTH][ASSOC][AUTHORIZED]"))
        return -1;

    hapd_sae_keyid_fixup(hapd, sta, client);

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
    const char *arg;
    int err = 0;

    /* FIXME: check if I can use hapd->phy instead od hapd->bss above on qca */
    if (hapd->group_by_phy_name == 1) {
        arg = F("bss_config=%s:%s", hapd->phy, hapd->confpath);
    } else {
        arg = F("bss_config=%s:%s", hapd->ctrl.bss, hapd->confpath);
    }

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
hapd_ctrl_reload_rxkhs(struct hapd *hapd)
{
    LOGI("%s: reloading rxkh: %s", hapd->ctrl.bss, hapd->rxkhpath);
    return strcmp("OK", HAPD_CLI(hapd, "reload_rxkhs") ?: "");
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
                && strstr(line, "vht_oper_centr_freq_seg0_idx=") != line
                && strstr(line, "r0kh=") != line
                && strstr(line, "r1kh=") != line))
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

static char *
hapd_conf_rxkh_strip(char *buf)
{
    const size_t buf_size = strlen(buf) + 1;
    size_t len = buf_size;
    char *lines = buf;
    char tmp[buf_size];
    char *ptr = tmp;
    char *line;
    *tmp = 0;

    while ((line = strsep(&lines, "\n")))
        if (strstr(line, "r0kh=") == line ||
            strstr(line, "r1kh=") == line)
            csnprintf(&ptr, &len, "%s\n", line);
    strscpy(buf, tmp, buf_size); /* its guaranteed to fit */
    return buf;
}

static int
hapd_conf_rxkh_changed(const char *a, const char *b)
{
    const char *x = hapd_conf_rxkh_strip(strdupa(a));
    const char *y = hapd_conf_rxkh_strip(strdupa(b));
    return strcmp(x, y);
}

int
hapd_conf_apply(struct hapd *hapd)
{
    const char *oldconf = R(hapd->confpath) ?: "";
    const char *oldpsks = R(hapd->pskspath) ?: "";
    const char *oldrxkh = R(hapd->rxkhpath) ?: oldconf;
    bool running = ctrl_running(&hapd->ctrl);
    int changed_conf = hapd_conf_changed(oldconf, hapd->conf, running);
    int changed_psks = strcmp(oldpsks, hapd->psks);
    int changed_rxkh = hapd_conf_rxkh_changed(oldrxkh, hapd->rxkh);
    int add = !ctrl_running(&hapd->ctrl) && hapd_configured(hapd);
    int reload = ctrl_running(&hapd->ctrl) && hapd_configured(hapd);
    int reload_all = reload && (changed_conf ||
                               (!hapd->use_reload_rxkhs &&
                                changed_rxkh));
    int reload_psk = reload && changed_psks && !reload_all;
    int reload_rxkh = reload && changed_rxkh && !reload_all &&
                      hapd->use_reload_rxkhs;
    int write_rxkhfile = changed_rxkh && hapd->use_rxkh_file;
    int del = ctrl_running(&hapd->ctrl) && !hapd_configured(hapd);
    int err = 0;

    if (write_rxkhfile) WARN_ON(W(hapd->rxkhpath, hapd->rxkh) < 0);
    if (changed_conf) WARN_ON(W(hapd->confpath, hapd->conf) < 0);
    if (changed_psks) WARN_ON(W(hapd->pskspath, hapd->psks) < 0);
    if (add) err |= WARN_ON(W(hapd->pskspath, hapd->psks) < 0);
    if (add) err |= WARN_ON(hapd_ctrl_add(hapd));
    if (reload_rxkh) err |= WARN_ON(hapd_ctrl_reload_rxkhs(hapd));
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

void
hapd_release(struct hapd *hapd)
{
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
