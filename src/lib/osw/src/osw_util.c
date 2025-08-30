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

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <endian.h>
#include <osw_types.h>
#include <osw_util.h>
#include <const.h>
#include <os.h>
#include <log.h>

#define DOT11_EID_DSSS 3
#define DOT11_EID_HT_OP 61
#define DOT11_EID_VHT_OP 192
#define DOT11_EID_EXT 255
#define DOT11_EID_EXT_HE_OP 36
#define DOT11_EID_EXT_EHT_OP 106

#define DOT11_FRAGMENT 0xf2
#define DOT11_EXTENDED_CAPS_TAG 0x7f
#define DOT11_EXTENDED_CAPS_BTM (1 << 3)
#define DOT11_RM_ENABLED_CAPS_TAG 0x46
#define DOT11_RM_ENABLED_CAPS_LINK_MEAS (1 << 0)
#define DOT11_RM_ENABLED_CAPS_NEIGH_REP (1 << 1)
#define DOT11_RM_ENABLED_CAPS_BCN_PAS_MEAS (1 << 4)
#define DOT11_RM_ENABLED_CAPS_BCN_ACT_MEAS (1 << 5)
#define DOT11_RM_ENABLED_CAPS_BCN_TAB_MEAS (1 << 6)
#define DOT11_RM_ENABLED_CAPS_LCI_MEAS (1 << 4)
#define DOT11_RM_ENABLED_CAPS_FTM_RANGE_REP (1 << 2)
#define DOT11_SUPPORTED_OP_CLASSES 0x3b
#define DOT11_SUPPORTED_CHANNELS 0x24
#define DOT11_HT_CAPS 0x2d
#define DOT11_HT_CAPS_INFO_CHAN_W_SET_MASK 0x02
#define DOT11_HT_CAPS_INFO_SMPS_MASK 0x0c
#define DOT11_VHT_CAPS 0xbf
#define DOT11_SUP_CHAN_WIDTH_SET_MASK 0x0c
#define DOT11_MU_BEAMFORMEE_CAP_MASK 0x100000
#define DOT11_ELEM_ID_VENDOR_SPECIFIC 0xdd
#define DOT11_ELEM_ID_EXT 0xff
#define DOT11_EXT_HE_CAPS 0x23
#define DOT11_EXT_HE_OPER 0x24
#define DOT11_EXT_HE_CAPS_PHY_2GHZ_40 (1 << 1)
#define DOT11_EXT_HE_CAPS_PHY_40_80 (1 << 2)
#define DOT11_EXT_HE_CAPS_PHY_160 (1 << 3)
#define DOT11_EXT_HE_CAPS_PHY_160_8080 (1 << 4)
#define DOT11_POWER_CAP 0x21
#define DOT11_EXT_EHT_OP 106
#define DOT11_EXT_EHT_CAP 108
#define DOT11_EXT_EHT_CAP_MAC_LEN 2
#define DOT11_EXT_EHT_CAP_PHY_LEN 9
#define DOT11_EXT_EHT_CAP_PHY_320MHZ (1 << 1)
#define DOT11_EXT_EHT_OP_INFO (1 << 0)
#define DOT11_EXT_EHT_OP_INFO_CTL_WIDTH (0x1 + 0x2 + 0x4)
#define DOT11_EXT_ML 107

#define DOT11_VENDOR_SPECIFIC_IE_WFA_MBO_OCE 0x16
#define WFA_WNM_VENDOR_SPECIFIC_OUI_TYPE_MBO_OCE 0x16
#define WFA_MBO_ATTR_ID_NON_PREF_CHAN_REP 0x02
#define WFA_MBO_ATTR_ID_CELL_DATA_PREF 0x03

#define WFA_VENDOR_SPECIFIC_OUI_0 0x50
#define WFA_VENDOR_SPECIFIC_OUI_1 0x6f
#define WFA_VENDOR_SPECIFIC_OUI_2 0x9a
#define WFA_VENDOR_SPECIFIC_OUI (uint32_t) \
    (((uint32_t)(0x00 << 24))                      | \
     ((uint32_t)(WFA_VENDOR_SPECIFIC_OUI_0 << 16)) | \
     ((uint32_t)(WFA_VENDOR_SPECIFIC_OUI_1 << 8))  | \
     ((uint32_t)(WFA_VENDOR_SPECIFIC_OUI_2 << 0)))

#define DOT11_AS_LE32(ptr) ((((uint8_t *)(ptr))[0] >> 0) \
                          | (((uint8_t *)(ptr))[1] >> 8) \
                          | (((uint8_t *)(ptr))[2] >> 16) \
                          | (((uint8_t *)(ptr))[3] >> 24))
#define DOT11_AS_LE16(ptr) ((((uint8_t *)(ptr))[0] >> 0) \
                          | (((uint8_t *)(ptr))[1] >> 8))

#define __bf_shf(x) (__builtin_ffsll(x) - 1)
#define FIELD_GET(mask, value) ((typeof(mask))(((value) & (mask)) >> __bf_shf(mask)))
#define BIT(x) (1ULL << x)
#define BYTES_LE16(bytes) (((uint32_t)((bytes)[0]) << 0) \
                         | ((uint32_t)((bytes)[1]) << 8))
#define BYTES_LE24(bytes) (((uint32_t)((bytes)[0]) << 0) \
                         | ((uint32_t)((bytes)[1]) << 8) \
                         | ((uint32_t)((bytes)[2]) << 16))

struct dot11_elem_dsss {
    uint8_t chan;
} __attribute__((packed));

struct dot11_elem_supp_op_class {
    uint8_t current_op_class;
    uint8_t other_op_classes[0];
} __attribute__((packed));

struct dot11_elem_ht_op {
    uint8_t primary_chan;
    uint8_t info[5];
    uint8_t mcs[16];
} __attribute__((packed));

#define DOT11_ELEM_HT_OP_INFO0_SEC_CH_OFF (BIT(0) | BIT(1))
#define DOT11_ELEM_HT_OP_INFO0_STA_CH_WIDTH (BIT(2))
#define DOT11_ELEM_HT_OP_SEC_CH_OFF_SCA 1
#define DOT11_ELEM_HT_OP_SEC_CH_OFF_SCB 3
#define DOT11_ELEM_HT_OP_SEC_CH_OFF_SCN 0

struct dot11_elem_vht_op_info {
    uint8_t ch_width;
    uint8_t ch_center_freq_seg0;
    uint8_t ch_center_freq_seg1;
} __attribute__((packed));

struct dot11_elem_vht_op {
    struct dot11_elem_vht_op_info info;
    uint8_t mcs[2];
} __attribute__((packed));

#define DOT11_ELEM_VHT_OP_INFO_CH_WIDTH_2040 0
#define DOT11_ELEM_VHT_OP_INFO_CH_WIDTH_80 1
#define DOT11_ELEM_VHT_OP_INFO_CH_WIDTH_160_DEPRECATED 2
#define DOT11_ELEM_VHT_OP_INFO_CH_WIDTH_8080_DEPRECATED 3

struct dot11_elem_he_op_max_cohost {
    uint8_t indicator;
} __attribute__((packed));

struct dot11_elem_he_op_6g_info {
    uint8_t primary_channel;
    uint8_t control;
    uint8_t ch_center_freq_seg0;
    uint8_t ch_center_freq_seg1;
    uint8_t min_rate;
} __attribute__((packed));

#define DOT11_ELEM_HE_OP_6G_INFO_CONTROL_CH_WIDTH (BIT(0) | BIT(1))
#define DOT11_ELEM_HE_OP_6G_INFO_CONTROL_DUP_BEACON (BIT(2))
#define DOT11_ELEM_HE_OP_6G_INFO_CONTROL_REG_INFO (BIT(3) | BIT(4) | BIT(5))

#define DOT11_ELEM_HE_OP_6G_INFO_CONTROL_CH_WIDTH_20 0
#define DOT11_ELEM_HE_OP_6G_INFO_CONTROL_CH_WIDTH_40 1
#define DOT11_ELEM_HE_OP_6G_INFO_CONTROL_CH_WIDTH_80 2
#define DOT11_ELEM_HE_OP_6G_INFO_CONTROL_CH_WIDTH_160_8080 3

