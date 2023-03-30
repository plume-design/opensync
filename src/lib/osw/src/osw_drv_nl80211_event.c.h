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

/* This file groups event processing related helpers */

static const char *
osw_drv_nl80211_tb_to_phy_name(struct osw_drv *drv,
                               struct nlattr *tb[])
{
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct nl_80211 *nl = m->nl_80211;
    const struct nl_80211_phy *phy = nl_80211_phy_by_nla(nl, tb);
    if (phy != NULL) return phy->name;
    struct nlattr *wiphy_name = tb[NL80211_ATTR_WIPHY_NAME];
    if (wiphy_name != NULL) return nla_get_string(wiphy_name);
    return NULL;
}

static const char *
osw_drv_nl80211_tb_to_vif_name(struct osw_drv *drv,
                               struct nlattr *tb[])
{
    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct nl_80211 *nl = m->nl_80211;
    const struct nl_80211_vif *vif = nl_80211_vif_by_nla(nl, tb);
    if (vif != NULL) return vif->name;
    struct nlattr *ifname = tb[NL80211_ATTR_IFNAME];
    if (ifname != NULL) return nla_get_string(ifname);
    return NULL;
}

static void
osw_drv_nl80211_event_phy(struct osw_drv *drv,
                          struct nlattr *tb[],
                          const char *action)
{
    const char *phy_name = osw_drv_nl80211_tb_to_phy_name(drv, tb);
    if (WARN_ON(phy_name == NULL)) return;
    LOGI("osw: drv: nl80211: event: phy: %s: %s",
         phy_name, action);
    osw_drv_report_phy_changed(drv, phy_name);
}

static void
osw_drv_nl80211_event_vif(struct osw_drv *drv,
                          struct nlattr *tb[],
                          const char *action)
{
    const char *phy_name = osw_drv_nl80211_tb_to_phy_name(drv, tb);
    const char *vif_name = osw_drv_nl80211_tb_to_vif_name(drv, tb);
    WARN_ON(phy_name == NULL);
    WARN_ON(vif_name == NULL);
    if (phy_name != NULL &&
        vif_name != NULL) {
        LOGI("osw: drv: nl80211: event: vif: %s/%s: %s",
                phy_name, vif_name, action);
        osw_drv_report_vif_changed(drv, phy_name, vif_name);
    }
}

static void
osw_drv_nl80211_event_csa_started(struct osw_drv *drv,
                                  struct nlattr *tb[])
{
    const char *phy_name = osw_drv_nl80211_tb_to_phy_name(drv, tb);
    const char *vif_name = osw_drv_nl80211_tb_to_vif_name(drv, tb);
    struct osw_channel c;

    MEMZERO(c);
    nla_freq_to_osw_channel(tb, &c);

    struct nlattr *nla_cs_count = tb[NL80211_ATTR_CH_SWITCH_COUNT];
    struct nlattr *nla_block_tx = tb[NL80211_ATTR_CH_SWITCH_BLOCK_TX];
    const uint32_t cs_count = nla_cs_count ? nla_get_u32(nla_cs_count) : 0;
    const bool block_tx = nla_block_tx ? nla_get_flag(nla_block_tx) : 0;

    LOGI("osw: drv: nl80211: event: vif: %s/%s: csa started to " OSW_CHANNEL_FMT " cs=%" PRIu32 " %s",
         phy_name,
         vif_name,
         OSW_CHANNEL_ARG(&c),
         cs_count,
         block_tx ? "tx blocked" : "tx allowed");

    osw_drv_report_vif_channel_change_started(drv, phy_name, vif_name, &c);
}

static void
osw_drv_nl80211_event_sta(struct osw_drv *drv,
                          struct nlattr *tb[],
                          const char *action)
{
    struct nlattr *mac = tb[NL80211_ATTR_MAC];
    if (WARN_ON(mac == NULL)) return;
    if (WARN_ON(nla_len(mac) != ETH_ALEN)) return;

    const char *phy_name = osw_drv_nl80211_tb_to_phy_name(drv, tb);
    const char *vif_name = osw_drv_nl80211_tb_to_vif_name(drv, tb);
    WARN_ON(phy_name == NULL);
    WARN_ON(vif_name == NULL);

    if (phy_name != NULL &&
        vif_name != NULL) {
        struct osw_hwaddr sta_addr;
        memcpy(sta_addr.octet, nla_data(mac), ETH_ALEN);

        LOGI("osw: drv: nl80211: event: sta: %s/%s/"OSW_HWADDR_FMT": %s",
             phy_name, vif_name, OSW_HWADDR_ARG(&sta_addr), action);
        osw_drv_report_sta_changed(drv, phy_name, vif_name, &sta_addr);
    }
}

static void
osw_drv_nl80211_event_sta_del(struct osw_drv *drv,
                              struct nlattr *tb[])
{
    struct nlattr *mac = tb[NL80211_ATTR_MAC];
    if (WARN_ON(mac == NULL)) return;
    if (WARN_ON(nla_len(mac) != ETH_ALEN)) return;

    const char *phy_name = osw_drv_nl80211_tb_to_phy_name(drv, tb);
    const char *vif_name = osw_drv_nl80211_tb_to_vif_name(drv, tb);
    WARN_ON(phy_name == NULL);
    WARN_ON(vif_name == NULL);

    if (phy_name == NULL) return;
    if (vif_name == NULL) return;

    struct osw_hwaddr sta_addr;
    memcpy(sta_addr.octet, nla_data(mac), ETH_ALEN);

    struct osw_drv_nl80211 *m = osw_drv_get_priv(drv);
    struct osw_drv_nl80211_sta *sta = osw_drv_nl80211_sta_lookup(m, phy_name, vif_name, &sta_addr);
    if (sta != NULL) {
        osw_timer_disarm(&sta->delete_expiry);
    }

