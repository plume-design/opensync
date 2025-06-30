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
#include <inttypes.h>

#define OSW_NEIGH_FT_FMT OSW_HWADDR_FMT"/id=%s/keylen=%zu"
#define OSW_NEIGH_FT_ARG(n) OSW_HWADDR_ARG(&(n)->bssid), (n)->nas_identifier.buf, strlen((n)->ft_encr_key.buf)

enum osw_vif_status {
    OSW_VIF_UNKNOWN,
    OSW_VIF_DISABLED,
    OSW_VIF_ENABLED,
    OSW_VIF_BROKEN,
};

void
osw_vif_status_set(enum osw_vif_status *status,
                   enum osw_vif_status new_status);

const char *
osw_vif_status_into_cstr(enum osw_vif_status status);

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
    OSW_CHANNEL_320MHZ,
};

enum osw_pmf {
    OSW_PMF_DISABLED,
    OSW_PMF_OPTIONAL,
    OSW_PMF_REQUIRED,
};

#define OSW_SUITE(a, b, c, d) \
    (((a & 0xff) << 24) | \
     ((b & 0xff) << 16) | \
     ((c & 0xff) <<  8) | \
     ((d & 0xff) <<  0))

#define OSW_SUITE_AKM_RSN_EAP             OSW_SUITE(0x00, 0x0f, 0xac, 1)
#define OSW_SUITE_AKM_RSN_PSK             OSW_SUITE(0x00, 0x0f, 0xac, 2)
#define OSW_SUITE_AKM_RSN_FT_EAP          OSW_SUITE(0x00, 0x0f, 0xac, 3)
#define OSW_SUITE_AKM_RSN_FT_PSK          OSW_SUITE(0x00, 0x0f, 0xac, 4)
#define OSW_SUITE_AKM_RSN_EAP_SHA256      OSW_SUITE(0x00, 0x0f, 0xac, 5)
#define OSW_SUITE_AKM_RSN_PSK_SHA256      OSW_SUITE(0x00, 0x0f, 0xac, 6)
#define OSW_SUITE_AKM_RSN_SAE             OSW_SUITE(0x00, 0x0f, 0xac, 8)
#define OSW_SUITE_AKM_RSN_FT_SAE          OSW_SUITE(0x00, 0x0f, 0xac, 9)
#define OSW_SUITE_AKM_RSN_EAP_SUITE_B     OSW_SUITE(0x00, 0x0f, 0xac, 11)
#define OSW_SUITE_AKM_RSN_EAP_SUITE_B_192 OSW_SUITE(0x00, 0x0f, 0xac, 12)
#define OSW_SUITE_AKM_RSN_FT_EAP_SHA384   OSW_SUITE(0x00, 0x0f, 0xac, 13)
#define OSW_SUITE_AKM_RSN_FT_PSK_SHA384   OSW_SUITE(0x00, 0x0f, 0xac, 19)
#define OSW_SUITE_AKM_RSN_PSK_SHA384      OSW_SUITE(0x00, 0x0f, 0xac, 20)
#define OSW_SUITE_AKM_RSN_EAP_SHA384      OSW_SUITE(0x00, 0x0f, 0xac, 23)
#define OSW_SUITE_AKM_RSN_SAE_EXT         OSW_SUITE(0x00, 0x0f, 0xac, 24)
#define OSW_SUITE_AKM_RSN_FT_SAE_EXT      OSW_SUITE(0x00, 0x0f, 0xac, 25)

#define OSW_SUITE_AKM_WPA_NONE            OSW_SUITE(0x00, 0x50, 0xf2, 0)
#define OSW_SUITE_AKM_WPA_8021X           OSW_SUITE(0x00, 0x50, 0xf2, 1)
#define OSW_SUITE_AKM_WPA_PSK             OSW_SUITE(0x00, 0x50, 0xf2, 2)

#define OSW_SUITE_AKM_WFA_DPP             OSW_SUITE(0x50, 0x6f, 0x9a, 2)