struct dot11_elem_he_op {
    uint8_t params[3];
    uint8_t bss_color_info;
    uint8_t mcs[2];
    /* optional: struct dot11_elem_vht_op_info */
    /* optional: struct dot11_elem_he_op_max_cohost */
    /* optional: struct dot11_elem_he_op_6g_info */
} __attribute__((packed));

#define DOT11_ELEM_HE_OP_PARAMS_VHT_OP_INFO_PRESENT (BIT(14))
#define DOT11_ELEM_HE_OP_PARAMS_MAX_COHOST_PRESENT (BIT(15))
#define DOT11_ELEM_HE_OP_PARAMS_6G_OP_PRESENT (BIT(17))

struct dot11_elem_eht_op_info {
    uint8_t control;
    uint8_t ccfs0;
    uint8_t ccfs1;
} __attribute__((packed));

#define DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH (BIT(0) | BIT(1) | BIT(2))
#define DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH_20 0
#define DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH_40 1
#define DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH_80 2
#define DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH_160 3
#define DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH_320 4

struct dot11_elem_eht_op_subchan {
    uint8_t bitmap[2];
} __attribute__((packed));

struct dot11_elem_eht_op {
    uint8_t params;
    uint8_t mcs[4];
    /* optional: struct dot11_elem_eht_op_info */
    /* optional: struct dot11_elem_eht_op_subchan */
} __attribute__((packed));

#define DOT11_ELEM_EHT_OP_PARAMS_INFO_PRESENT (BIT(0))
#define DOT11_ELEM_EHT_OP_PARAMS_SUBCHAN_PRESENT (BIT(1))

static bool
dot11_elem_eht_op_parse(const void *buf,
                        size_t buf_len,
                        const struct dot11_elem_eht_op_info **info,
                        const struct dot11_elem_eht_op_subchan **subchan)
{
    *info = NULL;
    *subchan = NULL;

    const struct dot11_elem_eht_op *eht_op = buf;
    size_t left = buf_len;
    if (left < sizeof(*eht_op)) return false;

    const void *tail = eht_op + 1;
    left -= sizeof(*eht_op);

    const uint32_t params = eht_op->params;

    if (FIELD_GET(DOT11_ELEM_EHT_OP_PARAMS_INFO_PRESENT, params)) {
        if (left < sizeof(**info)) return false;
        *info = tail;
        tail += sizeof(**info);
        left -= sizeof(**info);

        if (FIELD_GET(DOT11_ELEM_EHT_OP_PARAMS_SUBCHAN_PRESENT, params)) {
            if (left < sizeof(**subchan)) return false;
            *subchan = tail;
            tail += sizeof(**subchan);
            left -= sizeof(**subchan);
        }
    }

    return true;
}

static bool
dot11_elem_he_op_parse(const void *buf,
                       size_t buf_len,
                       const struct dot11_elem_vht_op_info **vht_op_info,
                       const struct dot11_elem_he_op_max_cohost **max_cohost,
                       const struct dot11_elem_he_op_6g_info **band_6g_info)
{
    *vht_op_info = NULL;
    *max_cohost = NULL;
    *band_6g_info = NULL;

    const struct dot11_elem_he_op *he_op = buf;
    size_t left = buf_len;
    if (left < sizeof(*he_op)) return false;

    const void *tail = he_op + 1;
    left -= sizeof(*he_op);
    const uint32_t params = BYTES_LE24(he_op->params);

    if (FIELD_GET(DOT11_ELEM_HE_OP_PARAMS_VHT_OP_INFO_PRESENT, params)) {
        if (left < sizeof(**vht_op_info)) return false;
        *vht_op_info = tail;
        tail += sizeof(**vht_op_info);
        left -= sizeof(**vht_op_info);
    }

    if (FIELD_GET(DOT11_ELEM_HE_OP_PARAMS_MAX_COHOST_PRESENT, params)) {
        if (left < sizeof(**max_cohost)) return false;
        *max_cohost = tail;
        tail += sizeof(**max_cohost);
        left -= sizeof(**max_cohost);
    }

    if (FIELD_GET(DOT11_ELEM_HE_OP_PARAMS_6G_OP_PRESENT, params)) {
        if (left < sizeof(**band_6g_info)) return false;
        *band_6g_info = tail;
        tail += sizeof(**band_6g_info);
        left -= sizeof(**band_6g_info);
    }

    return true;
}

unsigned int
osw_ht_mcs_idx_to_nss(const unsigned int mcs_idx)
{
    unsigned int nss = 0;
    if ((mcs_idx <= 7) || (mcs_idx == 32))
        nss = 1;
    if (((mcs_idx >= 8) && (mcs_idx <= 15)) || ((mcs_idx >= 33) && (mcs_idx <= 38)))
        nss = 2;
    if (((mcs_idx >= 16) && (mcs_idx <= 23)) || ((mcs_idx >= 39) && (mcs_idx <= 52)))
        nss = 3;
    if (((mcs_idx >= 24) && (mcs_idx <= 31)) || ((mcs_idx >= 53) && (mcs_idx <= 76)))
        nss = 4;
    return nss;
}

unsigned int
osw_ht_mcs_idx_to_mcs(const unsigned int mcs_idx)
{
    /* Indices >= 32 are special MCS, like dual
     * 20+20MHz. Don't consider them as typical
     * data rates.
     */
    if (mcs_idx >= 32) return 0;
    return (mcs_idx % 8);
}

unsigned int
osw_vht_he_mcs_to_max_nss(const uint16_t mcs_field)
{
    unsigned int max_nss = 0;
    unsigned int n;
    for (n = 0; n < 8; n++) {
        const unsigned int shift =  2 * n;
        const unsigned int mcs_tuple = ((mcs_field & (0x03 << shift)) >> shift);
        const bool mcs_available = (mcs_tuple != 0x03);
        if (mcs_available == false) continue;
        /* Each subfield is 2 bits wide and represents
         * nss=1, nss=2, etc. LSB to MSB. The first subfield
         * denotes nss=1 but is actually when n=0.
         */
        const unsigned int nss = n + 1;
        if (nss > max_nss) max_nss = nss;
    }
    return max_nss;
}

static unsigned int
osw_mcs_field_to_max_mcs(uint16_t mcs_field,
                         const uint8_t *subfield_into_mcs)
{
    unsigned int max_mcs = 0;
    while (mcs_field != 0) {
        const unsigned int subfield = (mcs_field & 0x3);
        const unsigned int mcs = subfield_into_mcs[subfield];
        if (mcs > max_mcs) max_mcs = mcs;
        mcs_field >>= 2;
    }
    return max_mcs;
}

unsigned int
osw_vht_mcs_to_max_mcs(const uint16_t mcs_field)
{
    /* 802-11-2020 Figure 9-611 */
    static const uint8_t vht_subfield_into_mcs[] = {
        [0] = 7,
        [1] = 8,
        [2] = 9,
        [3] = 0,
    };
    return osw_mcs_field_to_max_mcs(mcs_field, vht_subfield_into_mcs);
}

unsigned int
osw_he_mcs_to_max_mcs(const uint16_t mcs_field)
{
    /* 802-11-2021 Figure 9-899e */
    static const uint8_t he_subfield_into_mcs[] = {
        [0] = 7,
        [1] = 9,
        [2] = 11,
        [3] = 0,
    };
    return osw_mcs_field_to_max_mcs(mcs_field, he_subfield_into_mcs);
}

static bool
osw_assoc_req_is_2ghz(const struct osw_assoc_req_info *info)
{
    /* The first entry on the list is the current Operating
     * Class the STA is connecting on. All subsequent ones
     * are alternatives it supports.
     */
    const uint8_t op_class = (info->op_class_cnt > 0)
                           ? info->op_class_list[0]
                           : 0;
    const enum osw_band band = (op_class != 0)
                             ? osw_op_class_to_band(op_class)
                             : OSW_BAND_UNDEFINED;
    /* Technically spec forbids VHT on 2.4GHz. HE
     * devices should support Operating Classes,
     * but in the unlikely event that is missing,
     * try guessing.
     */
    const bool probably_2ghz = (info->ht_caps_present == true &&
                                info->he_caps_present == true &&
                                info->vht_caps_present == false);
    const bool is_2ghz = (band == OSW_BAND_2GHZ)
                      || (band == OSW_BAND_UNDEFINED && probably_2ghz);

    return is_2ghz;
}

