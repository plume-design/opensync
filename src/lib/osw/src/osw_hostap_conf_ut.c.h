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

#include <osw_ut.h>
#include "os.h"
#include "util.h"

OSW_UT(osw_hostap_supp_rates)
{
    const char *brates = "02 04 0b 16";
    const char *grates = "0c 12 18 24 30 48 60 6c";
    const char *bgrates = "02 04 0b 16 0c 12 18 24 30 48 60 6c";
    /* Generated outputs have trailing space, it is intended now */
    const char *bsupp = "10 20 55 110 ";
    const char *gsupp = "60 90 120 180 240 360 480 540 ";
    const char *bgsupp = "10 20 55 110 60 90 120 180 240 360 480 540 ";
    const char *empty = "";
    const char *garbage = "sdkfl4s sd1l;fk";
    const uint16_t bmask = osw_rate_legacy_cck();
    const uint16_t gmask = osw_rate_legacy_ofdm();
    const uint16_t bgmask = bmask | gmask;
    const uint16_t toomuch = 0xffff;

    uint16_t rates;

    rates = 0;
    hapd_util_supp_rates_to_osw(brates, &rates);
    assert(rates == bmask);

    rates = 0;
    hapd_util_supp_rates_to_osw(grates, &rates);
    assert(rates == gmask);

    rates = 0;
    hapd_util_supp_rates_to_osw(bgrates, &rates);
    assert(rates == bgmask);

    rates = 0;
    hapd_util_supp_rates_to_osw(empty, &rates);
    assert(rates == 0);

    rates = 0;
    hapd_util_supp_rates_to_osw(garbage, &rates);
    assert(rates == 0);

    char buf[128];

    MEMZERO(buf);
    osw_hostap_conf_set_rates_to_hapd(bmask, buf, sizeof(buf));
    assert(strcmp(buf, bsupp) == 0);

    MEMZERO(buf);
    osw_hostap_conf_set_rates_to_hapd(gmask, buf, sizeof(buf));
    assert(strcmp(buf, gsupp) == 0);

    MEMZERO(buf);
    osw_hostap_conf_set_rates_to_hapd(bgmask, buf, sizeof(buf));
    assert(strcmp(buf, bgsupp) == 0);

    MEMZERO(buf);
    osw_hostap_conf_set_rates_to_hapd(toomuch, buf, sizeof(buf));
    assert(strcmp(buf, bgsupp) == 0);
}

OSW_UT(osw_hostap_basic_rates)
{
    const char *brates = "10 20";
    const char *grates = "60 120";
    const char *empty = "";
    const char *garbage = "sdkfl4s sd1l;fk";
    const uint16_t bmask = osw_rate_legacy_bit(OSW_RATE_CCK_1_MBPS)
                         | osw_rate_legacy_bit(OSW_RATE_CCK_2_MBPS);
    const uint16_t gmask = osw_rate_legacy_bit(OSW_RATE_OFDM_6_MBPS)
                         | osw_rate_legacy_bit(OSW_RATE_OFDM_12_MBPS);

    uint16_t mask;

    mask = 0;
    hapd_util_basic_rates_to_osw(brates, &mask);
    assert(mask == bmask);

    mask = 0;
    hapd_util_basic_rates_to_osw(grates, &mask);
    assert(mask == gmask);

    mask = 0;
    hapd_util_basic_rates_to_osw(empty, &mask);
    assert(mask == 0);

    mask = 0;
    hapd_util_basic_rates_to_osw(garbage, &mask);
    assert(mask == 0);
}

