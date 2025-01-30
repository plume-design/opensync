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
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

/* opensync */
#include <memutil.h>
#include <util.h>
#include <os.h>
#include <log.h>

/* unit */
#include <osw_drv.h>
#include <osw_types.h>
#include <osw_wpas_conf.h>
#include <osw_hostap_common.h>

/* STA config generation */
static void
osw_wpas_util_fill_global_block(struct osw_drv_vif_config_sta *sta,
                                struct osw_hostap_conf_sta_global_config *conf)
{
    /* FIXME - this hardcodes ctrl path! Get it from the caller instead */
    OSW_HOSTAP_CONF_SET_BUF(conf->ctrl_interface, "/var/run/wpa_supplicant");
    OSW_HOSTAP_CONF_SET_VAL(conf->scan_cur_freq, true);
}

static void
osw_wpas_util_fill_network_block(struct osw_drv_vif_sta_network *network,
                                 struct osw_hostap_conf_sta_network_config *conf)
{
    if (osw_hwaddr_is_zero(&network->bssid) == false) {
        struct osw_hwaddr_str hwaddr;
        osw_hwaddr2str(&network->bssid, &hwaddr);
        OSW_HOSTAP_CONF_SET_BUF(conf->bssid, hwaddr.buf);
    }

    /* FIXME - conversion assumes null-terminated ssid! */
    OSW_HOSTAP_CONF_SET_BUF_Q_LEN(conf->ssid, network->ssid.buf, network->ssid.len);
    OSW_HOSTAP_CONF_SET_BUF_Q(conf->psk, network->psk.str);
    OSW_HOSTAP_CONF_SET_VAL(conf->multi_ap_backhaul_sta, network->multi_ap ? 1 : 0);
    OSW_HOSTAP_CONF_SET_VAL(conf->ieee80211w, osw_hostap_conf_pmf_from_osw(&network->wpa));
    OSW_HOSTAP_CONF_SET_VAL(conf->scan_ssid, true);
    OSW_HOSTAP_CONF_SET_VAL(conf->priority, network->priority);

    const char *proto = osw_hostap_conf_proto_from_osw(&network->wpa);
    if (proto != NULL) {
        OSW_HOSTAP_CONF_SET_BUF(conf->proto, proto);
        FREE(proto);
    }

    const char *key_mgmt = osw_hostap_conf_wpa_key_mgmt_from_osw(&network->wpa);
    if (key_mgmt != NULL) {
        OSW_HOSTAP_CONF_SET_BUF(conf->key_mgmt, key_mgmt);
        FREE(key_mgmt);
    }

    const char *pairwise = osw_hostap_conf_pairwise_from_osw(&network->wpa);
    if (pairwise != NULL) {
        OSW_HOSTAP_CONF_SET_BUF(conf->pairwise, pairwise);
        FREE(pairwise);
    }
}

static void
osw_wpas_util_parse_network_block(const char *network,
                                  struct osw_drv_vif_sta_network *drv_network)
{
    char *line = NULL;
    char *kv;
    const char *k, *v;
    char tmp[256];
    char *ptmp = tmp;
    char *local_network = STRDUP(network);
    char *net = local_network;

