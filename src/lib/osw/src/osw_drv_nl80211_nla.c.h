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

/* This file groups nlattr related helpers */

/* nlattr */

static bool
nla_u32_equal(struct nlattr *tb[], int attr, uint32_t value)
{
    struct nlattr *nla = tb[attr];
    if (nla == NULL) return false;
    return nla_get_u32(nla) == value;
}

static bool
nla_data_equal(struct nlattr *tb[], int attr, const void *data, size_t len)
{
    struct nlattr *nla = tb[attr];
    if (WARN_ON(len == 0)) return false; /* memcmp() would be always true */
    if (WARN_ON(data == NULL)) return false; /* caller bug */
    if ((size_t)nla_len(nla) != len) return false;
    if (memcmp(nla_data(nla), data, len) != 0) return false;
    return true;
}

static uint32_t
nla_get_u32_or(struct nlattr *tb[], int attr, uint32_t default_value)
{
    struct nlattr *nla = tb[attr];
    if (nla == NULL) return default_value;
    return nla_get_u32(nla);
}

/* nl80211 */

static enum nl80211_iftype
nla_get_iftype(struct nlattr *tb[])
{
    return nla_get_u32_or(tb, NL80211_ATTR_IFTYPE, NL80211_IFTYPE_UNSPECIFIED);
}

static bool
nla_wiphy_equal(struct nlattr *tb[],
                uint32_t wiphy)
{
    return nla_u32_equal(tb, NL80211_ATTR_WIPHY, wiphy);
}

static bool
nla_ifindex_equal(struct nlattr *tb[],
                  uint32_t ifindex)
{
    return nla_u32_equal(tb, NL80211_ATTR_IFINDEX, ifindex);
}

static bool
nla_mac_equal(struct nlattr *tb[],
              const void *mac)
{
    return nla_data_equal(tb, NL80211_ATTR_MAC, mac, ETH_ALEN);
}


static bool
nla_get_link_connected(enum nl80211_bss_status status)
{
    switch (status) {
        case NL80211_BSS_STATUS_AUTHENTICATED:
            return false; /* not yet */
        case NL80211_BSS_STATUS_ASSOCIATED:
            return true;
        case NL80211_BSS_STATUS_IBSS_JOINED:
            return true;
    }
    return false;
}

static struct nlattr *
nla_get_bssid(struct nlattr *tb[])
{
    struct nlattr *bss = tb[NL80211_ATTR_BSS];
    const size_t mac_len = OSW_HWADDR_LEN;
    struct nla_policy policy[NL80211_BSS_MAX + 1] = {
        [NL80211_BSS_STATUS] = { .type = NLA_U32 },
        [NL80211_BSS_BSSID] = { .minlen = mac_len, .maxlen = mac_len },
    };
    if (bss == NULL) return NULL;

    struct nlattr *tb_bss[NL80211_BSS_MAX + 1];
    const int parse_err = nla_parse_nested(tb_bss, NL80211_BSS_MAX, bss, policy);
    const bool parse_failed = (parse_err == 0 ? false : true);
    if (parse_failed) return NULL;

    struct nlattr *status = tb_bss[NL80211_BSS_STATUS];
    struct nlattr *bssid = tb_bss[NL80211_BSS_BSSID];

    if (status == NULL) return NULL;
    if (bssid == NULL) return NULL;
    if (nla_get_link_connected(nla_get_u32(status)) == false) return NULL;

    return bssid;
}
/* nl80211 <-> OSW */

static int
nla_chan_type_to_offset_mhz(enum nl80211_channel_type type)
{
    switch (type) {
        case NL80211_CHAN_NO_HT: return 0;
        case NL80211_CHAN_HT20: return 0;
        case NL80211_CHAN_HT40MINUS: return -4;
        case NL80211_CHAN_HT40PLUS: return 4;
    }
    WARN_ON(1);
    return 0;
}

static enum osw_channel_width
nla_chan_type_to_osw_width(enum nl80211_channel_type type)
{
    switch (type) {
        case NL80211_CHAN_NO_HT: return OSW_CHANNEL_20MHZ;
        case NL80211_CHAN_HT20: return OSW_CHANNEL_20MHZ;
        case NL80211_CHAN_HT40MINUS: return OSW_CHANNEL_40MHZ;
        case NL80211_CHAN_HT40PLUS: return OSW_CHANNEL_40MHZ;
    }
    WARN_ON(1);
    return OSW_CHANNEL_20MHZ;
}

