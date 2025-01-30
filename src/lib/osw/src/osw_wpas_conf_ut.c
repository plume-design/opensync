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

#include "osw_ut.h"
#include <osw_conf.h>
#include <osw_mux.h>
#include "util.h"

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
                        .vif_name = "vif0.10_sta",
                        .vif_type = OSW_VIF_STA,
                        .enabled = true,
                        .u.sta = {
                            .operation = OSW_DRV_VIF_CONFIG_STA_CONNECT,
                            .network_changed = false,
                            .network = (struct osw_drv_vif_sta_network[]) {
                                {
                                    .ssid = {
                                        .buf = "Dummy_ssid_vif0.0",
                                        .len = 17,
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
                                    .psk.str = "hello",
                                    .bssid = {
                                        .octet = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 },
                                    },
                                    .priority = 42,
                                    .next = NULL,
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
    size_t i, j;
    struct osw_drv_conf *drv_conf;
    struct osw_drv_phy_config *g_phy, *l_phy;
    struct osw_drv_vif_config *g_vif, *l_vif;
    struct osw_drv_vif_sta_network *g_net, *l_net;

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
            g_net = g_vif->u.sta.network;
            /* FIXME - this code hardcodes only one possible network entry in dummy struct! */
            l_vif->u.sta.network = calloc(1, sizeof(struct osw_drv_vif_sta_network));
            assert(l_vif->u.sta.network);
            l_net = l_vif->u.sta.network;
            memcpy(l_net, g_net, sizeof(struct osw_drv_vif_sta_network));
            l_net->next = NULL;
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
    struct osw_drv_vif_sta_network *net, *net_next;

    phy = drv_conf->phy_list;
    n_phy = drv_conf->n_phy_list;

    while (n_phy-- > 0)
    {
        vif = phy->vif_list.list;
        n_vif = phy->vif_list.count;
        while (n_vif-- > 0) {
            net = vif->u.sta.network;
            while (net) {
                net_next = net->next;
                FREE(net);
                net = net_next;
            }
            FREE(vif->vif_name);
        }
        FREE(phy->phy_name);
    }
    FREE(vif);
    FREE(phy);
}

OSW_UT(osw_wpas_conf_generate_sta_config_ut)
{
    struct osw_hostap_conf_sta_config sta_conf;
    MEMZERO(sta_conf);
    char *conf = sta_conf.conf_buf;

    osw_hostap_conf_fill_sta_config(&g_drv_conf,
                                    "phy0",
                                    "vif0.10_sta",
                                    &sta_conf);
    osw_hostap_conf_generate_sta_config_bufs(&sta_conf);

    OSW_UT_EVAL(strstr(conf, "\tieee80211w=2"));
    OSW_UT_EVAL(strstr(conf, "\tid_str=\"id_0\""));
    OSW_UT_EVAL(strstr(conf, "\tssid=\"Dummy_ssid_vif0.0\""));
    OSW_UT_EVAL(strstr(conf, "\tscan_ssid=1"));
    OSW_UT_EVAL(strstr(conf, "\tproto=WPA RSN"));
    OSW_UT_EVAL(strstr(conf, "\tbssid=01:02:03:04:05:06"));
    OSW_UT_EVAL(strstr(conf, "\tpairwise=TKIP CCMP"));
    OSW_UT_EVAL(strstr(conf, "\tpsk=\"hello\""));
    OSW_UT_EVAL(strstr(conf, "\tbssid=01:02:03:04:05:06"));
    OSW_UT_EVAL(strstr(conf, "\tpriority=42"));
}

OSW_UT(osw_wpas_conf_generate_sta_config_psk_ut)
{
    struct osw_drv_conf *drv_conf = template_config_copy();
    struct osw_hostap_conf_sta_config sta_conf;
    MEMZERO(sta_conf);
    char *conf = sta_conf.conf_buf;
    struct osw_psk *psk = &drv_conf->phy_list[0].vif_list.list[0].u.sta.network[0].psk;

    /* Basic test */
    osw_hostap_conf_fill_sta_config(drv_conf,
                                    "phy0",
                                    "vif0.10_sta",
                                    &sta_conf);
    osw_hostap_conf_generate_sta_config_bufs(&sta_conf);
    OSW_UT_EVAL(strstr(conf, "\tpsk=\"hello\""));

    /* Test special characters */
    STRSCPY(psk->str, "!@#$%^&*()-={}?><");
    osw_hostap_conf_fill_sta_config(drv_conf,
                                    "phy0",
                                    "vif0.10_sta",
                                    &sta_conf);
    osw_hostap_conf_generate_sta_config_bufs(&sta_conf);
    OSW_UT_EVAL(strstr(conf, "\tpsk=\"!@#$%^&*()-={}?><\""));

    /* Test long value passed in configuration file */
    STRSCPY(psk->str, "1111111111222222222233333333334444444444555555555566666666667777");
    osw_hostap_conf_fill_sta_config(drv_conf,
                                    "phy0",
                                    "vif0.10_sta",
                                    &sta_conf);
    osw_hostap_conf_generate_sta_config_bufs(&sta_conf);
    OSW_UT_EVAL(strstr(conf, "\tpsk=\"1111111111222222222233333333334444444444555555555566666666667777\""));

    template_config_free(drv_conf);
}

OSW_UT(osw_wpas_conf_generate_sta_config_ssid_ut)
{
    struct osw_drv_conf *drv_conf = template_config_copy();
    struct osw_hostap_conf_sta_config sta_conf;
    MEMZERO(sta_conf);
    char *conf = sta_conf.conf_buf;
    struct osw_ssid *ssid = &drv_conf->phy_list[0].vif_list.list[0].u.sta.network[0].ssid;

    /* Basic test */
    osw_hostap_conf_fill_sta_config(drv_conf,
                                    "phy0",
                                    "vif0.10_sta",
                                    &sta_conf);
    osw_hostap_conf_generate_sta_config_bufs(&sta_conf);
    OSW_UT_EVAL(strstr(conf, "\tssid=\"Dummy_ssid_vif0.0\""));

    /* Test special characters */
    STRSCPY(ssid->buf, "!@#$%^&*()-={}?><");
    osw_hostap_conf_fill_sta_config(drv_conf,
                                    "phy0",
                                    "vif0.10_sta",
                                    &sta_conf);
    osw_hostap_conf_generate_sta_config_bufs(&sta_conf);
    OSW_UT_EVAL(strstr(conf, "\tssid=\"!@#$%^&*()-={}?><\""));

    /* newline as part of ssid */
    STRSCPY(ssid->buf, "non_null_terminated_ssid\012\040\0");
    ssid->len = 25;
    osw_hostap_conf_fill_sta_config(drv_conf,
                                    "phy0",
                                    "vif0.10_sta",
                                    &sta_conf);
    osw_hostap_conf_generate_sta_config_bufs(&sta_conf);
    OSW_UT_EVAL(strstr(conf, "\tssid=\"non_null_terminated_ssid\n\""));

    /* string too big. len limits the ssid */
    STRSCPY(ssid->buf, "123456789012345678901234567890");
    ssid->len = 5;
    osw_hostap_conf_fill_sta_config(drv_conf,
                                    "phy0",
                                    "vif0.10_sta",
                                    &sta_conf);
    osw_hostap_conf_generate_sta_config_bufs(&sta_conf);
    OSW_UT_EVAL(strstr(conf, "\tssid=\"12345\""));

    /* LEN to big. null termination limits the ssid */
    STRSCPY(ssid->buf, "abc\0\0\0");
    ssid->len = 5;
    osw_hostap_conf_fill_sta_config(drv_conf,
                                    "phy0",
                                    "vif0.10_sta",
                                    &sta_conf);
    osw_hostap_conf_generate_sta_config_bufs(&sta_conf);
    OSW_UT_EVAL(strstr(conf, "\tssid=\"abc\""));

    template_config_free(drv_conf);
}

OSW_UT(osw_wpas_conf_generate_sta_state_link_ut)
{
    struct osw_hostap_conf_sta_state_bufs bufs = {0};
    struct osw_drv_vif_state *vstate;
    struct osw_drv_vif_state_sta_link *link;

    const char *config = "ctrl_interface=/var/run/wpa_supplicant-wifi0\n"
                         "scan_cur_freq=0\n"
                         "#bridge=\n"
                         "network={\n"
                         "\n"
                         "        id_str=\"id_0\"\n"
                         "        disabled=0\n"
                         "        scan_ssid=1\n"
                         "        bgscan=\"\"\n"
                         "        bssid=ab:cd:ef:01:23:45\n"
                         "        scan_freq=5180 5200 5220 5240 5745 5765 5785 5805\n"
                         "        freq_list=5180 5200 5220 5240 5745 5765 5785 5805\n"
                         "        ssid=\"Dummy_ssid_vif0.0\"\n"
                         "        psk=\"!@#$$%^&*(){}\"\n"
                         "        key_mgmt=WPA-PSK\n"
                         "        pairwise=CCMP\n"
                         "        sae_password=\"!@#$$%^&*(){}\"\n"
                         "        proto=RSN\n"
                         "        multi_ap_backhaul_sta=0\n"
                         "        #bssid=\n"
                         "        ieee80211w=0\n"
                         "}\n";

    const char *status = "bssid=a1:a2:a3:a4:a5:a6\n"
                         "freq=5220\n"
                         "ssid=Dummy_ssid_vif0.0\n"
                         "id_str=id_0\n"
                         "mode=station\n"
                         "wifi_generation=6\n"
                         "pairwise_cipher=CCMP\n"
                         "group_cipher=CCMP\n"
                         "key_mgmt=SAE\n"
                         "pmf=1\n"
                         "mgmt_group_cipher=BIP\n"
                         "sae_group=19\n"
                         "sae_h2e=0\n"
                         "sae_pk=0\n"
                         "wpa_state=COMPLETED\n"
                         "ip_address=192.168.40.204\n"
                         "p2p_device_address=ff:ee:dd:cc:bb:aa\n"
                         "address=aa:bb:cc:dd:ee:ff\n"
                         "uuid=416d6fd3-166c-5c07-a302-4f965a167861\n"
                         "ieee80211ac=1\n";

    vstate = CALLOC(1, sizeof(struct osw_drv_vif_state));
    bufs.config = config;
    bufs.status = status;

    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    link = &vstate->u.sta.link;

    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED);
    OSW_UT_EVAL(link->channel.control_freq_mhz == 5220);
    OSW_UT_EVAL(strcmp(link->ssid.buf, "Dummy_ssid_vif0.0") == 0);
    OSW_UT_EVAL(strcmp(link->psk.str, "!@#$$%^&*(){}") == 0);
    OSW_UT_EVAL(link->bssid.octet[0] == 0xa1);
    OSW_UT_EVAL(link->bssid.octet[1] == 0xa2);
    OSW_UT_EVAL(link->bssid.octet[2] == 0xa3);
    OSW_UT_EVAL(link->bssid.octet[3] == 0xa4);
    OSW_UT_EVAL(link->bssid.octet[4] == 0xa5);
    OSW_UT_EVAL(link->bssid.octet[5] == 0xa6);
    OSW_UT_EVAL(link->wpa.pairwise_ccmp == true);
    OSW_UT_EVAL(link->wpa.pairwise_tkip == false);
    OSW_UT_EVAL(link->wpa.pmf == OSW_PMF_OPTIONAL);
    OSW_UT_EVAL(link->wpa.akm_psk == false);
    OSW_UT_EVAL(link->wpa.akm_sae == true);
    OSW_UT_EVAL(link->wpa.akm_ft_psk == false);
    OSW_UT_EVAL(link->wpa.akm_ft_sae == false);
}

OSW_UT(osw_wpas_conf_generate_sta_state_ssid_ut)
{
    struct osw_hostap_conf_sta_state_bufs bufs = {0};
    struct osw_drv_vif_state *vstate;
    struct osw_drv_vif_state_sta_link *link;

    vstate = CALLOC(1, sizeof(struct osw_drv_vif_state));
    link = &vstate->u.sta.link;

    MEMZERO(link->ssid);
    bufs.status = "ssid=@!#$%^&*(){}\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(strcmp(link->ssid.buf,"@!#$%^&*(){}") == 0);
    OSW_UT_EVAL(link->ssid.len == 12);

    MEMZERO(link->ssid);
    bufs.status = "ssid=12345678901234567890123456789012\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(strcmp(link->ssid.buf,"12345678901234567890123456789012") == 0);
    OSW_UT_EVAL(link->ssid.len == 32);

    MEMZERO(link->ssid);
    bufs.status = "ssid=1234567890123456789012345678901212345678901234567890123456789012\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(strcmp(link->ssid.buf,"12345678901234567890123456789012") == 0);
    OSW_UT_EVAL(link->ssid.len == 32);

    MEMZERO(link->ssid);
    bufs.status = "ssid=f\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(strcmp(link->ssid.buf,"f") == 0);
    OSW_UT_EVAL(link->ssid.len == 1);

    /* Expected is to maintain previous ssid because MEMZERO isn't called */
    bufs.status = "";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(strcmp(link->ssid.buf,"f") == 0);
    OSW_UT_EVAL(link->ssid.len == 1);

    /* Expected result is for error to get printed (STRSCPY failed) */
    MEMZERO(link->ssid);
    bufs.status = "";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->ssid.len == 0);
}

