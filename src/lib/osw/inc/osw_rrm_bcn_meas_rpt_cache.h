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

#ifndef OSW_RRM_BCN_MEAS_RPT_CACHE_H
#define OSW_RRM_BCN_MEAS_RPT_CACHE_H

struct osw_rrm_bcn_meas_rpt_cache;
struct osw_rrm_bcn_meas_rpt;
struct osw_rrm_bcn_meas_rpt_cache_observer;

typedef void
osw_rrm_bcn_meas_rpt_cache_update_fn_t(struct osw_rrm_bcn_meas_rpt_cache_observer *observer,
                                       const struct osw_hwaddr *sta_addr,
                                       const struct osw_hwaddr *bssid,
                                       const struct osw_rrm_bcn_meas_rpt *meac_rpt);

struct osw_rrm_bcn_meas_rpt_cache_observer {
    const char *name;
    osw_rrm_bcn_meas_rpt_cache_update_fn_t *update_cb;

    struct ds_dlist_node node;
};

struct osw_rrm_bcn_meas_rpt {
    uint8_t op_class;
    uint8_t channel;
    uint8_t rcpi;
};

const struct osw_rrm_bcn_meas_rpt*
osw_rrm_bcn_meas_rpt_cache_lookup(struct osw_rrm_bcn_meas_rpt_cache *cache,
                                  const struct osw_hwaddr *sta_addr,
                                  const struct osw_hwaddr *bssid);

void
osw_rrm_bcn_meas_rpt_cache_register_observer(struct osw_rrm_bcn_meas_rpt_cache *cache,
                                             struct osw_rrm_bcn_meas_rpt_cache_observer *observer);

void
osw_rrm_bcn_meas_rpt_cache_unregister_observer(struct osw_rrm_bcn_meas_rpt_cache *cache,
                                               struct osw_rrm_bcn_meas_rpt_cache_observer *observer);

#endif /* OSW_RRM_BCN_MEAS_RPT_CACHE_H */