OSW_UT(osw_hostap_beacon_rate)
{
    const char *ht4 = "ht:4";
    const char *vht5 = "vht:5";
    const char *he1 = "he:1";
    const char *leg5 = "55"; /* cck 5mbps */
    const char *empty = "";
    const char *garbage = "fsdkjfk23j4xcv";

    const struct osw_beacon_rate rleg5 = { .type = OSW_BEACON_RATE_ABG, .u = { .legacy = OSW_RATE_CCK_5_5_MBPS } };
    const struct osw_beacon_rate rht4 = { .type = OSW_BEACON_RATE_HT, .u = { .ht_mcs = 4 } };
    const struct osw_beacon_rate rvht5 = { .type = OSW_BEACON_RATE_VHT, .u = { .vht_mcs = 5 } };
    const struct osw_beacon_rate rhe1 = { .type = OSW_BEACON_RATE_HE, .u = { .he_mcs = 1 } };
    struct osw_beacon_rate rzero;
    MEMZERO(rzero);

    char buf[128];

    MEMZERO(buf);
    osw_hostap_conf_set_brate_to_hapd(&rleg5, buf, sizeof(buf));
    assert(strcmp(buf, leg5) == 0);

    MEMZERO(buf);
    osw_hostap_conf_set_brate_to_hapd(&rht4, buf, sizeof(buf));
    assert(strcmp(buf, ht4) == 0);

    MEMZERO(buf);
    osw_hostap_conf_set_brate_to_hapd(&rvht5, buf, sizeof(buf));
    assert(strcmp(buf, vht5) == 0);

    MEMZERO(buf);
    osw_hostap_conf_set_brate_to_hapd(&rhe1, buf, sizeof(buf));
    assert(strcmp(buf, he1) == 0);

    MEMZERO(buf);
    osw_hostap_conf_set_brate_to_hapd(&rzero, buf, sizeof(buf));
    assert(strcmp(buf, empty) == 0);

    struct osw_beacon_rate rate;
    const size_t size = sizeof(rate);

    MEMZERO(rate);
    hapd_util_beacon_rate_to_osw(leg5, &rate);
    assert(memcmp(&rate, &rleg5, size) == 0);

    MEMZERO(rate);
    hapd_util_beacon_rate_to_osw(ht4, &rate);
    assert(memcmp(&rate, &rht4, size) == 0);

    MEMZERO(rate);
    hapd_util_beacon_rate_to_osw(vht5, &rate);
    assert(memcmp(&rate, &rvht5, size) == 0);

    MEMZERO(rate);
    hapd_util_beacon_rate_to_osw(he1, &rate);
    assert(memcmp(&rate, &rhe1, size) == 0);

    MEMZERO(rate);
    hapd_util_beacon_rate_to_osw(empty, &rate);
    assert(memcmp(&rate, &rzero, size) == 0);

    MEMZERO(rate);
    hapd_util_beacon_rate_to_osw(garbage, &rate);
    assert(memcmp(&rate, &rzero, size) == 0);
}

static const char *g_config = ""
    "interface=iface_X.Y\n"
    "bridge=brlan\n"
    "driver=nl80211\n"
    "logger_syslog=-1\n"
    "logger_syslog_level=3\n"
    "ctrl_interface=/var/run/hostapd\n"
    "ssid=Dummy_ssid_1.2.3\n"
    "hw_mode=a\n"
    "channel=44\n"
    "op_class=128\n"
    "beacon_int=101\n"
    "auth_algs=25\n"
    "ignore_broadcast_ssid=0\n"
    "wmm_enabled=1\n"
    "uapsd_advertisement_enabled=1\n"
    "ap_isolate=1\n"
    "send_probe_response=0\n"
    "ieee80211n=1\n"
    "ht_capab=[HT40+]\n"
    "require_ht=0\n"
    "ieee80211ac=1\n"
    "require_vht=0\n"
    "vht_oper_chwidth=1\n"
    "vht_oper_centr_freq_seg0_idx=42\n"
    "ieee80211ax=1\n"
    "he_oper_centr_freq_seg0_idx=42\n"
    "eap_server=1\n"
    "wpa=2\n"
    "wpa_psk_file=/var/run/hostapd-iface_X.Y.pskfile\n"
    "wpa_key_mgmt=WPA-PSK SAE\n"
    "wpa_pairwise=CCMP\n"
    "wpa_group_rekey=86400\n"
    "ieee80211w=1\n"
    "sae_password=SomeRandomPassword\n"
    "sae_require_mfp=1\n"
    "wps_state=2\n"
    "config_methods=virtual_push_button\n"
    "device_type=6-0050F204-1\n"
    "pbc_in_m1=1\n"
    "bss_transition=1\n"
    "rrm_neighbor_report=1\n";

static const char *g_status = \
    "state=ENABLED\n"
    "phy=iface_X.Y\n"
    "freq=5220\n"
    "num_sta_non_erp=0\n"
    "num_sta_no_short_slot_time=1\n"
    "num_sta_no_short_preamble=1\n"
    "olbc=0\n"
    "num_sta_ht_no_gf=1\n"
    "num_sta_no_ht=0\n"
    "num_sta_ht_20_mhz=0\n"
    "num_sta_ht40_intolerant=0\n"
    "olbc_ht=0\n"
    "ht_op_mode=0x4\n"
    "cac_time_seconds=0\n"
    "cac_time_left_seconds=N/A\n"
    "channel=44\n"
    "edmg_enable=0\n"
    "edmg_channel=0\n"
    "secondary_channel=1\n"
    "ieee80211n=1\n"
    "ieee80211ac=1\n"
    "ieee80211ax=1\n"
    "beacon_int=101\n"
    "dtim_period=2\n"
    "he_oper_chwidth=1\n"
    "he_oper_centr_freq_seg0_idx=42\n"
    "he_oper_centr_freq_seg1_idx=0\n"
    "vht_oper_chwidth=1\n"
    "vht_oper_centr_freq_seg0_idx=42\n"
    "vht_oper_centr_freq_seg1_idx=0\n"
    "vht_caps_info=00000000\n"
    "rx_vht_mcs_map=ffaa\n"
    "tx_vht_mcs_map=ffaa\n"
    "ht_caps_info=000e\n"
    "ht_mcs_bitmask=ffffffff000000000000\n"
    "supported_rates=0c 12 18 24 30 48 60 6c\n"
    "max_txpower=20\n"
    "bss[0]=iface_X.Y\n"
    "bssid[0]=ab:cd:ef:01:23:45\n"
    "ssid[0]=Dummy_ssid_1.2.3\n"
    "num_sta[0]=1\n";

