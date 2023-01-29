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

#ifndef OSW_STATS_SUBSCRIBER_H_INCLUDED
#define OSW_STATS_SUBSCRIBER_H_INCLUDED

#include <osw_stats_enum.h>
#include <osw_tlv.h>

struct osw_stats_subscriber;

typedef void osw_stats_subscriber_report_fn_t(enum osw_stats_id id,
                                              const struct osw_tlv *data,
                                              const struct osw_tlv *last,
                                              void *priv);

struct osw_stats_subscriber *
osw_stats_subscriber_alloc(void);

void
osw_stats_subscriber_free(struct osw_stats_subscriber *sub);

void
osw_stats_subscriber_set_report_fn(struct osw_stats_subscriber *sub,
                                   osw_stats_subscriber_report_fn_t *fn,
                                   void *priv);

void
osw_stats_subscriber_set_report_seconds(struct osw_stats_subscriber *sub,
                                        double seconds);

void
osw_stats_subscriber_set_poll_seconds(struct osw_stats_subscriber *sub,
                                      double seconds);

void
osw_stats_subscriber_set_phy(struct osw_stats_subscriber *sub,
                             bool enabled);

void
osw_stats_subscriber_set_vif(struct osw_stats_subscriber *sub,
                             bool enabled);

void
osw_stats_subscriber_set_sta(struct osw_stats_subscriber *sub,
                             bool enabled);

void
osw_stats_subscriber_set_chan(struct osw_stats_subscriber *sub,
                              bool enabled);

void
osw_stats_subscriber_set_bss(struct osw_stats_subscriber *sub,
                              bool enabled);

#endif /* OSW_STATS_SUBSCRIBER_H_INCLUDED */