    osw_drv_nl80211_event_sta(drv, tb, "del");
}

static void
osw_drv_nl80211_event_scan(struct osw_drv *drv,
                           struct nlattr *tb[],
                           bool aborted)
{
    const char *vif_name = osw_drv_nl80211_tb_to_vif_name(drv, tb);
    if (vif_name == NULL) return;

    struct osw_drv_nl80211_vif *vif = osw_drv_nl80211_vif_lookup(drv, vif_name);
    if (vif == NULL) return;

    const enum osw_drv_scan_complete_reason reason = aborted
        ? OSW_DRV_SCAN_ABORTED
        : OSW_DRV_SCAN_DONE;

    osw_drv_nl80211_scan_complete(vif, reason);
}

static void
osw_drv_nl80211_event_unknown(struct osw_drv *drv,
                              struct nlattr *tb[],
                              const char *action)
{
    const char *phy_name = osw_drv_nl80211_tb_to_phy_name(drv, tb);
    const char *vif_name = osw_drv_nl80211_tb_to_vif_name(drv, tb);
    struct nlattr *mac = tb[NL80211_ATTR_MAC]
                       ? tb[NL80211_ATTR_MAC]
                       : NULL;
    if (mac != NULL && WARN_ON(nla_len(mac) != ETH_ALEN)) {
        mac = NULL;
    }
    char path[64] = {0};
    const char *type = (mac != NULL) ? "sta"
                     : (vif_name != NULL) ? "vif"
                     : (phy_name != NULL) ? "phy"
                     : "glob"; /* FIXME: maybe regulatory */
    if (phy_name != NULL) {
        STRSCAT(path, phy_name);
    }
    if (vif_name != NULL) {
        STRSCAT(path, "/");
        STRSCAT(path, vif_name);
    }
    if (mac != NULL) {
        struct osw_hwaddr addr;
        struct osw_hwaddr_str addr_str;
        memcpy(addr.octet, nla_data(mac), ETH_ALEN);
        const char *mac_str = osw_hwaddr2str(&addr, &addr_str);
        STRSCAT(path, "/");
        STRSCAT(path, mac_str);
    }
    LOGI("osw: drv: nl80211: event: %s: %s: %s", type, path, action);
}

static void
osw_drv_nl80211_event_process(struct osw_drv_nl80211 *m,
                              struct nl_msg *msg)
{
    struct osw_drv *drv = m->drv;
    if (drv == NULL) return;

    const uint8_t cmd = genlmsg_hdr(nlmsg_hdr(msg))->cmd;
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, NULL);
    if (err) return;

    char unknown_str[32];
    snprintf(unknown_str, sizeof(unknown_str), "unknown cmd=%d", cmd);

    switch (cmd) {
        case NL80211_CMD_VENDOR: return;
        case NL80211_CMD_FRAME_TX_STATUS: return;
        case NL80211_CMD_TRIGGER_SCAN: return;
        case NL80211_CMD_SCAN_ABORTED: return osw_drv_nl80211_event_scan(drv, tb, true);
        case NL80211_CMD_NEW_SCAN_RESULTS: return osw_drv_nl80211_event_scan(drv, tb, false);
        case NL80211_CMD_REMAIN_ON_CHANNEL: return;
        case NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL: return osw_drv_nl80211_event_scan(drv, tb, false);
        case NL80211_CMD_NEW_WIPHY: return osw_drv_nl80211_event_phy(drv, tb, "new");
        case NL80211_CMD_SET_WIPHY: return osw_drv_nl80211_event_phy(drv, tb, "set");
        case NL80211_CMD_DEL_WIPHY: return osw_drv_nl80211_event_phy(drv, tb, "del");
        case NL80211_CMD_NEW_INTERFACE: return osw_drv_nl80211_event_vif(drv, tb, "new");
        case NL80211_CMD_SET_INTERFACE: return osw_drv_nl80211_event_vif(drv, tb, "set");
        case NL80211_CMD_DEL_INTERFACE: return osw_drv_nl80211_event_vif(drv, tb, "del");
        case NL80211_CMD_START_AP: return osw_drv_nl80211_event_vif(drv, tb, "start ap");
        case NL80211_CMD_STOP_AP: return osw_drv_nl80211_event_vif(drv, tb, "stop ap");
        case NL80211_CMD_CH_SWITCH_NOTIFY: return osw_drv_nl80211_event_csa_started(drv, tb);
        case NL80211_CMD_NEW_STATION: return osw_drv_nl80211_event_sta(drv, tb, "new");
        case NL80211_CMD_SET_STATION: return osw_drv_nl80211_event_sta(drv, tb, "set");
        case NL80211_CMD_DEL_STATION: return osw_drv_nl80211_event_sta_del(drv, tb);
        case NL80211_CMD_AUTHENTICATE: return osw_drv_nl80211_event_vif(drv, tb, "auth");
        case NL80211_CMD_DEAUTHENTICATE: return osw_drv_nl80211_event_vif(drv, tb, "deauth");
        case NL80211_CMD_ASSOCIATE: return osw_drv_nl80211_event_vif(drv, tb, "assoc");
        case NL80211_CMD_DISASSOCIATE: return osw_drv_nl80211_event_vif(drv, tb, "disassoc");
        case NL80211_CMD_CONNECT: return osw_drv_nl80211_event_vif(drv, tb, "connect");
        case NL80211_CMD_DISCONNECT: return osw_drv_nl80211_event_vif(drv, tb, "disconnect");
        /* FIXME: Add more events: RADAR, CSA, .. */
        default: return osw_drv_nl80211_event_unknown(drv, tb, unknown_str);
    }
}