enum osw_channel_width
osw_assoc_req_to_max_chwidth(const struct osw_assoc_req_info *info)
{
    assert(info != NULL);
    enum osw_channel_width ht_max_chwidth = OSW_CHANNEL_20MHZ;
    enum osw_channel_width vht_max_chwidth = OSW_CHANNEL_20MHZ;
    enum osw_channel_width he_max_chwidth = OSW_CHANNEL_20MHZ;
    const enum osw_channel_width eht_max_chwidth = info->eht_cap_chwidth;
    const bool is_2ghz = osw_assoc_req_is_2ghz(info);

    if (info->ht_caps_present == true) {
        if (info->ht_caps_40) ht_max_chwidth = OSW_CHANNEL_40MHZ;
    }
    if (info->vht_caps_present == true) {
        vht_max_chwidth = OSW_CHANNEL_80MHZ;
        if (info->vht_caps_sup_chan_w_set == 1) vht_max_chwidth = OSW_CHANNEL_160MHZ;
        if (info->vht_caps_sup_chan_w_set == 2) vht_max_chwidth = OSW_CHANNEL_80P80MHZ;
    }
    if (info->he_caps_present == true) {
        if (is_2ghz) {
            if (info->he_caps_2ghz_40) he_max_chwidth = OSW_CHANNEL_40MHZ;
        }
        else {
            if (info->he_caps_40_80 == true) he_max_chwidth = OSW_CHANNEL_80MHZ;
            if (info->he_caps_160 == true) he_max_chwidth = OSW_CHANNEL_160MHZ;
            if (info->he_caps_160_8080 == true) he_max_chwidth = OSW_CHANNEL_80P80MHZ;
        }
    }

    /* This abuses enums as ints to derive max value */
    enum osw_channel_width max_chwidth = OSW_CHANNEL_20MHZ;
    if (ht_max_chwidth > max_chwidth) max_chwidth = ht_max_chwidth;
    if (vht_max_chwidth > max_chwidth) max_chwidth = vht_max_chwidth;
    if (he_max_chwidth > max_chwidth) max_chwidth = he_max_chwidth;
    if (eht_max_chwidth > max_chwidth) max_chwidth = eht_max_chwidth;
    return max_chwidth;
}

unsigned int
osw_assoc_req_to_max_nss(const struct osw_assoc_req_info *info)
{
    ASSERT(info != NULL, "");
    unsigned int ht_max_nss = 0;
    unsigned int vht_max_nss = 0;
    unsigned int he_max_nss = 0;
    const unsigned int eht_max_nss = info->eht_cap_nss;

    if (info->ht_caps_present == true) {
        unsigned int i;
        for (i = 0; i < sizeof(info->ht_caps_rx_mcs); i++) {
            unsigned int j;
            const uint8_t mcs_byte = info->ht_caps_rx_mcs[i];
            for (j = 0; j < 8; j++) {
                bool mcs_available = ((mcs_byte & (1 << j)) != 0);
                if (mcs_available == false) continue;
                int mcs_idx = (i * 8) + j;
                unsigned int curr_nss = osw_ht_mcs_idx_to_nss(mcs_idx);
                if (curr_nss > ht_max_nss) ht_max_nss = curr_nss;
            }
        }
    }
    if (info->vht_caps_present == true) {
        vht_max_nss = osw_vht_he_mcs_to_max_nss(info->vht_caps_rx_mcs_map);
    }
    if (info->he_caps_present == true) {
        he_max_nss = osw_vht_he_mcs_to_max_nss(info->he_caps_rx_mcs_map_le_80);
        if (info->he_caps_rx_mcs_map_160_present == true) {
            const unsigned int nss = osw_vht_he_mcs_to_max_nss(info->he_caps_rx_mcs_map_160);
            if (nss > he_max_nss) he_max_nss = nss;
        }
        if (info->he_caps_rx_mcs_map_8080_present == true) {
            const unsigned int nss = osw_vht_he_mcs_to_max_nss(info->he_caps_rx_mcs_map_8080);
            if (nss > he_max_nss) he_max_nss = nss;
        }
    }

    unsigned int max_streams = 0;
    if (ht_max_nss > max_streams) max_streams = ht_max_nss;
    if (vht_max_nss > max_streams) max_streams = vht_max_nss;
    if (he_max_nss > max_streams) max_streams = he_max_nss;
    if (eht_max_nss > max_streams) max_streams = eht_max_nss;
    return max_streams;
}

unsigned int
osw_assoc_req_to_max_mcs(const struct osw_assoc_req_info *info)
{
    ASSERT(info != NULL, "");
    unsigned int ht_max_mcs = 0;
    unsigned int vht_max_mcs = 0;
    unsigned int he_max_mcs = 0;
    const unsigned int eht_max_mcs = info->eht_cap_mcs;

    if (info->ht_caps_present == true) {
        unsigned int i;
        for (i = 0; i < sizeof(info->ht_caps_rx_mcs); i++) {
            unsigned int j;
            const uint8_t mcs_byte = info->ht_caps_rx_mcs[i];
            for (j = 0; j < 8; j++) {
                bool mcs_available = ((mcs_byte & (1 << j)) != 0);
                if (mcs_available == false) continue;
                unsigned int mcs_idx = (i * 8) + j;
                unsigned int mcs = osw_ht_mcs_idx_to_mcs(mcs_idx);
                if (mcs > ht_max_mcs) ht_max_mcs = mcs;
            }
        }
    }
    if (info->vht_caps_present == true) {
        vht_max_mcs = osw_vht_mcs_to_max_mcs(info->vht_caps_rx_mcs_map);
    }
    if (info->he_caps_present == true) {
        he_max_mcs = osw_he_mcs_to_max_mcs(info->he_caps_rx_mcs_map_le_80);
        if (info->he_caps_rx_mcs_map_160_present == true) {
            const unsigned int mcs = osw_he_mcs_to_max_mcs(info->he_caps_rx_mcs_map_160);
            if (mcs > he_max_mcs) he_max_mcs = mcs;
        }
        if (info->he_caps_rx_mcs_map_8080_present == true) {
            const unsigned int mcs = osw_he_mcs_to_max_mcs(info->he_caps_rx_mcs_map_8080);
            if (mcs > he_max_mcs) he_max_mcs = mcs;
        }
    }

    unsigned int max_mcs = 0;
    if (ht_max_mcs > max_mcs) max_mcs = ht_max_mcs;
    if (vht_max_mcs > max_mcs) max_mcs = vht_max_mcs;
    if (he_max_mcs > max_mcs) max_mcs = he_max_mcs;
    if (eht_max_mcs > max_mcs) max_mcs = eht_max_mcs;
    return max_mcs;
}

static void
osw_parse_supported_op_classes(const struct element *elem,
                               struct osw_assoc_req_info *info)
{
    const uint8_t delim_130 = 130;
    bool delim_130_present = false;
    const uint8_t delim_0 = 0;
    bool delim_0_present = false;

    unsigned int i;
    for (i = 1; i < elem->datalen; i++) {
        if (elem->data[i] == delim_0) {
            delim_0_present = true;
        }
        else if (elem->data[i] == delim_130) {
            delim_130_present = true;
        }
    }

    if (delim_130_present == true || delim_0_present == true) {
        /* >=2016 802.11 specs define values 0 and 130 as delimiters. 130 is
         * also a valid opclass. Some client STAs do not take it into account
         * and simply list supported op classes which might include 130.
         * It creates ambiguity as it is not always possible to detect
         * if it is a valid Current Operating Class Extension Sequence
         * or simply a continuation of such (spec incompliant) op class list.
         * Example Supported Operating Classes IE dump with malformed list from
         * Samsung Galaxy phone:
         * 3b 15 80 70 73 74 75 7c 7d 7e 7f 80 81 82 76 77 78 79 7a 7b 51 53 54
         * Since using 0 nor 130 as a delimiter was not yet spotted in the field,
         * only this notice will be printed and the list will be parsed as
         * is, excluding 0. This should work with both cases as Extension
         * and Duple Sequence both contain only bytes representing op_classes,
         * excluding the delimiter itself. Caveat: op_class 130 will be always
         * detected when value 130 is present, even if used as a delimiter.
         */
        info->op_class_parse_errors++;
        LOGD("osw: util: parse_assoc_req: present delimiters - possible malformed frame, "
              " onehundredandthirty: "OSW_BOOL_FMT
              " zero: "OSW_BOOL_FMT,
              OSW_BOOL_ARG(delim_130_present),
              OSW_BOOL_ARG(delim_0_present));
    }