static enum osw_channel_width
nla_chan_width_to_osw_width(enum nl80211_chan_width width)
{
    switch (width) {
        case NL80211_CHAN_WIDTH_20_NOHT: return OSW_CHANNEL_20MHZ;
        case NL80211_CHAN_WIDTH_20: return OSW_CHANNEL_20MHZ;
        case NL80211_CHAN_WIDTH_40: return OSW_CHANNEL_40MHZ;
        case NL80211_CHAN_WIDTH_80: return OSW_CHANNEL_80MHZ;
        case NL80211_CHAN_WIDTH_80P80: return OSW_CHANNEL_80P80MHZ;
        case NL80211_CHAN_WIDTH_160: return OSW_CHANNEL_160MHZ;
        default: break;
    }
    return OSW_CHANNEL_20MHZ;
}

static bool
nla_mac_to_osw_hwaddr(struct nlattr *nla, struct osw_hwaddr *addr)
{
    if (nla == NULL) return false;
    const void *mac = nla_data(nla);
    const bool mac_len_ok = (nla_len(nla) == OSW_HWADDR_LEN);
    const bool bad_mac_len = !mac_len_ok;
    WARN_ON(bad_mac_len);
    if (mac_len_ok) memcpy(&addr->octet, mac, OSW_HWADDR_LEN);
    return mac_len_ok;
}

static enum osw_channel_state_dfs
nla_dfs_to_osw(bool radar, enum nl80211_dfs_state s)
{
    if (radar == false) return OSW_CHANNEL_NON_DFS;

    switch (s) {
        case NL80211_DFS_USABLE: return OSW_CHANNEL_DFS_CAC_POSSIBLE;
        case NL80211_DFS_UNAVAILABLE: return OSW_CHANNEL_DFS_NOL;
        case NL80211_DFS_AVAILABLE: return OSW_CHANNEL_DFS_CAC_COMPLETED;
    }

    /* FIXME
     *
     * There's no way to know if nl80211 driver is doing CAC
     * at the time. Technically cfg80211 keeps track of it
     * in wdev->cac_start_time.
     *
     * Idea for now is to either rely on
     * NL80211_RADAR_CAC_STARTED/ABORTED/COMPLETED, although
     * these can be missed if CAC is already in progress
     * when this code boots up.
     *
     * Alternative is to overlap the dfs state with
     * specializations, eg. hostapd, or vendor specific
     * queries.
     */

    return OSW_CHANNEL_NON_DFS;
}

static void
nla_freq_to_osw_chan_state(struct osw_channel_state **cs,
                           size_t *n_cs,
                           struct nlattr *nl_freq)
{
    struct nlattr *tb_freq[NL80211_FREQUENCY_ATTR_MAX+1] = {0};
    void *data = nla_data(nl_freq);
    size_t len = nla_len(nl_freq);
    nla_parse(tb_freq, NL80211_FREQUENCY_ATTR_MAX, data, len, NULL);

    struct nlattr *nl_mhz = tb_freq[NL80211_FREQUENCY_ATTR_FREQ];
    struct nlattr *nl_radar = tb_freq[NL80211_FREQUENCY_ATTR_RADAR];
    struct nlattr *nl_dfs = tb_freq[NL80211_FREQUENCY_ATTR_DFS_STATE];
    struct nlattr *nl_disabled = tb_freq[NL80211_FREQUENCY_ATTR_DISABLED];

    const uint32_t mhz = nl_mhz ? nla_get_u32(nl_mhz) : 0;
    const bool radar = nl_radar != NULL;
    const enum nl80211_dfs_state dfs_default = radar ? NL80211_DFS_USABLE : NL80211_DFS_AVAILABLE;
    const enum nl80211_dfs_state dfs = nl_dfs ? nla_get_u32(nl_dfs) : dfs_default;
    const bool disabled = (nl_disabled != NULL);

    if (disabled) return;

    const enum osw_channel_state_dfs osw_dfs = nla_dfs_to_osw(radar, dfs);

