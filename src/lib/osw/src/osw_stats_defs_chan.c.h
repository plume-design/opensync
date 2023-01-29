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

static const struct osw_tlv_policy
g_osw_stats_tp_chan_cnt[OSW_STATS_CHAN_CNT_MAX__] = {
    [OSW_STATS_CHAN_CNT_TX] = { .type = OSW_TLV_U32 },
    [OSW_STATS_CHAN_CNT_RX] = { .type = OSW_TLV_U32 },
    [OSW_STATS_CHAN_CNT_RX_INBSS] = { .type = OSW_TLV_U32 },
    [OSW_STATS_CHAN_CNT_BUSY] = { .type = OSW_TLV_U32 },
};

static const struct osw_tlv_policy
g_osw_stats_tp_chan[OSW_STATS_CHAN_MAX__] = {
    [OSW_STATS_CHAN_PHY_NAME] = { .type = OSW_TLV_STRING },
    [OSW_STATS_CHAN_FREQ_MHZ] = { .type = OSW_TLV_U32 },
    [OSW_STATS_CHAN_ACTIVE_MSEC] = { .type = OSW_TLV_U32 },
    [OSW_STATS_CHAN_CNT_MSEC] = { .type = OSW_TLV_NESTED,
                                  .tb_size = OSW_STATS_CHAN_CNT_MAX__,
                                  .nested = g_osw_stats_tp_chan_cnt },
    [OSW_STATS_CHAN_CNT_PERCENT] = { .type = OSW_TLV_NESTED,
                                     .tb_size = OSW_STATS_CHAN_CNT_MAX__,
                                     .nested = g_osw_stats_tp_chan_cnt },
    [OSW_STATS_CHAN_NOISE_FLOOR_DBM] = { .type = OSW_TLV_FLOAT },
};

static const struct osw_tlv_merge_policy
g_osw_stats_mp_chan_cnt_msec[OSW_STATS_CHAN_MAX__] = {
    [OSW_STATS_CHAN_CNT_TX] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_CHAN_CNT_RX] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_CHAN_CNT_RX_INBSS] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_CHAN_CNT_BUSY] = { .type = OSW_TLV_OP_ACCUMULATE },
};

static const struct osw_tlv_merge_policy
g_osw_stats_mp_chan_cnt_percent[OSW_STATS_CHAN_MAX__] = {
    [OSW_STATS_CHAN_CNT_TX] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_CHAN_CNT_RX] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_CHAN_CNT_RX_INBSS] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_CHAN_CNT_BUSY] = { .type = OSW_TLV_OP_OVERWRITE },
};

static const struct osw_tlv_merge_policy
g_osw_stats_mp_chan[OSW_STATS_CHAN_MAX__] = {
    [OSW_STATS_CHAN_PHY_NAME] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_CHAN_FREQ_MHZ] = { .type = OSW_TLV_OP_OVERWRITE },
    [OSW_STATS_CHAN_ACTIVE_MSEC] = { .type = OSW_TLV_OP_ACCUMULATE },
    [OSW_STATS_CHAN_CNT_MSEC] = { .type = OSW_TLV_OP_MERGE,
                                  .tb_size = OSW_STATS_CHAN_CNT_MAX__,
                                  .nested = g_osw_stats_mp_chan_cnt_msec },
    /* FIXME: This would actually benefit
     * from an average weighted by ACTIVE_MSEC.
     */
    [OSW_STATS_CHAN_CNT_PERCENT] = { .type = OSW_TLV_OP_MERGE,
                                     .tb_size = OSW_STATS_CHAN_CNT_MAX__,
                                     .nested = g_osw_stats_mp_chan_cnt_percent },
    [OSW_STATS_CHAN_NOISE_FLOOR_DBM] = { .type = OSW_TLV_OP_OVERWRITE },
};

static void
osw_stats_defs_chan_synthesize_percents(struct osw_tlv *data,
                                        const struct osw_tlv_hdr **tb)
{
    /* Don't do it if it's already there */
    if (tb[OSW_STATS_CHAN_CNT_PERCENT] != NULL) return;

    /* Don't do it if there's no sufficient data */
    if (tb[OSW_STATS_CHAN_ACTIVE_MSEC] == NULL) return;
    if (tb[OSW_STATS_CHAN_CNT_MSEC] == NULL) return;

    const struct osw_tlv_hdr *msecs[OSW_STATS_CHAN_CNT_MAX__] = {0};
    const uint32_t active = osw_tlv_get_u32(tb[OSW_STATS_CHAN_ACTIVE_MSEC]);

    /* 0 duration means divide by 0. Skip sample. */
    if (active == 0) return;

    const void *cnts = osw_tlv_get_data(tb[OSW_STATS_CHAN_CNT_MSEC]);
    const size_t len = tb[OSW_STATS_CHAN_CNT_MSEC]->len;
    const size_t left = osw_tlv_parse(cnts,
                                      len,
                                      g_osw_stats_tp_chan_cnt,
                                      msecs,
                                      OSW_STATS_CHAN_CNT_MAX__);
    (void)left;
    size_t start = osw_tlv_put_nested(data, OSW_STATS_CHAN_CNT_PERCENT);
    {
        const int ids[] = {
            OSW_STATS_CHAN_CNT_TX,
            OSW_STATS_CHAN_CNT_RX,
            OSW_STATS_CHAN_CNT_RX_INBSS,
            OSW_STATS_CHAN_CNT_BUSY,
        };
        const size_t n = sizeof(ids) / sizeof(ids[0]);
        size_t i;
        for (i = 0; i < n; i++) {
            const struct osw_tlv_hdr *hdr = msecs[ids[i]];
            if (hdr == NULL) continue;
            const uint32_t x = osw_tlv_get_u32(hdr);
            const uint32_t percent = (x * 100) / active;
            osw_tlv_put_u32(data, ids[i], percent);
        }
    }
    osw_tlv_end_nested(data, start);
}

static void
osw_stats_defs_chan_postprocess_cb(struct osw_tlv *data)
{
    const struct osw_tlv_hdr *tb[OSW_STATS_CHAN_MAX__] = {0};
    const size_t left = osw_tlv_parse(data->data,
                                      data->used,
                                      g_osw_stats_tp_chan,
                                      tb,
                                      OSW_STATS_CHAN_MAX__);
    (void)left;
    osw_stats_defs_chan_synthesize_percents(data, tb);
}

static const struct osw_stats_defs g_osw_stats_defs_chan = {
    .postprocess_fn = osw_stats_defs_chan_postprocess_cb,
    .first = OSW_TLV_TWO_SAMPLES_MINIMUM,
    .tpolicy = g_osw_stats_tp_chan,
    .mpolicy = g_osw_stats_mp_chan,
    .size = OSW_STATS_CHAN_MAX__,
};