    info->op_class_cnt = 0;
    for (i = 0; i < elem->datalen; i++) {
        uint8_t elem_byte = elem->data[i];
        if (elem_byte == delim_0) continue;

        unsigned int j;
        bool duplicate = false;
        for (j = 0; j < info->op_class_cnt; j++) {
            if (elem_byte == info->op_class_list[j]) duplicate = true;
        }
        if (duplicate == true) continue;

        info->op_class_list[info->op_class_cnt] = elem_byte;
        info->op_class_cnt++;

        if (info->op_class_cnt > ARRAY_SIZE(info->op_class_list)) {
            LOGT("osw: util: too many op_classes");
            break;
        }
    }
}

static void
osw_parse_supported_channels(const struct element *elem,
                             struct osw_assoc_req_info *info)
{
    unsigned int space_left = ARRAY_SIZE(info->channel_list);
    unsigned int i;
    info->channel_cnt = 0;
    for (i = 0; (i + 1) < elem->datalen; i += 2) {
        const uint8_t chan_start = elem->data[i];
        const uint8_t chan_count = elem->data[i+1];

        /* 6GHz capable devices should not be using this
         * IE, so the channel - frequency mapping is not a
         * problem. Looking up spacing based on op class is
         * possible, but to support [165, 1] pair it would
         * need to walk every chan in every op class to find
         * if an op class matches.. silly. Just do fixed
         * spacing.
         */
        const uint8_t chan_spacing = (chan_start < 36) ? 1 : 4;

        unsigned int j;
        for (j = 0; j < chan_count && space_left > 0; j++) {
            info->channel_list[info->channel_cnt] = chan_start + (j * chan_spacing);
            info->channel_cnt++;
            space_left--;
        }

        const bool did_not_finish = (j < chan_count);
        if (did_not_finish && space_left == 0) {
            LOGT("osw: util: too many channels");
            break;
        }
    }
}

static void
osw_parse_ht_caps(const struct element *elem,
                  struct osw_assoc_req_info *info)
{
    info->ht_caps_present = true;
    /* ht capabilities info */
    info->ht_caps_40 = ((elem->data[0] & DOT11_HT_CAPS_INFO_CHAN_W_SET_MASK) != 0);
    info->ht_caps_smps = ((elem->data[0] & DOT11_HT_CAPS_INFO_SMPS_MASK) >> 2);
    /* ht supported mcs set */
    memcpy(info->ht_caps_rx_mcs, &elem->data[3], sizeof(info->ht_caps_rx_mcs));
}

static void
osw_parse_vht_caps(const struct element *elem,
                   struct osw_assoc_req_info *info)
{
    info->vht_caps_present = true;
    /* vht capabilities info */
    const uint32_t vht_caps_info = (elem->data[0] << 0) |
                                   (elem->data[1] << 8) |
                                   (elem->data[2] << 16) |
                                   (elem->data[3] << 24);
    info->vht_caps_sup_chan_w_set = ((vht_caps_info & DOT11_SUP_CHAN_WIDTH_SET_MASK) >> 2);
    info->vht_caps_mu_beamformee = ((vht_caps_info & DOT11_MU_BEAMFORMEE_CAP_MASK) != 0);
    /* vht supported mcs set */
    info->vht_caps_rx_mcs_map = ((elem->data[5] << 8) | elem->data[4]);
}

static void
osw_parse_he_caps(const struct element *elem,
                  struct osw_assoc_req_info *info)
{
   if (elem->data[0] == DOT11_EXT_HE_CAPS) {
       info->he_caps_present = true;
       /* he phy capabilities information */
       const uint8_t phy_caps = elem->data[7];
       info->he_caps_2ghz_40 = ((phy_caps & DOT11_EXT_HE_CAPS_PHY_2GHZ_40) != 0);
       info->he_caps_40_80 = ((phy_caps & DOT11_EXT_HE_CAPS_PHY_40_80) != 0);
       info->he_caps_160 = ((phy_caps & DOT11_EXT_HE_CAPS_PHY_160) != 0);
       info->he_caps_160_8080 = ((phy_caps & DOT11_EXT_HE_CAPS_PHY_160_8080) != 0);
       /* supported he-mcs and nss set */
       /* <= 80 mhz */
       info->he_caps_rx_mcs_map_le_80 = ((elem->data[19] << 8) | elem->data[18]);
       /* 160 mhz */
       if (info->he_caps_160 == true) {
           info->he_caps_rx_mcs_map_160_present = true;
           info->he_caps_rx_mcs_map_160 = ((elem->data[23] << 8) | elem->data[22]);
       }
       /* 160 and 80+80 mhz */
       if (info->he_caps_160 == false &&
           info->he_caps_160_8080 == true) {
           info->he_caps_rx_mcs_map_8080_present = true;
           info->he_caps_rx_mcs_map_8080 = ((elem->data[23] << 8) | elem->data[22]);
       }
       else if (info->he_caps_160 == true &&
                info->he_caps_160_8080 == true) {
           info->he_caps_rx_mcs_map_8080_present = true;
           info->he_caps_rx_mcs_map_8080 = ((elem->data[27] << 8) | elem->data[26]);
       }
   }
}

static void
osw_parse_wfa_ie_mbo(const uint8_t *buf,
                     const size_t len,
                     struct osw_assoc_req_info *info)
{
    const struct element *elem;
    if (len < 2) return;
    info->mbo_capable = true;
    for_each_ie(elem, buf, len) {
        switch(elem->id) {
            case WFA_MBO_ATTR_ID_NON_PREF_CHAN_REP:
                /* TODO Parse Non-Preferred Channels Report attribute */
                break;
            case WFA_MBO_ATTR_ID_CELL_DATA_PREF:
                WARN_ON(elem->datalen != 1);
                switch (elem->data[0]) {
                    case 1:
                        info->mbo_cell_capability = OSW_STA_CELL_AVAILABLE;
                        break;
                    case 2:
                        info->mbo_cell_capability = OSW_STA_CELL_NOT_AVAILABLE;
                        break;
                    case 3: /* fall through */
                    default:
                        info->mbo_cell_capability = OSW_STA_CELL_UNKNOWN;
                        break;
                }
                break;
            default:
                LOGD("osw: util: parse_wfa_ie_mbo: attribute id unknown (%d)", elem->id);
        }
    }
}

static void
osw_parse_vendor_specific_ie(const struct element *elem,
                             struct osw_assoc_req_info *info)
{
    /* OUI can be either 3 or 5 bytes long (OUI-24 vs OUI-36).
     * At the time of writing no OUI-36 is known to be
     * used in here. Assuming OUI is always OUI-24 */
    uint32_t oui = bin2oui24(elem->data, elem->datalen);
    size_t len = elem->datalen - 3;

    if (len <= 2) return;
    const uint8_t type = elem->data[3];
    len--;
    const uint8_t *attrs = &(elem->data[4]);
    switch (oui) {
        case WFA_VENDOR_SPECIFIC_OUI:
            switch (type) {
                case WFA_WNM_VENDOR_SPECIFIC_OUI_TYPE_MBO_OCE:
                    osw_parse_wfa_ie_mbo(attrs, len, info);
                break;
            }
        break;
    }
}