static const char *g_get_config = \
    "bssid=ab:cd:ef:12:34:56\n"
    "ssid=Dummy_ssid_1.2.3\n"
    "wps_state=configured\n"
    "psk=798cc2d5d558d33f5517ba0ad92f798cc2d5d558d33f5517ba0ad9cc2d5d558d\n"
    "wpa=2\n"
    "key_mgmt=WPA-PSK SAE\n"
    "group_cipher=CCMP\n"
    "rsn_pairwise_cipher=CCMP\n";

static const char *g_show_neighbor = \
    "ab:cd:ef:54:32:10 ssid=44756D6D795F737369645F312E322E330A nr=abcdef5432108f000000802c0e\n"
    "fe:dc:ba:12:34:56 ssid=44756D6D795F737369645F312E322E330A nr=fedcba1234568f000000802c0e\n";

/* The structure below is unnecessarily exporting g_drv_conf symbol
 * to global scope. Moving it into template_config_copy results in
 * compiler errors, as nested structures are not constant. To resolve
 * this issue all the nested struct has to be initialized as statics
 * before linking to osw_drv_conf. This, however, complicates the
 * code and is not worth the struggle. */
static struct osw_drv_conf g_drv_conf = {
    .n_phy_list = 1,
    .phy_list = (struct osw_drv_phy_config[]) {
        {
            .phy_name = "phy0",
            .enabled = true,
            .tx_chainmask = 0x15,
            .radar = OSW_RADAR_DETECT_ENABLED,
            .reg_domain = {
                .ccode = "US\0",
                .revision = 644,
                .dfs = OSW_REG_DFS_ETSI
            },
            .vif_list = {
                .count = 1,
                .list = (struct osw_drv_vif_config[]) {
                    {
                        .vif_name = "vif0.10_ap",
                        .vif_type = OSW_VIF_AP,
                        .enabled = true,
                        .u.ap = {
                            .bridge_if_name.buf = "br-home",
                            .beacon_interval_tu = 101,
                            .channel = {
                                .width = OSW_CHANNEL_80MHZ,
                                .control_freq_mhz = 5220,
                                .center_freq0_mhz = 5210,
                            },
                            .isolated = true,
                            .ssid_hidden = false,
                            .mcast2ucast = true,
                            .mode = {
                                .wnm_bss_trans = true,
                                .rrm_neighbor_report = true,
                                .wmm_enabled = true,
                                .wmm_uapsd_enabled = true,
                                .ht_enabled = true,
                                .ht_required = true,
                                .vht_enabled = true,
                                .vht_required = true,
                                .he_enabled = true,
                                .he_required = true,
                                .wps = true,
                            },
                            .acl_policy = OSW_ACL_ALLOW_LIST,
                            .ssid = {
                                .buf = "Dummy_ssid_wif0.0",
                                .len = 17,
                            },
                            .acl = {
                                .count = 3,
                                .list = (struct osw_hwaddr[]) {
                                    {.octet = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 }},
                                    {.octet = { 0x02, 0x02, 0x03, 0x04, 0x05, 0x07 }},
                                    {.octet = { 0x03, 0x02, 0x03, 0x04, 0x05, 0x08 }},
                                }
                            },
                            .wpa = {
                                .wpa = true,
                                .rsn = true,
                                .akm_psk = true,
                                .akm_sae = true,
                                .akm_ft_psk = true,
                                .akm_ft_sae = true,
                                .pairwise_tkip = true,
                                .pairwise_ccmp = true,
                                .pmf = OSW_PMF_REQUIRED,
                                .group_rekey_seconds = 3600,
                                .ft_mobility_domain = 0xddf7,
                            },
                            .psk_list = {
                                .count = 3,
                                .list = (struct osw_ap_psk[]) {
                                    { .key_id = 1, .psk.str = "password1" },
                                    { .key_id = 2, .psk.str = "password2" },
                                    { .key_id = 3, .psk.str = "password3" },
                                },
                            },
                            .radius_list = {
                                .count = 2,
                                .list = (struct osw_radius[]) {
                                    { .server = "192.168.7.1", .passphrase = "no_idea", .port = 1812 },
                                    { .server = "192.168.7.2", .passphrase = "no_idea", .port = 1812 },
                                },
                            },
                            .wps_cred_list = {
                                .count = 1,
                                .list = (struct osw_wps_cred[]) {
                                    { .psk.str = "password2" },
                                },
                            },
                        },
                    },
                },
            },
        },
    },
};