    while ((line = strsep(&net, "\n\0")) != NULL) {
        /* Remove prefix spaces */
        kv = line;
        while (isspace(*kv)) kv++;
        /* Find key=value pairs */
        if ((k = strsep(&kv, "=")) &&
            (v = strsep(&kv, ""))) {
            if (strcmp(k, "psk") == 0) {
                osw_hostap_util_unquote(v, ptmp);
                STRSCPY_WARN(drv_network->psk.str, ptmp);
            }
            /* Please keep sae_password AFTER psk. This gives
             * precedence to sae_password if both exist. */
            if (strcmp(k, "sae_password") == 0) {
                osw_hostap_util_unquote(v, ptmp);
                STRSCPY_WARN(drv_network->psk.str, ptmp);
            }
            if (strcmp(k, "ssid") == 0) {
                osw_hostap_util_unquote(v, ptmp);
                STRSCPY_WARN(drv_network->ssid.buf, ptmp);
                drv_network->ssid.len = strlen(ptmp);
            }
            if (strcmp(k, "bssid") == 0) {
                osw_hwaddr_from_cstr(v, &drv_network->bssid);
            }
            if (strcmp(k, "pairwise") == 0) {
                osw_hostap_util_pairwise_to_osw(v, &drv_network->wpa);
            }
            if (strcmp(k, "proto") == 0) {
                osw_hostap_util_proto_to_osw(v, &drv_network->wpa);
            }
            if (strcmp(k, "key_mgmt") == 0) {
                osw_hostap_util_wpa_key_mgmt_to_osw(v, &drv_network->wpa);
            }
            if (strcmp(k, "ieee80211w") == 0) {
                osw_hostap_util_ieee80211w_to_osw(v, &drv_network->wpa);
            }
            if (strcmp(k, "multi_ap_backhaul_sta") == 0) {
                drv_network->multi_ap = atoi(v) ? true : false;
            }
            if (strcmp(k, "priority") == 0) {
                drv_network->priority = atoi(v);
            }
        }
    }
    FREE(local_network);
}

static void
osw_wpas_util_parse_config_to_networks(const char *config,
                                       struct osw_drv_vif_sta_network **ppnetwork)
{
    const char *begin = "network={\n";
    const char *end   = "\n}\n";
    char *start = NULL;
    char *stop = NULL;

    if (config == NULL) return;
    char *local_config = STRDUP(config);

    start = local_config;
    stop = local_config;
    while ( (start = strstr(start, begin)) != NULL &&
            (stop  = strstr(start, end)) != NULL ) {
        start += strlen(begin);
        if (stop != NULL) stop[0] = '\0';

        *ppnetwork = CALLOC(1, sizeof(struct osw_drv_vif_sta_network));
        osw_wpas_util_parse_network_block(start, *ppnetwork);
        ppnetwork = &(*ppnetwork)->next;

        start = stop + 1;
    }
    FREE(local_config);
}

static void
osw_wpas_util_get_psk_from_config_id(const char *config,
                                     const char *id_str,
                                     bool link_connected,
                                     struct osw_psk *osw_psk)
{
    char *line = NULL;
    char *kv;
    const char *k, *v;
    const char *begin = "network={\n";
    const char *end   = "\n}\n";
    const char *id_str_pattern = "id_str=";
    char tmp[256];
    char psk[65];
    char sae_password[65];
    char *ptmp = tmp;
    char id_str_buf[20];
    char *start = NULL;
    char *stop = NULL;
    size_t networks_count = 0;
    bool psk_found = false;
    bool sae_found = false;
    bool network_found = false;

    if (config == NULL) return;
    if (id_str == NULL) return;
    char *local_config = STRDUP(config);

    MEMZERO(id_str_buf);
    STRSCPY_WARN(id_str_buf, id_str_pattern);
    STRSCAT(id_str_buf, id_str);

    start = local_config;
    stop = local_config;
    while ( (start = strstr(start, begin)) != NULL &&
            (stop  = strstr(start, end)) != NULL ) {
        start += strlen(begin);
        if (stop != NULL) stop[0] = '\0';

        psk_found = false;
        sae_found = false;

        while ((line = strsep(&start, "\n\0")) != NULL) {
            /* Remove prefix spaces */
            kv = line;
            while (isspace(*kv)) kv++;
            /* Find key=value pairs */
            if ((k = strsep(&kv, "=")) &&
                (v = strsep(&kv, ""))) {
                if (strcmp(k, "id_str") == 0) {
                    osw_hostap_util_unquote(v, ptmp);
                    if (strcmp(ptmp, id_str) == 0)
                        network_found = true;
                }
                if (strcmp(k, "psk") == 0) {
                    osw_hostap_util_unquote(v, ptmp);
                    STRSCPY_WARN(psk, ptmp);
                    psk_found = true;
                }
                if (strcmp(k, "sae_password") == 0) {
                    osw_hostap_util_unquote(v, ptmp);
                    STRSCPY_WARN(sae_password, ptmp);
                    sae_found = true;
                }
            }
        }

        if (network_found == true) {
            /* Please keep sae_password AFTER psk. This gives
             * precedence to sae_password if both exist. */
            if (psk_found == true)
                STRSCPY_WARN(osw_psk->str, psk);
            if (sae_found == true)
                STRSCPY_WARN(osw_psk->str, sae_password);
            break;
        }

        networks_count++;
        start = stop + 1;
    }