static void
osw_parse_eht_cap(struct osw_assoc_req_info *info,
                  const bool is_2ghz,
                  const bool is_non_ap,
                  const uint8_t *he_cap,
                  const uint8_t *eht_cap,
                  size_t he_cap_len,
                  size_t eht_cap_len)
{
    const bool non_eht_sta = (eht_cap == NULL);
    if (non_eht_sta) return;

    /* Note: 802.11be Draft 1.0 uses wording "20MHz-only
     * STA" but 802.11be Draft 3.0 uses "20MHz-only non-AP
     * STA". This means this is not backward compatible and
     * can't be made so. If a buggy encoding is seen in the
     * wild it's probably because of using a pre-802.11be
     * Draft-3.0 wirelss stack.
     */

    if (eht_cap_len < (2 + 4)) return;
    const uint32_t eht_phy_cap0 = DOT11_AS_LE32(eht_cap + 2);

    if (he_cap_len < 7) return;
    const uint16_t he_phy_info0 = he_cap[6];

    const bool is_not_2ghz = (is_2ghz == false);
    const bool is_eht_phy_cap_320mhz = !!(eht_phy_cap0 & DOT11_EXT_EHT_CAP_PHY_320MHZ);
    const bool is_2ghz_40mhz = (is_2ghz && !!(he_phy_info0 & DOT11_EXT_HE_CAPS_PHY_2GHZ_40));
    const bool is_2ghz_20mhz_only = (is_2ghz && !(he_phy_info0 & DOT11_EXT_HE_CAPS_PHY_2GHZ_40));
    const bool is_5_6ghz_40_80mhz = (is_not_2ghz && !!(he_phy_info0 & DOT11_EXT_HE_CAPS_PHY_40_80));
    const bool is_5_6ghz_160mhz = (is_not_2ghz && !!(he_phy_info0 & DOT11_EXT_HE_CAPS_PHY_160));
    const bool is_5_6ghz_160_8080mhz = (is_not_2ghz && !!(he_phy_info0 & DOT11_EXT_HE_CAPS_PHY_160_8080));
    const bool is_5_6ghz_20mhz_only = !is_5_6ghz_40_80mhz
                                   && !is_5_6ghz_160mhz
                                   && !is_5_6ghz_160_8080mhz;

    const bool is_mcs_20only_non_ap = (is_2ghz_20mhz_only || is_5_6ghz_20mhz_only);
    const bool is_mcs_80 = is_non_ap
                         ? is_2ghz_40mhz
                         : true; /* AP always has it */
    const bool is_mcs_160 = is_5_6ghz_160mhz;
    const bool is_mcs_320 = is_eht_phy_cap_320mhz;

    const uint8_t *mcs_set = eht_cap
                       + DOT11_EXT_EHT_CAP_MAC_LEN
                       + DOT11_EXT_EHT_CAP_PHY_LEN;
    size_t len = eht_cap_len
               - DOT11_EXT_EHT_CAP_MAC_LEN
               - DOT11_EXT_EHT_CAP_PHY_LEN;

    const uint8_t *mcs_20only_non_ap = NULL;
    if (is_mcs_20only_non_ap) {
        if (len >= 4) {
            mcs_20only_non_ap = mcs_set;
            mcs_set += 4;
            len -= 4;
        }
    }

    const uint8_t *mcs_80 = NULL;
    if (is_mcs_80) {
        if (len < 3) return;

        mcs_80 = mcs_set;
        mcs_set += 3;
        len -= 3;
    }

    const uint8_t *mcs_160 = NULL;
    if (is_mcs_160) {
        if (len < 3) return;

        mcs_160 = mcs_set;
        mcs_set += 3;
        len -= 3;
    }

    const uint8_t *mcs_320 = NULL;
    if (is_mcs_320) {
        if (len < 3) return;

        mcs_320 = mcs_set;
        mcs_set += 3;
        len -= 3;
    }

    uint8_t max_mcs = 0;
    uint8_t max_nss = 0;
    enum osw_channel_width max_chwidth = OSW_CHANNEL_20MHZ;

    const uint8_t mcs[] = {
        mcs_20only_non_ap && mcs_20only_non_ap[0] ? 7 : 0,
        mcs_20only_non_ap && mcs_20only_non_ap[1] ? 9 : 0,
        mcs_20only_non_ap && mcs_20only_non_ap[2] ? 11 : 0,
        mcs_20only_non_ap && mcs_20only_non_ap[3] ? 13 : 0,

        mcs_80 && mcs_80[0] ? 9 : 0,
        mcs_80 && mcs_80[1] ? 11 : 0,
        mcs_80 && mcs_80[2] ? 13 : 0,

        mcs_160 && mcs_160[0] ? 9 : 0,
        mcs_160 && mcs_160[1] ? 11 : 0,
        mcs_160 && mcs_160[2] ? 13 : 0,

        mcs_320 && mcs_320[0] ? 9 : 0,
        mcs_320 && mcs_320[1] ? 11 : 0,
        mcs_320 && mcs_320[2] ? 13 : 0,
    };

    const uint8_t nss[] = {
        mcs_20only_non_ap ? ((mcs_20only_non_ap[0] >> 0) & 0x0f) : 0,
        mcs_20only_non_ap ? ((mcs_20only_non_ap[0] >> 4) & 0x0f) : 0,
        mcs_20only_non_ap ? ((mcs_20only_non_ap[1] >> 0) & 0x0f) : 0,
        mcs_20only_non_ap ? ((mcs_20only_non_ap[1] >> 4) & 0x0f) : 0,
        mcs_20only_non_ap ? ((mcs_20only_non_ap[2] >> 0) & 0x0f) : 0,
        mcs_20only_non_ap ? ((mcs_20only_non_ap[2] >> 4) & 0x0f) : 0,
        mcs_20only_non_ap ? ((mcs_20only_non_ap[3] >> 0) & 0x0f) : 0,
        mcs_20only_non_ap ? ((mcs_20only_non_ap[3] >> 4) & 0x0f) : 0,

        mcs_80 ? ((mcs_80[0] >> 0) & 0x0f) : 0,
        mcs_80 ? ((mcs_80[0] >> 4) & 0x0f) : 0,
        mcs_80 ? ((mcs_80[1] >> 0) & 0x0f) : 0,
        mcs_80 ? ((mcs_80[1] >> 4) & 0x0f) : 0,
        mcs_80 ? ((mcs_80[2] >> 0) & 0x0f) : 0,
        mcs_80 ? ((mcs_80[2] >> 4) & 0x0f) : 0,

        mcs_160 ? ((mcs_160[0] >> 0) & 0x0f) : 0,
        mcs_160 ? ((mcs_160[0] >> 4) & 0x0f) : 0,
        mcs_160 ? ((mcs_160[1] >> 0) & 0x0f) : 0,
        mcs_160 ? ((mcs_160[1] >> 4) & 0x0f) : 0,
        mcs_160 ? ((mcs_160[2] >> 0) & 0x0f) : 0,
        mcs_160 ? ((mcs_160[2] >> 4) & 0x0f) : 0,

        mcs_320 ? ((mcs_320[0] >> 0) & 0x0f) : 0,
        mcs_320 ? ((mcs_320[0] >> 4) & 0x0f) : 0,
        mcs_320 ? ((mcs_320[1] >> 0) & 0x0f) : 0,
        mcs_320 ? ((mcs_320[1] >> 4) & 0x0f) : 0,
        mcs_320 ? ((mcs_320[2] >> 0) & 0x0f) : 0,
        mcs_320 ? ((mcs_320[2] >> 4) & 0x0f) : 0,
    };

    size_t i;

    for (i = 0; i < ARRAY_SIZE(mcs); i++) {
        if (mcs[i] > max_mcs) {
            max_mcs = mcs[i];
        }
    }

    for (i = 0; i < ARRAY_SIZE(nss); i++) {
        if (nss[i] > max_nss) {
            max_nss = nss[i];
        }
    }

    if (is_eht_phy_cap_320mhz) {
        max_chwidth = OSW_CHANNEL_320MHZ;
    }


    info->eht_cap_mcs = max_mcs;
    info->eht_cap_nss = max_nss;
    info->eht_cap_chwidth = max_chwidth;
}