OSW_UT(osw_wpas_conf_generate_sta_state_status_ut)
{
    struct osw_hostap_conf_sta_state_bufs bufs = {0};
    struct osw_drv_vif_state *vstate;
    struct osw_drv_vif_state_sta_link *link;

    vstate = CALLOC(1, sizeof(struct osw_drv_vif_state));
    link = &vstate->u.sta.link;

    bufs.status = "wpa_state=COMPLETED\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED);

    bufs.status = "wpa_state=DISCONNECTED\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED);

    bufs.status = "wpa_state=SCANNING\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTING);

    bufs.status = "wpa_state=INACTIVE\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED);

    bufs.status = "wpa_state=INTERFACE_DISABLED\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED);

    bufs.status = "wpa_state=AUTHENTICATING\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTING);

    bufs.status = "wpa_state=ASSOCIATING\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTING);

    bufs.status = "wpa_state=ASSOCIATED\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTING);

    bufs.status = "wpa_state=4WAY_HANDSHAKE\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTING);

    bufs.status = "wpa_state=GROUP_HANDSHAKE\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTING);

    bufs.status = "wpa_state=GROUP_HANDSHAKE\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTING);

    bufs.status = "wpa_state=UNKNOWN\n";
    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    OSW_UT_EVAL(link->status == OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN);
}