static struct osw_drv_conf *
template_config_copy(void)
{
    size_t i, j, k;
    struct osw_drv_conf *drv_conf;
    struct osw_drv_phy_config *g_phy, *l_phy;
    struct osw_drv_vif_config *g_vif, *l_vif;
    struct osw_drv_vif_config_ap *g_ap, *l_ap;
    struct osw_hwaddr *g_acl, *l_acl;
    struct osw_ap_psk *g_psk, *l_psk;
    struct osw_radius *g_radius, *l_radius;

    drv_conf = calloc(1, sizeof(struct osw_drv_conf));
    assert(drv_conf != NULL);
    drv_conf->n_phy_list = g_drv_conf.n_phy_list;

    drv_conf->phy_list = calloc(drv_conf->n_phy_list, sizeof(struct osw_drv_phy_config));
    assert(drv_conf->phy_list);

    for (g_phy = g_drv_conf.phy_list, l_phy = drv_conf->phy_list, i = 0;
         i < drv_conf->n_phy_list;
         g_phy++, l_phy++, i++) {
        memcpy(l_phy, g_phy, sizeof(struct osw_drv_phy_config));
        l_phy->phy_name = strdup(g_phy->phy_name);
        l_phy->vif_list.list = calloc(g_phy->vif_list.count, sizeof(struct osw_drv_vif_config));
        assert(l_phy->vif_list.list != NULL);
        for (g_vif = g_phy->vif_list.list, l_vif = l_phy->vif_list.list, j=0;
                j < g_phy->vif_list.count;
                g_vif++, l_phy++, j++) {
            memcpy(l_vif, g_vif, sizeof(struct osw_drv_vif_config));
            l_vif->vif_name = strdup(g_vif->vif_name);

            g_ap = &g_vif->u.ap;
            l_ap = &l_vif->u.ap;

            /* Copy list of MAC addresses in ACL list */
            l_ap->acl.list = calloc(l_ap->acl.count, sizeof(struct osw_hwaddr));
            assert(l_ap->acl.list);
            for (g_acl = g_ap->acl.list, l_acl = l_ap->acl.list, k = 0;
                    k < g_ap->acl.count;
                    g_acl++, l_acl++, k++) {
                memcpy(l_acl, g_acl, sizeof(struct osw_hwaddr));
            }

            /* Copy list of PSKs */
            l_ap->psk_list.list = calloc(l_ap->psk_list.count, sizeof(struct osw_ap_psk));
            assert(l_ap->psk_list.list);
            for (g_psk = g_ap->psk_list.list, l_psk = l_ap->psk_list.list, k = 0;
                 k < g_ap->psk_list.count;
                 g_psk++, l_psk++, k++) {
                memcpy(l_psk, g_psk, sizeof(struct osw_ap_psk));
            }

            /* Copy Radius list */
            l_ap->radius_list.list = calloc(l_ap->radius_list.count, sizeof(struct osw_radius));
            assert(l_ap->radius_list.list);
            for (g_radius = g_ap->radius_list.list, l_radius = l_ap->radius_list.list, k = 0;
                    k < g_ap->radius_list.count;
                    g_radius++, l_radius++, k++) {
                l_radius->server = strdup(g_radius->server);
                l_radius->passphrase = strdup(g_radius->passphrase);
                l_radius->port = g_radius->port;
            }
        }
    }
    return drv_conf;
}

static void
template_config_free(struct osw_drv_conf *drv_conf)
{
    size_t n_phy, n_vif;
    struct osw_drv_phy_config *phy = NULL;
    struct osw_drv_vif_config *vif = NULL;
    struct osw_drv_vif_config_ap *ap;

    phy = drv_conf->phy_list;
    n_phy = drv_conf->n_phy_list;

    while (n_phy-- > 0)
    {
        vif = phy->vif_list.list;
        n_vif = phy->vif_list.count;
        while (n_vif-- > 0) {
            FREE(vif->vif_name);

            ap = &vif->u.ap;
            FREE(ap->acl.list);
            FREE(ap->psk_list.list);
            FREE(ap->radius_list.list);
        }
        FREE(phy->phy_name);
    }
    FREE(vif);
    FREE(phy);
    FREE(drv_conf);
}