#define OSW_SUITE_CIPHER_RSN_NONE         OSW_SUITE(0x00, 0x0f, 0xac, 0)
#define OSW_SUITE_CIPHER_RSN_WEP_40       OSW_SUITE(0x00, 0x0f, 0xac, 1)
#define OSW_SUITE_CIPHER_RSN_TKIP         OSW_SUITE(0x00, 0x0f, 0xac, 2)
#define OSW_SUITE_CIPHER_RSN_CCMP_128     OSW_SUITE(0x00, 0x0f, 0xac, 4)
#define OSW_SUITE_CIPHER_RSN_WEP_104      OSW_SUITE(0x00, 0x0f, 0xac, 5)
#define OSW_SUITE_CIPHER_RSN_BIP_CMAC_128 OSW_SUITE(0x00, 0x0f, 0xac, 6)
#define OSW_SUITE_CIPHER_RSN_GCMP_128     OSW_SUITE(0x00, 0x0f, 0xac, 8)
#define OSW_SUITE_CIPHER_RSN_GCMP_256     OSW_SUITE(0x00, 0x0f, 0xac, 9)
#define OSW_SUITE_CIPHER_RSN_CCMP_256     OSW_SUITE(0x00, 0x0f, 0xac, 10)
#define OSW_SUITE_CIPHER_RSN_BIP_GMAC_128 OSW_SUITE(0x00, 0x0f, 0xac, 11)
#define OSW_SUITE_CIPHER_RSN_BIP_GMAC_256 OSW_SUITE(0x00, 0x0f, 0xac, 12)
#define OSW_SUITE_CIPHER_RSN_BIP_CMAC_256 OSW_SUITE(0x00, 0x0f, 0xac, 13)

#define OSW_SUITE_CIPHER_WPA_NONE         OSW_SUITE(0x00, 0x50, 0xf2, 0)
#define OSW_SUITE_CIPHER_WPA_WEP_40       OSW_SUITE(0x00, 0x50, 0xf2, 1)
#define OSW_SUITE_CIPHER_WPA_TKIP         OSW_SUITE(0x00, 0x50, 0xf2, 2)
#define OSW_SUITE_CIPHER_WPA_CCMP         OSW_SUITE(0x00, 0x50, 0xf2, 4)
#define OSW_SUITE_CIPHER_WPA_WEP_104      OSW_SUITE(0x00, 0x50, 0xf2, 5)

enum osw_akm {
    OSW_AKM_UNSPEC,

    OSW_AKM_RSN_EAP, /* 00-0f-ac-1 */
    OSW_AKM_RSN_PSK, /* 00-0f-ac-2 */
    OSW_AKM_RSN_FT_EAP, /* 00-0f-ac-3 */
    OSW_AKM_RSN_FT_PSK, /* 00-0f-ac-4 */
    OSW_AKM_RSN_EAP_SHA256, /* 00-0f-ac-5 */
    OSW_AKM_RSN_PSK_SHA256, /* 00-0f-ac-6 */
    OSW_AKM_RSN_SAE, /* 00-0f-ac-8 */
    OSW_AKM_RSN_FT_SAE, /* 00-0f-ac-9 */
    OSW_AKM_RSN_EAP_SUITE_B, /* 00-0f-ac-11 */
    OSW_AKM_RSN_EAP_SUITE_B_192, /* 00-0f-ac-12 */
    OSW_AKM_RSN_FT_EAP_SHA384, /* 00-0f-ac-13 */
    OSW_AKM_RSN_FT_PSK_SHA384, /* 00-0f-ac-19 */
    OSW_AKM_RSN_PSK_SHA384, /* 00-0f-ac-20 */
    OSW_AKM_RSN_EAP_SHA384, /* 00-0f-ac-23 */
    OSW_AKM_RSN_SAE_EXT, /* 00-0f-ac-24 */
    OSW_AKM_RSN_FT_SAE_EXT, /* 00-0f-ac-25 */

    OSW_AKM_WPA_NONE, /* 00-50-f2-0 */
    OSW_AKM_WPA_8021X, /* 00-50-f2-1 */
    OSW_AKM_WPA_PSK, /* 00-50-f2-2 */

    OSW_AKM_WFA_DPP, /* 50-6f-9a-2 */
};

enum osw_cipher {
    OSW_CIPHER_UNSPEC,

