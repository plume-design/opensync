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
#define HAPD_GLOB_CLI(...) E(CMD_TIMEOUT("wpa_cli", "-g", "/var/run/hostapd/global", ## __VA_ARGS__))
#define HAPD_CLI(hapd, ...) E(CMD_TIMEOUT("hostapd_cli", "-p", hapd->ctrl.sockdir, "-i", hapd->ctrl.bss, ## __VA_ARGS__))
#define EV(x) strchomp(strdupa(x), " ")

#define MODULE_ID LOG_MODULE_ID_HAPD

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

static void
hapd_ctrl_cb(struct ctrl *ctrl, int level, const char *buf, size_t len)
{
    struct hapd *hapd = container_of(ctrl, struct hapd, ctrl);
    const char *keyid = NULL;
    const char *event;
    const char *mac;
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

        while ((kv = strsep(&args, " ")))
            if ((k = strsep(&kv, "=")) &&
                    (v = strsep(&kv, "")))
                if (!strcmp(k, "keyid"))
                    keyid = v;

        LOGI("%s: %s: connected keyid=%s", hapd->ctrl.bss, mac, keyid ?: "");
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
    size_t len = sizeof(hapd->psks);
    char *buf = hapd->psks;
    int i;
    int j;
    int wps;

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

int
hapd_conf_gen(struct hapd *hapd,
              const struct schema_Wifi_Radio_Config *rconf,
              const struct schema_Wifi_VIF_Config *vconf)
{
    const char *wpa_pairwise;
    const char *wpa_key_mgmt;
    const char *radius_server_ip;
    const char *radius_server_port;
    const char *radius_server_secret;
    size_t len = sizeof(hapd->conf);
    bool suppress_wps = false;
    char *buf = hapd->conf;
    char keys[32];
    int closed;
    int wpa;

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
    csnprintf(&buf, &len, "hw_mode=%s\n", rconf->channel > 20 ? "a" : "g");
    csnprintf(&buf, &len, "channel=%d\n", rconf->channel);
    csnprintf(&buf, &len, "ignore_broadcast_ssid=%d\n", closed);
    csnprintf(&buf, &len, "wmm_enabled=1\n");
    csnprintf(&buf, &len, "%s", hapd->respect_multi_ap ? "" : "#");
    csnprintf(&buf, &len, "multi_ap=%d\n", hapd_map_str2int(vconf));
    csnprintf(&buf, &len, "send_probe_response=%d\n", hapd->skip_probe_response ? 0 : 1);

    if (strlen(hapd->country) > 0) {
        csnprintf(&buf, &len, "country_code=%s\n", hapd->country);
        csnprintf(&buf, &len, "ieee80211d=1\n");
        csnprintf(&buf, &len, "ieee80211h=1\n");
    }

    /* FIXME: ieee80211n, iee80211ac, ieee80211ax is missing, also min_hw_mode..
     * perhaps some of this needs to be scheduled for wireless api rework
     */

    /* FIXME: this should be able to implement min_hw_mode on setups where
     * hostapd's driver respects things like: supported_rates, basic_rates,
     * beacon_rate,  etc.
     */

    if (vconf->ft_psk_exists) {
        csnprintf(&buf, &len, "#ft_psk=%d\n", vconf->ft_psk); // FIXME: rely on FT-PSK only

        if (vconf->ft_psk) {
            csnprintf(&buf, &len, "nas_identifier=%s\n", hapd_util_ft_nas_id());
            csnprintf(&buf, &len, "reassociation_deadline=%d\n", hapd_util_ft_reassoc_deadline_tu());
            csnprintf(&buf, &len, "mobility_domain=%04x\n", hapd_util_ft_md(vconf));
            csnprintf(&buf, &len, "ft_over_ds=0\n");
            csnprintf(&buf, &len, "ft_psk_generate_local=1\n");
        }
    }

    if (vconf->btm_exists)
        csnprintf(&buf, &len, "bss_transition=%d\n", !!vconf->btm);

    if (vconf->rrm_exists)
        csnprintf(&buf, &len, "rrm_neighbor_report=%d\n", !!vconf->rrm);

    if (strlen(vconf->bridge) > 0)
        csnprintf(&buf, &len, "bridge=%s\n", vconf->bridge);

    if (vconf->group_rekey_exists && vconf->group_rekey >= 0)
        csnprintf(&buf, &len, "wpa_group_rekey=%d\n", vconf->group_rekey);

    wpa = hapd_util_get_mode(SCHEMA_KEY_VAL(vconf->security, "mode"));
    wpa_pairwise = hapd_util_get_pairwise(wpa);
    wpa_key_mgmt = SCHEMA_KEY_VAL(vconf->security, "encryption");

    if (!strcmp(wpa_key_mgmt, "WPA-PSK")) {
        snprintf(keys, sizeof(keys), "%s%s",
                 vconf->ft_psk_exists && vconf->ft_psk ? "FT-PSK " : "",
                 wpa_key_mgmt);

        csnprintf(&buf, &len, "auth_algs=1\n");
        csnprintf(&buf, &len, "wpa_key_mgmt=%s\n", keys);
        csnprintf(&buf, &len, "wpa_psk_file=%s\n", hapd->pskspath);
        csnprintf(&buf, &len, "wpa=%d\n", wpa);
        csnprintf(&buf, &len, "wpa_pairwise=%s\n", wpa_pairwise);
    } else if (!strcmp(wpa_key_mgmt, "WPA-EAP")) {
        csnprintf(&buf, &len, "auth_algs=1\n");
        csnprintf(&buf, &len, "ieee8021x=1\n");
        csnprintf(&buf, &len, "own_ip_addr=127.0.0.1\n");
        csnprintf(&buf, &len, "wpa_key_mgmt=WPA-EAP\n");
        /* WPA-EAP supports solely WPA2 */
        csnprintf(&buf, &len, "wpa=2\n");
        csnprintf(&buf, &len, "wpa_pairwise=CCMP\n");

        radius_server_ip = SCHEMA_KEY_VAL(vconf->security, "radius_server_ip");
        radius_server_port = SCHEMA_KEY_VAL(vconf->security, "radius_server_port");
        radius_server_secret = SCHEMA_KEY_VAL(vconf->security, "radius_server_secret");

        csnprintf(&buf, &len, "auth_server_addr=%s\n", radius_server_ip);
        csnprintf(&buf, &len, "auth_server_port=%s\n", radius_server_port);
        csnprintf(&buf, &len, "auth_server_shared_secret=%s\n", radius_server_secret);
    } else if (!strcmp(wpa_key_mgmt, "OPEN")) {
        csnprintf(&buf, &len, "wpa=0\n");
        csnprintf(&buf, &len, "auth_algs=1\n");
    } else {
        LOGW("%s: key mgmt '%s' not supported", vconf->if_name, wpa_key_mgmt);
        errno = ENOTSUP;
        return -1;
    }

    /*
     * WPA-EAP + remote RADIUS excludes WPS on single VAP (hostapd doesn't support
     * such configuration).
     */
    if (!strcmp(wpa_key_mgmt, "WPA-EAP") && vconf->wps) {
        suppress_wps |= true;
        LOGW("%s: Disabling WPS because WPA-EAP is enabled", vconf->if_name);
    }

    /*
     * OPEN mode and WPS cannot be configred on single VAP.
     */
    if (!strcmp(wpa_key_mgmt, "OPEN") && vconf->wps) {
        suppress_wps |= true;
        LOGW("%s: Disabling WPS because OPEN mode is enabled", vconf->if_name);
    }

    if (kconfig_enabled(CONFIG_HOSTAP_PSK_FILE_WPS) && !suppress_wps) {
        csnprintf(&buf, &len, "wps_state=%d\n", vconf->wps ? 2 : 0);
        csnprintf(&buf, &len, "eap_server=%d\n", vconf->wps ? 1 : 0);
        csnprintf(&buf, &len, "config_methods=virtual_push_button\n");
        csnprintf(&buf, &len, "pbc_in_m1=1\n");
    }

    WARN_ON(len == 1); /* likely buf was truncated */

    return hapd_conf_gen_psk(hapd, vconf);
}

static void
hapd_bss_get_security(struct schema_Wifi_VIF_State *vstate,
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

    /* FIXME: WPA3? */
}

static void
hapd_bss_get_psks(struct schema_Wifi_VIF_State *vstate,
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

    LOGT("%s: parsing psk entries", vstate->if_name);

    for (;;) {
        const char *oftag_key;

        if (!(oftagline = strsep(&ptr, "\n")))
            break;
        if (!(pskline = strsep(&ptr, "\n")))
            break;

        LOGT("%s: parsing pskfile: raw: oftagline='%s' pskline='%s'",
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

        LOGT("%s: parsing pskfile: stripped: key_id='%s' wps='%s' oftag='%s' psk='%s'",
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

    if (status) {
        hapd_bss_get_security(vstate, conf, status);
        hapd_bss_get_wps(hapd, vstate, status);
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
    const char *k;
    const char *v;
    char *lines = strdupa(sta);
    char *kv;

    while ((kv = strsep(&lines, "\r\n")))
        if ((k = strsep(&kv, "=")) &&
            (v = strsep(&kv, "")))
            if (!strcmp(k, "keyid"))
                keyid = v;

    SCHEMA_SET_STR(client->key_id, keyid ?: "");
    SCHEMA_SET_STR(client->mac, mac);
    SCHEMA_SET_STR(client->state, "active");

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
        if (!running || strstr(line, "channel=") != line)
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