OSW_UT(osw_hostap_conf_generate_ap_config_ut)
{
    struct osw_drv_conf *drv_conf = template_config_copy();
    struct osw_hostap_conf_ap_config ap_conf = {0};
    char *conf = ap_conf.conf_buf;

    osw_hostap_conf_fill_ap_config(drv_conf,
                                   "phy0",
                                   "vif0.10_ap",
                                   &ap_conf);
    osw_hostap_conf_generate_ap_config_bufs(&ap_conf);

    OSW_UT_EVAL(strstr(conf, "interface=vif0.10_ap\n"));
    OSW_UT_EVAL(strstr(conf, "bridge=br-home\n"));
    OSW_UT_EVAL(strstr(conf, "driver=nl80211\n"));
    OSW_UT_EVAL(strstr(conf, "logger_syslog=-1\n"));
    OSW_UT_EVAL(strstr(conf, "logger_syslog_level=3\n"));
    OSW_UT_EVAL(strstr(conf, "ssid=Dummy_ssid_wif0.0\n"));
    OSW_UT_EVAL(strstr(conf, "country_code=US\n"));
    OSW_UT_EVAL(strstr(conf, "ieee80211d=1\n"));
    OSW_UT_EVAL(strstr(conf, "ieee80211h=1\n"));
    OSW_UT_EVAL(strstr(conf, "hw_mode=a\n"));
    OSW_UT_EVAL(strstr(conf, "channel=44\n"));
    OSW_UT_EVAL(strstr(conf, "op_class=128\n"));
    OSW_UT_EVAL(strstr(conf, "beacon_int=101\n"));
    OSW_UT_EVAL(strstr(conf, "auth_algs=25\n"));
    OSW_UT_EVAL(strstr(conf, "ignore_broadcast_ssid=0\n"));
    OSW_UT_EVAL(strstr(conf, "wmm_enabled=1\n"));
    OSW_UT_EVAL(strstr(conf, "uapsd_advertisement_enabled=1\n"));
    OSW_UT_EVAL(strstr(conf, "ap_isolate=1\n"));
    OSW_UT_EVAL(strstr(conf, "ieee80211n=1\n"));
    OSW_UT_EVAL(strstr(conf, "ht_capab=[HT40+]\n"));
    OSW_UT_EVAL(strstr(conf, "require_ht=1\n"));
    OSW_UT_EVAL(strstr(conf, "ieee80211ac=1\n"));
    OSW_UT_EVAL(strstr(conf, "require_vht=1\n"));
    OSW_UT_EVAL(strstr(conf, "vht_oper_chwidth=1\n"));
    OSW_UT_EVAL(strstr(conf, "vht_oper_centr_freq_seg0_idx=42\n"));
    OSW_UT_EVAL(strstr(conf, "ieee80211ax=1\n"));
    OSW_UT_EVAL(strstr(conf, "he_oper_centr_freq_seg0_idx=42\n"));
    OSW_UT_EVAL(strstr(conf, "eap_server=1\n"));
    OSW_UT_EVAL(strstr(conf, "wpa=3\n"));
    OSW_UT_EVAL(strstr(conf, "wpa_psk_file=SOMETHING\n"));
    OSW_UT_EVAL(strstr(conf, "wpa_key_mgmt=WPA-PSK SAE FT-PSK FT-SAE \n"));
    OSW_UT_EVAL(strstr(conf, "wpa_pairwise=TKIP CCMP \n"));
    OSW_UT_EVAL(strstr(conf, "wpa_group_rekey=3600\n"));
    OSW_UT_EVAL(strstr(conf, "ieee80211w=2\n"));
    OSW_UT_EVAL(strstr(conf, "sae_password=password1\n"));
    OSW_UT_EVAL(strstr(conf, "sae_require_mfp=1\n"));
    OSW_UT_EVAL(strstr(conf, "wps_state=2\n"));
    OSW_UT_EVAL(strstr(conf, "config_methods=virtual_push_button\n"));
    OSW_UT_EVAL(strstr(conf, "device_type=6-0050F204-1\n"));
    OSW_UT_EVAL(strstr(conf, "pbc_in_m1=1\n"));
    OSW_UT_EVAL(strstr(conf, "bss_transition=1\n"));
    OSW_UT_EVAL(strstr(conf, "rrm_neighbor_report=1\n"));
}

