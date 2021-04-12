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

// TODO description
#define _GNU_SOURCE

/* std libc */
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/un.h>
#include <linux/limits.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>

/* 3rd party */
#include <ev.h>

/* internal */
#include <target.h>
#include <schema.h>
#include <util.h>
#include <ovsdb_table.h>
#include <opensync-ctrl.h>
#include <opensync-hapd.h>
#include <opensync-wpas.h>
#include <opensync-ctrl-dpp.h>

#define F(x) "/tmp/target_hwsim_" x
#define R(x) str_trimws(file_geta(x))
/*
 * channel 6 (2437 MHz), width: 20 MHz, center1: 2437 MHz
 * channel 36 (5180 MHz), width: 80 MHz, center1: 5210 MHz
 */
#define E(...) strexa(__VA_ARGS__)
#define SH(...) E("sh", "-c", ##__VA_ARGS__)
#define VIFCHAN(vif) SH("iw $0 info | grep -o channel.*", vif)
#define VIFCHANNUM(chan) atoi(strchr(chan ?: "", ' ') ?: "")
#define VIFCHANBW(chan) atoi((strstr(chan ?: "", "width: ") ?: "width: ") + strlen("width: "))
// TODO: use R()
#define MAC(ifname) str_trimws(file_geta(strfmta("/sys/class/net/%s/address", vif)) ?: strdupa(""))
#define VIF2PHY(vif) str_trimws(file_geta(strfmta("/sys/class/net/%s/phy80211/name", vif)) ?: strdupa(""))
#define VIFS(phy) SH("grep -l $0 /sys/class/net/*/phy80211/name " \
                                "| xargs -n1 dirname " \
                                "| xargs -n1 dirname " \
                                "| xargs -n1 basename " \
                                "| xargs", phy)

static const struct target_radio_ops *g_ops;


static const char *
hwsim_phy_get_freq_band(const char *phy)
{
    return "2.4G";
}

static const char *
hwsim_phy_get_hw_mode(const char *phy)
{
    return "11n";
}

static void
hwsim_update_sta(struct hapd *hapd, const char *mac)
{
    struct schema_Wifi_Associated_Clients client;
    int exists;

    memset(&client, 0, sizeof(client));
    schema_Wifi_Associated_Clients_mark_all_present(&client);
    client._partial_update = true;
    exists = (hapd_sta_get(hapd, mac, &client) == 0);
    LOGI("%s: %s: updating exists=%d", hapd->ctrl.bss, mac, exists);

    if (g_ops->op_client)
        g_ops->op_client(&client, hapd->ctrl.bss, exists);
}

static void
hwsim_update_sta_iter(struct hapd *hapd, const char *mac, void *data)
{
    hwsim_update_sta(hapd, mac);
}

static void
hwsim_update_sta_all(const char *vif)
{
    struct hapd *hapd = hapd_lookup(vif);
    if (!hapd) return;

    LOGI("%s: regenerating sta list", hapd->ctrl.bss);

    if (g_ops->op_flush_clients)
        g_ops->op_flush_clients(hapd->ctrl.bss);

    hapd_sta_iter(hapd, hwsim_update_sta_iter, NULL);
}

static void
hwsim_update_vif(const char *vif)
{
    struct schema_Wifi_VIF_State vstate = {0};
    struct hapd *hapd = hapd_lookup(vif);
    struct wpas *wpas = wpas_lookup(vif);
    const char *phy;
    const char *chan;
    int chan_num;

    chan = VIFCHAN(vif);
    chan_num = VIFCHANNUM(chan);

    vstate._partial_update = true;
    SCHEMA_SET_INT(vstate.enabled, hapd || wpas);
    SCHEMA_SET_STR(vstate.if_name, vif);
    SCHEMA_SET_STR(vstate.mac, MAC(vif));
    if (chan_num) SCHEMA_SET_INT(vstate.channel, chan_num);
    if (hapd) SCHEMA_SET_STR(vstate.mode, "ap");
    if (wpas) SCHEMA_SET_STR(vstate.mode, "sta");

    // TODO
    // - ap_bridge
    // - btm
    // - mac_list
    // - mac_list_type
    // - min_hw_mode
    // - ssid_broadcast
    // - rrm
    // - uapsd_enable
    // - vif_dbg_lvl
    // - vif_radio_idx
    // - vlan_id
    // - wds

    phy = VIF2PHY(vif);
    if (hapd) hapd_bss_get(hapd, &vstate);
    if (wpas) wpas_bss_get(wpas, &vstate);

    g_ops->op_vstate(&vstate, phy);
}