    const size_t old_count = *n_cs;
    const size_t new_count = old_count + 1;
    const size_t last_idx = old_count;
    const size_t elem_size = sizeof(**cs);
    const size_t bytes = new_count * elem_size;
    *cs = REALLOC(*cs, bytes);
    *n_cs = new_count;
    struct osw_channel_state *last = (*cs) + last_idx;
    memset(last, 0, sizeof(*last));
    last->channel.width = OSW_CHANNEL_20MHZ;
    last->channel.control_freq_mhz = mhz;
    last->dfs_state = osw_dfs;

    /* FIXME: Figure out how to infer remaining NOL time for
     * a given channel.
     */
}

static void
nla_band_to_osw_chan_states(struct osw_channel_state **cs,
                            size_t *n_cs,
                            struct nlattr *nl_bands)
{
    if (nl_bands == NULL) return;
    struct nlattr *nl_band;
    int rem_band;
    nla_for_each_nested(nl_band, nl_bands, rem_band) {
        struct nlattr *tb_band[NL80211_BAND_ATTR_MAX+1] = {0};
        void *data = nla_data(nl_band);
        size_t len = nla_len(nl_band);
        nla_parse(tb_band, NL80211_BAND_ATTR_MAX, data, len, NULL);

        struct nlattr *nl_freqs = tb_band[NL80211_BAND_ATTR_FREQS];
        if (nl_freqs != NULL) {
            struct nlattr *nl_freq;
            int rem_freq;
            nla_for_each_nested(nl_freq, nl_freqs, rem_freq) {
                nla_freq_to_osw_chan_state(cs, n_cs, nl_freq);
            }
        }
    }
}

static void
nla_freq_to_osw_channel(struct nlattr *tb[],
                        struct osw_channel *c)
{
    struct nlattr *nl_freq = tb[NL80211_ATTR_WIPHY_FREQ];
    struct nlattr *nl_width = tb[NL80211_ATTR_CHANNEL_WIDTH];
    struct nlattr *nl_center1 = tb[NL80211_ATTR_CENTER_FREQ1];
    struct nlattr *nl_center2 = tb[NL80211_ATTR_CENTER_FREQ2];
    struct nlattr *nl_type = tb[NL80211_ATTR_WIPHY_CHANNEL_TYPE];

    if (nl_freq == NULL) return;

    const uint32_t freq_mhz = nla_get_u32(nl_freq);

    c->control_freq_mhz = freq_mhz;

    if (nl_width != NULL) {
        enum nl80211_chan_width width = nla_get_u32(nl_width);
        const uint32_t center1_mhz = nl_center1 ? nla_get_u32(nl_center1) : 0;
        const uint32_t center2_mhz = nl_center2 ? nla_get_u32(nl_center2) : 0;
        c->width = nla_chan_width_to_osw_width(width);
        c->center_freq0_mhz = center1_mhz;
        c->center_freq1_mhz = center2_mhz;
    }
    else if (nl_type != NULL) {
        const enum nl80211_channel_type type = nla_get_u32(nl_type);
        const int offset_mhz = nla_chan_type_to_offset_mhz(type);
        c->width = nla_chan_type_to_osw_width(type);
        c->center_freq0_mhz = freq_mhz + offset_mhz;
    }
}

static void
nla_ssid_to_osw_ssid(struct nlattr *nla, struct osw_ssid *ssid)
{
    if (nla == NULL) return;

    const void *data = nla_data(nla);
    const size_t len = nla_len(nla);
    const size_t max_len = sizeof(ssid->buf);

    if (WARN_ON(len > max_len)) return;

    memcpy(ssid->buf, data, len);
    ssid->len = len;
}

static enum osw_vif_type
nla_iftype_to_osw_vif_type(enum nl80211_iftype type)
{
    switch (type) {
        case NL80211_IFTYPE_UNSPECIFIED: return OSW_VIF_UNDEFINED;
        case NL80211_IFTYPE_STATION: return OSW_VIF_STA;
        case NL80211_IFTYPE_AP: return OSW_VIF_AP;
        case NL80211_IFTYPE_AP_VLAN: return OSW_VIF_AP_VLAN;
        /* FIXME: Can't really avoid default in this
         * switch() because of meta labels that overlap, ie.
         * NL80211_IFTYPE_MAX)
         */
        default: break;
    }
    return OSW_VIF_UNDEFINED;
}

static void
nla_vif_to_osw_vif_state_ap(struct nlattr *tb[],
                            struct osw_drv_vif_state_ap *state)
{
    nla_freq_to_osw_channel(tb, &state->channel);
    nla_ssid_to_osw_ssid(tb[NL80211_ATTR_SSID], &state->ssid);
}