OSW_UT(osw_hostap_conf_generate_ap_config_ssid_ut)
{
    struct osw_drv_conf *drv_conf = template_config_copy();
    struct osw_hostap_conf_ap_config ap_conf = {0};
    char *conf = ap_conf.conf_buf;
    struct osw_ssid *ssid = &drv_conf->phy_list[0].vif_list.list[0].u.ap.ssid;

    osw_hostap_conf_fill_ap_config(drv_conf,
                                   "phy0",
                                   "vif0.10_ap",
                                   &ap_conf);
    osw_hostap_conf_generate_ap_config_bufs(&ap_conf);
    OSW_UT_EVAL(strstr(conf, "ssid=Dummy_ssid_wif0.0"));


    STRSCPY(ssid->buf, "!@#$%^&*()-={}?><");
    osw_hostap_conf_fill_ap_config(drv_conf,
                                   "phy0",
                                   "vif0.10_ap",
                                   &ap_conf);
    osw_hostap_conf_generate_ap_config_bufs(&ap_conf);
    OSW_UT_EVAL(strstr(conf, "ssid=!@#$%^&*()-={}?><"));


    STRSCPY(ssid->buf, "12345678901234567890\0");
    ssid->len = 32;
    osw_hostap_conf_fill_ap_config(drv_conf,
                                   "phy0",
                                   "vif0.10_ap",
                                   &ap_conf);
    osw_hostap_conf_generate_ap_config_bufs(&ap_conf);
    OSW_UT_EVAL(strstr(conf, "ssid=12345678901234567890\n"));


    STRSCPY(ssid->buf, "1234567890123456789012");
    ssid->len = 32;
    osw_hostap_conf_fill_ap_config(drv_conf,
                                   "phy0",
                                   "vif0.10_ap",
                                   &ap_conf);
    osw_hostap_conf_generate_ap_config_bufs(&ap_conf);
    OSW_UT_EVAL(strstr(conf, "ssid=1234567890123456789012\n"));

    template_config_free(drv_conf);
}

OSW_UT(osw_hostap_conf_generate_ap_config_psk_file_ut)
{
    struct osw_drv_conf *drv_conf = template_config_copy();
    struct osw_hostap_conf_ap_config ap_conf = {0};
    char *psks_buf = ap_conf.psks_buf;
    struct osw_ap_psk *psk_list = drv_conf->phy_list[0].vif_list.list[0].u.ap.psk_list.list;

    osw_hostap_conf_fill_ap_config(drv_conf,
                                   "phy0",
                                   "vif0.10_ap",
                                   &ap_conf);
    osw_hostap_conf_generate_ap_config_bufs(&ap_conf);
    OSW_UT_EVAL(strstr(psks_buf, "keyid=key-1 00:00:00:00:00:00 password1\n"));
    OSW_UT_EVAL(strstr(psks_buf, "wps=1 keyid=key-2 00:00:00:00:00:00 password2\n"));
    OSW_UT_EVAL(strstr(psks_buf, "keyid=key-3 00:00:00:00:00:00 password3\n\0"));



    psk_list[0].key_id = -100;
    STRSCPY(psk_list[0].psk.str, "!@#$%^&*()-={}?><//\\");

    psk_list[1].key_id = -2147483647;
    STRSCPY(psk_list[1].psk.str, "123456789012345678901234567890123456789023456789012345678901234");

    psk_list[2].key_id =  2147483647;
    STRSCPY(psk_list[2].psk.str, "-");

    osw_hostap_conf_fill_ap_config(drv_conf,
                                   "phy0",
                                   "vif0.10_ap",
                                   &ap_conf);
    osw_hostap_conf_generate_ap_config_bufs(&ap_conf);
    OSW_UT_EVAL(strstr(psks_buf, "keyid=key--100 00:00:00:00:00:00 !@#$%^&*()-={}?><//\\\n"));
    OSW_UT_EVAL(strstr(psks_buf, "keyid=key--2147483647 00:00:00:00:00:00 "
                "123456789012345678901234567890123456789023456789012345678901234\n"));
    OSW_UT_EVAL(strstr(psks_buf, "keyid=key-2147483647 00:00:00:00:00:00 -\n"));

    template_config_free(drv_conf);
}

#ifdef HOSTAPD_HANDLES_ACL
OSW_UT(osw_hostap_conf_generate_ap_config_acl_ut)
{
    /* FIXME - implementation incomplete */
}
#endif

OSW_UT(osw_hostap_conf_generate_ap_config_radius_ut)
{
    /* FIXME - implementation incomplete */
}