static void
hwsim_update_phy(const char *phy)
{
    struct schema_Wifi_Radio_State rstate = {0};
    const char *chan = NULL;
    const char *chan_bw = NULL;
    const char *vif;
    int enabled = false;
    int chan_num = 0;
    DIR *d = opendir(strfmta("/sys/class/ieee80211/%s/device/net", phy));

    if (d) {
        struct dirent *p;
        while ((p = readdir(d))) {
            vif = p->d_name;
            if (strcmp(vif, ".") == 0 || strcmp(vif, "..") == 0) continue;
            chan = VIFCHAN(vif);
            if (chan)
                break; /* TODO: dont bother checking if all channels match for now */
        }
        closedir(d);
    }

    if (chan) {
        enabled = true;
        chan_num = VIFCHANNUM(chan);
        chan_bw = strfmta("HT%d", VIFCHANBW(chan) ?: 20);
    }

    rstate._partial_update = true;
    SCHEMA_SET_INT(rstate.enabled, enabled);
    SCHEMA_SET_STR(rstate.if_name, phy);
    SCHEMA_SET_STR(rstate.freq_band, hwsim_phy_get_freq_band(phy));
    SCHEMA_SET_STR(rstate.hw_mode, hwsim_phy_get_hw_mode(phy));
    if (chan_num) SCHEMA_SET_INT(rstate.channel, chan_num);
    if (chan_bw) SCHEMA_SET_STR(rstate.ht_mode, chan_bw);

    g_ops->op_rstate(&rstate);
}

static void
hwsim_update_vif_and_phy(const char *vif)
{
    const char *phy = VIF2PHY(vif);
    WARN_ON(!phy);

    hwsim_update_vif(vif);
    if (phy) hwsim_update_phy(phy);
}

static void
hwsim_op_dpp_chirp_received(const struct target_dpp_chirp_obj *chirp)
{
    if (WARN_ON(!g_ops->op_dpp_announcement)) return;
    g_ops->op_dpp_announcement(chirp);
}

static void
hwsim_op_dpp_conf_sent(const struct target_dpp_conf_enrollee *enrollee)
{
    if (WARN_ON(!g_ops->op_dpp_conf_enrollee)) return;
    g_ops->op_dpp_conf_enrollee(enrollee);
}

static void
hwsim_op_dpp_conf_received(const struct target_dpp_conf_network *conf)
{
    if (WARN_ON(!g_ops->op_dpp_conf_network)) return;
    g_ops->op_dpp_conf_network(conf);
}

static void
hwsim_op_ctrl_opened(struct ctrl *ctrl)
{
    hwsim_update_vif_and_phy(ctrl->bss);
    hwsim_update_sta_all(ctrl->bss);
}

static void
hwsim_op_ctrl_closed(struct ctrl *ctrl)
{
    hwsim_update_vif_and_phy(ctrl->bss);
    hwsim_update_sta_all(ctrl->bss);
}

static void
hwsim_op_hapd_ap_enabled(struct hapd *hapd)
{
    hwsim_update_vif_and_phy(hapd->ctrl.bss);
}

static void
hwsim_op_hapd_ap_disabled(struct hapd *hapd)
{
    hwsim_update_vif_and_phy(hapd->ctrl.bss);
}

static void
hwsim_op_hapd_sta_connected(struct hapd *hapd, const char *mac, const char *keyid)
{
    hwsim_update_sta(hapd, mac);
}

static void
hwsim_op_hapd_sta_disconnected(struct hapd *hapd, const char *mac)
{
    hwsim_update_sta(hapd, mac);
}

static void
hwsim_op_wpas_connected(struct wpas *wpas, const char *bssid, int id, const char *id_str)
{
    hwsim_update_vif(wpas->ctrl.bss);
    hwsim_update_phy(wpas->phy);
}

static void
hwsim_op_wpas_disconnected(struct wpas *wpas, const char *bssid, int reason, int local)
{
    hwsim_update_vif(wpas->ctrl.bss);
    hwsim_update_phy(wpas->phy);
}

