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

#ifndef OSW_STATS_ENUM_H_INCLUDED
#define OSW_STATS_ENUM_H_INCLUDED

enum osw_stats_id {
    OSW_STATS_PHY,
    OSW_STATS_VIF,
    OSW_STATS_STA,
    OSW_STATS_CHAN,
    OSW_STATS_BSS_SCAN,

    OSW_STATS_MAX__, /* keep last */
};

enum osw_stats_sta_id {
    OSW_STATS_STA_PHY_NAME,
    OSW_STATS_STA_VIF_NAME,
    OSW_STATS_STA_MAC_ADDRESS,
    OSW_STATS_STA_SNR_DB,
    OSW_STATS_STA_TX_BYTES,
    OSW_STATS_STA_TX_FRAMES,
    OSW_STATS_STA_TX_RETRIES,
    OSW_STATS_STA_TX_ERRORS,
    OSW_STATS_STA_TX_RATE_MBPS,
    OSW_STATS_STA_RX_BYTES,
    OSW_STATS_STA_RX_FRAMES,
    OSW_STATS_STA_RX_RETRIES,
    OSW_STATS_STA_RX_ERRORS,
    OSW_STATS_STA_RX_RATE_MBPS,
    OSW_STATS_STA_VIF_ADDRESS,

    OSW_STATS_STA_MAX__, /* keep last */
};

enum osw_stats_bss_scan_id {
    OSW_STATS_BSS_SCAN_PHY_NAME,
    OSW_STATS_BSS_SCAN_MAC_ADDRESS,
    OSW_STATS_BSS_SCAN_FREQ_MHZ,
    OSW_STATS_BSS_SCAN_SSID,
    OSW_STATS_BSS_SCAN_WIDTH_MHZ,
    OSW_STATS_BSS_SCAN_IES,
    OSW_STATS_BSS_SCAN_SNR_DB,
    OSW_STATS_BSS_SCAN_CENTER_FREQ0_MHZ,
    OSW_STATS_BSS_SCAN_CENTER_FREQ1_MHZ,

    OSW_STATS_BSS_SCAN_MAX__, /* keep last */
};

enum osw_stats_vif_id {
    OSW_STATS_VIF_NAME,
    OSW_STATS_VIF_PHY_NAME,
};

enum osw_stats_phy_id {
    OSW_STATS_PHY_NAME,
};

enum osw_stats_chan_counter_id {
    OSW_STATS_CHAN_CNT_TX,
    OSW_STATS_CHAN_CNT_RX,
    OSW_STATS_CHAN_CNT_RX_INBSS,
    OSW_STATS_CHAN_CNT_BUSY,

    OSW_STATS_CHAN_CNT_MAX__, /* keep last */
};

enum osw_stats_chan_id {
    OSW_STATS_CHAN_PHY_NAME,
    OSW_STATS_CHAN_FREQ_MHZ,
    OSW_STATS_CHAN_ACTIVE_MSEC,
    OSW_STATS_CHAN_CNT_MSEC, /* nested: osw_stats_chan_counter_id */
    OSW_STATS_CHAN_CNT_PERCENT, /* nested: osw_stats_chan_counter_id */
    OSW_STATS_CHAN_NOISE_FLOOR_DBM,

    OSW_STATS_CHAN_MAX__, /* keep last */
};

#endif /* OSW_STATS_ENUM_H_INCLUDED */