OSW_UT(osw_hostap_conf_parse_ap_state_ut)
{
    struct osw_drv_vif_state vstate;
    MEMZERO(vstate);

    const struct osw_hostap_conf_ap_state_bufs g_ap_state_bufs = {
        .config = g_config,
        .get_config = g_get_config,
        .status = g_status,
        .show_neighbor = g_show_neighbor,
    };

    osw_hostap_conf_fill_ap_state(&g_ap_state_bufs,
                                  &vstate);

    //OSW_UT_EVAL(vstate.enabled == true);
    //OSW_UT_EVAL(vstate.vif_type == OSW_VIF_AP);
    struct osw_hwaddr hwaddr = {.octet = {0xab, 0xcd, 0xef, 0x01, 0x23, 0x45}};
    OSW_UT_EVAL(osw_hwaddr_cmp(&vstate.mac_addr, &hwaddr) == 0);


    struct osw_drv_vif_state_ap *ap = &vstate.u.ap;;
    OSW_UT_EVAL(ap->isolated == true);
    OSW_UT_EVAL(ap->ssid_hidden == false);
    OSW_UT_EVAL(ap->mcast2ucast == false);

    OSW_UT_EVAL(ap->beacon_interval_tu == 101);
    OSW_UT_EVAL(strcmp(ap->bridge_if_name.buf, "brlan") == 0);
    struct osw_ap_mode *mode = &ap->mode;
    OSW_UT_EVAL(mode->wnm_bss_trans == true);
    OSW_UT_EVAL(mode->rrm_neighbor_report == true);
    OSW_UT_EVAL(mode->wmm_enabled == true);
    OSW_UT_EVAL(mode->wmm_uapsd_enabled == true);
    OSW_UT_EVAL(mode->ht_enabled == true);
    //OSW_UT_EVAL(mode->ht_required == true);
    OSW_UT_EVAL(mode->vht_enabled == true);
    //OSW_UT_EVAL(mode->vht_required == true);
    OSW_UT_EVAL(mode->he_enabled == true);
    //OSW_UT_EVAL(mode->he_required == true);
    OSW_UT_EVAL(mode->wps == true);
    /* FIXME - code for reporting channel needs rework */
    //struct osw_channel *channel = &ap->channel;
    //OSW_UT_EVAL(channel->width == OSW_CHANNEL_80MHZ);
    //OSW_UT_EVAL(channel->control_freq_mhz == 5220);
    //OSW_UT_EVAL(channel->center_freq0_mhz
    //OSW_UT_EVAL(channel->center_freq1_mhz
    OSW_UT_EVAL(strcmp(ap->ssid.buf, "Dummy_ssid_1.2.3") == 0);
    OSW_UT_EVAL(ap->wpa.wpa == false);
    OSW_UT_EVAL(ap->wpa.rsn == true);
    OSW_UT_EVAL(ap->wpa.akm_psk == true);
    OSW_UT_EVAL(ap->wpa.akm_sae == true);
    OSW_UT_EVAL(ap->wpa.akm_ft_psk == false);
    OSW_UT_EVAL(ap->wpa.akm_ft_sae == false);
    OSW_UT_EVAL(ap->wpa.pairwise_tkip == false);
    OSW_UT_EVAL(ap->wpa.pairwise_ccmp == true);
    OSW_UT_EVAL(ap->wpa.pmf == OSW_PMF_OPTIONAL);
    OSW_UT_EVAL(ap->wpa.group_rekey_seconds == 86400);
    OSW_UT_EVAL(ap->wpa.ft_mobility_domain == 0x00);
}

OSW_UT(osw_hostap_conf_parse_ap_state_ssid_ut)
{
    struct osw_hostap_conf_ap_state_bufs ap_bufs;
    struct osw_drv_vif_state vstate;

    MEMZERO(ap_bufs);
    MEMZERO(vstate);

    char *status = "ssid=12415\n";
    ap_bufs.status = status;
    osw_hostap_conf_fill_ap_state(&ap_bufs,
                                  &vstate);
    OSW_UT_EVAL(strcmp(vstate.u.ap.ssid.buf, "12415") == 0);


    MEMZERO(vstate);
    status = "ssid[0]=Dummy_ssid";
    ap_bufs.status = status;
    osw_hostap_conf_fill_ap_state(&ap_bufs,
                                  &vstate);
    OSW_UT_EVAL(strcmp(vstate.u.ap.ssid.buf, "Dummy_ssid") == 0);

    MEMZERO(vstate);
    status = "ssid[0]=!@#$%^&*()_+\\''//";
    ap_bufs.status = status;
    osw_hostap_conf_fill_ap_state(&ap_bufs,
                                  &vstate);
    OSW_UT_EVAL(strcmp(vstate.u.ap.ssid.buf, "!@#$%^&*()_+\\''//") == 0);
}