    OSW_CIPHER_RSN_NONE, /* 00-0f-ac-0 */
    OSW_CIPHER_RSN_WEP_40, /* 00-0f-ac-1 */
    OSW_CIPHER_RSN_TKIP, /* 00-0f-ac-2 */
    OSW_CIPHER_RSN_CCMP_128, /* 00-0f-ac-4 */
    OSW_CIPHER_RSN_WEP_104, /* 00-0f-ac-5 */
    OSW_CIPHER_RSN_BIP_CMAC_128, /* 00-0f-ac-6 */
    OSW_CIPHER_RSN_GCMP_128, /* 00-0f-ac-8 */
    OSW_CIPHER_RSN_GCMP_256, /* 00-0f-ac-9 */
    OSW_CIPHER_RSN_CCMP_256, /* 00-0f-ac-10 */
    OSW_CIPHER_RSN_BIP_GMAC_128, /* 00-0f-ac-11 */
    OSW_CIPHER_RSN_BIP_GMAC_256, /* 00-0f-ac-12 */
    OSW_CIPHER_RSN_BIP_CMAC_256, /* 00-0f-ac-13 */

    OSW_CIPHER_WPA_NONE, /* 00-50-f2-0 */
    OSW_CIPHER_WPA_WEP_40, /* 00-50-f2-1 */
    OSW_CIPHER_WPA_TKIP, /* 00-50-f2-2 */
    OSW_CIPHER_WPA_CCMP, /* 00-50-f2-4 */
    OSW_CIPHER_WPA_WEP_104, /* 00-50-f2-5 */
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
    uint16_t puncture_bitmap;
};

enum osw_reg_dfs {
    OSW_REG_DFS_UNDEFINED,
    OSW_REG_DFS_FCC,
    OSW_REG_DFS_ETSI,
};

enum osw_mbss_vif_ap_mode {
    OSW_MBSS_NONE,
    OSW_MBSS_TX_VAP,
    OSW_MBSS_NON_TX_VAP,
};

enum osw_sta_cell_cap {
    OSW_STA_CELL_UNKNOWN,
    OSW_STA_CELL_AVAILABLE,
    OSW_STA_CELL_NOT_AVAILABLE,
};

struct osw_reg_domain {
    char ccode[3]; /* 2-letter ISO name, \0-terminated */
    int iso3166_num;
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

#define OSW_CHANNEL_FMT "%d (%s/%d %d) 0x%"PRIx16
#define OSW_CHANNEL_ARG(c) \
    (c)->control_freq_mhz, \
    osw_channel_width_to_str((c)->width), \
    (c)->center_freq0_mhz, \
    (c)->center_freq1_mhz, \
    (c)->puncture_bitmap

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
} __attribute__((packed));

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

static inline const struct osw_hwaddr *
osw_hwaddr_from_cptr_unchecked(const void *ptr)
{
    return ptr;
}

static inline const struct osw_hwaddr *
osw_hwaddr_from_cptr(const void *ptr, size_t len)
{
    const struct osw_hwaddr *addr = ptr;
    if (len < sizeof(*addr)) {
        return NULL;
    }
    else {
        return addr;
    }
}

#define OSW_IEEE80211_SSID_LEN 32

struct osw_ifname {
    char buf[32];
};

int
osw_ifname_cmp(const struct osw_ifname *a,
               const struct osw_ifname *b);

bool
osw_ifname_is_equal(const struct osw_ifname *a,
                    const struct osw_ifname *b);

bool
osw_ifname_is_valid(const struct osw_ifname *a);

#define OSW_IFNAME_LEN sizeof(struct osw_ifname)
#define OSW_IFNAME_FMT "%.*s"
#define OSW_IFNAME_ARG(x) (int)OSW_IFNAME_LEN, (x)->buf

#define OSW_NAS_ID_LEN 48
#define OSW_NAS_ID_FMT "%.*s"
#define OSW_NAS_ID_ARG(x) (int)OSW_NAS_ID_LEN, (x)->buf

#define OSW_FT_ENCR_KEY_LEN 64
#define OSW_FT_ENCR_KEY_FMT "%.*s"
#define OSW_FT_ENCR_KEY_ARG(x) (int)OSW_FT_ENCR_KEY_LEN, (x)->buf

struct osw_nas_id {
    char buf[OSW_NAS_ID_LEN + 1];
};

struct osw_ft_encr_key {
    char buf[OSW_FT_ENCR_KEY_LEN + 1];
};

