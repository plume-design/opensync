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

#ifndef OSW_TYPES_H_INCLUDED
#define OSW_TYPES_H_INCLUDED

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

enum osw_vif_type {
    OSW_VIF_UNDEFINED,
    OSW_VIF_AP,
    OSW_VIF_AP_VLAN,
    OSW_VIF_STA,
};

enum osw_acl_policy {
    OSW_ACL_NONE,
    OSW_ACL_ALLOW_LIST,
    OSW_ACL_DENY_LIST,
};

enum osw_channel_width {
    OSW_CHANNEL_20MHZ,
    OSW_CHANNEL_40MHZ,
    OSW_CHANNEL_80MHZ,
    OSW_CHANNEL_160MHZ,
    OSW_CHANNEL_80P80MHZ,
};

enum osw_pmf {
    OSW_PMF_DISABLED,
    OSW_PMF_OPTIONAL,
    OSW_PMF_REQUIRED,
};

enum osw_radar_detect {
    OSW_RADAR_UNSUPPORTED,
    OSW_RADAR_DETECT_ENABLED,
    OSW_RADAR_DETECT_DISABLED,
};

enum osw_channel_state_dfs {
    OSW_CHANNEL_NON_DFS,
    OSW_CHANNEL_DFS_CAC_POSSIBLE,
    OSW_CHANNEL_DFS_CAC_IN_PROGRESS,
    OSW_CHANNEL_DFS_CAC_COMPLETED,
    OSW_CHANNEL_DFS_NOL,
};

enum osw_band {
    OSW_BAND_UNDEFINED,
    OSW_BAND_2GHZ,
    OSW_BAND_5GHZ,
    OSW_BAND_6GHZ,
};

struct osw_channel {
    enum osw_channel_width width;
    int control_freq_mhz;
    int center_freq0_mhz;
    int center_freq1_mhz;
};

enum osw_reg_dfs {
    OSW_REG_DFS_UNDEFINED,
    OSW_REG_DFS_FCC,
    OSW_REG_DFS_ETSI,
};

struct osw_reg_domain {
    char ccode[3]; /* 2-letter ISO name, \0-terminated */
    int revision; /* vendor specific value */
    enum osw_reg_dfs dfs;
};

#define OSW_REG_DOMAIN_FMT "ccode %.*s rev %d dfs %s"
#define OSW_REG_DOMAIN_ARG(rd) \
    (int)sizeof((rd)->ccode) - 1, \
    (rd)->ccode, \
    (rd)->revision, \
    osw_reg_dfs_to_str((rd)->dfs)

/* FIXME: need osw_channel_ helper to convert freq->chan */

#define OSW_CHANNEL_FMT "%d (%s/%d)"
#define OSW_CHANNEL_ARG(c) \
    (c)->control_freq_mhz, \
    osw_channel_width_to_str((c)->width), \
    (c)->center_freq0_mhz

struct osw_channel_state {
    struct osw_channel channel;
    enum osw_channel_state_dfs dfs_state;
    int dfs_nol_remaining_seconds;
};

#define OSW_CHAN_STATE_FMT "%s %ds"
#define OSW_CHAN_STATE_ARG(x) \
    osw_channel_dfs_state_to_str((x)->dfs_state), \
    (x)->dfs_nol_remaining_seconds

#define OSW_HWADDR_LEN 6
#define OSW_HWADDR_FMT "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx"
#define OSW_HWADDR_ARG(ptr) \
    (ptr)->octet[0], \
    (ptr)->octet[1], \
    (ptr)->octet[2], \
    (ptr)->octet[3], \
    (ptr)->octet[4], \
    (ptr)->octet[5]
#define OSW_HWADDR_SARG(ptr) \
    &(ptr)->octet[0], \
    &(ptr)->octet[1], \
    &(ptr)->octet[2], \
    &(ptr)->octet[3], \
    &(ptr)->octet[4], \
    &(ptr)->octet[5]
#define OSW_HWADDR_BROADCAST { .octet = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } }

struct osw_hwaddr {
    unsigned char octet[OSW_HWADDR_LEN];
};

struct osw_hwaddr_str {
    char buf[18];
};

static inline char *
osw_hwaddr2str(const struct osw_hwaddr *addr, struct osw_hwaddr_str *str)
{
    snprintf(str->buf, sizeof(str->buf), OSW_HWADDR_FMT, OSW_HWADDR_ARG(addr));
    return str->buf;
}