OSW_UT(osw_hostap_conf_parse_ap_state_psk_ut)
{
    struct osw_hostap_conf_ap_state_bufs ap_bufs;
    struct osw_drv_vif_state vstate;
    const char *config;
    const char *wpa_psk_file;

    MEMZERO(ap_bufs);
    MEMZERO(vstate);

    wpa_psk_file = ""
        "wps=1 keyid=key-1 00:00:00:00:00:00 password1\n"
        "keyid=key-2 00:00:00:00:00:00 password2\n"
        "wps=1 keyid=key--100 00:00:00:00:00:00 password3\n";
    ap_bufs.wpa_psk_file = wpa_psk_file;
    osw_hostap_conf_fill_ap_state(&ap_bufs,
                                  &vstate);
    OSW_UT_EVAL(vstate.u.ap.psk_list.count == 3);
    OSW_UT_EVAL(vstate.u.ap.psk_list.list[0].key_id == 1);
    OSW_UT_EVAL(strcmp(vstate.u.ap.psk_list.list[0].psk.str, "password1") == 0);
    OSW_UT_EVAL(vstate.u.ap.psk_list.list[1].key_id == 2);
    OSW_UT_EVAL(strcmp(vstate.u.ap.psk_list.list[1].psk.str, "password2") == 0);
    OSW_UT_EVAL(vstate.u.ap.psk_list.list[2].key_id == -100);
    OSW_UT_EVAL(strcmp(vstate.u.ap.psk_list.list[2].psk.str, "password3") == 0);
    OSW_UT_EVAL(vstate.u.ap.wps_cred_list.count == 2);
    OSW_UT_EVAL(strcmp(vstate.u.ap.wps_cred_list.list[0].psk.str, "password1") == 0);
    OSW_UT_EVAL(strcmp(vstate.u.ap.wps_cred_list.list[1].psk.str, "password3") == 0);
    FREE(vstate.u.ap.psk_list.list);
    /* Intentionally leave ap_bufs.wpa_psk_file.
     * This test checks precedence if wpa_psk_file
     * AND wpa_psk AND sae_password exist */
    MEMZERO(vstate);

    config = "sae_password=!@#$%^&*()_+\\''//\n";
    ap_bufs.config = config;
    osw_hostap_conf_fill_ap_state(&ap_bufs,
                                  &vstate);
    OSW_UT_EVAL(vstate.u.ap.psk_list.count == 1);
    OSW_UT_EVAL(strcmp(vstate.u.ap.psk_list.list[0].psk.str, "!@#$%^&*()_+\\''//") == 0);
    FREE(vstate.u.ap.psk_list.list);
}

OSW_UT(osw_hostap_conf_parse_ap_state_acl_ut)
{
    /* FIXME - implementation incomplete */
}

OSW_UT(osw_hostap_conf_parse_ap_state_radius_ut)
{
    /* FIXME - implementation incomplete */
}

OSW_UT(osw_hostap_conf_parse_funky_psks)
{
    struct osw_ap_psk_list psks;
    struct osw_wps_cred_list creds;
    MEMZERO(psks);
    MEMZERO(creds);
    hapd_util_hapd_psk_file_to_osw(
        "keyid=key--1 00:00:00:00:00:00 hello\n"
        "keyid=key-2 00:00:00:00:00:00 foo bar\n"
        "keyid=key-4 00:00:00:00:00:00 0123456789012345678901234567890123456789012345678901234567890123\n" /* 64 chars */
        "keyid=key-5 unknown_attr=xx 00:00:00:00:00:00 left=right\n"
        "bogus line\n"
        "incomplete=1 00:00:00\n" /* invalid mac */
        "incomplete=2 00:00:00:00:00:00\n" /* no space + passphrase */
        "broken=1 00:00:00:00:00:00 01234567890123456789012345678901234567890123456789012345678901234\n" /* 65 chars, too long */
        "#keyid=key-5 00:00:00:00:00:00 commented\n"
        "keyid=key-3 00:00:00:00:00:00 one\"two\n",
        &psks,
        &creds);
    LOGT("count = %zu", psks.count);
    assert(psks.count == 5);
    assert(psks.list[0].key_id == -1);
    assert(psks.list[1].key_id == 2);
    assert(psks.list[2].key_id == 4);
    assert(psks.list[3].key_id == 5);
    assert(psks.list[4].key_id == 3);
    assert(strcmp(psks.list[0].psk.str, "hello") == 0);
    assert(strcmp(psks.list[1].psk.str, "foo bar") == 0);
    assert(strcmp(psks.list[2].psk.str, "0123456789012345678901234567890123456789012345678901234567890123") == 0);
    assert(strcmp(psks.list[3].psk.str, "left=right") == 0);
    assert(strcmp(psks.list[4].psk.str, "one\"two") == 0);
    FREE(psks.list);
}
