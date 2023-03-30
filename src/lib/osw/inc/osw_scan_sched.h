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

#ifndef OSW_SCAN_SCHED_H_INCLUDED
#define OSW_SCAN_SCHED_H_INCLUDED

#include <osw_types.h>

struct osw_scan_sched;

enum osw_scan_sched_filter {
    OSW_SCAN_SCHED_ALLOW,
    OSW_SCAN_SCHED_DENY,
};

enum osw_scan_sched_mode {
    OSW_SCAN_SCHED_RR,
    OSW_SCAN_SCHED_ALL,
};

typedef enum osw_scan_sched_filter
osw_scan_sched_filter_fn_t(struct osw_scan_sched *ss,
                           void *priv);

struct osw_scan_sched *
osw_scan_sched_alloc(void);

void
osw_scan_sched_free(struct osw_scan_sched *ss);

void
osw_scan_sched_set_interval(struct osw_scan_sched *ss,
                            double seconds);

void
osw_scan_sched_set_offset(struct osw_scan_sched *ss,
                          double seconds);

void
osw_scan_sched_set_mode(struct osw_scan_sched *ss,
                        enum osw_scan_sched_mode mode);

void
osw_scan_sched_set_dwell_time_msec(struct osw_scan_sched *ss,
                                   unsigned int dwell_time_msec);

void
osw_scan_sched_set_phy_name(struct osw_scan_sched *ss,
                            const char *phy_name);

void
osw_scan_sched_set_vif_name(struct osw_scan_sched *ss,
                            const char *vif_name);

void
osw_scan_sched_set_filter_fn(struct osw_scan_sched *ss,
                             osw_scan_sched_filter_fn_t *fn,
                             void *priv);

void
osw_scan_sched_set_channels(struct osw_scan_sched *ss,
                            const struct osw_channel *channels,
                            const size_t n_channels);

#endif /* OSW_SCAN_SCHED_H_INCLUDED */
