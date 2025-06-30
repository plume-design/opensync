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

#include <stdint.h>
#include <ev.h>
#include <osw_types.h>
#include <osw_ut.h>
#include <const.h>
#include <util.h>
#include <log.h>

struct osw_op_class_matrix {
    int op_class;
    int start_freq_mhz;
    enum osw_channel_width width;
    int ctrl_chan[64];
    int center_chan_idx[32];
    int flags;
};

#define OSW_OP_CLASS_END -1
#define OSW_CHANNEL_END -2
#define OSW_CH_LOWER (1 << 0)
#define OSW_CH_UPPER (1 << 1)

/*
 * FIXME
 * The OSW_CHANNEL_WILDCARD is a temporary hack because I don't know how verify
 * channel for Operating Classes that don't have Channel Set defined in spec.
 */
#define OSW_CHANNEL_WILDCARD -3

/*
 * Combines "Table E-4—Global operating classes" from:
 * - IEEE Std 802.11-2020
 * - IEEE Std 802.11ax-2021
 */
static const struct osw_op_class_matrix g_op_class_matrix[] = {
    /* 2.4 GHz */
    { 81, 2407, OSW_CHANNEL_20MHZ, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, OSW_CHANNEL_END },
                                   { OSW_CHANNEL_END }, 0, },
    { 82, 2414, OSW_CHANNEL_20MHZ, { 14, OSW_CHANNEL_END },
                                   { OSW_CHANNEL_END }, 0, },
    { 83, 2407, OSW_CHANNEL_40MHZ, { 1, 2, 3, 4, 5, 6, 7, 8, 9, OSW_CHANNEL_END },
                                   { OSW_CHANNEL_END }, OSW_CH_LOWER },
    { 84, 2407, OSW_CHANNEL_40MHZ, { 5, 6, 7, 8, 9, 10, 11, 12, 13, OSW_CHANNEL_END },
                                   { OSW_CHANNEL_END }, OSW_CH_UPPER },
    /* 5 GHz */
    { 115, 5000, OSW_CHANNEL_20MHZ, { 36, 40, 44, 48, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, 0 },
    { 116, 5000, OSW_CHANNEL_40MHZ, { 36, 44, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, OSW_CH_LOWER },
    { 117, 5000, OSW_CHANNEL_40MHZ, { 40, 48, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, OSW_CH_UPPER },
    { 118, 5000, OSW_CHANNEL_20MHZ, { 52, 56, 60, 64, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, 0 },
    { 119, 5000, OSW_CHANNEL_40MHZ, { 52, 60, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, OSW_CH_LOWER },
    { 120, 5000, OSW_CHANNEL_40MHZ, { 56, 64, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, OSW_CH_UPPER },
    { 121, 5000, OSW_CHANNEL_20MHZ, { 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, 0, },
    { 122, 5000, OSW_CHANNEL_40MHZ, { 100, 108, 116, 124, 132, 140, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, OSW_CH_LOWER },
    { 123, 5000, OSW_CHANNEL_40MHZ, { 104, 112, 120, 128, 136, 144, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, OSW_CH_UPPER },
    { 124, 5000, OSW_CHANNEL_20MHZ, { 149, 153, 157, 161, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, 0, },
    { 125, 5000, OSW_CHANNEL_20MHZ, { 149, 153, 157, 161, 165, 169, 173, 177, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, 0, },
    { 126, 5000, OSW_CHANNEL_40MHZ, { 149, 157, 165, 173, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, OSW_CH_LOWER },
    { 127, 5000, OSW_CHANNEL_40MHZ, { 153, 161, 169, 177, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, OSW_CH_UPPER },
    { 128, 5000, OSW_CHANNEL_80MHZ, { OSW_CHANNEL_WILDCARD, OSW_CHANNEL_END },
                                    { 42, 58, 106, 122, 138, 155, 171, OSW_CHANNEL_END }, 0 },
    { 129, 5000, OSW_CHANNEL_160MHZ, { OSW_CHANNEL_WILDCARD, OSW_CHANNEL_END },
                                     { 50, 114, 163, OSW_CHANNEL_END }, 0, },
    { 130, 5000, OSW_CHANNEL_80P80MHZ, { OSW_CHANNEL_WILDCARD, OSW_CHANNEL_END, 0 },
                                       { 42, 58, 106, 122, 138, 155, 171, OSW_CHANNEL_END }, 0, },
    /* 6 GHz */
    { 131, 5950, OSW_CHANNEL_20MHZ, { 1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45,
                                      49, 53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 93,
                                      97, 101, 105, 109, 113, 117, 121, 125, 129, 133,
                                      137, 141, 145, 149, 153, 157, 161, 165, 169, 173,
                                      177, 181, 185, 189, 193, 197, 201, 205, 209, 213,
                                      217, 221, 225, 229, 233, OSW_CHANNEL_END, 0 },
                                    { OSW_CHANNEL_END }, 0, },
    { 132, 5950, OSW_CHANNEL_40MHZ, { OSW_CHANNEL_WILDCARD, OSW_CHANNEL_END },
                                    { 3, 11, 19, 27, 35, 43, 51, 59, 67, 75, 83, 91, 99,
                                      107, 115, 123, 131, 139, 147, 155, 163, 171, 179,
                                      187, 195, 203, 211, 219, 227, OSW_CHANNEL_END }, 0, },
    { 133, 5950, OSW_CHANNEL_80MHZ, { OSW_CHANNEL_WILDCARD, OSW_CHANNEL_END },
                                    { 7, 23, 39, 55, 71, 87, 103, 119, 135, 151, 167, 83,
                                      199, 215, OSW_CHANNEL_END }, 0, },
    { 134, 5950, OSW_CHANNEL_160MHZ, { OSW_CHANNEL_WILDCARD, OSW_CHANNEL_END },
                                     { 15, 47, 79, 111, 143, 175, 207, OSW_CHANNEL_END }, 0, },
    { 135, 5950, OSW_CHANNEL_80P80MHZ, { OSW_CHANNEL_WILDCARD, OSW_CHANNEL_END },
                                       { 7, 23, 39, 55, 71, 87, 103, 119, 135, 151, 167, 83,
                                         199, 215, OSW_CHANNEL_END }, 0, },
    { 136, 5925, OSW_CHANNEL_20MHZ, { 2, OSW_CHANNEL_END },
                                    { OSW_CHANNEL_END }, 0, },
    { 137, 5950, OSW_CHANNEL_320MHZ, { OSW_CHANNEL_WILDCARD, OSW_CHANNEL_END },
                                     { 31, 63, 95, 127, 159, 191, OSW_CHANNEL_END }, 0, },

    /* End */
    { OSW_OP_CLASS_END, 0, 0, { OSW_CHANNEL_END, }, { OSW_CHANNEL_END }, 0 },
};

static int
osw_channel_width_offsets(const enum osw_channel_width w)
{
    switch (w) {
        case OSW_CHANNEL_20MHZ: return 0;
        case OSW_CHANNEL_40MHZ: return 2;
        case OSW_CHANNEL_80MHZ: return 6;
        case OSW_CHANNEL_160MHZ: return 14;
        case OSW_CHANNEL_80P80MHZ: return 6; /* refers to the center0 */
        case OSW_CHANNEL_320MHZ: return 30;
    }
    return false;
}

static void
strip_trailing_whitespace(char *str)
{
    char *p;
    while ((p = strrchr(str, ' ')) != NULL)
        *p = '\0';
}

int
osw_ifname_cmp(const struct osw_ifname *a,
               const struct osw_ifname *b)
{
    assert(a != NULL);
    assert(b != NULL);

    const size_t max_len = sizeof(a->buf);
    WARN_ON(strnlen(a->buf, max_len) == max_len);
    WARN_ON(strnlen(b->buf, max_len) == max_len);
    return strncmp(a->buf, b->buf, max_len);
}

bool
osw_ifname_is_equal(const struct osw_ifname *a,
                    const struct osw_ifname *b)
{
    return (osw_ifname_cmp(a, b) == 0);
}

bool
osw_ifname_is_valid(const struct osw_ifname *a)
{
    const size_t len = strnlen(a->buf, OSW_IFNAME_LEN);
    const bool too_small = (len == 0);
    const bool too_big = (len == OSW_IFNAME_LEN);
    const bool impossible = (len > OSW_IFNAME_LEN);
    if (too_small) return false;
    if (too_big) return false;
    if (WARN_ON(impossible)) return false;
    return true;
}

void
osw_wps_cred_list_to_str(char *out,
                         size_t len,
                         const struct osw_wps_cred_list *creds)
{
    size_t i;

    out[0] = 0;
    for (i = 0; i < creds->count; i++) {
        const struct osw_wps_cred *p = &creds->list[i];
        csnprintf(&out, &len,
                  "len=%u,",
                  strlen(p->psk.str));
    }

    if (creds->count > 0 && out[-1] == ',')
        out[-1] = 0;
}

size_t
osw_wps_cred_list_count_matches(const struct osw_wps_cred_list *a,
                                const struct osw_wps_cred_list *b)
{
    size_t n_matches = 0;
    size_t i;
    for (i = 0; i < a->count; i++) {
        const char *p = a->list[i].psk.str;
        size_t j;
        for (j = 0; j < b->count; j++) {
            const char *q = b->list[j].psk.str;
            const bool same = (strcmp(p, q) == 0);
            if (same) n_matches++;
        }
    }
    return n_matches;
}

bool
osw_wps_cred_list_is_same(const struct osw_wps_cred_list *a,
                          const struct osw_wps_cred_list *b)
{
    return a->count == b->count
        && a->count == osw_wps_cred_list_count_matches(a, b);
}

const char *
osw_pmf_to_str(enum osw_pmf pmf)
{
    switch (pmf) {
        case OSW_PMF_DISABLED: return "disabled";
        case OSW_PMF_OPTIONAL: return "optional";
        case OSW_PMF_REQUIRED: return "required";
    }
    return "";
}
const char *
osw_vif_type_to_str(enum osw_vif_type t)
{
    switch (t) {
        case OSW_VIF_UNDEFINED: return "undefined";
        case OSW_VIF_AP: return "ap";
        case OSW_VIF_AP_VLAN: return "ap_vlan";
        case OSW_VIF_STA: return "sta";
    }
    return "";
}

const char *
osw_acl_policy_to_str(enum osw_acl_policy p)
{
    switch (p) {
        case OSW_ACL_NONE: return "none";
        case OSW_ACL_ALLOW_LIST: return "allow";
        case OSW_ACL_DENY_LIST: return "deny";
    }
    return "";
}

const char *
osw_channel_width_to_str(enum osw_channel_width w)
{
    switch (w) {
        case OSW_CHANNEL_20MHZ: return "20";
        case OSW_CHANNEL_40MHZ: return "40";
        case OSW_CHANNEL_80MHZ: return "80";
        case OSW_CHANNEL_160MHZ: return "160";
        case OSW_CHANNEL_80P80MHZ: return "80p80";
        case OSW_CHANNEL_320MHZ: return "320";
    }
    return "";
}

const char *
osw_channel_dfs_state_to_str(enum osw_channel_state_dfs s)
{
    switch (s) {
        case OSW_CHANNEL_NON_DFS: return "non_dfs";
        case OSW_CHANNEL_DFS_CAC_POSSIBLE: return "cac_possible";
        case OSW_CHANNEL_DFS_CAC_IN_PROGRESS: return "cac_in_progress";
        case OSW_CHANNEL_DFS_CAC_COMPLETED: return "cac_completed";
        case OSW_CHANNEL_DFS_NOL: return "nol";
    }
    return "";
}

const char *
osw_radar_to_str(enum osw_radar_detect r)
{
    switch (r) {
        case OSW_RADAR_UNSUPPORTED: return "unsupported";
        case OSW_RADAR_DETECT_ENABLED: return "enabled";
        case OSW_RADAR_DETECT_DISABLED: return "disabled";
    }
    return "";
}

const char *
osw_band_to_str(enum osw_band b)
{
    switch (b) {
        case OSW_BAND_UNDEFINED: return "undefined";
        case OSW_BAND_2GHZ: return "2ghz";
        case OSW_BAND_5GHZ: return "5ghz";
        case OSW_BAND_6GHZ: return "6ghz";
    }
    return "";
}

void
osw_wpa_to_str(char *out, size_t len, const struct osw_wpa *wpa)
{
    out[0] = 0;
    csnprintf(&out, &len, " wpa=");
    if (wpa->wpa) csnprintf(&out, &len, "wpa ");
    if (wpa->rsn) csnprintf(&out, &len, "rsn ");
    if (wpa->pairwise_tkip) csnprintf(&out, &len, "tkip ");
    if (wpa->pairwise_ccmp) csnprintf(&out, &len, "ccmp ");
    if (wpa->pairwise_ccmp256) csnprintf(&out, &len, "ccmp256 ");
    if (wpa->pairwise_gcmp) csnprintf(&out, &len, "gcmp ");
    if (wpa->pairwise_gcmp256) csnprintf(&out, &len, "gcmp256 ");
    if (wpa->akm_psk) csnprintf(&out, &len, "psk ");
    if (wpa->akm_psk_sha256) csnprintf(&out, &len, "psk_sha256 ");
    if (wpa->akm_sae) csnprintf(&out, &len, "sae ");
    if (wpa->akm_sae_ext) csnprintf(&out, &len, "sae_ext ");
    if (wpa->akm_ft_psk) csnprintf(&out, &len, "ft_psk ");
    if (wpa->akm_ft_sae) csnprintf(&out, &len, "ft_sae ");
    if (wpa->akm_ft_sae_ext) csnprintf(&out, &len, "ft_sae_ext ");
    if (wpa->akm_eap) csnprintf(&out, &len, "eap ");
    if (wpa->akm_eap_sha256) csnprintf(&out, &len, "eap_sha256 ");
    if (wpa->akm_eap_sha384) csnprintf(&out, &len, "eap_sha384 ");
    if (wpa->akm_eap_suite_b) csnprintf(&out, &len, "eap_suite_b ");
    if (wpa->akm_eap_suite_b192) csnprintf(&out, &len, "eap_suite_b192 ");
    if (wpa->akm_ft_eap) csnprintf(&out, &len, "ft_eap ");
    if (wpa->akm_ft_eap_sha384) csnprintf(&out, &len, "ft_eap_sha384 ");
    csnprintf(&out, &len, "pmf=%s ", osw_pmf_to_str(wpa->pmf));
    if (wpa->beacon_protection) csnprintf(&out, &len, "b_prot ");
    csnprintf(&out, &len, "gtk=%d ", wpa->group_rekey_seconds);
    strip_trailing_whitespace(out);
}

void
osw_ap_mode_to_str(char *out, size_t len, const struct osw_ap_mode *mode)
{
    out[0] = 0;
    if (mode->wnm_bss_trans) csnprintf(&out, &len, "btm ");
    if (mode->rrm_neighbor_report) csnprintf(&out, &len, "rrm ");
    if (mode->wmm_enabled) csnprintf(&out, &len, "wmm ");
    if (mode->wmm_uapsd_enabled) csnprintf(&out, &len, "uapsd ");
    if (mode->ht_enabled) csnprintf(&out, &len, "ht ");
    if (mode->ht_required) csnprintf(&out, &len, "htR ");
    if (mode->vht_enabled) csnprintf(&out, &len, "vht ");
    if (mode->vht_required) csnprintf(&out, &len, "vhtR ");
    if (mode->he_enabled) csnprintf(&out, &len, "he ");
    if (mode->he_required) csnprintf(&out, &len, "heR ");
    if (mode->eht_enabled) csnprintf(&out, &len, "eht ");
    if (mode->eht_required) csnprintf(&out, &len, "ehtR ");
    if (mode->wps) csnprintf(&out, &len, "wps ");
    if (mode->supported_rates) csnprintf(&out, &len, "supp:" OSW_RATES_FMT " ", OSW_RATES_ARG(mode->supported_rates));
    if (mode->basic_rates) csnprintf(&out, &len, "basic:" OSW_RATES_FMT " ", OSW_RATES_ARG(mode->basic_rates));
    if (mode->beacon_rate.type != OSW_BEACON_RATE_UNSPEC) csnprintf(&out, &len, "bcn:"OSW_BEACON_RATE_FMT " ", OSW_BEACON_RATE_ARG(&mode->beacon_rate));
    if (mode->mgmt_rate != OSW_RATE_UNSPEC) csnprintf(&out, &len, "mgmt:%d kbps ", osw_rate_legacy_to_halfmbps(mode->mgmt_rate) * 500);
    if (mode->mcast_rate != OSW_RATE_UNSPEC) csnprintf(&out, &len, "mcast:%d kbps ", osw_rate_legacy_to_halfmbps(mode->mcast_rate) * 500);
    strip_trailing_whitespace(out);
}

char *
osw_multi_ap_into_str(const struct osw_multi_ap *map)
{
    char *out = MALLOC(1);
    out[0] = 0;
    if (map->fronthaul_bss) strgrow(&out, "fronthaul-bss,");
    if (map->backhaul_bss) strgrow(&out, "backhaul-bss,");
    const size_t len = strlen(out);
    if (len > 0) {
        const size_t last_comma = len - 1;
        out[last_comma] = '\0';
    }
    return out;
}

enum osw_band
osw_freq_to_band(const int freq)
{
    const int b2ch1 = 2412;
    const int b2ch13 = 2472;
    const int b2ch14 = 2484;
    const int b5ch36 = 5180;
    const int b5ch177 = 5885;
    const int b6ch1 = 5955;
    const int b6ch2 = 5935;
    const int b6ch233 = 7115;

    if (freq == b2ch14)
        return OSW_BAND_2GHZ;
    if (freq == b6ch2)
        return OSW_BAND_6GHZ;
    if (freq >= b2ch1 && freq <= b2ch13)
        return OSW_BAND_2GHZ;
    if (freq >= b5ch36 && freq <= b5ch177)
        return OSW_BAND_5GHZ;
    if (freq >= b6ch1 && freq <= b6ch233)
        return OSW_BAND_6GHZ;

    return OSW_BAND_UNDEFINED;
}

int
osw_channel_cmp(const struct osw_channel *a,
                const struct osw_channel *b)
{
    return memcmp(a, b, sizeof(*a));
}

bool
osw_channel_is_equal(const struct osw_channel *a,
                     const struct osw_channel *b)
{
    return ((a == NULL) && (b == NULL))
       ||  ((a != NULL) &&
            (b != NULL) &&
            (a->control_freq_mhz == b->control_freq_mhz) &&
            (a->center_freq0_mhz == b->center_freq0_mhz) &&
            (a->center_freq1_mhz == b->center_freq1_mhz) &&
            (a->width == b->width));
}

bool
osw_channel_is_subchannel(const struct osw_channel *narrower,
                          const struct osw_channel *wider)
{
    return (narrower != NULL) && (wider != NULL) &&
           (abs(narrower->control_freq_mhz - wider->center_freq0_mhz) < (osw_channel_width_to_mhz(wider->width) / 2));
}

enum osw_channel_width
osw_channel_width_mhz_to_width(const int w)
{
    switch (w)
    {
    case 320:
        return OSW_CHANNEL_320MHZ;
    case 160:
        return OSW_CHANNEL_160MHZ;
    case 80:
        return OSW_CHANNEL_80MHZ;
    case 40:
        return OSW_CHANNEL_40MHZ;
    case 20:
        return OSW_CHANNEL_20MHZ;
    default:
        return OSW_CHANNEL_20MHZ;
    }
}

enum osw_band
osw_channel_to_band(const struct osw_channel *channel)
{
    assert(channel != NULL);
    return osw_freq_to_band(channel->control_freq_mhz);
}

enum osw_band
osw_chan_to_band_guess(const int chan)
{
    if (chan >= 1 && chan <= 14) return OSW_BAND_2GHZ;
    if (chan >= 36 && chan <= 177) return OSW_BAND_5GHZ;
    return OSW_BAND_UNDEFINED;
}

int
osw_freq_to_chan(const int freq)
{
    const int b2ch1 = 2412;
    const int b2ch13 = 2472;
    const int b2ch14 = 2484;
    const int b5ch36 = 5180;
    const int b5ch177 = 5885;
    const int b6ch1 = 5955;
    const int b6ch2 = 5935;
    const int b6ch233 = 7115;

    if (freq == b2ch14)
        return 14;
    if (freq == b6ch2)
        return 2;
    if (freq >= b2ch1 && freq <= b2ch13)
        return (freq - 2407) / 5;
    if (freq >= b5ch36 && freq <= b5ch177)
        return (freq - 5000) / 5;
    if (freq >= b6ch1 && freq <= b6ch233)
        return (freq - 5950) / 5;

    return 0;
}

int
osw_channel_width_to_mhz(const enum osw_channel_width w)
{
    switch (w) {
        case OSW_CHANNEL_20MHZ: return 20;
        case OSW_CHANNEL_40MHZ: return 40;
        case OSW_CHANNEL_80MHZ: return 80;
        case OSW_CHANNEL_160MHZ: return 160;
        case OSW_CHANNEL_80P80MHZ: return 0; /* N/A */
        case OSW_CHANNEL_320MHZ: return 320;
    }
    return 0;
}

bool
osw_channel_width_down(enum osw_channel_width *w)
{
    switch (*w) {
        case OSW_CHANNEL_20MHZ:
            return false;
        case OSW_CHANNEL_40MHZ:
            *w = OSW_CHANNEL_20MHZ;
            return true;
        case OSW_CHANNEL_80MHZ:
            *w = OSW_CHANNEL_40MHZ;
            return true;
        case OSW_CHANNEL_160MHZ:
            *w = OSW_CHANNEL_80MHZ;
            return true;
        case OSW_CHANNEL_80P80MHZ:
            *w = OSW_CHANNEL_80MHZ;
            return true;
        case OSW_CHANNEL_320MHZ:
            *w = OSW_CHANNEL_160MHZ;
            return true;
    }
    return false;
}

bool
osw_channel_downgrade(struct osw_channel *c)
{
    enum osw_channel_width w = c->width;
    if (WARN_ON(w == OSW_CHANNEL_80P80MHZ)) return false;
    if (osw_channel_width_down(&w) == false) return false;

    const int offset_old = osw_channel_width_offsets(c->width) * 5;
    const int offset_new = osw_channel_width_offsets(w) * 5;
    const int freq_leftmost = c->center_freq0_mhz - offset_old;
    const int freq_rightmost = c->center_freq0_mhz + offset_old;
    const int center_new = (c->control_freq_mhz < c->center_freq0_mhz)
                         ? freq_leftmost + offset_new
                         : freq_rightmost - offset_new;
    const int n_segments_new = ((freq_rightmost - freq_leftmost + 20) / 20) / 2;
    const int segments_mask = ((1 << n_segments_new) - 1);
    const int segments_shift = (c->control_freq_mhz < c->center_freq0_mhz)
                             ? 0
                             : n_segments_new;
    const uint16_t puncture_new = ((c->puncture_bitmap >> segments_shift) & segments_mask);
    c->width = w;
    c->center_freq0_mhz = center_new;
    c->puncture_bitmap = puncture_new;
    return true;
}

bool
osw_channel_downgrade_to(struct osw_channel *c,
                         enum osw_channel_width w)
{
    if (c->width < w) return false;
    while (c->width != w) {
        const bool ok = osw_channel_downgrade(c);
        const bool not_ok = (ok == false);
        if (not_ok) return false;
    }
    return true;
}

enum osw_channel_width
osw_channel_width_min(const enum osw_channel_width a,
                      const enum osw_channel_width b)
{
    if (a < b) return a;
    else return b;
}

static const int *
osw_2g_chan_list(int chan, int width, int max_chan)
{
    static const int lists[] = {
        20, 1, 0,
        20, 2, 0,
        20, 3, 0,
        20, 4, 0,
        20, 5, 0,
        20, 6, 0,
        20, 7, 0,
        20, 8, 0,
        20, 9, 0,
        20, 10, 0,
        20, 11, 0,
        20, 12, 0,
        20, 13, 0,
        40, 9, 13, 0,
        40, 8, 12, 0,
        40, 7, 11, 0,
        40, 6, 10, 0,
        40, 5, 9, 0,
        40, 4, 8, 0,
        40, 3, 7, 0,
        40, 2, 6, 0,
        40, 1, 5, 0,
        -1,
    };
    const int *start;
    const int *p;

    for (p = lists; *p != -1; p++) {
        if (*p == width) {
            bool out_of_range = false;
            bool found = false;
            for (start = ++p; *p; p++) {
                if (*p == chan) found = true;
                if (*p > max_chan) out_of_range = true;
            }
            if (found == true && out_of_range == false) {
                return start;
            }
        }
    }

    return NULL;
}

const int *
osw_channel_sidebands(enum osw_band band, int chan, int width, int max_2g_chan)
{
    /* It doesn't make sense to fix all the callers of this
     * function as it's a convenience helper for places
     * where code is oblivious, or has inherently no access
     * to sideband locations. Code expecting to support
     * 320MHz must be aware of the sidebands already so no
     * need to change this.
     */
    WARN_ON(width > 160);
    static int empty[] = { 0 };
    switch (band) {
        case OSW_BAND_UNDEFINED: return empty;
        case OSW_BAND_2GHZ: return osw_2g_chan_list(chan, width, max_2g_chan);
        case OSW_BAND_5GHZ: return unii_5g_chan2list(chan, width);
        case OSW_BAND_6GHZ: return unii_6g_chan2list(chan, width);
    }
    return empty;
}

static size_t
osw_channel_20mhz_segments_raw(const int center_freq,
                               const int num_segments,
                               int *segments,
                               size_t segments_len)
{
    const int segment_width = 20;
    const int leftmost = center_freq + (segment_width / 2) - (5 * 4 * num_segments / 2);
    const int rightmost = center_freq - (segment_width / 2) + (5 * 4 * num_segments / 2);
    int freq;
    size_t n = 0;
    for (freq = leftmost; freq <= rightmost; freq += segment_width) {
        if (segments_len < 1) return 0;
        *segments++ = freq;
        segments_len--;
        n++;
    }
    return n;
}

size_t
osw_channel_20mhz_segments(const struct osw_channel *c,
                           int *segments,
                           size_t segments_len)
{
    if (c->control_freq_mhz == 0) return 0;

    ASSERT(c->center_freq0_mhz != 0, "center freq required");

    /* FIXME: This could do sanity check to verify control +
     * center + width make sense at all. */

    struct osw_channel cpy = *c;
    osw_channel_compute_center_freq(&cpy, 11);

    switch (c->width) {
        case OSW_CHANNEL_20MHZ:
            return osw_channel_20mhz_segments_raw(cpy.center_freq0_mhz,
                                                  1,
                                                  segments,
                                                  segments_len);
        case OSW_CHANNEL_40MHZ:
            return osw_channel_20mhz_segments_raw(cpy.center_freq0_mhz,
                                                  2,
                                                  segments,
                                                  segments_len);
        case OSW_CHANNEL_80MHZ:
            return osw_channel_20mhz_segments_raw(cpy.center_freq0_mhz,
                                                  4,
                                                  segments,
                                                  segments_len);
        case OSW_CHANNEL_160MHZ:
            return osw_channel_20mhz_segments_raw(cpy.center_freq0_mhz,
                                                  8,
                                                  segments,
                                                  segments_len);
        case OSW_CHANNEL_320MHZ:
            return osw_channel_20mhz_segments_raw(cpy.center_freq0_mhz,
                                                  16,
                                                  segments,
                                                  segments_len);
        case OSW_CHANNEL_80P80MHZ:
            {
                const size_t n = osw_channel_20mhz_segments_raw(cpy.center_freq0_mhz,
                                                                4,
                                                                segments,
                                                                segments_len);
                const size_t m = osw_channel_20mhz_segments_raw(cpy.center_freq1_mhz,
                                                                4,
                                                                segments,
                                                                segments_len);
                if (n == 0) return 0;
                if (m == 0) return 0;
                return n + m;
            }
    }
    return 0;
}

void
osw_channel_20mhz_segments_to_chans(int *segs,
                                    size_t n_segs)
{
    while (n_segs > 0) {
        *segs = osw_freq_to_chan(*segs);
        segs++;
        n_segs--;
    }
}

int
osw_channel_ht40_offset(const struct osw_channel *c)
{
    ASSERT(c->center_freq0_mhz != 0, "center freq required");
    struct osw_channel copy = *c;
    const bool ok = osw_channel_downgrade_to(&copy, OSW_CHANNEL_40MHZ);
    const bool not_ok = (ok == false);
    if (not_ok) return 0;
    if (copy.control_freq_mhz < copy.center_freq0_mhz) return 1;
    if (copy.control_freq_mhz > copy.center_freq0_mhz) return -1;
    return 0;
}

int
osw_chan_to_freq(enum osw_band band, int chan)
{
    switch (band) {
        case OSW_BAND_UNDEFINED: return 0;
        case OSW_BAND_2GHZ: return chan == 14 ? 2484 : (2407 + (chan * 5));
        case OSW_BAND_5GHZ: return 5000 + (chan * 5);
        case OSW_BAND_6GHZ: return chan == 2 ? 5935 : (5950 + (chan * 5));
    }
    return 0;
}

int
osw_chan_avg(const int *chans)
{
    int sum = 0;
    int n = 0;
    while (chans != NULL && *chans != 0) {
        sum += *chans;
        n++;
        chans++;
    }
    if (n == 0) return 0;
    else return sum / n;
}

// Last resort, it's not recommended to use
void
osw_channel_compute_center_freq(struct osw_channel *c, int max_2g_chan)
{
    if (c->center_freq0_mhz != 0) return;
    const enum osw_band b = osw_freq_to_band(c->control_freq_mhz);
    const int w = osw_channel_width_to_mhz(c->width);
    const int cn = osw_freq_to_chan(c->control_freq_mhz);
    const int *chans = osw_channel_sidebands(b, cn, w, max_2g_chan);
    const int avg = osw_chan_avg(chans);
    c->center_freq0_mhz = osw_chan_to_freq(b, avg);
}

int
osw_hwaddr_cmp(const struct osw_hwaddr *addr_a,
               const struct osw_hwaddr *addr_b)
{
    return memcmp(addr_a->octet, addr_b->octet, sizeof(addr_a->octet));
}

bool
osw_hwaddr_is_equal(const struct osw_hwaddr *a,
                    const struct osw_hwaddr *b)
{
    return (osw_hwaddr_cmp(a, b) == 0);
}

bool
osw_hwaddr_is_zero(const struct osw_hwaddr *addr)
{
    static const struct osw_hwaddr zero = {0};
    return osw_hwaddr_is_equal(addr, &zero);
}

const struct osw_hwaddr *
osw_hwaddr_first_nonzero(const struct osw_hwaddr *a,
                         const struct osw_hwaddr *b)
{
    if (osw_hwaddr_is_zero(a) == false) return a;
    if (osw_hwaddr_is_zero(b) == false) return b;
    return NULL;
}

bool
osw_hwaddr_is_bcast(const struct osw_hwaddr *addr)
{
    static const struct osw_hwaddr bcast = { .octet = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } };
    return osw_hwaddr_is_equal(addr, &bcast);
}

bool
osw_hwaddr_is_to_addr(const struct osw_hwaddr *da,
                      const struct osw_hwaddr *self)
{
    if (osw_hwaddr_is_bcast(da)) return true;
    return osw_hwaddr_is_equal(da, self);
}

bool
osw_hwaddr_list_contains(const struct osw_hwaddr *array,
                         size_t len,
                         const struct osw_hwaddr *addr)
{
    while (len > 0) {
        if (osw_hwaddr_is_equal(array, addr)) {
            return true;
        }
        len--;
        array++;
    }
    return false;
}

bool
osw_hwaddr_list_is_equal(const struct osw_hwaddr_list *a,
                         const struct osw_hwaddr_list *b)
{
    if (a->count != b->count) {
        return false;
    }

    size_t i;
    for (i = 0; i < a->count; i++) {
        const struct osw_hwaddr *addr = &a->list[i];
        if (osw_hwaddr_list_contains(b->list, b->count, addr) == false)
            return false;
    }

    return true;
}

void
osw_hwaddr_list_append(struct osw_hwaddr_list *list,
                       const struct osw_hwaddr *addr)
{
    const size_t index = list->count;
    list->count++;
    const size_t elem_size = sizeof(*addr);
    const size_t new_size = (list->count * elem_size);
    list->list = REALLOC(list->list, new_size);
    list->list[index] = *addr;
}

void
osw_hwaddr_list_flush(struct osw_hwaddr_list *list)
{
    if (list == NULL) return;
    FREE(list->list);
    list->list = NULL;
    list->count = 0;
}

static int
osw_channel_width_num_segments(enum osw_channel_width width)
{
    switch (width) {
        case OSW_CHANNEL_20MHZ: return 1;
        case OSW_CHANNEL_40MHZ: return 2;
        case OSW_CHANNEL_80MHZ: return 4;
        case OSW_CHANNEL_160MHZ: return 8;
        case OSW_CHANNEL_320MHZ: return 16;
        case OSW_CHANNEL_80P80MHZ: return 1; /* not supported */
    }
    return 0;
}

static bool
osw_int_in_range(int first, int last, int step, int needle)
{
    for (; first <= last; first += step)
        if (needle == first)
            return true;
    return false;
}

static bool
osw_channel_control_fits_center(int control,
                                int center,
                                enum osw_channel_width width)
{
    /* Example: control=36, center=42, width=80MHz
     *     segments = 4
     *     channels = 4 * 4 = 16
     *     first = 42 - (16/2) + 2 = 42 - 8 + 2 = 36
     *     last = 42 + (16/2) - 2 = 42 + 8 - 2 = 48
     * This virtually maps to the constituent channel list:
     *   36, 40, 44, 48
     * If control channel is found on the list, then it's
     * good.
     */
    const int segments = osw_channel_width_num_segments(width);
    const int channels = segments * 4;
    const int first = center - (channels / 2) + 2;
    const int last = center + (channels / 2) - 2;
    const int step = 4;
    return osw_int_in_range(first, last, step, control);
}

static bool
osw_op_class_entry_center_chan_idx_matches_control(const struct osw_op_class_matrix *entry,
                                                   int control)
{
    const enum osw_channel_width width = entry->width;
    const int *center;
    for (center = entry->center_chan_idx; *center != OSW_CHANNEL_END; center++) {
        if (osw_channel_control_fits_center(control, *center, width)) {
            return true;
        }
    }
    return false;
}

static bool
osw_op_class_entry_ctrl_chan_matches_control(const struct osw_op_class_matrix *entry,
                                             int control)
{
    const int *ctrl_chan;
    for (ctrl_chan = entry->ctrl_chan; *ctrl_chan != OSW_CHANNEL_END; ctrl_chan++) {
        if (*ctrl_chan == control) {
            return true;
        }
    }
    return false;
}

bool
osw_channel_from_channel_num_width(uint8_t channel_num,
                                   enum osw_channel_width width,
                                   struct osw_channel *channel)
{
    assert(channel != NULL);

    const struct osw_op_class_matrix* entry;
    struct osw_channel prev;
    size_t num_matches = 0;

    for (entry = g_op_class_matrix; entry->op_class != OSW_OP_CLASS_END; entry++) {
        if (entry->width != width)
            continue;
        if (entry->width == OSW_CHANNEL_80P80MHZ)
            continue;

        const bool match = osw_op_class_entry_center_chan_idx_matches_control(entry, channel_num)
                        || osw_op_class_entry_ctrl_chan_matches_control(entry, channel_num);
        if (match) {
            num_matches++;
            const bool ok = osw_channel_from_op_class(entry->op_class, channel_num, channel);
            if (WARN_ON(ok == false)) return false;

            const bool mismatch = num_matches > 1
                                ? prev.control_freq_mhz != channel->control_freq_mhz
                                : false;
            if (mismatch) return false;

            memcpy(&prev, channel, sizeof(*channel));
        }
    }

    return (num_matches == 1);
}

bool
osw_channel_from_channel_num_center_freq_width_op_class(uint8_t channel_num,
                                                        uint8_t center_freq_channel_num,
                                                        enum osw_channel_width width,
                                                        uint8_t op_class,
                                                        struct osw_channel *channel)
{
    assert(channel != NULL);
    memset(channel, 0, sizeof(*channel));

    const enum osw_band band = osw_op_class_to_band(op_class);
    if (band == OSW_BAND_UNDEFINED)
        return false;

    channel->width = width;
    channel->control_freq_mhz = osw_chan_to_freq(band, channel_num);
    channel->center_freq0_mhz = osw_chan_to_freq(band, center_freq_channel_num);
    // channel->puncture_bitmap could also be set, but it's not necessary at the moment
    return true;
}

bool
osw_channel_from_op_class(uint8_t op_class,
                          uint8_t channel_num,
                          struct osw_channel *channel)
{
    assert(channel != NULL);
    memset(channel, 0, sizeof(*channel));

    const struct osw_op_class_matrix* entry;
    for (entry = g_op_class_matrix; entry->op_class != OSW_OP_CLASS_END; entry++)
        if (entry->op_class == op_class)
            break;

    if (entry->op_class == OSW_OP_CLASS_END)
        return false;

    const int *ctrl_chan;
    for (ctrl_chan = entry->ctrl_chan; *ctrl_chan != OSW_CHANNEL_END; ctrl_chan++) {
        if (*ctrl_chan == channel_num)
            break;
        if (*ctrl_chan == OSW_CHANNEL_WILDCARD)
            break;
    }

    if (*ctrl_chan == OSW_CHANNEL_END)
        return false;

    int center_num = 0;
    if (*ctrl_chan == OSW_CHANNEL_WILDCARD) {
        const enum osw_channel_width width = entry->width;
        const int width_mhz = osw_channel_width_to_mhz(width);
        const int first_center = entry->center_chan_idx[0];
        const int spacing_mhz = 5;
        const int first_mhz = entry->start_freq_mhz + (first_center * spacing_mhz);
        const enum osw_band band = osw_freq_to_band(first_mhz);
        const int *chans = osw_channel_sidebands(band, channel_num, width_mhz, 13);
        if (chans == NULL)
            return false;

        center_num = chanlist_to_center(chans);

        const int *center_chan_idx;
        for (center_chan_idx = entry->center_chan_idx; *center_chan_idx != OSW_CHANNEL_END; center_chan_idx++) {
            if (*center_chan_idx == center_num)
                break;
        }

        if (*center_chan_idx == OSW_CHANNEL_END)
            return false;
    }

    if (center_num == 0) {
        switch (entry->width) {
            case OSW_CHANNEL_20MHZ:
                center_num = channel_num;
                break;
            case OSW_CHANNEL_40MHZ:
                if (entry->flags & OSW_CH_LOWER) {
                    center_num =  channel_num + 2;
                }
                else if (entry->flags & OSW_CH_UPPER) {
                    center_num = channel_num - 2;
                }
                else {
                    return false;
                }
                break;
            case OSW_CHANNEL_80MHZ:
            case OSW_CHANNEL_80P80MHZ:
            case OSW_CHANNEL_160MHZ:
            case OSW_CHANNEL_320MHZ:
                return false;
        }
    }

    channel->width = entry->width;
    channel->control_freq_mhz = entry->start_freq_mhz + (channel_num * 5);
    channel->center_freq0_mhz = entry->start_freq_mhz + (center_num * 5);

    return true;
}

static const int *
osw_op_class_matrx_chans_scan(const int *chans,
                              const int spacing_mhz,
                              const int base_freq_mhz,
                              const int scan_freq_mhz)
{
    for (; *chans != OSW_CHANNEL_END; chans++) {
        const int cur_freq_mhz = base_freq_mhz + ((*chans) * spacing_mhz);
        if (cur_freq_mhz == scan_freq_mhz)
            return chans;
    }
    return NULL;
}

bool
osw_channel_to_op_class(const struct osw_channel *orig_channel,
                        uint8_t *op_class)
{
    assert(orig_channel != NULL);
    assert(op_class != NULL);

    struct osw_channel copy = *orig_channel;
    struct osw_channel *channel = &copy;

    if (channel->center_freq0_mhz == 0) {
        osw_channel_compute_center_freq(channel, 13);
    }

    const struct osw_op_class_matrix* entry;
    for (entry = g_op_class_matrix; entry->op_class != OSW_OP_CLASS_END; entry++) {
        if (channel->width != entry->width)
            continue;

        const int spacing_mhz = 5;
        const int base_freq_mhz = entry->start_freq_mhz;
        const int *chans = entry->ctrl_chan;
        int scan_freq_mhz = channel->control_freq_mhz;

        if (entry->ctrl_chan[0] == OSW_CHANNEL_WILDCARD) {
            chans = entry->center_chan_idx;
            scan_freq_mhz = channel->center_freq0_mhz;
        }

        if (entry->flags & OSW_CH_LOWER) {
            if (channel->center_freq0_mhz <= channel->control_freq_mhz)
                continue;
        }

        if (entry->flags & OSW_CH_UPPER) {
            if (channel->center_freq0_mhz >= channel->control_freq_mhz)
                continue;
        }

        const int *found = osw_op_class_matrx_chans_scan(chans,
                                                         spacing_mhz,
                                                         base_freq_mhz,
                                                         scan_freq_mhz);
        if (found == NULL)
            continue;

        assert(entry->op_class > 0 && entry->op_class <= UINT8_MAX);
        *op_class = entry->op_class;
        return true;
    }

    return false;
}

bool
osw_op_class_to_20mhz(uint8_t op_class,
                      uint8_t chan_num,
                      uint8_t *op_class_20mhz)
{
    assert(op_class > 0);
    assert(chan_num > 0);
    assert(op_class_20mhz != NULL);

    const struct osw_op_class_matrix* entry;
    for (entry = g_op_class_matrix; entry->op_class != OSW_OP_CLASS_END; entry++) {
        if (entry->op_class != op_class)
            continue;

        if (entry->width == OSW_CHANNEL_20MHZ) {
            const int *ctrl_chan;
            for (ctrl_chan = entry->ctrl_chan; *ctrl_chan != OSW_CHANNEL_END; ctrl_chan++) {
                if (*ctrl_chan == chan_num) {
                    assert(entry->op_class > 0 && entry->op_class <= UINT8_MAX);
                    *op_class_20mhz = entry->op_class;
                    return true;
                }
            }

            return false;
        }

        /*
         * Move backward and look for the closest 20 MHz op_class and check
         * whether it contains given channel.
         */
        const struct osw_op_class_matrix* prev_entry = entry;
        for (prev_entry = entry; prev_entry >= g_op_class_matrix; prev_entry--) {
            if (prev_entry->width != OSW_CHANNEL_20MHZ)
                continue;

            const int *ctrl_chan;
            for (ctrl_chan = prev_entry->ctrl_chan; *ctrl_chan != OSW_CHANNEL_END; ctrl_chan++) {
                if (*ctrl_chan == chan_num) {
                    assert(prev_entry->op_class > 0 && prev_entry->op_class <= UINT8_MAX);
                    *op_class_20mhz = prev_entry->op_class;
                    return true;
                }
            }
        }

        return false;
    }

    return false;
}

enum osw_band
osw_op_class_to_band(uint8_t op_class)
{
    const struct osw_op_class_matrix *entry;
    for (entry = g_op_class_matrix; entry->op_class != OSW_OP_CLASS_END; entry++) {
        if (entry->op_class != op_class)
            continue;

        /* It's fine to fallback to center freq. The
         * osw_freq_to_band() is a smooth function and can
         * infer bands for, eg. channel 42 on 5GHz, even
         * though that's not a valid primary channel.
         */
        const int first_chan = (entry->ctrl_chan[0] != OSW_CHANNEL_WILDCARD)
                             ? (entry->ctrl_chan[0])
                             : (entry->center_chan_idx[0]);
        const int freq = entry->start_freq_mhz + (5 * first_chan);
        return osw_freq_to_band(freq);
    }
    return OSW_BAND_UNDEFINED;
}

int *
osw_op_class_to_freqs(uint8_t op_class)
{
    const struct osw_op_class_matrix *e;
    for (e = g_op_class_matrix; e->op_class != OSW_OP_CLASS_END; e++) {
        if (e->op_class != op_class) continue;

        size_t n = 1;
        int *freqs = MALLOC(n * sizeof(*freqs));
        const int *last = &e->ctrl_chan[ARRAY_SIZE(e->ctrl_chan) - 1];
        const int *c;
        for (c = e->ctrl_chan; *c != OSW_CHANNEL_END && c <= last; c++) {
            const int freq = e->start_freq_mhz + (*c * 5);

            n++;
            freqs = REALLOC(freqs, n * sizeof(*freqs));
            freqs[n - 2] = freq;
        }
        freqs[n - 1] = 0;

        return freqs;
    }
    return NULL;
}

bool
osw_freq_is_dfs(int freq_mhz)
{
    static const int dfs_freqs[] = {
        5260, 5280, 5300, 5320, /* 52-64 */
        5500, 5520, 5540, 5560, /* 100-112 */
        5580, 5600, 5620, 5640, /* 116-128 */
        5660, 5680, 5700, 5720, /* 132-144 */
    };
    size_t i;
    for (i = 0; i < ARRAY_SIZE(dfs_freqs); i++) {
        if (dfs_freqs[i] == freq_mhz)
            return true;
    }
    return false;
}

bool
osw_channel_overlaps_dfs(const struct osw_channel *c)
{
    if (c->control_freq_mhz == 0) return false;
    int segs[16];
    const size_t n_segs = osw_channel_20mhz_segments(c, segs, ARRAY_SIZE(segs));
    WARN_ON(n_segs == 0);
    size_t i;
    for (i = 0; i < n_segs; i++) {
        const int freq = segs[i];
        if (osw_freq_is_dfs(freq) == true) return true;
    }
    return false;
}

void osw_channel_state_get_min_max(const struct osw_channel_state *cs,
                                   const int n_cs,
                                   int *min_chan_n,
                                   int *max_chan_n)
{
    if (n_cs < 1) return;
    *min_chan_n = UINT8_MAX;
    *max_chan_n = 0;
    for (int i = 0; i < n_cs; i++) {
        const int chan_n = osw_freq_to_chan(cs[i].channel.control_freq_mhz);
        *max_chan_n = MAX(chan_n, *max_chan_n);
        *min_chan_n = MIN(chan_n, *min_chan_n);
    }
}

const char *
osw_reg_dfs_to_str(enum osw_reg_dfs dfs)
{
    switch (dfs) {
        case OSW_REG_DFS_UNDEFINED: return "undefined";
        case OSW_REG_DFS_FCC: return "fcc";
        case OSW_REG_DFS_ETSI: return "etsi";
    }
    return "unreachable";
}

const char *
osw_mbss_vif_ap_mode_to_str(enum osw_mbss_vif_ap_mode mbss_mode)
{
    switch (mbss_mode) {
        case OSW_MBSS_NONE: return "none";
        case OSW_MBSS_TX_VAP: return "tx_vap";
        case OSW_MBSS_NON_TX_VAP: return "non_tx_vap";
    }
    return "";
}

void
osw_hwaddr_list_to_str(char *out,
                       size_t len,
                       const struct osw_hwaddr_list *acl)
{
    size_t i;

    out[0] = 0;
    for (i = 0; i < acl->count; i++) {
        const struct osw_hwaddr *addr = &acl->list[i];
        csnprintf(&out, &len, OSW_HWADDR_FMT ",",
                  OSW_HWADDR_ARG(addr));
    }

    if (acl->count > 0 && out[-1] == ',')
        out[-1] = 0;
}

void
osw_ap_psk_list_to_str(char *out,
                       size_t len,
                       const struct osw_ap_psk_list *psk)
{
    size_t i;

    out[0] = 0;
    for (i = 0; i < psk->count; i++) {
        const struct osw_ap_psk *p = &psk->list[i];
        const size_t max = ARRAY_SIZE(p->psk.str);
        csnprintf(&out, &len, "%d:len=%d,",
                  p->key_id,
                  strnlen(p->psk.str, max));
    }

    if (psk->count > 0 && out[-1] == ',')
        out[-1] = 0;
}

int
osw_neigh_ft_cmp(const struct osw_neigh_ft *a,
                 const struct osw_neigh_ft *b)
{
    if (a == NULL && b == NULL) return 0;
    if (a == NULL && b != NULL) return -1;
    if (a != NULL && b == NULL) return 1;

    const int r1 = osw_hwaddr_cmp(&a->bssid, &b->bssid);
    const int r2 = osw_ft_encr_key_cmp(&a->ft_encr_key, &b->ft_encr_key);
    const int r3 = osw_nas_id_cmp(&a->nas_identifier, &b->nas_identifier);
    if (r1) return r1;
    if (r2) return r2;
    if (r3) return r3;
    return 0;
}

int
osw_neigh_ft_list_cmp(const struct osw_neigh_ft_list *a,
                      const struct osw_neigh_ft_list *b)
{
    if (a == NULL && b == NULL) return 0;
    if (a == NULL && b != NULL) return -1;
    if (a != NULL && b == NULL) return 1;

    const int r = (int)a->count - (int)b->count;
    if (r) return r;

    size_t i;
    for (i = 0; i < a->count; i++) {
        const struct osw_neigh_ft *x = &a->list[i];
        const struct osw_neigh_ft *y = osw_neigh_ft_list_lookup(b, &x->bssid);
        const int r = osw_neigh_ft_cmp(x, y);
        if (r) return r;
    }

    return 0;
}

const struct osw_neigh_ft *
osw_neigh_ft_list_lookup(const struct osw_neigh_ft_list *l,
                         const struct osw_hwaddr *bssid)
{
    if (l == NULL) return NULL;
    if (bssid == NULL) return NULL;

    size_t i;
    for (i = 0; i < l->count; i++) {
        const struct osw_neigh_ft *li = &l->list[i];
        if (osw_hwaddr_is_equal(&li->bssid, bssid)) {
            return li;
        }
    }
    return NULL;
}

void
osw_neigh_list_to_str(char *out,
                      size_t len,
                      const struct osw_neigh_list *neigh)
{
    size_t i;

    out[0] = 0;
    for (i = 0; i < neigh->count; i++) {
        const struct osw_neigh *p = &neigh->list[i];
        csnprintf(&out, &len,
                  " "OSW_HWADDR_FMT"/%08x/%u/%u/%u,",
                  OSW_HWADDR_ARG(&p->bssid),
                  p->bssid_info,
                  p->op_class,
                  p->channel,
                  p->phy_type);
    }

    if (neigh->count > 0 && out[-1] == ',')
        out[-1] = 0;
}

bool
osw_ap_psk_is_same(const struct osw_ap_psk *a,
                   const struct osw_ap_psk *b)
{
    const size_t max = ARRAY_SIZE(a->psk.str);

    if (a->key_id != b->key_id) return false;
    if (strncmp(a->psk.str, b->psk.str, max) != 0) return false;

    return true;
}

int
osw_cs_get_max_2g_chan(const struct osw_channel_state *channel_states,
                       size_t n_channel_states)
{
    int max = 0;
    size_t i;
    for (i = 0; channel_states != NULL && i < n_channel_states; i++) {
        const struct osw_channel_state *cs = &channel_states[i];
        const struct osw_channel *c = &cs->channel;
        const int freq = c->control_freq_mhz;
        const enum osw_band band = osw_freq_to_band(freq);
        if (band != OSW_BAND_2GHZ) continue;
        const int chan = osw_freq_to_chan(freq);
        if (max < chan) {
            max = chan;
        }
    }
    return max;
}

/*
 * osw_cs_chan_intersects_state implements the predicate of intersection
 * between channel_states and state.
 *
 * Input: ...
 * Output: True, if each of the 20 MHz segments has dfs_state equal to state.
 */
bool
osw_cs_chan_intersects_state(const struct osw_channel_state *channel_states,
                             size_t n_channel_states,
                             const struct osw_channel *c,
                             enum osw_channel_state_dfs state)
{
    ASSERT(c->center_freq0_mhz != 0, "center freq required");

    int freqs[16];
    size_t n_freqs = osw_channel_20mhz_segments(c, freqs, ARRAY_SIZE(freqs));
    if (n_freqs == 0) return false;

    while (n_freqs > 0) {
        const uint16_t puncture_bit = (1 << (n_freqs - 1));
        const bool punctured = ((c->puncture_bitmap & puncture_bit) != 0);
        const bool not_punctured = (punctured == false);
        size_t i;
        for (i = 0; i < n_channel_states && not_punctured; i++) {
            const struct osw_channel_state *cs = &channel_states[i];
            const struct osw_channel *oc = &cs->channel;
            const int oc_freq = oc->control_freq_mhz;
            if (oc_freq != freqs[n_freqs - 1]) continue;
            if (cs->dfs_state == state) return true;
        }
        n_freqs--;
    }

    return false;
}

/*
 * osw_cs_chan_get_subchannels_states
 *
 * Input: ...
 * Output: An array of osw_channel_state for each of the 20 MHz segments that
 * make up c. Caller must free the output array.
 */
struct osw_channel_state *
osw_cs_chan_get_segments_states(const struct osw_channel_state *channel_states,
                                   size_t n_channel_states,
                                   const struct osw_channel *c,
                                   size_t *n_output)
{
    ASSERT(c->center_freq0_mhz != 0, "center freq required");

    int freqs[16];
    size_t n_freqs = osw_channel_20mhz_segments(c, freqs, ARRAY_SIZE(freqs));
    if (n_freqs == 0) return NULL;

    const size_t old_n_freqs = n_freqs;
    *n_output = 0;
    struct osw_channel_state *output = NULL;
    while (n_freqs > 0) {
        const uint16_t puncture_bit = (1 << (n_freqs - 1));
        const bool punctured = ((c->puncture_bitmap & puncture_bit) != 0);
        const bool not_punctured = (punctured == false);
        size_t i;
        for (i = 0; i < n_channel_states && not_punctured; i++) {
            const struct osw_channel_state *cs = &channel_states[i];
            const struct osw_channel *oc = &cs->channel;
            const int oc_freq = oc->control_freq_mhz;
            if (oc_freq == freqs[n_freqs - 1]) {
                (*n_output)++;
                output = REALLOC(output, (*n_output) * sizeof(*output));
                output[*n_output - 1] = *cs;
            }
        }
        n_freqs--;
    }
    if (*n_output != old_n_freqs) {
        FREE(output);
        output = NULL;
        *n_output = 0;
    }
    return output;
}

bool
osw_cs_chan_is_valid(const struct osw_channel_state *channel_states,
                     size_t n_channel_states,
                     const struct osw_channel *c)
{
    ASSERT(c->center_freq0_mhz != 0, "");

    int freqs[16];
    size_t n_freqs = osw_channel_20mhz_segments(c, freqs, ARRAY_SIZE(freqs));
    if (n_freqs == 0) return false;

    while (n_freqs > 0) {
        size_t i;
        for (i = 0; i < n_channel_states; i++) {
            const struct osw_channel_state *cs = &channel_states[i];
            const struct osw_channel *oc = &cs->channel;
            const int oc_freq = oc->control_freq_mhz;
            if (oc_freq == freqs[n_freqs - 1]) break;
        }
        const bool not_found = (i == n_channel_states);
        if (not_found) return false;
        n_freqs--;
    }

    return true;
}

bool
osw_cs_chan_is_usable(const struct osw_channel_state *cs,
                      size_t n_cs,
                      const struct osw_channel *c)
{
    const enum osw_channel_state_dfs state = OSW_CHANNEL_DFS_NOL;
    const bool valid = osw_cs_chan_is_valid(cs, n_cs, c);
    const bool contains_nol = osw_cs_chan_intersects_state(cs, n_cs, c, state);
    return valid && !contains_nol;
}

bool
osw_cs_chan_is_control_dfs(const struct osw_channel_state *cs,
                           size_t n_cs,
                           const struct osw_channel *c)
{
    while ((cs != NULL) && (n_cs > 0)) {
        if (cs->channel.control_freq_mhz == c->control_freq_mhz) {
            switch (cs->dfs_state) {
                case OSW_CHANNEL_NON_DFS:
                    return false;
                case OSW_CHANNEL_DFS_CAC_POSSIBLE:
                case OSW_CHANNEL_DFS_CAC_IN_PROGRESS:
                case OSW_CHANNEL_DFS_CAC_COMPLETED:
                case OSW_CHANNEL_DFS_NOL:
                    return true;
            }
        }
        cs++;
        n_cs--;
    }
    return false;
}

enum osw_band
osw_cs_chan_get_band(const struct osw_channel_state *cs, const int n_cs)
{
    if (n_cs < 1)
        return OSW_BAND_UNDEFINED;
    enum osw_band prev_band = osw_channel_to_band(&cs[0].channel);
    for (int i = 1; i < n_cs; i++)
    {
        const enum osw_band chan_band = osw_channel_to_band(&cs[i].channel);
        if (prev_band != chan_band)
            return OSW_BAND_UNDEFINED;
    }
    return prev_band;
}

int
osw_rate_legacy_to_halfmbps(enum osw_rate_legacy rate)
{
    switch (rate) {
        case OSW_RATE_CCK_1_MBPS: return 2;
        case OSW_RATE_CCK_2_MBPS: return 4;
        case OSW_RATE_CCK_5_5_MBPS: return 11;
        case OSW_RATE_CCK_11_MBPS: return 22;
        case OSW_RATE_OFDM_6_MBPS: return 12;
        case OSW_RATE_OFDM_9_MBPS: return 18;
        case OSW_RATE_OFDM_12_MBPS: return 24;
        case OSW_RATE_OFDM_18_MBPS: return 36;
        case OSW_RATE_OFDM_24_MBPS: return 48;
        case OSW_RATE_OFDM_36_MBPS: return 72;
        case OSW_RATE_OFDM_48_MBPS: return 96;
        case OSW_RATE_OFDM_54_MBPS: return 108;
        case OSW_RATE_UNSPEC: return 0;
        case OSW_RATE_COUNT: return 0;
    }
    return 0;
}

enum osw_rate_legacy
osw_rate_legacy_from_halfmbps(int halfmbps)
{
    if (halfmbps == 2) return OSW_RATE_CCK_1_MBPS;
    if (halfmbps == 4) return OSW_RATE_CCK_2_MBPS;
    if (halfmbps == 11) return OSW_RATE_CCK_5_5_MBPS;
    if (halfmbps == 22) return OSW_RATE_CCK_11_MBPS;
    if (halfmbps == 12) return OSW_RATE_OFDM_6_MBPS;
    if (halfmbps == 18) return OSW_RATE_OFDM_9_MBPS;
    if (halfmbps == 24) return OSW_RATE_OFDM_12_MBPS;
    if (halfmbps == 36) return OSW_RATE_OFDM_18_MBPS;
    if (halfmbps == 48) return OSW_RATE_OFDM_24_MBPS;
    if (halfmbps == 72) return OSW_RATE_OFDM_36_MBPS;
    if (halfmbps == 96) return OSW_RATE_OFDM_48_MBPS;
    if (halfmbps == 108) return OSW_RATE_OFDM_54_MBPS;

    return OSW_RATE_UNSPEC;
}

const char *
osw_beacon_rate_type_to_str(enum osw_beacon_rate_type type)
{
    switch (type) {
        case OSW_BEACON_RATE_UNSPEC: return "unspec";
        case OSW_BEACON_RATE_ABG: return "abg";
        case OSW_BEACON_RATE_HT: return "ht";
        case OSW_BEACON_RATE_VHT: return "vhT";
        case OSW_BEACON_RATE_HE: return "he";
    }
    return "";
}

const struct osw_beacon_rate *
osw_beacon_rate_cck(void)
{
    static const struct osw_beacon_rate rate = {
        .type = OSW_BEACON_RATE_ABG,
        .u = {
            .legacy = OSW_RATE_CCK_1_MBPS,
        },
    };
    return &rate;
}

const struct osw_beacon_rate *
osw_beacon_rate_ofdm(void)
{
    static const struct osw_beacon_rate rate = {
        .type = OSW_BEACON_RATE_ABG,
        .u = {
            .legacy = OSW_RATE_OFDM_6_MBPS,
        },
    };
    return &rate;
}

uint32_t
osw_suite_from_dash_str(const char *str)
{
    uint8_t b[4] = {0};
    /* eg. "00-50-f2-5" -> b[] = {0x00, 0x50, 0xf2, 5} */
    sscanf(str, "%02"SCNx8"-%02"SCNx8"-%02"SCNx8"-%"SCNu8,
           &b[0], &b[1], &b[2], &b[3]);
    return ((uint32_t)b[0] << 24)
         | ((uint32_t)b[1] << 16)
         | ((uint32_t)b[2] <<  8)
         | ((uint32_t)b[3] <<  0);
}

void
osw_suite_into_dash_str(char *buf, size_t buf_size, uint32_t suite)
{
    const uint8_t b[] = {
        ((suite >> 24) & 0xff),
        ((suite >> 16) & 0xff),
        ((suite >>  8) & 0xff),
        ((suite >>  0) & 0xff),
    };
    snprintf(buf, buf_size,
             "%02"PRIx8"-%02"SCNx8"-%02"SCNx8"-%"SCNu8,
             b[0], b[1], b[2], b[3]);
}

enum osw_akm
osw_suite_into_akm(const uint32_t s)
{
    switch (s) {
        case OSW_SUITE_AKM_RSN_EAP: return OSW_AKM_RSN_EAP;
        case OSW_SUITE_AKM_RSN_PSK: return OSW_AKM_RSN_PSK;
        case OSW_SUITE_AKM_RSN_FT_EAP: return OSW_AKM_RSN_FT_EAP;
        case OSW_SUITE_AKM_RSN_FT_PSK: return OSW_AKM_RSN_FT_PSK;
        case OSW_SUITE_AKM_RSN_EAP_SHA256: return OSW_AKM_RSN_EAP_SHA256;
        case OSW_SUITE_AKM_RSN_EAP_SHA384: return OSW_AKM_RSN_EAP_SHA384;
        case OSW_SUITE_AKM_RSN_PSK_SHA256: return OSW_AKM_RSN_PSK_SHA256;
        case OSW_SUITE_AKM_RSN_SAE: return OSW_AKM_RSN_SAE;
        case OSW_SUITE_AKM_RSN_SAE_EXT: return OSW_AKM_RSN_SAE_EXT;
        case OSW_SUITE_AKM_RSN_FT_SAE: return OSW_AKM_RSN_FT_SAE;
        case OSW_SUITE_AKM_RSN_FT_SAE_EXT: return OSW_AKM_RSN_FT_SAE_EXT;
        case OSW_SUITE_AKM_RSN_EAP_SUITE_B: return OSW_AKM_RSN_EAP_SUITE_B;
        case OSW_SUITE_AKM_RSN_EAP_SUITE_B_192: return OSW_AKM_RSN_EAP_SUITE_B_192;
        case OSW_SUITE_AKM_RSN_FT_EAP_SHA384: return OSW_AKM_RSN_FT_EAP_SHA384;
        case OSW_SUITE_AKM_RSN_FT_PSK_SHA384: return OSW_AKM_RSN_FT_PSK_SHA384;
        case OSW_SUITE_AKM_RSN_PSK_SHA384: return OSW_AKM_RSN_PSK_SHA384;
        case OSW_SUITE_AKM_WPA_NONE: return OSW_AKM_WPA_NONE;
        case OSW_SUITE_AKM_WPA_8021X: return OSW_AKM_WPA_8021X;
        case OSW_SUITE_AKM_WPA_PSK: return OSW_AKM_WPA_PSK;
        case OSW_SUITE_AKM_WFA_DPP: return OSW_AKM_WFA_DPP;
    }

    return OSW_AKM_UNSPEC;
}

uint32_t
osw_suite_from_akm(const enum osw_akm akm)
{
    switch (akm) {
        case OSW_AKM_UNSPEC: return 0;
        case OSW_AKM_RSN_EAP: return OSW_SUITE_AKM_RSN_EAP;
        case OSW_AKM_RSN_PSK: return OSW_SUITE_AKM_RSN_PSK;
        case OSW_AKM_RSN_FT_EAP: return OSW_SUITE_AKM_RSN_FT_EAP;
        case OSW_AKM_RSN_FT_PSK: return OSW_SUITE_AKM_RSN_FT_PSK;
        case OSW_AKM_RSN_EAP_SHA256: return OSW_SUITE_AKM_RSN_EAP_SHA256;
        case OSW_AKM_RSN_EAP_SHA384: return OSW_SUITE_AKM_RSN_EAP_SHA384;
        case OSW_AKM_RSN_PSK_SHA256: return OSW_SUITE_AKM_RSN_PSK_SHA256;
        case OSW_AKM_RSN_SAE: return OSW_SUITE_AKM_RSN_SAE;
        case OSW_AKM_RSN_SAE_EXT: return OSW_SUITE_AKM_RSN_SAE_EXT;
        case OSW_AKM_RSN_FT_SAE: return OSW_SUITE_AKM_RSN_FT_SAE;
        case OSW_AKM_RSN_FT_SAE_EXT: return OSW_SUITE_AKM_RSN_FT_SAE_EXT;
        case OSW_AKM_RSN_EAP_SUITE_B: return OSW_SUITE_AKM_RSN_EAP_SUITE_B;
        case OSW_AKM_RSN_EAP_SUITE_B_192: return OSW_SUITE_AKM_RSN_EAP_SUITE_B_192;
        case OSW_AKM_RSN_FT_EAP_SHA384: return OSW_SUITE_AKM_RSN_FT_EAP_SHA384;
        case OSW_AKM_RSN_FT_PSK_SHA384: return OSW_SUITE_AKM_RSN_FT_PSK_SHA384;
        case OSW_AKM_RSN_PSK_SHA384: return OSW_SUITE_AKM_RSN_PSK_SHA384;
        case OSW_AKM_WPA_NONE: return OSW_SUITE_AKM_WPA_NONE;
        case OSW_AKM_WPA_8021X: return OSW_SUITE_AKM_WPA_8021X;
        case OSW_AKM_WPA_PSK: return OSW_SUITE_AKM_WPA_PSK;
        case OSW_AKM_WFA_DPP: return OSW_SUITE_AKM_WFA_DPP;
    }
    return 0;
}

enum osw_cipher
osw_suite_into_cipher(const uint32_t s)
{
    switch (s) {
        case OSW_SUITE_CIPHER_RSN_NONE: return OSW_CIPHER_RSN_NONE;
        case OSW_SUITE_CIPHER_RSN_WEP_40: return OSW_CIPHER_RSN_WEP_40;
        case OSW_SUITE_CIPHER_RSN_TKIP: return OSW_CIPHER_RSN_TKIP;
        case OSW_SUITE_CIPHER_RSN_CCMP_128: return OSW_CIPHER_RSN_CCMP_128;
        case OSW_SUITE_CIPHER_RSN_WEP_104: return OSW_CIPHER_RSN_WEP_104;
        case OSW_SUITE_CIPHER_RSN_BIP_CMAC_128: return OSW_CIPHER_RSN_BIP_CMAC_128;
        case OSW_SUITE_CIPHER_RSN_GCMP_128: return OSW_CIPHER_RSN_GCMP_128;
        case OSW_SUITE_CIPHER_RSN_GCMP_256: return OSW_CIPHER_RSN_GCMP_256;
        case OSW_SUITE_CIPHER_RSN_CCMP_256: return OSW_CIPHER_RSN_CCMP_256;
        case OSW_SUITE_CIPHER_RSN_BIP_GMAC_128: return OSW_CIPHER_RSN_BIP_GMAC_128;
        case OSW_SUITE_CIPHER_RSN_BIP_GMAC_256: return OSW_CIPHER_RSN_BIP_GMAC_256;
        case OSW_SUITE_CIPHER_RSN_BIP_CMAC_256: return OSW_CIPHER_RSN_BIP_CMAC_256;
        case OSW_SUITE_CIPHER_WPA_NONE: return OSW_CIPHER_WPA_NONE;
        case OSW_SUITE_CIPHER_WPA_WEP_40: return OSW_CIPHER_WPA_WEP_40;
        case OSW_SUITE_CIPHER_WPA_TKIP: return OSW_CIPHER_WPA_TKIP;
        case OSW_SUITE_CIPHER_WPA_CCMP: return OSW_CIPHER_WPA_CCMP;
        case OSW_SUITE_CIPHER_WPA_WEP_104: return OSW_CIPHER_WPA_WEP_104;
    }
    return OSW_CIPHER_UNSPEC;
}

uint32_t
osw_suite_from_cipher(const enum osw_cipher cipher)
{
    switch (cipher) {
        case OSW_CIPHER_UNSPEC: return 0;
        case OSW_CIPHER_RSN_NONE: return OSW_SUITE_CIPHER_RSN_NONE;
        case OSW_CIPHER_RSN_WEP_40: return OSW_SUITE_CIPHER_RSN_WEP_40;
        case OSW_CIPHER_RSN_TKIP: return OSW_SUITE_CIPHER_RSN_TKIP;
        case OSW_CIPHER_RSN_CCMP_128: return OSW_SUITE_CIPHER_RSN_CCMP_128;
        case OSW_CIPHER_RSN_WEP_104: return OSW_SUITE_CIPHER_RSN_WEP_104;
        case OSW_CIPHER_RSN_BIP_CMAC_128: return OSW_SUITE_CIPHER_RSN_BIP_CMAC_128;
        case OSW_CIPHER_RSN_GCMP_128: return OSW_SUITE_CIPHER_RSN_GCMP_128;
        case OSW_CIPHER_RSN_GCMP_256: return OSW_SUITE_CIPHER_RSN_GCMP_256;
        case OSW_CIPHER_RSN_CCMP_256: return OSW_SUITE_CIPHER_RSN_CCMP_256;
        case OSW_CIPHER_RSN_BIP_GMAC_128: return OSW_SUITE_CIPHER_RSN_BIP_GMAC_128;
        case OSW_CIPHER_RSN_BIP_GMAC_256: return OSW_SUITE_CIPHER_RSN_BIP_GMAC_256;
        case OSW_CIPHER_RSN_BIP_CMAC_256: return OSW_SUITE_CIPHER_RSN_BIP_CMAC_256;
        case OSW_CIPHER_WPA_NONE: return OSW_SUITE_CIPHER_WPA_NONE;
        case OSW_CIPHER_WPA_WEP_40: return OSW_SUITE_CIPHER_WPA_WEP_40;
        case OSW_CIPHER_WPA_TKIP: return OSW_SUITE_CIPHER_WPA_TKIP;
        case OSW_CIPHER_WPA_CCMP: return OSW_SUITE_CIPHER_WPA_CCMP;
        case OSW_CIPHER_WPA_WEP_104: return OSW_SUITE_CIPHER_WPA_WEP_104;
    }
    return 0;
}

const char *
osw_akm_into_cstr(const enum osw_akm akm)
{
    switch (akm) {
        case OSW_AKM_UNSPEC: return "unspec";
        case OSW_AKM_RSN_EAP: return "rsn-eap";
        case OSW_AKM_RSN_PSK: return "rsn-psk";
        case OSW_AKM_RSN_FT_EAP: return "rsn-ft-eap";
        case OSW_AKM_RSN_FT_PSK: return "rsn-ft-psk";
        case OSW_AKM_RSN_EAP_SHA256: return "rsn-eap-sha256";
        case OSW_AKM_RSN_EAP_SHA384: return "rsn-eap-sha384";
        case OSW_AKM_RSN_PSK_SHA256: return "rsn-psk-sha256";
        case OSW_AKM_RSN_SAE: return "rsn-sae";
        case OSW_AKM_RSN_SAE_EXT: return "rsn-sae-ext";
        case OSW_AKM_RSN_FT_SAE: return "rsn-ft-sae";
        case OSW_AKM_RSN_FT_SAE_EXT: return "rsn-ft-sae-ext";
        case OSW_AKM_RSN_EAP_SUITE_B: return "rsn-eap-suite-b";
        case OSW_AKM_RSN_EAP_SUITE_B_192: return "rsn-eap-suite-b-192";
        case OSW_AKM_RSN_FT_EAP_SHA384: return "rsn-ft-eap-sha384";
        case OSW_AKM_RSN_FT_PSK_SHA384: return "rsn-ft-psk-sha384";
        case OSW_AKM_RSN_PSK_SHA384: return "rsn-psk-sha384";
        case OSW_AKM_WPA_NONE: return "wpa-none";
        case OSW_AKM_WPA_8021X: return "wpa-8021x";
        case OSW_AKM_WPA_PSK: return "wpa-psk";
        case OSW_AKM_WFA_DPP: return "wfa-dpp";
    }
    return NULL;
}

const char *
osw_cipher_into_cstr(const enum osw_cipher cipher)
{
    switch (cipher) {
        case OSW_CIPHER_UNSPEC: return "unspec";
        case OSW_CIPHER_RSN_NONE: return "rsn-none";
        case OSW_CIPHER_RSN_WEP_40: return "rsn-wep-40";
        case OSW_CIPHER_RSN_TKIP: return "rsn-tkip";
        case OSW_CIPHER_RSN_CCMP_128: return "rsn-ccmp-128";
        case OSW_CIPHER_RSN_WEP_104: return "rsn-wep-104";
        case OSW_CIPHER_RSN_BIP_CMAC_128: return "rsn-bip-cmac-128";
        case OSW_CIPHER_RSN_GCMP_128: return "rsn-gcmp-128";
        case OSW_CIPHER_RSN_GCMP_256: return "rsn-gcmp-256";
        case OSW_CIPHER_RSN_CCMP_256: return "rsn-ccmp-256";
        case OSW_CIPHER_RSN_BIP_GMAC_128: return "rsn-bip-gmac-128";
        case OSW_CIPHER_RSN_BIP_GMAC_256: return "rsn-bip-gmac-256";
        case OSW_CIPHER_RSN_BIP_CMAC_256: return "rsn-bip-cmac-256";
        case OSW_CIPHER_WPA_NONE: return "wpa-none";
        case OSW_CIPHER_WPA_WEP_40: return "wpa-wep-40";
        case OSW_CIPHER_WPA_TKIP: return "wpa-tkip";
        case OSW_CIPHER_WPA_CCMP: return "wpa-ccmp";
        case OSW_CIPHER_WPA_WEP_104: return "wpa-wep-104";
    }
    return NULL;
}

const char *
osw_band_into_cstr(const enum osw_band band)
{
    switch (band) {
        case OSW_BAND_UNDEFINED:
            return "undefined";
        case OSW_BAND_2GHZ:
            return "2.4ghz";
        case OSW_BAND_5GHZ:
            return "5ghz";
        case OSW_BAND_6GHZ:
            return "6ghz";
    }

    return "invalid";
}

const char *
osw_ssid_into_hex(struct osw_ssid_hex *hex,
                  const struct osw_ssid *ssid)
{
    const void *in = ssid->buf;
    const int err = bin2hex(in, ssid->len, hex->buf, sizeof(hex->buf));
    return err ? NULL : hex->buf;
}

bool
osw_ssid_from_cbuf(struct osw_ssid *ssid,
                   const void *buf,
                   size_t len)
{
    memset(ssid, 0, sizeof(*ssid));
    if (buf == NULL) return false;
    if (len > OSW_IEEE80211_SSID_LEN) return false;
    memcpy(ssid->buf, buf, len);
    ssid->len = len;
    return true;
}

static void
osw_radius_free_internal(struct osw_radius *r)
{
    if (r == NULL) return;
    FREE(r->server);
    FREE(r->passphrase);
    r->server = NULL;
    r->passphrase = NULL;
    r->port = 0;
}

void
osw_radius_list_free(struct osw_radius_list *l)
{
    if (l == NULL) return;
    if (l->list == NULL) return;
    osw_radius_list_purge(l);
    FREE(l->list);
    l->list = NULL;
    l->count = 0;
}

void
osw_radius_list_purge(struct osw_radius_list *l)
{
    size_t i;
    if (l == NULL) return;
    for (i = 0; i < l->count; i++) {
        struct osw_radius *r = &l->list[i];
        osw_radius_free_internal(r);
    }
    l->count = 0;
}

int
osw_radius_cmp(const struct osw_radius *a,
               const struct osw_radius *b)
{
    if ((a == NULL) && (b == NULL)) return 0;
    if ((a == NULL) || (b == NULL)) return (a == NULL) ? 1 : -1;
    const int server = strcmp(a->server ?: "", b->server ?: "");
    const int pass = strcmp(a->passphrase ?: "", b->passphrase ?: "");
    if (server) return server;
    if (pass) return pass;
    return a->port - b->port;
}

bool
osw_radius_is_equal(const struct osw_radius *a,
                    const struct osw_radius *b)
{
    if ((a == NULL) && (b == NULL)) return true;
    if ((a == NULL) || (b == NULL)) return false;
    return (osw_radius_cmp(a, b) == 0);
}

bool
osw_radius_list_is_equal(const struct osw_radius_list *a,
                         const struct osw_radius_list *b)
{
    size_t i;
    const struct osw_radius *rad_a, *rad_b;

    if (a->count != b->count) return false;

    for (i = 0; i < a->count; i++) {
        rad_a = &a->list[i];
        rad_b = &b->list[i];
        if (osw_radius_is_equal(rad_a, rad_b) != true)
            return false;
    }
    return true;
}

void
osw_radius_list_copy(const struct osw_radius_list *src,
                     struct osw_radius_list *dst)
{
    if (src == NULL || src->list == NULL) {
        dst->list = NULL;
        return;
    }

    dst->list = NULL;
    dst->count = 0;
    size_t i;
    if (src->count > 0)
        dst->list = CALLOC(src->count, sizeof(*dst->list));
    for (i = 0; i < src->count; i++) {
        dst->list[i].server = STRDUP(src->list[i].server);
        dst->list[i].port = src->list[i].port;
        dst->list[i].passphrase = STRDUP(src->list[i].passphrase);
        dst->count++;
    }
}

void
osw_radius_list_to_str(char *out,
                       size_t len,
                       const struct osw_radius_list *radii)
{
    size_t i;

    out[0] = 0;
    for (i = 0; i < radii->count; i++) {
        const struct osw_radius *r = &radii->list[i];
        csnprintf(&out, &len,
                  " "OSW_RADIUS_FMT,
                  OSW_RADIUS_ARG(r));
    }

    if (radii->count > 0 && out[-1] == ',')
        out[-1] = 0;
}

int
osw_nas_id_cmp(const struct osw_nas_id *a,
               const struct osw_nas_id *b)
{
    assert(a != NULL);
    assert(b != NULL);

    const size_t max_len = sizeof(a->buf);
    WARN_ON(strnlen(a->buf, max_len) == max_len);
    WARN_ON(strnlen(b->buf, max_len) == max_len);
    return strncmp(a->buf, b->buf, max_len);
}

bool
osw_nas_id_is_equal(const struct osw_nas_id *a,
                    const struct osw_nas_id *b)
{
    return (osw_nas_id_cmp(a, b) == 0);
}

int
osw_ft_encr_key_cmp(const struct osw_ft_encr_key *a,
                    const struct osw_ft_encr_key *b)
{
    assert(a != NULL);
    assert(b != NULL);

    const size_t max_len = sizeof(a->buf);
    WARN_ON(strnlen(a->buf, max_len) == max_len);
    WARN_ON(strnlen(b->buf, max_len) == max_len);
    return strncmp(a->buf, b->buf, max_len);
}

bool
osw_ft_encr_key_is_equal(const struct osw_ft_encr_key *a,
                         const struct osw_ft_encr_key *b)
{
    return (osw_ft_encr_key_cmp(a, b) == 0);
}

bool
osw_passpoint_str_list_is_equal(char **const a,
                                char **const b,
                                const size_t a_len,
                                const size_t b_len)
{
    size_t i;

    if (a == NULL && b == NULL) return true;
    if (a == NULL || b == NULL) return false;
    if (a_len != b_len) return false;
    for (i = 0; i < a_len; i++) {
        if (STRSCMP(a[i], b[i]) != 0) return false;
    }
    return true;
}

/* FIXME make osw_passpoint_is_equal a wrapper to
 * not yet implemented _cmp function */
bool
osw_passpoint_is_equal(const struct osw_passpoint *a,
                       const struct osw_passpoint *b)
{
    if (a->hs20_enabled != b->hs20_enabled) return false;
    if (!osw_hwaddr_is_equal(&a->hessid, &b->hessid)) return false;
    if (a->osu_ssid.len != b->osu_ssid.len) return false;
    if (STRSCMP(a->osu_ssid.buf, b->osu_ssid.buf) != 0) return false;
    if (STRSCMP(a->t_c_filename, b->t_c_filename) != 0) return false;
    if (STRSCMP(a->anqp_elem, b->anqp_elem) != 0) return false;
    if (a->adv_wan_status != b->adv_wan_status) return false;
    if (a->adv_wan_symmetric != b->adv_wan_symmetric) return false;
    if (a->adv_wan_at_capacity != b->adv_wan_at_capacity) return false;
    if (a->osen != b->osen) return false;
    if (a->asra != b->asra) return false;
    if (a->ant != b->ant) return false;
    if (a->venue_group != b->venue_group) return false;
    if (a->venue_type != b->venue_type) return false;
    if (a->anqp_domain_id != b->anqp_domain_id) return false;
    if (a->pps_mo_id != b->pps_mo_id) return false;
    if (a->t_c_timestamp != b->t_c_timestamp) return false;
    if (!osw_passpoint_str_list_is_equal(a->domain_list, b->domain_list,
                                         a->domain_list_len, b->domain_list_len)) return false;
    if (!osw_passpoint_str_list_is_equal(a->nairealm_list, b->nairealm_list,
                                         a->nairealm_list_len, b->nairealm_list_len)) return false;
    if (!osw_passpoint_str_list_is_equal(a->roamc_list, b->roamc_list,
                                         a->roamc_list_len, b->roamc_list_len)) return false;
    if (!osw_passpoint_str_list_is_equal(a->oper_fname_list, b->oper_fname_list,
                                         a->oper_fname_list_len, b->oper_fname_list_len)) return false;
    if (!osw_passpoint_str_list_is_equal(a->venue_name_list, b->venue_name_list,
                                         a->venue_name_list_len, b->venue_name_list_len)) return false;
    if (!osw_passpoint_str_list_is_equal(a->venue_url_list, b->venue_url_list,
                                         a->venue_url_list_len, b->venue_url_list_len)) return false;
    if (!osw_passpoint_str_list_is_equal(a->list_3gpp_list, b->list_3gpp_list,
                                         a->list_3gpp_list_len, b->list_3gpp_list_len)) return false;

    if (a->net_auth_type_list_len != b->net_auth_type_list_len) return false;
    if (memcmp(a->net_auth_type_list, b->net_auth_type_list,
               sizeof(*a->net_auth_type_list) * a->net_auth_type_list_len) != 0) return false;

    return true;
};

void
osw_passpoint_free_internal(struct osw_passpoint *p)
{
    if (p == NULL) return;

    str_array_free(p->domain_list, p->domain_list_len);
    str_array_free(p->nairealm_list, p->nairealm_list_len);
    str_array_free(p->roamc_list, p->roamc_list_len);
    str_array_free(p->oper_fname_list, p->oper_fname_list_len);
    str_array_free(p->venue_name_list, p->venue_name_list_len);
    str_array_free(p->venue_url_list, p->venue_url_list_len);
    str_array_free(p->list_3gpp_list, p->list_3gpp_list_len);
    FREE(p->net_auth_type_list);

    memset(p, 0, sizeof(*p));
}

void
osw_passpoint_copy(const struct osw_passpoint *src,
                   struct osw_passpoint *dst)
{
    if (dst == NULL || src == NULL) {
        return;
    }

    *dst = *src;

    if (src->t_c_filename != NULL) {
        dst->t_c_filename = STRDUP(src->t_c_filename);
    } else {
        dst->t_c_filename = NULL;
    }

    if (src->anqp_elem != NULL) {
        dst->anqp_elem = STRDUP(src->anqp_elem);
    } else {
        dst->anqp_elem = NULL;
    }

    if (src->domain_list != NULL) {
        dst->domain_list = str_array_dup(src->domain_list, src->domain_list_len);
        dst->domain_list_len = src->domain_list_len;
    } else {
        dst->domain_list = NULL;
        dst->domain_list_len = 0;
    }

    if (src->nairealm_list != NULL) {
        dst->nairealm_list = str_array_dup(src->nairealm_list, src->nairealm_list_len);
        dst->nairealm_list_len = src->nairealm_list_len;
    } else {
        dst->nairealm_list = NULL;
        dst->nairealm_list_len = 0;
    }

    if (src->roamc_list != NULL) {
        dst->roamc_list = str_array_dup(src->roamc_list, src->roamc_list_len);
        dst->roamc_list_len = src->roamc_list_len;
    } else {
        dst->roamc_list = NULL;
        dst->roamc_list_len = 0;
    }

    if (src->oper_fname_list != NULL) {
        dst->oper_fname_list = str_array_dup(src->oper_fname_list,
                                             src->oper_fname_list_len);
        dst->oper_fname_list_len = src->oper_fname_list_len;
    } else {
        dst->oper_fname_list = NULL;
        dst->oper_fname_list_len = 0;
    }

    if (src->venue_name_list != NULL) {
        dst->venue_name_list = str_array_dup(src->venue_name_list,
                                             src->venue_name_list_len);
        dst->venue_name_list_len = src->venue_name_list_len;
    } else {
        dst->venue_name_list = NULL;
        dst->venue_name_list_len = 0;
    }

    if (src->venue_url_list != NULL) {
        dst->venue_url_list = str_array_dup(src->venue_url_list,
                                            src->venue_url_list_len);
        dst->venue_url_list_len = src->venue_url_list_len;
    } else {
        dst->venue_url_list = NULL;
        dst->venue_url_list_len = 0;
    }

    if (src->list_3gpp_list != NULL) {
        dst->list_3gpp_list = str_array_dup(src->list_3gpp_list,
                                            src->list_3gpp_list_len);
        dst->list_3gpp_list_len = src->list_3gpp_list_len;
    } else {
        dst->list_3gpp_list = NULL;
        dst->list_3gpp_list_len = 0;
    }

    if (src->net_auth_type_list != NULL) {
        dst->net_auth_type_list = MEMNDUP(src->net_auth_type_list,
                                          sizeof(*src->net_auth_type_list) *
                                          src->net_auth_type_list_len);
        dst->net_auth_type_list_len = src->net_auth_type_list_len;
    } else {
        dst->net_auth_type_list = NULL;
        dst->net_auth_type_list_len = 0;
    }
}

void
osw_vif_status_set(enum osw_vif_status *status,
                   enum osw_vif_status new_status)
{
    switch (*status) {
        case OSW_VIF_UNKNOWN:
            *status = new_status;
            break;
        case OSW_VIF_DISABLED:
            switch (new_status) {
                case OSW_VIF_UNKNOWN:
                    WARN_ON(1);
                    break;
                case OSW_VIF_DISABLED:
                    break;
                case OSW_VIF_ENABLED:
                    *status = OSW_VIF_BROKEN;
                    break;
                case OSW_VIF_BROKEN:
                    break;
            }
            break;
        case OSW_VIF_ENABLED:
            switch (new_status) {
                case OSW_VIF_UNKNOWN:
                    WARN_ON(1);
                    break;
                case OSW_VIF_DISABLED:
                    *status = OSW_VIF_BROKEN;
                    break;
                case OSW_VIF_ENABLED:
                    break;
                case OSW_VIF_BROKEN:
                    break;
            }
            break;
        case OSW_VIF_BROKEN:
            break;
    }
}

const char *
osw_vif_status_into_cstr(enum osw_vif_status status)
{
    switch (status) {
        case OSW_VIF_UNKNOWN: return "unknown";
        case OSW_VIF_DISABLED: return "disabled";
        case OSW_VIF_ENABLED: return "enabled";
        case OSW_VIF_BROKEN: return "broken";
    }
    return "uncaught";
}

bool
osw_channel_is_none(const struct osw_channel *c)
{
    if (c == NULL) return true;
    if (c->control_freq_mhz == 0) return true;
    return false;
}

const struct osw_channel *
osw_channel_none(void)
{
    static const struct osw_channel zero;
    return &zero;
}

const struct osw_hwaddr *
osw_hwaddr_zero(void)
{
    static const struct osw_hwaddr zero;
    return &zero;
}

void
osw_hwaddr_write(const struct osw_hwaddr *addr,
                 void *buf,
                 size_t buf_len)
{
    if (WARN_ON(buf_len < sizeof(addr->octet))) return;
    memcpy(buf, addr, sizeof(addr->octet));
}

bool
osw_wpa_is_ft(const struct osw_wpa *wpa)
{
    return wpa->akm_ft_eap || wpa->akm_ft_psk || wpa->akm_ft_sae ||
           wpa->akm_ft_sae_ext || wpa->akm_ft_eap_sha384;
}

const char *osw_sta_cell_cap_to_cstr(enum osw_sta_cell_cap cap)
{
    switch (cap)
    {
        case OSW_STA_CELL_UNKNOWN: return "unknown";
        case OSW_STA_CELL_AVAILABLE: return "available";
        case OSW_STA_CELL_NOT_AVAILABLE: return "not available";
    }
    return "";
}

#include "osw_types_ut.c"
