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

#ifndef OW_STEER_STA_H
#define OW_STEER_STA_H

struct ow_steer_sta;

struct ow_steer_sta*
ow_steer_sta_create(const struct osw_hwaddr *sta_addr);

void
ow_steer_sta_free(struct ow_steer_sta *sta);

const struct osw_hwaddr*
ow_steer_sta_get_addr(const struct ow_steer_sta *sta);

void
ow_steer_sta_sigusr1_dump(void);


typedef void
ow_steer_snr_fn_t(void *priv,
                  const struct osw_hwaddr *sta_addr,
                  const struct osw_hwaddr *bssid,
                  int snr_db);

struct ow_steer_snr_observer;

struct ow_steer_snr_observer *
ow_steer_snr_register(const struct osw_hwaddr *sta_addr,
                      ow_steer_snr_fn_t *fn,
                      void *priv);

void
ow_steer_snr_unregister(struct ow_steer_snr_observer *obs);

#endif /* OW_STEER_STA_H */
