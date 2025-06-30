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

#ifndef OW_STEER_HS_H_INCLUDED
#define OW_STEER_HS_H_INCLUDED

#include <inttypes.h>

typedef struct ow_steer_hs ow_steer_hs_t;
typedef struct ow_steer_hs_vif ow_steer_hs_vif_t;

ow_steer_hs_vif_t *ow_steer_hs_vif_alloc(ow_steer_hs_t *hs, const char *vif_name);
void ow_steer_hs_vif_drop(ow_steer_hs_vif_t *vif);
void ow_steer_hs_vif_set_soft_snr_db(ow_steer_hs_vif_t *vif, uint8_t soft_snr_db);
void ow_steer_hs_vif_set_hard_snr_db(ow_steer_hs_vif_t *vif, uint8_t hard_snr_db);
void ow_steer_hs_reset(ow_steer_hs_t *hs);

#endif /* OW_STEER_HS_H_INCLUDED */