static struct hapd *
hwsim_register_hapd(const char *vif)
{
    const char *phy = VIF2PHY(vif);
    if (WARN_ON(!phy)) return NULL;

    struct hapd *hapd = hapd_lookup(vif) ?: hapd_new(phy, vif);
    if (WARN_ON(!hapd)) return NULL;

    STRSCPY_WARN(hapd->driver, "nl80211");
    hapd->ctrl.opened = hwsim_op_ctrl_opened;
    hapd->ctrl.closed = hwsim_op_ctrl_closed;
    hapd->dpp_chirp_received = hwsim_op_dpp_chirp_received;
    hapd->dpp_conf_sent = hwsim_op_dpp_conf_sent;
    hapd->dpp_conf_received = hwsim_op_dpp_conf_received;
    hapd->sta_connected = hwsim_op_hapd_sta_connected;
    hapd->sta_disconnected = hwsim_op_hapd_sta_disconnected;
    hapd->ap_enabled = hwsim_op_hapd_ap_enabled;
    hapd->ap_disabled = hwsim_op_hapd_ap_disabled;
    // TODO: wps
    ctrl_enable(&hapd->ctrl);

    return hapd;
}

static struct wpas *
hwsim_register_wpas(const char *vif)
{
    const char *phy = VIF2PHY(vif);
    if (WARN_ON(!phy)) return NULL;

    struct wpas *wpas = wpas_lookup(vif) ?: wpas_new(phy, vif);
    if (WARN_ON(!wpas)) return NULL;

    STRSCPY_WARN(wpas->driver, "nl80211");
    wpas->ctrl.opened = hwsim_op_ctrl_opened;
    wpas->ctrl.closed = hwsim_op_ctrl_closed;
    wpas->dpp_conf_received = hwsim_op_dpp_conf_received;
    wpas->connected = hwsim_op_wpas_connected;
    wpas->disconnected = hwsim_op_wpas_disconnected;
    ctrl_enable(&wpas->ctrl);

    return wpas;
}

bool target_radio_init(const struct target_radio_ops *ops)
{
    g_ops = ops;
    return true;
}

bool target_radio_config_init2(void)
{
    return atoi(file_geta(F("radio_config_init2")) ?: "0");
}

bool target_radio_config_need_reset(void)
{
    return atoi(file_geta(F("radio_config_need_reset")) ?: "0");
}

bool target_radio_config_set2(const struct schema_Wifi_Radio_Config *rconf,
                              const struct schema_Wifi_Radio_Config_flags *changed)
{
    const char *phy = rconf->if_name;

    LOGI("phy: %s: configuring", phy);

    /* TODO: Nothing is actually changed now. CSA
     * not supported yet. Anything else is static
     * anyway.
     */

    hwsim_update_phy(phy);
    return true;
}

bool target_vif_config_set2(const struct schema_Wifi_VIF_Config *vconf,
                            const struct schema_Wifi_Radio_Config *rconf,
                            const struct schema_Wifi_Credential_Config *cconfs,
                            const struct schema_Wifi_VIF_Config_flags *changed,
                            int num_cconfs)
{
    const char *vif = vconf->if_name;
    struct hapd *hapd = hapd_lookup(vif);
    struct wpas *wpas = wpas_lookup(vif);

    LOGI("vif: %s: configuring", vif);

    if (!strcmp(vconf->mode, "ap") && vconf->enabled && !hapd)
        hapd = hwsim_register_hapd(vif);

    if (!strcmp(vconf->mode, "sta") && vconf->enabled && !wpas)
        wpas = hwsim_register_wpas(vif);

    if (!vconf->enabled && hapd) {
        hapd_destroy(hapd);
        hapd = NULL;
    }

    if (!vconf->enabled && wpas) {
        wpas_destroy(wpas);
        wpas = NULL;
    }

    /* FIXME: This is very lazy, but since this is
     * intended for function testing WM core it
     * should be fine like that in most cases.
     */

    if (hapd) {
        hapd_conf_gen(hapd, rconf, vconf);
        hapd_conf_apply(hapd);
    }

    if (wpas) {
        wpas_conf_gen(wpas, rconf, vconf, cconfs, num_cconfs);
        wpas_conf_apply(wpas);
    }

    hwsim_update_vif(vif);
    return true;
}

bool target_dpp_supported(void)
{
    return atoi(file_geta(F("dpp_supported")) ?: "1");
}

bool target_dpp_config_set(const struct schema_DPP_Config *config)
{
    return ctrl_dpp_config(config);
}

bool target_dpp_key_get(struct target_dpp_key *key)
{
    key->type = atoi(file_geta(F("dpp_key_curve")) ?: "0");
    STRSCPY_WARN(key->hex, file_geta(F("dpp_key_hex")) ?: "");

    return atoi(file_geta(F("dpp_key")) ?: "0");
}