static void
osw_parse_eht_op(struct osw_assoc_req_info *info,
                 const uint8_t *eht_op,
                 size_t eht_op_len)
{
    const bool eht_op_is_not_present = (eht_op == NULL);
    if (eht_op_is_not_present) return;

    if (eht_op_len < 1) return;
    const uint8_t eht_op_params = eht_op[0];
    eht_op += 1;
    eht_op_len -= 1;

    if (eht_op_len < 4) return;
    const uint8_t *eht_basic_mcs = eht_op;
    eht_op += 4;
    eht_op_len -= 4;

    (void)eht_basic_mcs;

    const bool eht_op_info_present = (eht_op_params & DOT11_EXT_EHT_OP_INFO);
    if (eht_op_info_present) {
        if (eht_op_len < 3) return;
        const uint8_t control = eht_op[0];
        const uint8_t ccfs0 = eht_op[1];
        const uint8_t ccfs1 = eht_op[2];
        eht_op += 3;
        eht_op_len -= 3;

        const uint8_t eht_op_width = control & DOT11_EXT_EHT_OP_INFO_CTL_WIDTH;
        enum osw_channel_width width = OSW_CHANNEL_20MHZ;
        switch (eht_op_width) {
            case 0: width = OSW_CHANNEL_20MHZ; break;
            case 1: width = OSW_CHANNEL_40MHZ; break;
            case 2: width = OSW_CHANNEL_80MHZ; break;
            case 3: width = OSW_CHANNEL_160MHZ; break;
            case 4: width = OSW_CHANNEL_320MHZ; break;
        }

        info->eht_op_chwidth_present = true;
        info->eht_op_chwidth = width;

        (void)ccfs0;
        (void)ccfs1;
    }
}

static void
osw_parse_mle(struct osw_assoc_req_info *info,
              const uint8_t *mle,
              size_t mle_len)
{
    if (mle == NULL) return;
    if (mle_len < 2) return;
    const uint16_t mle_ctrl = le16toh(*(const uint16_t *)mle);
    const uint16_t mle_ctrl_type = (mle_ctrl & 0x0007) >> 0;
    const uint16_t mle_ctrl_rsvd = (mle_ctrl & 0x0008) >> 3;
    const uint16_t mle_ctrl_presence = (mle_ctrl & 0xfff0) >> 4;
    (void)mle_ctrl_type;
    mle_len -= 2;
    mle += 2;
    (void)mle_ctrl_rsvd;
    const bool mle_ctrl_has_link_info = !!(mle_ctrl_presence & BIT(0));
    const bool mle_ctrl_has_bss_param = !!(mle_ctrl_presence & BIT(1));
    const bool mle_ctrl_has_medium_sync = !!(mle_ctrl_presence & BIT(2));
    const bool mle_ctrl_has_eml_cap = !!(mle_ctrl_presence & BIT(3));
    const bool mle_ctrl_has_mld_cap = !!(mle_ctrl_presence & BIT(4));
    const bool mle_ctrl_has_ap_mld_id = !!(mle_ctrl_presence & BIT(5));
    const bool mle_ctrl_has_ext_mld_cap = !!(mle_ctrl_presence & BIT(6));
    /* 5 remaining bits are unused */
    (void)mle_ctrl_has_link_info;
    (void)mle_ctrl_has_bss_param;
    (void)mle_ctrl_has_medium_sync;
    (void)mle_ctrl_has_eml_cap;
    (void)mle_ctrl_has_mld_cap;
    (void)mle_ctrl_has_ap_mld_id;
    (void)mle_ctrl_has_ext_mld_cap;

    if (mle_len < 1) return;
    const uint8_t common_len = mle[0];

    if (mle_len < common_len) return;
    /* can parse common info here */
    mle_len -= common_len;
    mle += common_len;

    while (mle_len >= 2) {
        uint8_t subelem_id = mle[0];
        uint8_t subelem_len = mle[1];
        const uint8_t *subelem = &mle[2];
        mle += 2;
        mle_len -= 2;
        if (mle_len < subelem_len) return;
        mle += subelem_len;
        mle_len -= subelem_len;

        switch (subelem_id) {
            case 0: /* per-sta profile */
                {
                    if (subelem_len < 2) return;
                    const uint16_t sta_ctrl = le16toh(*(const uint16_t *)subelem);
                    subelem_len -= 2;
                    subelem += 2;

                    const uint16_t link_id = (sta_ctrl & 0x000f) >> 0;
                    (void)link_id;

                    if (subelem_len < 1) return;
                    const uint8_t sta_info_len = subelem[0];
                    if (subelem_len < sta_info_len) return;
                    subelem_len -= sta_info_len;
                    subelem += sta_info_len;

                    if (subelem_len < 2) return;
                    const uint16_t capab_info = le16toh(*(const uint16_t *)subelem);
                    (void)capab_info;
                    subelem_len -= 2;
                    subelem += 2;

                    struct osw_assoc_req_info info_partner;
                    const bool ok = osw_parse_assoc_req_ies(subelem, subelem_len, &info_partner);
                    if (ok) {
                        info->eht_cap_mcs = MAX(info->eht_cap_mcs, info_partner.eht_cap_mcs);
                        info->eht_cap_nss = MAX(info->eht_cap_nss, info_partner.eht_cap_nss);
                        info->eht_cap_chwidth = MAX(info->eht_cap_chwidth, info_partner.eht_cap_chwidth);
                        info->per_sta_profiles++;
                    }
                }
                break;
        }
    }
}

bool
osw_parse_assoc_req_ies(const void *assoc_req_ies,
                        size_t assoc_req_ies_len,
                        struct osw_assoc_req_info *info)
{
    const struct element *elem;
    const uint8_t *ies;
    size_t len;

    if (assoc_req_ies == NULL || assoc_req_ies_len <= 0) {
        LOGT("osw: util: parse_assoc_req: assoc_req_ies is NULL or assoc_req_ies_len <= 0");
        return false;
    }

    memset(info, 0, sizeof(*info));
    ies = assoc_req_ies;
    len = assoc_req_ies_len;

    const uint8_t *he_cap = NULL;
    const uint8_t *eht_op = NULL;
    const uint8_t *eht_cap = NULL;
    uint8_t *mle = NULL;
    uint8_t **frag = NULL;
    uint8_t he_cap_len = 0;
    uint8_t eht_op_len = 0;
    uint8_t eht_cap_len = 0;
    size_t mle_len = 0;
    size_t *frag_len = NULL;

    for_each_ie(elem, ies, len) {
        if (frag != NULL && frag_len != NULL && elem->id == DOT11_FRAGMENT) {
            const size_t datalen = elem->datalen;
            const void *data = &elem->data[0];
            if (datalen > 0) {
                const size_t new_len = (*frag_len) + datalen;
                *frag = REALLOC(*frag, new_len);
                void *tail = (*frag) + (*frag_len);
                *frag_len = new_len;
                memcpy(tail, data, datalen);
            }
            if (datalen < 255) {
                frag = NULL;
                frag_len = NULL;
            }
        }
        else {
            frag = NULL;
            frag_len = NULL;
        }
        switch(elem->id) {
            case DOT11_EXTENDED_CAPS_TAG:
                info->wnm_bss_trans = ((elem->data[2] & DOT11_EXTENDED_CAPS_BTM) != 0);
                break;
            case DOT11_RM_ENABLED_CAPS_TAG:
                info->rrm_neighbor_link_meas = ((elem->data[0] & DOT11_RM_ENABLED_CAPS_LINK_MEAS) != 0);
                info->rrm_neighbor_bcn_pas_meas = ((elem->data[0] & DOT11_RM_ENABLED_CAPS_BCN_PAS_MEAS) != 0);
                info->rrm_neighbor_bcn_act_meas = ((elem->data[0] & DOT11_RM_ENABLED_CAPS_BCN_ACT_MEAS) != 0);
                info->rrm_neighbor_bcn_tab_meas = ((elem->data[0] & DOT11_RM_ENABLED_CAPS_BCN_TAB_MEAS) != 0);
                info->rrm_neighbor_lci_meas = ((elem->data[1] & DOT11_RM_ENABLED_CAPS_LCI_MEAS) != 0);
                info->rrm_neighbor_ftm_range_rep = ((elem->data[4] & DOT11_RM_ENABLED_CAPS_FTM_RANGE_REP) != 0);
                break;
            case DOT11_SUPPORTED_OP_CLASSES:
                osw_parse_supported_op_classes(elem,
                                               info);
                break;
            case DOT11_SUPPORTED_CHANNELS:
                osw_parse_supported_channels(elem,
                                             info);
                break;
            case DOT11_POWER_CAP:
                info->min_tx_power = elem->data[0];
                info->max_tx_power = elem->data[1];
                break;
            case DOT11_HT_CAPS:
                osw_parse_ht_caps(elem,
                                  info);
                break;
            case DOT11_VHT_CAPS:
                osw_parse_vht_caps(elem,
                                   info);
                break;
            case DOT11_ELEM_ID_VENDOR_SPECIFIC:
                osw_parse_vendor_specific_ie(elem,
                                             info);
                break;
            case DOT11_ELEM_ID_EXT:
                if (elem->datalen < 1) break;
                const uint8_t ext_id = elem->data[0];
                const uint8_t *data = &elem->data[1];
                const uint8_t datalen = elem->datalen - 1;

                osw_parse_he_caps(elem,
                                  info);

                switch (ext_id) {
                    case DOT11_EXT_HE_CAPS:
                        he_cap = data;
                        he_cap_len = datalen;
                        break;
                    case DOT11_EXT_EHT_OP:
                        eht_op = data;
                        eht_op_len = datalen;
                        break;
                    case DOT11_EXT_EHT_CAP:
                        eht_cap = data;
                        eht_cap_len = datalen;
                        break;
                    case DOT11_EXT_ML:
                        mle = MEMNDUP(data, datalen);
                        mle_len = datalen;
                        if (datalen == 254) {
                            frag = &mle;
                            frag_len = &mle_len;
                        }
                        break;
                }
                break;
        }
    }