static inline bool
osw_hwaddr_from_cstr(const char *src, struct osw_hwaddr *addr)
{
    return sscanf(src, OSW_HWADDR_FMT, OSW_HWADDR_SARG(addr)) == 6;
}

static inline bool
osw_hwaddr_is_zero(const struct osw_hwaddr *addr)
{
    const struct osw_hwaddr zero = {0};
    return memcmp(addr, &zero, sizeof(*addr)) == 0;
}

#define OSW_IEEE80211_SSID_LEN 32

struct osw_ifname {
    char buf[32];
};

int
osw_ifname_cmp(const struct osw_ifname *a,
               const struct osw_ifname *b);

struct osw_ssid {
    char buf[OSW_IEEE80211_SSID_LEN + 1];
    size_t len;
};

#define OSW_SSID_FMT "%.*s (len=%zu)"
#define OSW_SSID_ARG(x) (int)(x)->len, (x)->buf, (x)->len

static inline int
osw_ssid_cmp(const struct osw_ssid *a, const struct osw_ssid *b)
{
    struct osw_ssid x = *a;
    struct osw_ssid y = *b;
    x.buf[x.len] = 0;
    y.buf[y.len] = 0;
    return strcmp(x.buf, y.buf);
}

#define OSW_WPA_GROUP_REKEY_UNDEFINED -1

struct osw_wpa {
    bool wpa;
    bool rsn;
    bool akm_psk;
    bool akm_sae;
    bool akm_ft_psk;
    bool akm_ft_sae;
    bool pairwise_tkip;
    bool pairwise_ccmp;
    enum osw_pmf pmf;
    int group_rekey_seconds;
    int ft_mobility_domain;
};

enum osw_rate_legacy {
    OSW_RATE_CCK_1_MBPS,
    OSW_RATE_CCK_2_MBPS,
    OSW_RATE_CCK_5_5_MBPS,
    OSW_RATE_CCK_11_MBPS,

    OSW_RATE_OFDM_6_MBPS,
    OSW_RATE_OFDM_9_MBPS,
    OSW_RATE_OFDM_12_MBPS,
    OSW_RATE_OFDM_18_MBPS,
    OSW_RATE_OFDM_24_MBPS,
    OSW_RATE_OFDM_36_MBPS,
    OSW_RATE_OFDM_48_MBPS,
    OSW_RATE_OFDM_54_MBPS,
};

struct osw_rateset_legacy {
    enum osw_rate_legacy *rates;
    size_t n_rates;
};

enum osw_beacon_rate_type {
    OSW_RATE_BEACON_ABG,
    OSW_RATE_BEACON_HT,
    OSW_RATE_BEACON_VHT,
    OSW_RATE_BEACON_HE,
};

struct osw_beacon_rate {
    enum osw_beacon_rate_type type;
    union {
        enum osw_rate_legacy legacy;
        int ht_mcs;
        int vht_mcs;
        int he_mcs;
    } u;
};

struct osw_ap_mode {
    /* Rate values need extra work: they need
     * memory management code and comparison code
     * to be fixed in state, confsync, and conf
     * logic. Keep them commented out until
     * there's time to do them right.

    struct osw_rateset_legacy supported_rates;
    struct osw_rateset_legacy basic_rates;
    struct osw_beacon_rate beacon_rate;
    */
    bool wnm_bss_trans;
    bool rrm_neighbor_report;
    bool wmm_enabled;
    bool wmm_uapsd_enabled;
    bool ht_enabled;
    bool ht_required;
    bool vht_enabled;
    bool vht_required;
    bool he_enabled;
    bool he_required;
    bool wps;
};

struct osw_psk {
    char str[64 + 1]; /** 8-63 bytes: passphrase, 64 bytes: hex psk */
};

struct osw_ap_psk {
    int key_id;
    struct osw_psk psk;
};

struct osw_net {
    struct osw_ssid ssid;
    struct osw_wpa wpa;
    struct osw_psk psk;
};

struct osw_neigh {
    struct osw_hwaddr bssid;
    uint32_t bssid_info;
    uint8_t op_class;
    uint8_t channel;
    uint8_t phy_type;
};

struct osw_radius {
    char *server;
    char *passphrase;
    int port;
};

struct osw_hwaddr_list {
    struct osw_hwaddr *list;
    size_t count;
};

