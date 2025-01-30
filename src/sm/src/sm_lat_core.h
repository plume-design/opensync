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

#ifndef SM_LAT_CORE_H_INCLUDED
#define SM_LAT_CORE_H_INCLUDED

#include <stdint.h>

#define SM_LAT_CORE_DSCP_MISSING ((uint8_t)0xFF)
#define SM_LAT_CORE_DSCP_NONE    ((uint8_t)0xFE)

struct sm_lat_core;
struct sm_lat_core_stream;

enum sm_lat_core_sampling
{
    SM_LAT_CORE_SAMPLING_SEPARATE,
    SM_LAT_CORE_SAMPLING_MERGE,
};

struct sm_lat_core_sample
{
    uint32_t *min_ms;
    uint32_t *max_ms;
    uint32_t *last_ms;
    uint32_t *avg_sum_ms;
    uint32_t *avg_cnt;
    uint32_t *num_pkts;
    uint64_t timestamp_ms;
};

struct sm_lat_core_host
{
    uint8_t mac_address[6];
    char if_name[16];
    uint8_t dscp;
    struct sm_lat_core_sample *samples;
    size_t n_samples;
};

typedef struct sm_lat_core sm_lat_core_t;
typedef struct sm_lat_core_stream sm_lat_core_stream_t;
typedef struct sm_lat_core_sample sm_lat_core_sample_t;
typedef struct sm_lat_core_host sm_lat_core_host_t;
typedef void sm_lat_core_report_fn_t(void *priv, const sm_lat_core_host_t *const *hosts, size_t count);

sm_lat_core_t *sm_lat_core_alloc(void);
void sm_lat_core_drop(sm_lat_core_t *c);
void sm_lat_core_set_vif_mld_if_name(sm_lat_core_t *c, const char *vif_name, const char *mld_name);

sm_lat_core_stream_t *sm_lat_core_stream_alloc(sm_lat_core_t *c);
void sm_lat_core_stream_drop(sm_lat_core_stream_t *st);
void sm_lat_core_stream_set_report_fn(sm_lat_core_stream_t *st, sm_lat_core_report_fn_t *fn, void *priv);
void sm_lat_core_stream_set_report_ms(sm_lat_core_stream_t *st, uint32_t ms);
void sm_lat_core_stream_set_poll_ms(sm_lat_core_stream_t *st, uint32_t ms);
void sm_lat_core_stream_set_sampling(sm_lat_core_stream_t *st, enum sm_lat_core_sampling sampling);
void sm_lat_core_stream_set_dscp(sm_lat_core_stream_t *st, bool enable);
void sm_lat_core_stream_set_kind_min(sm_lat_core_stream_t *st, bool enable);
void sm_lat_core_stream_set_kind_max(sm_lat_core_stream_t *st, bool enable);
void sm_lat_core_stream_set_kind_avg(sm_lat_core_stream_t *st, bool enable);
void sm_lat_core_stream_set_kind_num_pkts(sm_lat_core_stream_t *st, bool enable);
void sm_lat_core_stream_set_kind_last(sm_lat_core_stream_t *st, bool enable);
void sm_lat_core_stream_set_ifname(sm_lat_core_stream_t *st, const char *if_name, bool enable);

#endif /* SM_LAT_CORE_H_INCLUDED */
