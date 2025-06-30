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

/* FIXME: Use U64 for some of the stats */

#include <osw_state.h>

static const struct osw_tlv_policy
g_osw_stats_tp_sta[OSW_STATS_STA_MAX__] = {
    [OSW_STATS_STA_PHY_NAME] = { .type = OSW_TLV_STRING },
    [OSW_STATS_STA_VIF_NAME] = { .type = OSW_TLV_STRING },
    [OSW_STATS_STA_MAC_ADDRESS] = { .type = OSW_TLV_HWADDR },
    [OSW_STATS_STA_SNR_DB] = { .type = OSW_TLV_U32 },
    [OSW_STATS_STA_TX_BYTES] = { .type = OSW_TLV_U32 },
    [OSW_STATS_STA_TX_FRAMES] = { .type = OSW_TLV_U32 },
    [OSW_STATS_STA_TX_RETRIES] = { .type = OSW_TLV_U32 },
    [OSW_STATS_STA_TX_ERRORS] = { .type = OSW_TLV_U32 },
    [OSW_STATS_STA_TX_RATE_MBPS] = { .type = OSW_TLV_U32 },
    [OSW_STATS_STA_RX_BYTES] = { .type = OSW_TLV_U32 },
    [OSW_STATS_STA_RX_FRAMES] = { .type = OSW_TLV_U32 },
    [OSW_STATS_STA_RX_RETRIES] = { .type = OSW_TLV_U32 },
    [OSW_STATS_STA_RX_ERRORS] = { .type = OSW_TLV_U32 },
    [OSW_STATS_STA_RX_RATE_MBPS] = { .type = OSW_TLV_U32 },
};

static const struct osw_tlv_merge_policy
g_osw_stats_mp_sta[OSW_STATS_STA_MAX__] = {
    [OSW_STATS_STA_PHY_NAME] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_STA_VIF_NAME] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_STA_MAC_ADDRESS] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_STA_SNR_DB] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_STA_TX_BYTES] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_STA_TX_FRAMES] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_STA_TX_RETRIES] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_STA_TX_ERRORS] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_STA_TX_RATE_MBPS] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_STA_RX_BYTES] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_STA_RX_FRAMES] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_STA_RX_RETRIES] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_STA_RX_ERRORS] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_STA_RX_RATE_MBPS] = { .type = OSW_TLV_OP_OVERWRITE },
};

static void
osw_stats_defs_sta_postprocess_cb(struct osw_tlv *data)
{
    const struct osw_tlv_hdr *tb[OSW_STATS_STA_MAX__] = {0};
    const size_t left = osw_tlv_parse(data->data,
                                      data->used,
                                      g_osw_stats_tp_sta,
                                      tb,
                                      OSW_STATS_STA_MAX__);
    (void)left;
    const struct osw_tlv_hdr *vif_name_hdr = tb[OSW_STATS_STA_VIF_NAME];
    const struct osw_tlv_hdr *vif_addr_hdr = tb[OSW_STATS_STA_VIF_ADDRESS];
    if ((vif_name_hdr != NULL) && (vif_addr_hdr == NULL)) {
        const char *vif_name = osw_tlv_get_string(vif_name_hdr);
        const struct osw_state_vif_info *vif_info = osw_state_vif_lookup_by_vif_name(vif_name);
        if (vif_info != NULL) {
            const struct osw_hwaddr *vif_addr = &vif_info->drv_state->mac_addr;
            osw_tlv_put_hwaddr(data, OSW_STATS_STA_VIF_ADDRESS, vif_addr);
        }
    }
}

static const struct osw_stats_defs g_osw_stats_defs_sta = {
    .postprocess_fn = osw_stats_defs_sta_postprocess_cb,
    .tpolicy = g_osw_stats_tp_sta,
    .mpolicy = g_osw_stats_mp_sta,
    .size = OSW_STATS_STA_MAX__,
};