OSW_UT(osw_wpas_conf_generate_sta_state_list_networks_ut)
{
    struct osw_hostap_conf_sta_state_bufs bufs = {0};
    struct osw_drv_vif_state *vstate;
    struct osw_drv_vif_sta_network *network;

    const char *config = "ctrl_interface=/var/run/wpa_supplicant-wifi0\n"
                         "scan_cur_freq=0\n"
                         "#bridge=\n"
                         "network={\n"
                         "        id_str=\"id_8\"\n"
                         "        disabled=0\n"
                         "        scan_ssid=1\n"
                         "        bgscan=\"\"\n"
                         "        bssid=ab:cd:ef:01:23:45\n"
                         "        scan_freq=5180 5200 5220 5240 5745 5765 5785 5805\n"
                         "        freq_list=5180 5200 5220 5240 5745 5765 5785 5805\n"
                         "        ssid=\"Dummy_ssid_vif0.0\"\n"
                         "        psk=\"QuofJ1a#$#4NOvXg1DB8oWzZky2r#YCC\"\n"
                         "        key_mgmt=WPA-PSK\n"
                         "        pairwise=CCMP\n"
                         "        proto=RSN\n"
                         "        multi_ap_backhaul_sta=0\n"
                         "        #bssid=\n"
                         "        ieee80211w=0\n"
                         "}\n"
                         "network={\n"
                         "        id_str=\"id_7\"\n"
                         "        disabled=0\n"
                         "        scan_ssid=1\n"
                         "        bgscan=\"\"\n"
                         "        scan_freq=5180 5200 5220 5240 5745 5765 5785 5805\n"
                         "        freq_list=5180 5200 5220 5240 5745 5765 5785 5805\n"
                         "        ssid=\"Dummy_ssid_vif0.1\"\n"
                         "        psk=\"a5SN9#aQ52Oeu#0SW2DugQS4@6nxfb81\"\n"
                         "        key_mgmt=WPA-PSK\n"
                         "        pairwise=CCMP\n"
                         "        proto=RSN\n"
                         "        multi_ap_backhaul_sta=0\n"
                         "        #bssid=\n"
                         "        ieee80211w=1\n"
                         "}\n"
                         "network={\n"
                         "        id_str=\"id_6\"\n"
                         "        disabled=0\n"
                         "        scan_ssid=1\n"
                         "        bgscan=\"\"\n"
                         "        scan_freq=5180 5200 5220 5240 5745 5765 5785 5805\n"
                         "        freq_list=5180 5200 5220 5240 5745 5765 5785 5805\n"
                         "        ssid=\"Dummy_ssid_vif0.2\"\n"
                         "        psk=\"EL!6jLLlKAn#C&6zyxGaI99avs6W@e%H\"\n"
                         "        key_mgmt=WPA-PSK\n"
                         "        pairwise=CCMP\n"
                         "        proto=RSN\n"
                         "        multi_ap_backhaul_sta=0\n"
                         "        #bssid=\n"
                         "        ieee80211w=2\n"
                         "}\n"
                         "network={\n"
                         "        id_str=\"id_5\"\n"
                         "        disabled=0\n"
                         "        scan_ssid=1\n"
                         "        bgscan=\"\"\n"
                         "        scan_freq=5180 5200 5220 5240 5745 5765 5785 5805\n"
                         "        freq_list=5180 5200 5220 5240 5745 5765 5785 5805\n"
                         "        ssid=\"Dummy_ssid_vif0.3\"\n"
                         "        psk=\"#gezH3*0TVr#f#wbTBMY6z5&78pjx1WD\"\n"
                         "        key_mgmt=WPA-PSK\n"
                         "        pairwise=CCMP\n"
                         "        proto=RSN\n"
                         "        multi_ap_backhaul_sta=0\n"
                         "        #bssid=\n"
                         "        ieee80211w=0\n"
                         "}\n";

    const char *list_networks = "Selected interface 'wlan0'\n"
                                "network id / ssid / bssid / flags\n"
                                "0\tDummy_ssid_vif0.0\tab:cd:ef:01:23:45\t[CURRENT]\n"
                                "1\tDummy_ssid_vif0.1\tany\t[DISABLED]\n"
                                "2\tDummy_ssid_vif0.2\tany\t[TEMP-DISABLED]\n"
                                "3\tDummy_ssid_vif0.3\tany\t\n";

    vstate = CALLOC(1, sizeof(struct osw_drv_vif_state));
    bufs.config = config;
    bufs.list_networks = list_networks;

    osw_hostap_conf_fill_sta_state(&bufs, vstate);
    network = vstate->u.sta.network;

    OSW_UT_EVAL(network != NULL);
    OSW_UT_EVAL(strcmp(network->ssid.buf, "Dummy_ssid_vif0.0") == 0);
    OSW_UT_EVAL(network->bssid.octet[0] == 0xab);
    OSW_UT_EVAL(network->bssid.octet[1] == 0xcd);
    OSW_UT_EVAL(network->bssid.octet[2] == 0xef);
    OSW_UT_EVAL(network->bssid.octet[3] == 0x01);
    OSW_UT_EVAL(network->bssid.octet[4] == 0x23);
    OSW_UT_EVAL(network->bssid.octet[5] == 0x45);
    OSW_UT_EVAL(strcmp(network->psk.str, "QuofJ1a#$#4NOvXg1DB8oWzZky2r#YCC") == 0);
    OSW_UT_EVAL(network->wpa.pmf == OSW_PMF_DISABLED);

    network = network->next;
    OSW_UT_EVAL(network != NULL);
    OSW_UT_EVAL(strcmp(network->ssid.buf, "Dummy_ssid_vif0.1") == 0);
    OSW_UT_EVAL(strcmp(network->psk.str, "a5SN9#aQ52Oeu#0SW2DugQS4@6nxfb81") == 0);
    OSW_UT_EVAL(network->wpa.pmf == OSW_PMF_OPTIONAL);

    network = network->next;
    OSW_UT_EVAL(network != NULL);
    OSW_UT_EVAL(strcmp(network->ssid.buf, "Dummy_ssid_vif0.2") == 0);
    OSW_UT_EVAL(strcmp(network->psk.str, "EL!6jLLlKAn#C&6zyxGaI99avs6W@e%H") == 0);
    OSW_UT_EVAL(network->wpa.pmf == OSW_PMF_REQUIRED);

    network = network->next;
    OSW_UT_EVAL(network != NULL);
    OSW_UT_EVAL(strcmp(network->ssid.buf, "Dummy_ssid_vif0.3") == 0);
}

