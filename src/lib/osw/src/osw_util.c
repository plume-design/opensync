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
#include <endian.h>
#include <osw_types.h>
#include <osw_util.h>
#include <const.h>
#include <log.h>

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
#define DOT11_ELEM_ID_EXT 0xff
#define DOT11_EXT_HE_CAPS 0x23
#define DOT11_EXT_HE_CAPS_PHY_40_80 (1 << 2)
#define DOT11_EXT_HE_CAPS_PHY_160 (1 << 3)
#define DOT11_EXT_HE_CAPS_PHY_160_8080 (1 << 4)
#define DOT11_POWER_CAP 0x21

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
osw_vht_he_mcs_to_max_nss(const uint16_t mcs_field)
{
    unsigned int max_nss = 0;
    unsigned int n;
    for (n = 0; n < 8; n++) {
        const unsigned int shift =  2 * n;
        const unsigned int mcs_tuple = ((mcs_field & (0x03 << shift)) >> shift);
        const bool mcs_available = (mcs_tuple != 0x03);
        if (mcs_available == false) continue;
        if (n > max_nss) max_nss = n;
    }
    return max_nss;
}

unsigned int
osw_vht_he_mcs_to_max_mcs(const uint16_t mcs_field)
{
    unsigned int max_mcs = 0;
    unsigned int n;
    for (n = 0; n < 8; n++) {
        const unsigned int shift =  2 * n;
        const unsigned int mcs_tuple = ((mcs_field & (0x03 << shift)) >> shift);
        const bool mcs_available = (mcs_tuple != 0x03);
        if (mcs_available == false) continue;
        const unsigned int mcs_decoded = 0x07 + mcs_tuple;
        if (mcs_decoded > max_mcs) max_mcs = mcs_decoded;
    }
    return max_mcs;
}

enum osw_channel_width
osw_assoc_req_to_max_chwidth(const struct osw_assoc_req_info *info)
{
    assert(info != NULL);
    enum osw_channel_width ht_max_chwidth = OSW_CHANNEL_20MHZ;
    enum osw_channel_width vht_max_chwidth = OSW_CHANNEL_20MHZ;
    enum osw_channel_width he_max_chwidth = OSW_CHANNEL_20MHZ;

    if (info->ht_caps_present == true) {
        if (info->ht_caps_40) ht_max_chwidth = OSW_CHANNEL_40MHZ;
    }
    if (info->vht_caps_present == true) {
        vht_max_chwidth = OSW_CHANNEL_80MHZ;
        if (info->vht_caps_sup_chan_w_set == 1) vht_max_chwidth = OSW_CHANNEL_160MHZ;
        if (info->vht_caps_sup_chan_w_set == 2) vht_max_chwidth = OSW_CHANNEL_80P80MHZ;
    }
    if (info->he_caps_present == true) {
        if (info->he_caps_40_80 == true) he_max_chwidth = OSW_CHANNEL_80MHZ;
        if (info->he_caps_160 == true) he_max_chwidth = OSW_CHANNEL_160MHZ;
        if (info->he_caps_160_8080 == true) he_max_chwidth = OSW_CHANNEL_80P80MHZ;
    }

    /* This abuses enums as ints to derive max value */
    enum osw_channel_width max_chwidth = OSW_CHANNEL_20MHZ;
    if (ht_max_chwidth > max_chwidth) max_chwidth = ht_max_chwidth;
    if (vht_max_chwidth > max_chwidth) max_chwidth = vht_max_chwidth;
    if (he_max_chwidth > max_chwidth) max_chwidth = he_max_chwidth;
    return max_chwidth;
}

unsigned int
osw_assoc_req_to_max_nss(const struct osw_assoc_req_info *info)
{
    ASSERT(info != NULL, "");
    unsigned int ht_max_nss = 1;
    unsigned int vht_max_nss = 1;
    unsigned int he_max_nss = 1;

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
    return max_streams;
}

unsigned int
osw_assoc_req_to_max_mcs(const struct osw_assoc_req_info *info)
{
    ASSERT(info != NULL, "");
    unsigned int ht_max_mcs = 0;
    unsigned int vht_max_mcs = 0;
    unsigned int he_max_mcs = 0;

    if (info->ht_caps_present == true) {
        unsigned int i;
        for (i = 0; i < sizeof(info->ht_caps_rx_mcs); i++) {
            unsigned int j;
            const uint8_t mcs_byte = info->ht_caps_rx_mcs[i];
            for (j = 0; j < 8; j++) {
                bool mcs_available = ((mcs_byte & (1 << j)) != 0);
                if (mcs_available == false) continue;
                unsigned int mcs_idx = (i * 8) + j;
                if (mcs_idx > ht_max_mcs) ht_max_mcs = mcs_idx;
            }
        }
    }
    if (info->vht_caps_present == true) {
        vht_max_mcs = osw_vht_he_mcs_to_max_mcs(info->vht_caps_rx_mcs_map);
    }
    if (info->he_caps_present == true) {
        he_max_mcs = osw_vht_he_mcs_to_max_mcs(info->he_caps_rx_mcs_map_le_80);
        if (info->he_caps_rx_mcs_map_160_present == true) {
            const unsigned int mcs = osw_vht_he_mcs_to_max_mcs(info->he_caps_rx_mcs_map_160);
            if (mcs > he_max_mcs) he_max_mcs = mcs;
        }
        if (info->he_caps_rx_mcs_map_8080_present == true) {
            const unsigned int mcs = osw_vht_he_mcs_to_max_mcs(info->he_caps_rx_mcs_map_8080);
            if (mcs > he_max_mcs) he_max_mcs = mcs;
        }
    }

    unsigned int max_mcs = 0;
    if (ht_max_mcs > max_mcs) max_mcs = ht_max_mcs;
    if (vht_max_mcs > max_mcs) max_mcs = vht_max_mcs;
    if (he_max_mcs > max_mcs) max_mcs = he_max_mcs;
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
         * if it is a valid Current Operating Class Extention Sequence
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
        LOGN("osw: util: parse_assoc_req: present delimiters - possible malformed frame, "
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
    unsigned int i;
    info->channel_cnt = 0;
    for (i = 0; (i + 1) < elem->datalen; i += 2) {
        const uint8_t chan_num = elem->data[i];
        const uint8_t chan_range = elem->data[i+1];
        unsigned int j;
        bool channel_list_full = false;
        for (j = 0; j < chan_range ; j++) {
            info->channel_list[j] = chan_num + j;
            info->channel_cnt++;
            if (info->channel_cnt > ARRAY_SIZE(info->channel_list)) channel_list_full = true;
        }
        if (channel_list_full == true) {
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

bool
osw_parse_assoc_req_ies(const void *assoc_req_ies,
                        size_t assoc_req_ies_len,
                        struct osw_assoc_req_info *info)
{
    const struct element *elem;
    const uint8_t *ies;
    size_t len;

    if (assoc_req_ies == NULL) {
        LOGT("osw: util: parse_assoc_req: assoc_req_ies is NULL");
        return false;
    }

    memset(info, 0, sizeof(*info));
    ies = assoc_req_ies;
    len = assoc_req_ies_len;

    for_each_ie(elem, ies, len) {
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
            case DOT11_ELEM_ID_EXT:
                osw_parse_he_caps(elem,
                                  info);
                break;
        }
    }

    return true;
}

#include "osw_util_ut.c"