static void
nla_vif_to_osw_vif_state_ap_vlan(struct nlattr *tb[],
                                 struct osw_drv_vif_state_ap_vlan *state)
{
    /* FIXME: Necessary for 4addr/WDS */
}

static void
nla_vif_to_osw_vif_state_sta(struct nlattr *tb[],
                             struct osw_drv_vif_state_sta *state)
{
    nla_freq_to_osw_channel(tb, &state->link.channel);
    nla_ssid_to_osw_ssid(tb[NL80211_ATTR_SSID], &state->link.ssid);
    state->link.status = OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED;
}

static void
nla_fill_tx_chainmask(int *chainmask,
                      struct nlattr *tb[])
{
    const uint32_t txca = nla_get_u32_or(tb, NL80211_ATTR_WIPHY_ANTENNA_AVAIL_TX, 0);
    const uint32_t rxca = nla_get_u32_or(tb, NL80211_ATTR_WIPHY_ANTENNA_AVAIL_RX, 0);
    const uint32_t txc = nla_get_u32_or(tb, NL80211_ATTR_WIPHY_ANTENNA_TX, 0);
    const uint32_t rxc = nla_get_u32_or(tb, NL80211_ATTR_WIPHY_ANTENNA_RX, 0);

    if (*chainmask) return;

    /* FIXME: This isn't ideal. phy_state should allow
     * expressing these explicitly so this doesn't need to
     * flatten this out. It's easier to copy or leave 0s
     * rather than squeeze 4 possibly different values into
     * 1.
     */
    if (txc) *chainmask = txc;
    else if (rxc) *chainmask = rxc;
    else if (txca) *chainmask = txca;
    else if (rxca) *chainmask = rxca;
}

static void
nla_fill_reg_domain(char *ccode,
                    struct nlattr *tb[],
                    uint32_t wiphy)
{
    if (tb[NL80211_ATTR_WIPHY] != NULL) {
        if (nla_wiphy_equal(tb, wiphy) == false) return;
        if (tb[NL80211_ATTR_WIPHY_SELF_MANAGED_REG] == NULL) return;
    }
    struct nlattr *reg = tb[NL80211_ATTR_REG_ALPHA2];
    if (reg == NULL) return;
    if (nla_len(reg) < 2) return;
    const char *country_code = nla_data(reg);
    ccode[0] = country_code[0];
    ccode[1] = country_code[1];
    ccode[2] = '\0';
}

static bool
nla_comb_is_radar_supported(struct nlattr *comb)
{
    struct nla_policy policy[MAX_NL80211_IFACE_COMB + 1] = {
        [NL80211_IFACE_COMB_RADAR_DETECT_WIDTHS] = { .type = NLA_U32 },
    };
    struct nlattr *tb[MAX_NL80211_IFACE_COMB + 1];
    const int err = nla_parse_nested(tb, MAX_NL80211_IFACE_COMB, comb, policy);
    if (err) return false;

    struct nlattr *widths = tb[NL80211_IFACE_COMB_RADAR_DETECT_WIDTHS];
    if (widths == NULL) return false;

    /* FIXME: This is naive, but probably sufficient given
     * what kind of result is expected "is any radar
     * detection supported or not". Typically all possible
     * widths will be advertised as supported anyway.
     */
    return nla_get_u32(widths) != 0;
}

static bool
nla_wiphy_is_radar_supported(struct nlattr *tb[])
{
    struct nlattr *combs = tb[NL80211_ATTR_INTERFACE_COMBINATIONS];
    if (combs == NULL) return false;

    int rem;
    struct nlattr *comb;
    nla_for_each_nested(comb, combs, rem) {
        if (nla_comb_is_radar_supported(comb)) {
            return true;
        }
    }

    return false;
}

static void
nla_fill_radar_detect(enum osw_radar_detect *radar,
                      struct nlattr *tb[])
{
    /* FIXME: nl80211 itself doesn't really allow disabling
     * radar detection. This can be vendor specific function
     * and is expected to be overridden by vendor
     * specialization(s).
     */
    if (nla_wiphy_is_radar_supported(tb)) {
        *radar = OSW_RADAR_DETECT_ENABLED;
        return;
    }
}