struct osw_ssid {
    char buf[OSW_IEEE80211_SSID_LEN + 1];
    size_t len;
};

struct osw_ssid_hex {
    char buf[OSW_IEEE80211_SSID_LEN * 2 + 1];
};

#define OSW_SSID_FMT "%.*s (len=%zu)"
#define OSW_SSID_ARG(x) (int)(x)->len, (x)->buf, (x)->len

static inline int
osw_ssid_cmp(const struct osw_ssid *a, const struct osw_ssid *b)
{
    if (a->len != b->len) {
        return a->len - b->len;
    }
    else {
        return memcmp(a->buf, b->buf, a->len);
    }
}

const char *
osw_ssid_into_hex(struct osw_ssid_hex *hex,
                  const struct osw_ssid *ssid);

bool
osw_ssid_from_cbuf(struct osw_ssid *ssid,
                   const void *buf,
                   size_t len);

#define OSW_RADIUS_FMT "(%s:%d)(psk=<secret>:len=%zu)"
#define OSW_RADIUS_ARG(x) (x)->server, (x)->port, strlen((x)->passphrase ?: "")

struct osw_radius {
    char *server;
    char *passphrase;
    int port;
};

struct osw_radius_list {
    struct osw_radius *list;
    size_t count;
};

void
osw_radius_list_free(struct osw_radius_list *l);

void
osw_radius_list_purge(struct osw_radius_list *l);

int
osw_radius_cmp(const struct osw_radius *a,
               const struct osw_radius *b);

bool
osw_radius_is_equal(const struct osw_radius *a,
                    const struct osw_radius *b);

bool
osw_radius_list_is_equal(const struct osw_radius_list *a,
                         const struct osw_radius_list *b);

void
osw_radius_list_copy(const struct osw_radius_list *src,
                     struct osw_radius_list *dst);

void
osw_radius_list_to_str(char *out,
                       size_t len,
                       const struct osw_radius_list *radii);

struct osw_osu_provider {
    char *server_uri;
    char *fname;
    char *osu_nai;
    char *osu_osen_nai;
    int *method_list;
    size_t method_list_len;
    char  *osu_service_desc;
};

struct osw_passpoint {
    bool hs20_enabled;
    bool adv_wan_status;
    bool adv_wan_symmetric;
    bool adv_wan_at_capacity;
    bool osen;
    bool asra;  /* Additional step required for access */
    int ant;   /* Access network type */
    int venue_group;
    int venue_type;
    int anqp_domain_id;
    int pps_mo_id;
    int t_c_timestamp;
    char *t_c_filename;
    char *anqp_elem;

    struct osw_hwaddr hessid;
    struct osw_ssid osu_ssid;

    char **domain_list;
    size_t domain_list_len;

    char **nairealm_list;
    size_t nairealm_list_len;

    char **roamc_list;
    size_t roamc_list_len;

    char **oper_fname_list;
    size_t oper_fname_list_len;

    char **venue_name_list;
    size_t venue_name_list_len;

    char **venue_url_list;
    size_t venue_url_list_len;

    char **list_3gpp_list;
    size_t list_3gpp_list_len;

    int *net_auth_type_list;
    size_t net_auth_type_list_len;
    /* FIXME */
    //struct osw_osu_provider_list osu_list;
    //size_t osu_list_len;
};

#define OSW_PASSPOINT_ADV_WAN_STATUS 0x01
#define OSW_PASSPOINT_ADV_WAN_SYMMETRIC 0x04
#define OSW_PASSPOINT_ADV_WAN_AT_CAP 0x08

void
osw_passpoint_to_str(char *out,
                     size_t len,
                     const char *ref_id);

void
osw_passpoint_free_internal(struct osw_passpoint *p);

void
osw_passpoint_copy(const struct osw_passpoint *src, struct osw_passpoint *dst);

bool
osw_passpoint_is_equal(const struct osw_passpoint *a, const struct osw_passpoint *b);

bool
osw_passpoint_str_list_is_equal(char **const a, char **const b,
                                const size_t a_len, const size_t b_len);

#define OSW_WPA_GROUP_REKEY_UNDEFINED -1