    const bool is_2ghz = osw_assoc_req_is_2ghz(info);
    const bool is_non_ap = false; /* fixme */

    osw_parse_eht_cap(info, is_2ghz, is_non_ap, he_cap, eht_cap, he_cap_len, eht_cap_len);
    osw_parse_eht_op(info, eht_op, eht_op_len);
    osw_parse_mle(info, mle, mle_len);
    FREE(mle);

    /* FIXME:
     *  - add ccfs support?
     *  - add non-AP / AP input config
     *  - add 2/5/6ghz input optional config
     *  - rework parsing to collect+comprehend instead of
     *    applying comprehension on subsequent _get_foo()
     *    calls
     */

    return true;
}

void
osw_parsed_ies_from_buf(struct osw_parsed_ies *parsed,
                        const void *buf,
                        size_t buf_len)
{
    const struct element *elem;
    for_each_ie (elem, buf, buf_len) {
        switch (elem->id) {
            default:
                parsed->base[elem->id].datalen = elem->datalen;
                parsed->base[elem->id].data = elem->data;
                break;
            case DOT11_ELEM_ID_EXT:
                if (elem->datalen < 1) break;
                const uint8_t ext_id = elem->data[0];
                const uint8_t *data = &elem->data[1];
                const uint8_t datalen = elem->datalen - 1;
                parsed->ext[ext_id].datalen = datalen;
                parsed->ext[ext_id].data = data;
                break;
        }
    }
}

const struct osw_channel *
osw_channel_select_wider(const struct osw_channel *a,
                         const struct osw_channel *b)
{
    if (a->control_freq_mhz == 0) return b;
    if (b->control_freq_mhz == 0) return a;
    return (a->width > b->width) ? a : b;
}

void
osw_parsed_ies_get_channels(const struct osw_parsed_ies *parsed,
                            struct osw_channel *non_ht_channel,
                            struct osw_channel *ht_channel,
                            struct osw_channel *vht_channel,
                            struct osw_channel *he_channel,
                            struct osw_channel *eht_channel)
{
    enum osw_band opband = OSW_BAND_UNDEFINED;

    if (parsed->base[DOT11_SUPPORTED_OP_CLASSES].data != NULL &&
        parsed->base[DOT11_SUPPORTED_OP_CLASSES].datalen >= 1) {
        const struct dot11_elem_supp_op_class *op_classes = parsed->base[DOT11_SUPPORTED_OP_CLASSES].data;
        opband = osw_op_class_to_band(op_classes->current_op_class);
    }

    if (parsed->base[DOT11_EID_DSSS].data != NULL &&
        parsed->base[DOT11_EID_DSSS].datalen >= sizeof(struct dot11_elem_dsss)) {
        const struct dot11_elem_dsss *dsss = parsed->base[DOT11_EID_DSSS].data;
        const uint8_t chan = dsss->chan;
        const enum osw_band band = opband ?: (chan >= 30)
                                 ? OSW_BAND_5GHZ
                                 : OSW_BAND_2GHZ;
        const int freq = osw_chan_to_freq(band, chan);
        if (non_ht_channel != NULL) {
            MEMZERO(*non_ht_channel);
            non_ht_channel->control_freq_mhz = freq;
            non_ht_channel->center_freq0_mhz = freq;
            non_ht_channel->center_freq1_mhz = 0;
            non_ht_channel->width = OSW_CHANNEL_20MHZ;
        }
    }

    if (parsed->base[DOT11_EID_HT_OP].data != NULL &&
        parsed->base[DOT11_EID_HT_OP].datalen >= sizeof(struct dot11_elem_ht_op)) {
        const struct dot11_elem_ht_op *ht_op = parsed->base[DOT11_EID_HT_OP].data;
        const uint8_t chan = ht_op->primary_chan;
        const uint8_t sta_ch_width = FIELD_GET(DOT11_ELEM_HT_OP_INFO0_STA_CH_WIDTH,
                                               ht_op->info[0]);
        const uint8_t sec_ch_off = sta_ch_width
                                 ? FIELD_GET(DOT11_ELEM_HT_OP_INFO0_SEC_CH_OFF,
                                             ht_op->info[0])
                                 : DOT11_ELEM_HT_OP_SEC_CH_OFF_SCN;
        const enum osw_band band = opband ?: (chan >= 30)
                                 ? OSW_BAND_5GHZ
                                 : OSW_BAND_2GHZ;
        const int freq = osw_chan_to_freq(band, chan);
        if (ht_channel != NULL) {
            ht_channel->control_freq_mhz = freq;
            ht_channel->center_freq1_mhz = 0;

            switch (sec_ch_off) {
                case DOT11_ELEM_HT_OP_SEC_CH_OFF_SCA:
                    ht_channel->center_freq0_mhz = freq + 10;
                    ht_channel->width = OSW_CHANNEL_40MHZ;
                    break;
                case DOT11_ELEM_HT_OP_SEC_CH_OFF_SCB:
                    ht_channel->center_freq0_mhz = freq - 10;
                    ht_channel->width = OSW_CHANNEL_40MHZ;
                    break;
                case DOT11_ELEM_HT_OP_SEC_CH_OFF_SCN:
                    ht_channel->center_freq0_mhz = freq;
                    ht_channel->width = OSW_CHANNEL_20MHZ;
                    break;
            }
        }
    }

    if (parsed->base[DOT11_EID_VHT_OP].data != NULL &&
        parsed->base[DOT11_EID_VHT_OP].datalen >= sizeof(struct dot11_elem_vht_op)) {
        const struct dot11_elem_vht_op *vht_op = parsed->base[DOT11_EID_VHT_OP].data;

        if (vht_channel != NULL) {
            if (ht_channel != NULL) {
                const enum osw_band band = OSW_BAND_5GHZ;
                const int seg0 = osw_chan_to_freq(band, vht_op->info.ch_center_freq_seg0);
                const int seg1 = osw_chan_to_freq(band, vht_op->info.ch_center_freq_seg1);
                const int diff = seg0 > seg1 ? (seg0 - seg1) : (seg1 - seg0);

                vht_channel->control_freq_mhz = ht_channel->control_freq_mhz
                                             ?: non_ht_channel->control_freq_mhz;

                switch (vht_op->info.ch_width) {
                    case DOT11_ELEM_VHT_OP_INFO_CH_WIDTH_2040:
                        vht_channel->width = ht_channel->width;
                        vht_channel->center_freq0_mhz = ht_channel->center_freq0_mhz;
                        vht_channel->center_freq1_mhz = 0;
                        break;
                    case DOT11_ELEM_VHT_OP_INFO_CH_WIDTH_80:
                        if (vht_op->info.ch_center_freq_seg1 == 0) {
                            vht_channel->width = OSW_CHANNEL_80MHZ;
                            vht_channel->center_freq0_mhz = seg0;
                            vht_channel->center_freq1_mhz = 0;
                        }
                        else if (diff == 40) {
                            vht_channel->width = OSW_CHANNEL_160MHZ;
                            vht_channel->center_freq0_mhz = seg1;
                            vht_channel->center_freq1_mhz = 0;
                        }
                        else {
                            vht_channel->width = OSW_CHANNEL_80P80MHZ;
                            vht_channel->center_freq0_mhz = seg0;
                            vht_channel->center_freq1_mhz = seg1;
                        }
                        break;
                    case DOT11_ELEM_VHT_OP_INFO_CH_WIDTH_160_DEPRECATED:
                        vht_channel->width = OSW_CHANNEL_160MHZ;
                        vht_channel->center_freq0_mhz = seg0;
                        break;
                    case DOT11_ELEM_VHT_OP_INFO_CH_WIDTH_8080_DEPRECATED:
                        vht_channel->width = OSW_CHANNEL_80P80MHZ;
                        vht_channel->center_freq0_mhz = seg0;
                        vht_channel->center_freq1_mhz = seg1;
                        break;
                }
            }
        }
    }