    const bool no_matching_id_str = (network_found == false);
    const bool only_1_network_block = (networks_count == 1);
    const bool infer_the_passphrase = no_matching_id_str
                                   && only_1_network_block
                                   && link_connected;
    if (infer_the_passphrase) {
        /* When reconfiguring networks the network intended to be
         * remain to be used may be on the new list, but the config
         * file may end up being re-generated causing the id_str to no
         * longer match.
         *
         * However, if the link is known to be active for a given
         * config file, and there's only a single network block in the
         * config file, then it's fair to assume the passphrase used
         * to setup that link is from that single network block, even
         * if the id_str didn't match.
         */
        if (psk_found == true)
            STRSCPY_WARN(osw_psk->str, psk);
        if (sae_found == true)
            STRSCPY_WARN(osw_psk->str, sae_password);
    }

    FREE(local_config);
}

static bool
osw_wpas_util_net_list_has_same_bridge(struct osw_drv_vif_sta_network *net)
{
    const char *br = (net ? net->bridge_if_name.buf : NULL);
    const ssize_t max_len= sizeof(net->bridge_if_name.buf);
    while (net != NULL) {
        const bool mismatch = (strncmp(br, net->bridge_if_name.buf, max_len) != 0);
        if (mismatch) {
            return false;
        }
        net = net->next;
    }
    return true;
}

static void
osw_wpas_util_net_list_set_bridge_if_name(struct osw_drv_vif_sta_network *net,
                                          const char *bridge_if_name)
{
    WARN_ON(osw_wpas_util_net_list_has_same_bridge(net) == false);
    while (net != NULL) {
        STRSCPY_WARN(net->bridge_if_name.buf, bridge_if_name ?: "");
        net = net->next;
    }
}

static void
osw_wpas_util_fill_network_list(const struct osw_hostap_conf_sta_state_bufs *bufs,
                                struct osw_drv_vif_state *vstate)
{
    osw_wpas_util_parse_config_to_networks(bufs->config, &vstate->u.sta.network);
    osw_wpas_util_net_list_set_bridge_if_name(vstate->u.sta.network,
                                              bufs->bridge_if_name);
}

