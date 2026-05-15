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

#ifndef OW_HS_MQTT_H_INCLUDED
#define OW_HS_MQTT_H_INCLUDED
#include <stdbool.h>

#include <osw_types.h>

struct ow_hs_mqtt;
typedef struct ow_hs_mqtt ow_hs_mqtt_t;

ow_hs_mqtt_t *ow_hs_mqtt_alloc(void);
void ow_hs_mqtt_drop(ow_hs_mqtt_t *m);
void ow_hs_mqtt_report_drop(ow_hs_mqtt_t *m);
void ow_hs_mqtt_set_node_id(ow_hs_mqtt_t *m, const char *node_id);
void ow_hs_mqtt_set_topic(ow_hs_mqtt_t *m, const char *topic);
void ow_hs_mqtt_set_interval_secs(ow_hs_mqtt_t *m, double interval_secs);

typedef struct ow_hs_mqtt_steer_builder ow_hs_mqtt_steer_builder_t;

ow_hs_mqtt_steer_builder_t *ow_hs_mqtt_steer_builder_alloc_soft_event(ow_hs_mqtt_t *m);
ow_hs_mqtt_steer_builder_t *ow_hs_mqtt_steer_builder_alloc_hard_event(ow_hs_mqtt_t *m);
void ow_hs_mqtt_steer_builder_drop(ow_hs_mqtt_steer_builder_t **builder_ptr);

void ow_hs_mqtt_steer_builder_set_mac(ow_hs_mqtt_steer_builder_t *builder, const struct osw_hwaddr *mac_address);

bool ow_hs_mqtt_steer_builder_set_snr_on_start(
        ow_hs_mqtt_steer_builder_t *builder,
        const struct osw_hwaddr *bssid,
        const struct osw_hwaddr *sta_addr,
        uint32_t link_snr,
        uint32_t channel_mhz);
bool ow_hs_mqtt_steer_builder_btm_sent(ow_hs_mqtt_steer_builder_t *builder);
bool ow_hs_mqtt_steer_builder_btm_submitted(ow_hs_mqtt_steer_builder_t *builder);
bool ow_hs_mqtt_steer_builder_btm_response(ow_hs_mqtt_steer_builder_t *builder, uint32_t dot11_btm_response);
void ow_hs_mqtt_steer_builder_event_preempted_by_soft_steer(ow_hs_mqtt_steer_builder_t *builder);
void ow_hs_mqtt_steer_builder_event_preempted_by_hard_steer(ow_hs_mqtt_steer_builder_t *builder);
void ow_hs_mqtt_steer_builder_event_preempted_by_link_is_good(ow_hs_mqtt_steer_builder_t *builder);
void ow_hs_mqtt_steer_builder_sta_disconnected(ow_hs_mqtt_steer_builder_t *builder);
void ow_hs_mqtt_steer_builder_set_cell_mbo(ow_hs_mqtt_steer_builder_t *builder, uint32_t dot11_mbo_state);
void ow_hs_mqtt_steer_builder_deauth_sent(ow_hs_mqtt_steer_builder_t *builder);
bool ow_hs_mqtt_steer_builder_report_submit(ow_hs_mqtt_steer_builder_t *builder);

#endif /* OW_HS_MQTT_H_INCLUDED */