struct osw_wpa {
    bool wpa;
    bool rsn;
    bool akm_eap;
    bool akm_eap_sha256;
    bool akm_eap_sha384;
    bool akm_eap_suite_b;
    bool akm_eap_suite_b192;
    bool akm_psk;
    bool akm_psk_sha256;
    bool akm_sae;
    bool akm_sae_ext;
    bool akm_ft_eap;
    bool akm_ft_eap_sha384;
    bool akm_ft_psk;
    bool akm_ft_sae;
    bool akm_ft_sae_ext;
    bool pairwise_tkip;
    bool pairwise_ccmp;
    bool pairwise_ccmp256;
    bool pairwise_gcmp;
    bool pairwise_gcmp256;
    enum osw_pmf pmf;
    bool beacon_protection;
    int group_rekey_seconds;
};

enum osw_rate_legacy {
    OSW_RATE_UNSPEC,

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

    OSW_RATE_COUNT,
};

#define OSW_RATE_MASK (1 << (OSW_RATE_OFDM_54_MBPS + 1)) - 1

#define OSW_RATES_FMT "%04hx"
#define OSW_RATES_ARG(x) ((uint16_t)(x))

int
osw_rate_legacy_to_halfmbps(enum osw_rate_legacy rate);

enum osw_rate_legacy
osw_rate_legacy_from_halfmbps(int halfmbps);

static inline uint16_t
osw_rate_legacy_bit(enum osw_rate_legacy rate)
{
    return 1 << rate;
}

static inline bool
osw_rate_is_invalid(enum osw_rate_legacy rate)
{
    if (rate == OSW_RATE_UNSPEC) return true;
    if (rate >= OSW_RATE_COUNT) return true;
    return false;
}

static inline bool
osw_rate_is_valid(enum osw_rate_legacy rate)
{
    return osw_rate_is_invalid(rate) == false;
}

static inline uint16_t
osw_rate_legacy_cck(void)
{
    return 0
        | osw_rate_legacy_bit(OSW_RATE_CCK_1_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_CCK_2_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_CCK_5_5_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_CCK_11_MBPS)
        ;
}

static inline uint16_t
osw_rate_legacy_cck_basic(void)
{
    return 0
        | osw_rate_legacy_bit(OSW_RATE_CCK_1_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_CCK_2_MBPS)
        ;
}

static inline uint16_t
osw_rate_legacy_ofdm(void)
{
    return 0
        | osw_rate_legacy_bit(OSW_RATE_OFDM_6_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_OFDM_9_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_OFDM_12_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_OFDM_18_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_OFDM_24_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_OFDM_36_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_OFDM_48_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_OFDM_54_MBPS)
        ;
}

static inline uint16_t
osw_rate_legacy_ofdm_basic(void)
{
    return 0
        | osw_rate_legacy_bit(OSW_RATE_OFDM_6_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_OFDM_9_MBPS)
        | osw_rate_legacy_bit(OSW_RATE_OFDM_12_MBPS)
        ;
}

enum osw_beacon_rate_type {
    OSW_BEACON_RATE_UNSPEC,
    OSW_BEACON_RATE_ABG,
    OSW_BEACON_RATE_HT,
    OSW_BEACON_RATE_VHT,
    OSW_BEACON_RATE_HE,
};

const char *
osw_beacon_rate_type_to_str(enum osw_beacon_rate_type type);

struct osw_beacon_rate {
    enum osw_beacon_rate_type type;
    union {
        enum osw_rate_legacy legacy;
        int ht_mcs;
        int vht_mcs;
        int he_mcs;
    } u;
};

#define OSW_BEACON_RATE_FMT "%s / %d %s"
#define OSW_BEACON_RATE_ARG(x) osw_beacon_rate_type_to_str((x)->type), \
                               ((x)->type == OSW_BEACON_RATE_ABG \
                                ? (osw_rate_legacy_to_halfmbps((x)->u.legacy) / 2) \
                                : (x)->u.ht_mcs), \
                               ((x)->type == OSW_BEACON_RATE_ABG \
                                ? "mbps" \
                                : "mcs")

const struct osw_beacon_rate *
osw_beacon_rate_cck(void);

const struct osw_beacon_rate *
osw_beacon_rate_ofdm(void);

struct osw_ap_mode {
    uint16_t supported_rates; /* osw_rate_legacy_bit */
    uint16_t basic_rates; /* osw_rate_legacy_bit */
    struct osw_beacon_rate beacon_rate;
    enum osw_rate_legacy mcast_rate;
    enum osw_rate_legacy mgmt_rate;
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
    bool eht_enabled;
    bool eht_required;
    bool wps;
};