void
osw_hostap_conf_generate_sta_config_bufs(struct osw_hostap_conf_sta_config *config)
{
    struct osw_hostap_conf_sta_global_config *conf;
    struct osw_hostap_conf_sta_network_config *net_conf;
    unsigned char network_counter = 0;
    char id_str[8];

    if (config == NULL) {
        LOGE("%s: function called with config == NULL", __func__);
        return;
    }

    conf = &config->global;
    net_conf = config->network;

    CONF_INIT(config->conf_buf);
    CONF_APPEND(update_config, "%d");
    /* global configuration (shared by all network blocks) */
    CONF_APPEND(ctrl_interface, "%s");
    CONF_APPEND(eapol_version, "%d");
    CONF_APPEND(ap_scan, "%d");
    CONF_APPEND(passive_scan, "%d");
    CONF_APPEND(country, "%s");
    CONF_APPEND(pmf, "%d");
    CONF_APPEND(sae_check_mfp, "%d");
    CONF_APPEND(sae_groups, "%s");
    CONF_APPEND(sae_pwe, "%d");
    CONF_APPEND(scan_cur_freq, "%d");
    CONF_APPEND(disallow_dfs, "%d");
    CONF_APPEND(interworking, "%d");
    CONF_APPEND_BUF(config->extra_buf);

    /* network block */
    while (net_conf != NULL) {
        /* shadow conf variable for macros */
        struct osw_hostap_conf_sta_network_config *conf = net_conf;
        /* append id_str=counter] as this is unique enough */
        snprintf(id_str, sizeof(id_str), "id_%u", network_counter++);
        OSW_HOSTAP_CONF_SET_BUF_Q(conf->id_str, id_str);

        OSW_HOSTAP_CONF_NETWORK_BLOCK_START();
            CONF_APPEND(disabled, "%d");
            CONF_APPEND(id_str, "%s");
            CONF_APPEND(ssid, "%s");
            CONF_APPEND(scan_ssid, "%d");
            CONF_APPEND(bssid, "%s");
            CONF_APPEND(ignore_broadcast_ssid, "%d");
            CONF_APPEND(priority, "%d");
            CONF_APPEND(mode, "%d");
            CONF_APPEND(frequency, "%d");
            CONF_APPEND(scan_freq, "%d");
            CONF_APPEND(freq_list, "%s");
            CONF_APPEND(bgscan, "%s");
            CONF_APPEND(proto, "%s");
            CONF_APPEND(key_mgmt, "%s");
            CONF_APPEND(ieee80211w, "%d");
            CONF_APPEND(ocv, "%d");
            CONF_APPEND(auth_alg, "%s");
            CONF_APPEND(pairwise, "%s");
            CONF_APPEND(group, "%s");
            CONF_APPEND(group_mgmt, "%s");
            CONF_APPEND(psk, "%s");
            CONF_APPEND(mem_only_psk, "%d");
            CONF_APPEND(sae_password, "%s");
            CONF_APPEND(sae_password_id, "%s");
            CONF_APPEND(proactive_key_caching, "%d");
            CONF_APPEND(ft_eap_pmksa_caching, "%d");
            CONF_APPEND(group_rekey, "%d");

            /* Station inactivity limit */
            CONF_APPEND(ap_max_inactivity, "%d");
            CONF_APPEND(dtim_period, "%d");
            CONF_APPEND(beacon_int, "%d");
            CONF_APPEND(wps_disabled, "%d");
            CONF_APPEND(beacon_prot, "%d");

            CONF_APPEND(multi_ap_backhaul_sta, "%d");

            CONF_APPEND(dot11RSNAConfigPMKLifetime, "%d");
            CONF_APPEND(dot11RSNAConfigReauthThreshold, "%d");
            CONF_APPEND(dot11RSNAConfigSATimeout, "%d");
        OSW_HOSTAP_CONF_NETWORK_BLOCK_END();

        net_conf = net_conf->next;
    }
    CONF_FINI();
}

static const char *
osw_hostap_conf_compute_bridge(const struct osw_drv_vif_sta_network *first)
{
    if (first == NULL) return "";

    const char *name1 = first->bridge_if_name.buf;
    const size_t max_len = sizeof(first->bridge_if_name.buf);
    const size_t len1 = strnlen(name1, max_len);
    if (WARN_ON(len1 == max_len)) return "";

    /* This does a sanity check. Currently network blocks in
     * wpa_supplicant don't support per-network block
     * bridging setup. This by proxy also means that
     * non-multi-ap and multi-ap networks can't be easily
     * mixed in a single wpa_supplicant config file because
     * their briging would not be configurable.
     */
    bool collision = false;
    const struct osw_drv_vif_sta_network *second = first->next;
    const struct osw_drv_vif_sta_network *i = second;
    for (;;) {
        if (i == NULL) break;
        if (collision) break;

        const char *name2 = i->bridge_if_name.buf;
        const size_t len2 = strnlen(name2, max_len);
        collision = (len1 != len2)
                 || (len2 == max_len)
                 || (strcmp(name1, name2) != 0);
        i = i->next;
    }

    WARN_ON(collision);
    return name1;
}

