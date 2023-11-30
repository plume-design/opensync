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

#ifndef OSW_STA_CHAN_CAP_H_INCLUDED
#define OSW_STA_CHAN_CAP_H_INCLUDED

#include <osw_types.h>

struct osw_sta_chan_cap;
struct osw_sta_chan_cap_obs;

typedef struct osw_sta_chan_cap_obs osw_sta_chan_cap_obs_t;
typedef struct osw_sta_chan_cap osw_sta_chan_cap_t;

typedef void osw_sta_chan_cap_op_added_fn_t(void *priv,
                                            const struct osw_hwaddr *sta_addr,
                                            int freq_mhz);

typedef void osw_sta_chan_cap_op_removed_fn_t(void *priv,
                                              const struct osw_hwaddr *sta_addr,
                                              int freq_mhz);

struct osw_sta_chan_cap_ops {
    osw_sta_chan_cap_op_added_fn_t *added_fn;
    osw_sta_chan_cap_op_removed_fn_t *removed_fn;
};

enum osw_sta_chan_cap_status {
    OSW_STA_CHAN_CAP_MAYBE,
    OSW_STA_CHAN_CAP_SUPPORTED,
    OSW_STA_CHAN_CAP_NOT_SUPPORTED,
};

enum osw_sta_chan_cap_status
osw_sta_chan_cap_supports(osw_sta_chan_cap_obs_t *obs,
                          const struct osw_hwaddr *sta_addr,
                          int freq_mhz);

osw_sta_chan_cap_obs_t *
osw_sta_chan_cap_register(osw_sta_chan_cap_t *m,
                          const struct osw_hwaddr *sta_addr,
                          const struct osw_sta_chan_cap_ops *ops,
                          void *priv);

void
osw_sta_chan_cap_obs_unregister(osw_sta_chan_cap_obs_t *obs);

#endif /* OSW_STA_CHAN_CAP_H_INCLUDED */
