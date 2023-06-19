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

#include <os.h>
#include <osw_util.h>

static const struct osw_tlv_policy
g_osw_stats_tp_bss_scan[OSW_STATS_BSS_SCAN_MAX__] = {
    [OSW_STATS_BSS_SCAN_PHY_NAME] = { .type = OSW_TLV_STRING },
    [OSW_STATS_BSS_SCAN_MAC_ADDRESS] = { .type = OSW_TLV_HWADDR },
    [OSW_STATS_BSS_SCAN_FREQ_MHZ] = { .type = OSW_TLV_U32 },
    [OSW_STATS_BSS_SCAN_SSID] = { .type = OSW_TLV_UNSPEC },
    [OSW_STATS_BSS_SCAN_WIDTH_MHZ] = { .type = OSW_TLV_U32 },
    [OSW_STATS_BSS_SCAN_IES] = { .type = OSW_TLV_UNSPEC },
    [OSW_STATS_BSS_SCAN_SNR_DB] = { .type = OSW_TLV_U32 },
};

static const struct osw_tlv_merge_policy
g_osw_stats_mp_bss_scan[OSW_STATS_BSS_SCAN_MAX__] = {
    [OSW_STATS_BSS_SCAN_PHY_NAME] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_BSS_SCAN_MAC_ADDRESS] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_BSS_SCAN_FREQ_MHZ] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_BSS_SCAN_SSID] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_BSS_SCAN_IES] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_BSS_SCAN_WIDTH_MHZ] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_BSS_SCAN_SNR_DB] = { .type = OSW_TLV_OP_OVERWRITE },
};

static void
osw_stats_defs_bss_scan_parse_ies(struct osw_tlv *data,
                                  const struct osw_tlv_hdr **tb)
{
    const struct osw_tlv_hdr *hdr = tb[OSW_STATS_BSS_SCAN_IES];
    if (hdr == NULL) return;

    const void *ies = osw_tlv_get_data(hdr);
    const size_t ies_len = hdr->len;

    if (tb[OSW_STATS_BSS_SCAN_SSID] == NULL) {
        size_t ssid_len = 0;
        const int ssid_eid = 0;
        const void *ssid_buf = osw_ie_find(ies, ies_len, ssid_eid, &ssid_len);
        if (ssid_buf != NULL && ssid_len > 0) {
            osw_tlv_put_buf(data, OSW_STATS_BSS_SCAN_SSID, ssid_buf, ssid_len);
        }
    }

    if (tb[OSW_STATS_BSS_SCAN_WIDTH_MHZ] == NULL) {
        /* FIXME: This should also parse / include HT, VHT
         * Operation IEs, not just Capabilities.
         */
        struct osw_assoc_req_info info;
        MEMZERO(info);
        const bool parsed = osw_parse_assoc_req_ies(ies, ies_len, &info);
        const enum osw_channel_width width = parsed
                                           ? osw_assoc_req_to_max_chwidth(&info)
                                           : OSW_CHANNEL_20MHZ;
        const uint32_t width_mhz = osw_channel_width_to_mhz(width);
        osw_tlv_put_u32(data, OSW_STATS_BSS_SCAN_WIDTH_MHZ, width_mhz);
    }
}

static void
osw_stats_defs_bss_scan_postprocess_cb(struct osw_tlv *data)
{
    const struct osw_tlv_hdr *tb[OSW_STATS_BSS_SCAN_MAX__] = {0};
    const size_t left = osw_tlv_parse(data->data,
                                      data->used,
                                      g_osw_stats_tp_bss_scan,
                                      tb,
                                      OSW_STATS_BSS_SCAN_MAX__);
    (void)left;
    osw_stats_defs_bss_scan_parse_ies(data, tb);
}

static const struct osw_stats_defs g_osw_stats_defs_bss_scan = {
    .postprocess_fn = osw_stats_defs_bss_scan_postprocess_cb,
    .tpolicy = g_osw_stats_tp_bss_scan,
    .mpolicy = g_osw_stats_mp_bss_scan,
    .size = OSW_STATS_BSS_SCAN_MAX__,
};