bool
osw_hostap_conf_fill_sta_config(struct osw_drv_conf *drv_conf,
                                const char *phy_name,
                                const char *vif_name,
                                struct osw_hostap_conf_sta_config *conf)
{
    struct osw_drv_phy_config *phy = NULL;
    struct osw_drv_vif_config *vif = NULL;
    struct osw_drv_vif_config_sta *sta = NULL;
    struct osw_drv_vif_sta_network *network = NULL;
    struct osw_hostap_conf_sta_network_config *wpas_network = NULL;

    phy = osw_hostap_conf_phy_lookup(drv_conf, phy_name);
    if (phy == NULL)
        return false;

    vif = osw_hostap_conf_vif_lookup(phy, vif_name);
    if (vif == NULL || vif->vif_type != OSW_VIF_STA)
        return false;

    sta = &vif->u.sta;
    network = &sta->network[0];

    STRSCPY_WARN(conf->bridge_if_name.buf, osw_hostap_conf_compute_bridge(network));
    osw_wpas_util_fill_global_block(sta, &conf->global);

    if (network != NULL) {
        wpas_network = CALLOC(1, sizeof(struct osw_hostap_conf_sta_network_config));
        conf->network = wpas_network;
    }
    while (network != NULL) {
        osw_wpas_util_fill_network_block(network, wpas_network);
        network = network->next;
        if (network != NULL) {
            wpas_network->next = CALLOC(1, sizeof(struct osw_hostap_conf_sta_network_config));
            wpas_network = wpas_network->next;
        }
    }

    return true;
}

bool
osw_hostap_conf_free_sta_config(struct osw_hostap_conf_sta_config *conf)
{
    while (conf->network != NULL) {
        struct osw_hostap_conf_sta_network_config *next = conf->network->next;
        FREE(conf->network);
        conf->network = next;
    }

    return true;
}

static void
osw_wpas_util_fill_link_details(const struct osw_hostap_conf_sta_state_bufs *bufs,
                                struct osw_drv_vif_state *vstate)
{
    const char *status = bufs->status;
    const char *config = bufs->config;
    struct osw_drv_vif_state_sta *sta = &vstate->u.sta;
    struct osw_drv_vif_state_sta_link *link = &sta->link;
    const char *id;
    const bool no_status = (status == NULL)
                        || (strlen(status) == 0);

    if (no_status) {
        osw_vif_status_set(&vstate->status, OSW_VIF_DISABLED);
        return;
    }

    osw_vif_status_set(&vstate->status, OSW_VIF_ENABLED);

    STRSCPY_WARN(link->bridge_if_name.buf, bufs->bridge_if_name ?: "");
    STATE_GET_BY_FN(link->status, status, "wpa_state", osw_hostap_util_sta_state_to_osw);
    STATE_GET_BY_FN(link->channel, status, "freq", osw_hostap_util_sta_freq_to_channel);
    STATE_GET_BY_FN(link->bssid, status, "bssid", osw_hwaddr_from_cstr);
    STATE_GET_BY_FN(link->ssid, status, "ssid", osw_hostap_util_ssid_to_osw);

    STATE_GET_BY_FN(link->wpa, status, "pmf", osw_hostap_util_ieee80211w_to_osw);
    STATE_GET_BY_FN(link->wpa, status, "pairwise_cipher", osw_hostap_util_pairwise_to_osw);
    STATE_GET_BY_FN(link->wpa, status, "key_mgmt", osw_hostap_util_key_mgmt_to_osw);

    id = ini_geta(status, "id_str");
    if (id == NULL) return;
    if (config == NULL) return;

    const bool connected = (link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED);
    osw_wpas_util_get_psk_from_config_id(config, id, connected, &link->psk);
}

void
osw_hostap_conf_fill_sta_state(const struct osw_hostap_conf_sta_state_bufs *bufs,
                               struct osw_drv_vif_state *vstate)
{
    /* fill in existing link (if connected/connecting) */
    osw_wpas_util_fill_link_details(bufs, vstate);

    /* fill in configured networks from list_networks and config */
    osw_wpas_util_fill_network_list(bufs, vstate);
}

/* Unit test inclusion */
#include "osw_wpas_conf_ut.c"