struct osw_multi_ap {
    bool fronthaul_bss;
    bool backhaul_bss;
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

/* FIXME: needs to have osw_ssid added */
struct osw_neigh {
    struct osw_hwaddr bssid;
    uint32_t bssid_info;
    uint8_t op_class;
    uint8_t channel;
    uint8_t phy_type;
};

struct osw_neigh_ft {
    struct osw_hwaddr bssid;
    struct osw_ft_encr_key ft_encr_key;
    struct osw_nas_id nas_identifier;
};

struct osw_wps_cred {
    struct osw_psk psk;
};

struct osw_hwaddr_list {
    struct osw_hwaddr *list;
    size_t count;
};

struct osw_ap_psk_list {
    struct osw_ap_psk *list;
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

struct osw_neigh_ft_list {
    struct osw_neigh_ft *list;
    size_t count;
};

struct osw_wps_cred_list {
    struct osw_wps_cred *list;
    size_t count;
};

void
osw_wps_cred_list_to_str(char *out,
                         size_t len,
                         const struct osw_wps_cred_list *creds);

size_t
osw_wps_cred_list_count_matches(const struct osw_wps_cred_list *a,
                                const struct osw_wps_cred_list *b);

bool
osw_wps_cred_list_is_same(const struct osw_wps_cred_list *a,
                          const struct osw_wps_cred_list *b);

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

char *
osw_multi_ap_into_str(const struct osw_multi_ap *map);

enum osw_band
osw_freq_to_band(const int freq);

int
osw_channel_cmp(const struct osw_channel *a,
                const struct osw_channel *b);

bool
osw_channel_is_equal(const struct osw_channel *a,
                     const struct osw_channel *b);

bool
osw_channel_is_subchannel(const struct osw_channel *a,
                          const struct osw_channel *b);

enum osw_band
osw_channel_to_band(const struct osw_channel *channel);

/* Some channels on 5GHz and 6GHz overlap. No way to tell,
 * hence _guess() suffix. This is intended to be used where
 * the caller _knows_ the channel cannot be 6GHz - then it
 * is safe to assume the result of this is correct.
 */
enum osw_band
osw_chan_to_band_guess(const int chan);

int
osw_freq_to_chan(const int freq);

int
osw_channel_width_to_mhz(const enum osw_channel_width w);

enum osw_channel_width
osw_channel_width_mhz_to_width(const int w);

bool
osw_channel_width_down(enum osw_channel_width *w);

bool
osw_channel_downgrade(struct osw_channel *c);

bool
osw_channel_downgrade_to(struct osw_channel *c,
                         enum osw_channel_width w);

enum osw_channel_width
osw_channel_width_min(const enum osw_channel_width a,
                      const enum osw_channel_width b);

const int *
osw_channel_sidebands(enum osw_band band, int chan, int width, int max_2g_chan);

size_t
osw_channel_20mhz_segments(const struct osw_channel *c,
                           int *segments,
                           size_t segments_len);

int
osw_channel_ht40_offset(const struct osw_channel *c);

int
osw_chan_to_freq(enum osw_band band, int chan);

int
osw_chan_avg(const int *chans);

// Last resort, it's not recommended to use
void
osw_channel_compute_center_freq(struct osw_channel *c, int max_2g_chan);

int
osw_hwaddr_cmp(const struct osw_hwaddr *addr_a,
               const struct osw_hwaddr *addr_b);

bool
osw_hwaddr_is_equal(const struct osw_hwaddr *a,
                    const struct osw_hwaddr *b);

bool
osw_hwaddr_is_zero(const struct osw_hwaddr *addr);

const struct osw_hwaddr *
osw_hwaddr_first_nonzero(const struct osw_hwaddr *a,
                         const struct osw_hwaddr *b);

bool
osw_hwaddr_is_bcast(const struct osw_hwaddr *addr);

bool
osw_hwaddr_is_to_addr(const struct osw_hwaddr *da,
                      const struct osw_hwaddr *self);

bool
osw_hwaddr_list_contains(const struct osw_hwaddr *array,
                         size_t len,
                         const struct osw_hwaddr *addr);

bool
osw_hwaddr_list_is_equal(const struct osw_hwaddr_list *a,
                         const struct osw_hwaddr_list *b);

void
osw_hwaddr_list_append(struct osw_hwaddr_list *list,
                       const struct osw_hwaddr *addr);

void
osw_hwaddr_list_flush(struct osw_hwaddr_list *list);

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
osw_channel_from_channel_num_center_freq_width_op_class(uint8_t channel_num,
                                                        uint8_t center_freq_channel_num,
                                                        enum osw_channel_width width,
                                                        uint8_t op_class,
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

int *
osw_op_class_to_freqs(uint8_t op_class);

bool
osw_freq_is_dfs(int freq_mhz);

bool
osw_channel_overlaps_dfs(const struct osw_channel *c);

void osw_channel_state_get_min_max(const struct osw_channel_state *channel_states,
                                   const int n_channel_states,
                                   int *min_channel_number,
                                   int *max_channel_number);

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

void
osw_neigh_list_to_str(char *out,
                      size_t len,
                      const struct osw_neigh_list *neigh);

int
osw_neigh_ft_cmp(const struct osw_neigh_ft *a,
                 const struct osw_neigh_ft *b);

int
osw_neigh_ft_list_cmp(const struct osw_neigh_ft_list *a,
                      const struct osw_neigh_ft_list *b);

const struct osw_neigh_ft *
osw_neigh_ft_list_lookup(const struct osw_neigh_ft_list *l,
                         const struct osw_hwaddr *bssid);

const char *
osw_mbss_vif_ap_mode_to_str(enum osw_mbss_vif_ap_mode mbss_mode);

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

struct osw_channel_state *
osw_cs_chan_get_segments_states(const struct osw_channel_state *channel_states,
                                   size_t n_channel_states,
                                   const struct osw_channel *c,
                                   size_t *n_states);

bool
osw_cs_chan_is_valid(const struct osw_channel_state *channel_states,
                     size_t n_channel_states,
                     const struct osw_channel *c);

bool
osw_cs_chan_is_usable(const struct osw_channel_state *channel_states,
                      size_t n_channel_states,
                      const struct osw_channel *c);

bool
osw_cs_chan_is_control_dfs(const struct osw_channel_state *cs,
                           size_t n_cs,
                           const struct osw_channel *c);

enum osw_band
osw_cs_chan_get_band(const struct osw_channel_state *cs, const int n_cs);

uint32_t
osw_suite_from_dash_str(const char *str);

void
osw_suite_into_dash_str(char *buf, size_t buf_size, uint32_t suite);

enum osw_akm
osw_suite_into_akm(const uint32_t s);

uint32_t
osw_suite_from_akm(const enum osw_akm akm);

enum osw_cipher
osw_suite_into_cipher(const uint32_t s);

uint32_t
osw_suite_from_cipher(const enum osw_cipher cipher);

const char *
osw_akm_into_cstr(const enum osw_akm akm);

const char *
osw_cipher_into_cstr(const enum osw_cipher cipher);

const char *
osw_band_into_cstr(const enum osw_band band);

bool
osw_channel_is_none(const struct osw_channel *c);

const struct osw_channel *
osw_channel_none(void);

const struct osw_hwaddr *
osw_hwaddr_zero(void);

void
osw_hwaddr_write(const struct osw_hwaddr *addr,
                 void *buf,
                 size_t buf_len);

int
osw_nas_id_cmp(const struct osw_nas_id *a,
               const struct osw_nas_id *b);

bool
osw_nas_id_is_equal(const struct osw_nas_id *a,
                    const struct osw_nas_id *b);

int
osw_ft_encr_key_cmp(const struct osw_ft_encr_key *a,
                    const struct osw_ft_encr_key *b);

bool
osw_ft_encr_key_is_equal(const struct osw_ft_encr_key *a,
                         const struct osw_ft_encr_key *b);

bool
osw_wpa_is_ft(const struct osw_wpa *wpa);

const char *osw_sta_cell_cap_to_cstr(enum osw_sta_cell_cap cap);

#define OSW_HWADDR_WRITE(addr, buf) osw_hwaddr_write(addr, buf, sizeof(buf))

#endif /* OSW_TYPES_H_INCLUDED */