struct osw_ap_psk_list {
    struct osw_ap_psk *list;
    size_t count;
};

struct osw_radius_list {
    struct osw_radius *list;
    size_t count;
};

struct osw_net_list {
    struct osw_net *list;
    size_t count;
};

struct osw_neigh_list {
    struct osw_neigh *list;
    size_t count;
};

const char *
osw_pmf_to_str(enum osw_pmf pmf);

const char *
osw_vif_type_to_str(enum osw_vif_type t);

const char *
osw_acl_policy_to_str(enum osw_acl_policy p);

const char *
osw_channel_width_to_str(enum osw_channel_width w);

const char *
osw_channel_dfs_state_to_str(enum osw_channel_state_dfs s);

const char *
osw_radar_to_str(enum osw_radar_detect r);

const char *
osw_band_to_str(enum osw_band b);

void
osw_wpa_to_str(char *out, size_t len, const struct osw_wpa *wpa);

void
osw_ap_mode_to_str(char *out, size_t len, const struct osw_ap_mode *mode);

enum osw_band
osw_freq_to_band(const int freq);

enum osw_band
osw_channel_to_band(const struct osw_channel *channel);

int
osw_freq_to_chan(const int freq);

int
osw_channel_width_to_mhz(const enum osw_channel_width w);

bool
osw_channel_width_down(enum osw_channel_width *w);

const int *
osw_channel_sidebands(enum osw_band band, int chan, int width, int max_2g_chan);

int
osw_channel_ht40_offset(const struct osw_channel *c, int max_2g_chan);

int
osw_chan_to_freq(enum osw_band band, int chan);

void
osw_channel_compute_center_freq(struct osw_channel *c, int max_2g_chan);

int
osw_hwaddr_cmp(const struct osw_hwaddr *addr_a,
               const struct osw_hwaddr *addr_b);

static inline int
osw_channel_nf_20mhz_fixup(const int nf)
{
    const int white_noise_dbm = -174; /* 10*log10(1.38*10^-23*290*10^3) */
    const int noise_20mhz_db = 73; /* 10*log10(20*10^6) */
    const int mds_dbm = white_noise_dbm + noise_20mhz_db; /* = -101 */
    const int front_end_att_db = 5; /* rule of thumb, typically 4-10dB */
    const int def_nf = mds_dbm + front_end_att_db; /* = -96 */

    if (nf < 0 && nf > mds_dbm) return nf;
    return def_nf;
}

bool
osw_channel_from_channel_num_width(uint8_t channel_num,
                                   enum osw_channel_width width,
                                   struct osw_channel *channel);

bool
osw_channel_from_op_class(uint8_t op_class,
                          uint8_t channel_num,
                          struct osw_channel *channel);

bool
osw_channel_to_op_class(const struct osw_channel *channel,
                        uint8_t *op_class);

bool
osw_op_class_to_20mhz(uint8_t op_class,
                      uint8_t chan_num,
                      uint8_t *op_class_20mhz);

enum osw_band
osw_op_class_to_band(uint8_t op_class);

bool
osw_freq_is_dfs(int freq_mhz);

bool
osw_channel_overlaps_dfs(const struct osw_channel *c);

const char *
osw_reg_dfs_to_str(enum osw_reg_dfs dfs);

void
osw_hwaddr_list_to_str(char *out,
                       size_t len,
                       const struct osw_hwaddr_list *acl);

void
osw_ap_psk_list_to_str(char *out,
                       size_t len,
                       const struct osw_ap_psk_list *psk);

bool
osw_ap_psk_is_same(const struct osw_ap_psk *a,
                   const struct osw_ap_psk *b);

int
osw_cs_get_max_2g_chan(const struct osw_channel_state *channel_states,
                       size_t n_channel_states);

bool
osw_cs_chan_intersects_state(const struct osw_channel_state *channel_states,
                             size_t n_channel_states,
                             const struct osw_channel *c,
                             enum osw_channel_state_dfs state);

bool
osw_cs_chan_is_valid(const struct osw_channel_state *channel_states,
                     size_t n_channel_states,
                     const struct osw_channel *c);

bool
osw_cs_chan_is_usable(const struct osw_channel_state *channel_states,
                      size_t n_channel_states,
                      const struct osw_channel *c);

#endif /* OSW_TYPES_H_INCLUDED */