    if (parsed->ext[DOT11_EID_EXT_HE_OP].data != NULL &&
        parsed->ext[DOT11_EID_EXT_HE_OP].datalen >= sizeof(struct dot11_elem_he_op)) {
        const struct dot11_elem_vht_op_info *vht_op_info;
        const struct dot11_elem_he_op_max_cohost *max_cohost;
        const struct dot11_elem_he_op_6g_info *band_6g_info;
        const bool ok = dot11_elem_he_op_parse(parsed->ext[DOT11_EID_EXT_HE_OP].data,
                                               parsed->ext[DOT11_EID_EXT_HE_OP].datalen,
                                               &vht_op_info,
                                               &max_cohost,
                                               &band_6g_info);
        if (ok && he_channel != NULL) {
            if (band_6g_info != NULL) {
                const int chan = band_6g_info->primary_channel;
                const int freq = osw_chan_to_freq(OSW_BAND_6GHZ, chan);
                const int seg0 = osw_chan_to_freq(OSW_BAND_6GHZ, band_6g_info->ch_center_freq_seg0);
                const int seg1 = osw_chan_to_freq(OSW_BAND_6GHZ, band_6g_info->ch_center_freq_seg1);
                const int diff = seg0 > seg1 ? (seg0 - seg1) : (seg1 - seg0);
                const int width = FIELD_GET(DOT11_ELEM_HE_OP_6G_INFO_CONTROL_CH_WIDTH,
                                            band_6g_info->control);

                he_channel->control_freq_mhz = freq;

                switch (width) {
                    case DOT11_ELEM_HE_OP_6G_INFO_CONTROL_CH_WIDTH_20:
                        he_channel->width = OSW_CHANNEL_20MHZ;
                        he_channel->center_freq0_mhz = freq;
                        break;
                    case DOT11_ELEM_HE_OP_6G_INFO_CONTROL_CH_WIDTH_40:
                        he_channel->width = OSW_CHANNEL_40MHZ;
                        he_channel->center_freq0_mhz = seg0;
                        he_channel->center_freq1_mhz = 0;
                        break;
                    case DOT11_ELEM_HE_OP_6G_INFO_CONTROL_CH_WIDTH_80:
                        he_channel->width = OSW_CHANNEL_80MHZ;
                        he_channel->center_freq0_mhz = seg0;
                        he_channel->center_freq1_mhz = 0;
                        break;
                    case DOT11_ELEM_HE_OP_6G_INFO_CONTROL_CH_WIDTH_160_8080:
                        switch (diff) {
                            case 40:
                                he_channel->width = OSW_CHANNEL_160MHZ;
                                he_channel->center_freq0_mhz = seg1;
                                he_channel->center_freq1_mhz = 0;
                                break;
                            case 80:
                                he_channel->width = OSW_CHANNEL_80P80MHZ;
                                he_channel->center_freq0_mhz = seg0;
                                he_channel->center_freq1_mhz = seg1;
                                break;
                        }
                        break;

                }
            }
        }
    }

    if (parsed->ext[DOT11_EID_EXT_EHT_OP].data != NULL &&
        parsed->ext[DOT11_EID_EXT_EHT_OP].datalen >= sizeof(struct dot11_elem_eht_op)) {
        const struct dot11_elem_eht_op_info *info;
        const struct dot11_elem_eht_op_subchan *subchan;
        const bool ok = dot11_elem_eht_op_parse(parsed->ext[DOT11_EID_EXT_EHT_OP].data,
                                                parsed->ext[DOT11_EID_EXT_EHT_OP].datalen,
                                                &info,
                                                &subchan);
        if (ok && eht_channel != NULL) {
            if (info != NULL) {
                /* Technically 320MHz is defined for 6GHz only, so EHT
                 * Op should appear only there in practice as either
                 * HE Op can express 160MHz already, or HT/VHT will be
                 * present on 2.4G and 5G. However, to play it safe
                 * against vendor extensions, be careful and don't
                 * assume the band to be 6GHz.
                 */
                const enum osw_band band = osw_freq_to_band(non_ht_channel->control_freq_mhz ?:
                                                            ht_channel->control_freq_mhz ?:
                                                            vht_channel->control_freq_mhz ?:
                                                            he_channel->control_freq_mhz);
                const int width = FIELD_GET(DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH,
                                            info->control);
                const int seg0 = osw_chan_to_freq(band, info->ccfs0);
                const int seg1 = osw_chan_to_freq(band, info->ccfs1);

                eht_channel->control_freq_mhz = he_channel->control_freq_mhz
                                             ?: vht_channel->control_freq_mhz
                                             ?: ht_channel->control_freq_mhz
                                             ?: non_ht_channel->control_freq_mhz;

                switch (width) {
                    case DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH_20:
                        eht_channel->width = OSW_CHANNEL_20MHZ;
                        eht_channel->center_freq0_mhz = seg0;
                        eht_channel->center_freq1_mhz = 0;
                        break;
                    case DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH_40:
                        eht_channel->width = OSW_CHANNEL_40MHZ;
                        eht_channel->center_freq0_mhz = seg0;
                        eht_channel->center_freq1_mhz = 0;
                        break;
                    case DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH_80:
                        eht_channel->width = OSW_CHANNEL_80MHZ;
                        eht_channel->center_freq0_mhz = seg0;
                        eht_channel->center_freq1_mhz = 0;
                        break;
                    case DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH_160:
                        eht_channel->width = OSW_CHANNEL_160MHZ;
                        eht_channel->center_freq0_mhz = seg1;
                        eht_channel->center_freq1_mhz = 0;
                        break;
                    case DOT11_ELEM_EHT_OP_INFO_CONTROL_CH_WIDTH_320:
                        eht_channel->width = OSW_CHANNEL_320MHZ;
                        eht_channel->center_freq0_mhz = seg1;
                        eht_channel->center_freq1_mhz = 0;
                        break;
                }
            }
        }
    }
}

double
osw_periodic_get_next(const double interval_seconds,
                      const double offset_seconds,
                      const double now)
{
    if (interval_seconds <= 0) return 0;
    if (fabs(offset_seconds) >= interval_seconds) return 0;
    return ((floor(now / interval_seconds) + 1) * interval_seconds) + offset_seconds;
}

void *buf_pull(void **buf, ssize_t *remaining, ssize_t how_much)
{
    if (WARN_ON(buf == NULL)) return NULL;
    if (WARN_ON(remaining == NULL)) return NULL;
    if (WARN_ON(*buf == NULL)) return NULL;
    if (WARN_ON(how_much <= 0)) return NULL;
    void *p = *buf;
    (*remaining) -= how_much;
    (*buf) += how_much;
    if (*remaining < 0) return NULL;
    return p;
}

const void *buf_pull_const(const void **buf, ssize_t *remaining, ssize_t how_much)
{
    /* This is intended to be a wrapper for const callers
     * only - hence the cast.
     */
    return buf_pull((void **)buf, remaining, how_much);
}

bool buf_write(void *dst, const void *src, size_t len)
{
    if (dst == NULL) return false;
    if (src == NULL) return false;
    memcpy(dst, src, len);
    return true;
}

ssize_t buf_len(const void *start, const void *end)
{
    return end - start;
}

bool buf_restore(void **buf, ssize_t *remaining, void *old_buf)
{
    if (buf == NULL) return false;
    if (*buf == NULL) return false;
    const ssize_t ahead_by = (*buf) - old_buf;
    if (ahead_by < 0) return false;
    (*buf) -= ahead_by;
    (*remaining) += ahead_by;
    return true;
}

#include "osw_util_ut.c"
