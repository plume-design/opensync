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

#include <memutil.h>
#include <log.h>
#include <hotspot.pb-c.h>
#include <qm_conn.h>
#include <os_types.h>
#include <os_time.h>
#include <stdint.h>
#include <ds_dlist.h>

#include "ow_hs_mqtt.h"
#include "complex.h"
#include "ev.h"
#include "protobuf-c/protobuf-c.h"
#include <osw_types.h>

#define LOG_PREFIX(fmt, ...) "ow: hs: mqtt: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_MQTT(m, fmt, ...) LOG_PREFIX("%p: %s: " fmt, (m), (m)->node_id ?: "", ##__VA_ARGS__)

#define LOG_PREFIX_MQTT_MAC(m, mac, fmt, ...) \
    LOG_PREFIX("%p: %s: " OSW_HWADDR_FMT ": " fmt, (m), (m)->node_id ?: "", OSW_HWADDR_ARG(mac), ##__VA_ARGS__)

#define OW_HS_MQTT_TIME_TO_SUBMIT_SEC 20.0

typedef enum ow_hs_mqtt_steer_builder_event_type
{
    OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT,
    OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD,
} ow_hs_mqtt_steer_builder_event_type_e;

struct ow_hs_mqtt_steer_builder
{
    struct ds_dlist_node node_m;
    ow_hs_mqtt_t *m;
    ow_hs_mqtt_steer_builder_event_type_e type;
    struct ev_loop *loop;
    ev_timer submit_timer;
    struct osw_hwaddr mac;
    Hotspot__HardSteerEvent *hard_event;
    Hotspot__SoftSteerEvent *soft_event;
    uint64_t btm_submitted_ts_ms;
    uint64_t sta_disconnected_ts_ms;
    bool dot11_mbo_cell_state_set;
};

struct ow_hs_mqtt
{
    struct ds_dlist builders;
    struct ev_loop *loop;
    ev_periodic periodic_try_send;
    Hotspot__HotspotMessage *hm;
    char *node_id;
    char *topic;
};

static Hotspot__HotspotMessage *ow_hs_mqtt_get_alloc_root(ow_hs_mqtt_t *m)
{
    if (m->hm == NULL)
    {
        Hotspot__HotspotMessage *hs = MALLOC(sizeof(*hs));
        hotspot__hotspot_message__init(hs);
        hs->node_id = STRDUP(m->node_id ?: "");
        m->hm = hs;
    }
    return m->hm;
}

static void ow_hs_mqtt_report_send(ow_hs_mqtt_t *m)
{
    if (m->hm == NULL)
    {
        LOGD(LOG_PREFIX_MQTT(m, "report: cannot send: no data has been collected"));
        return;
    }

    if (m->topic == NULL)
    {
        LOGW(LOG_PREFIX_MQTT(m, "report: cannot send: topic is undefined"));
        return;
    }

    const size_t size = hotspot__hotspot_message__get_packed_size(m->hm);
    void *buf = MALLOC(size);
    const size_t len = hotspot__hotspot_message__pack(m->hm, buf);
    LOGI(LOG_PREFIX_MQTT(m, "sending %zu bytes", len));
    qm_response_t res;
    const bool sent = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, m->topic, buf, len, &res);
    WARN_ON(sent == false);
    FREE(buf);
}

static void ow_hs_mqtt_try_send_cb(struct ev_loop *l, ev_periodic *a, int mask)
{
    ow_hs_mqtt_t *m = a->data;
    if (m->hm == NULL) return;

    LOGI(LOG_PREFIX_MQTT(
            m,
            "trying to send report. Reporting %zu client telemetry entries",
            m->hm->n_client_telemetry));
    ow_hs_mqtt_report_send(m);
    ow_hs_mqtt_report_drop(m);
}

static Hotspot__SoftSteerEvent *ow_hs_mqtt_soft_steer_event_alloc(void)
{
    Hotspot__SoftSteerEvent *evt = MALLOC(sizeof(*evt));
    hotspot__soft_steer_event__init(evt);

    return evt;
}

static Hotspot__HardSteerEvent *ow_hs_mqtt_hard_steer_event_alloc(void)
{
    Hotspot__HardSteerEvent *evt = MALLOC(sizeof(*evt));
    hotspot__hard_steer_event__init(evt);

    return evt;
}

static Hotspot__AssocSnapshot *ow_hs_mqtt_assoc_snapshot_alloc(void)
{
    Hotspot__AssocSnapshot *assoc = MALLOC(sizeof(*assoc));
    hotspot__assoc_snapshot__init(assoc);
    return assoc;
}

static Hotspot__LinkSnr *ow_hs_mqtt_link_snr_find(
        Hotspot__AssocSnapshot *assoc,
        const struct osw_hwaddr *bssid,
        const struct osw_hwaddr *sta_addr)
{
    for (size_t i = 0; i < assoc->n_link_snr; i++)
    {
        Hotspot__LinkSnr *snr = assoc->link_snr[i];
        const struct osw_hwaddr *link_address = osw_hwaddr_from_cptr_unchecked(snr->link_address.data);
        const struct osw_hwaddr *link_bssid = osw_hwaddr_from_cptr_unchecked(snr->bssid.data);
        if (osw_hwaddr_is_equal(link_address, sta_addr) && osw_hwaddr_is_equal(link_bssid, bssid)) return snr;
    }
    return NULL;
}

static Hotspot__LinkSnr **ow_hs_mqtt_link_snr_grow(Hotspot__AssocSnapshot *assoc)
{
    const size_t last = assoc->n_link_snr++;
    const size_t elem_size = sizeof(assoc->link_snr[0]);
    const size_t new_size = assoc->n_link_snr * elem_size;
    assoc->link_snr = REALLOC(assoc->link_snr, new_size);
    return &assoc->link_snr[last];
}

static Hotspot__ClientTelemetry **ow_hs_mqtt_client_telemetry_grow(Hotspot__HotspotMessage *message)
{
    const size_t last = message->n_client_telemetry++;
    const size_t elem_size = sizeof(message->client_telemetry[0]);
    const size_t new_size = message->n_client_telemetry * elem_size;
    message->client_telemetry = REALLOC(message->client_telemetry, new_size);
    return &message->client_telemetry[last];
}

static Hotspot__ClientEvent **ow_hs_mqtt_client_event_grow(Hotspot__ClientTelemetry *ct)
{
    const size_t last = ct->n_events++;
    const size_t elem_size = sizeof(ct->events[0]);
    const size_t new_size = ct->n_events * elem_size;
    ct->events = REALLOC(ct->events, new_size);
    return &ct->events[last];
}

static Hotspot__LinkSnr *ow_hs_mqtt_link_snr_alloc(void)
{
    Hotspot__LinkSnr *snr = MALLOC(sizeof(*snr));
    hotspot__link_snr__init(snr);
    return snr;
}

static Hotspot__ClientTelemetry *ow_hs_mqtt_client_telemetry_alloc(void)
{
    Hotspot__ClientTelemetry *ct = MALLOC(sizeof(*ct));
    hotspot__client_telemetry__init(ct);
    return ct;
}

static Hotspot__ClientEvent *ow_hs_mqtt_client_event_alloc(void)
{
    Hotspot__ClientEvent *ce = MALLOC(sizeof(*ce));
    hotspot__client_event__init(ce);
    return ce;
}

static bool ow_hs_mqtt_set_steering_result(ow_hs_mqtt_steer_builder_t *builder, Hotspot__SteerResult steering_result)
{
    if (builder == NULL || builder->m == NULL) return false;
    if (builder->hard_event == NULL && builder->soft_event == NULL) return false;
    switch (builder->type)
    {
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT:
            builder->soft_event->steer_result = steering_result;
            builder->soft_event->has_steer_result = true;
            break;
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD:
            builder->hard_event->steer_result = steering_result;
            builder->hard_event->has_steer_result = true;
            break;
    }
    return true;
}

static void ow_hs_mqtt_submit_cb(struct ev_loop *l, ev_timer *w, int revents)
{
    ow_hs_mqtt_steer_builder_t *builder = w->data;

    ow_hs_mqtt_set_steering_result(builder, HOTSPOT__STEER_RESULT__STA_REMAINED_CONNECTED);
    ow_hs_mqtt_steer_builder_report_submit(builder);
}

static void ow_hs_mqtt_register_builder(ow_hs_mqtt_t *m, ow_hs_mqtt_steer_builder_t *builder)
{
    if (m == NULL || builder == NULL) return;
    ds_dlist_insert_tail(&m->builders, builder);
}

static void ow_hs_mqtt_unregister_builder(ow_hs_mqtt_t *m, ow_hs_mqtt_steer_builder_t *builder)
{
    if (m == NULL || builder == NULL) return;
    ds_dlist_remove(&m->builders, builder);
}

static ow_hs_mqtt_steer_builder_t *ow_hs_mqtt_steer_builder_alloc(
        ow_hs_mqtt_t *m,
        ow_hs_mqtt_steer_builder_event_type_e type)
{
    if (m == NULL) return NULL;
    ow_hs_mqtt_steer_builder_t *builder = CALLOC(1, sizeof(*builder));
    builder->m = m;
    builder->type = type;
    builder->loop = EV_DEFAULT;
    ev_timer_init(&builder->submit_timer, ow_hs_mqtt_submit_cb, OW_HS_MQTT_TIME_TO_SUBMIT_SEC, 0.0);
    builder->submit_timer.data = builder;
    ev_timer_start(builder->loop, &builder->submit_timer);
    ow_hs_mqtt_register_builder(m, builder);

    Hotspot__AssocSnapshot *assoc_on_start = ow_hs_mqtt_assoc_snapshot_alloc();

    switch (type)
    {
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT:
            builder->soft_event = ow_hs_mqtt_soft_steer_event_alloc();
            builder->soft_event->assoc_on_start = assoc_on_start;
            break;
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD:
            builder->hard_event = ow_hs_mqtt_hard_steer_event_alloc();
            builder->hard_event->assoc_on_start = assoc_on_start;
            break;
    }

    return builder;
}

ow_hs_mqtt_steer_builder_t *ow_hs_mqtt_steer_builder_alloc_soft_event(ow_hs_mqtt_t *m)
{
    return ow_hs_mqtt_steer_builder_alloc(m, OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT);
}

ow_hs_mqtt_steer_builder_t *ow_hs_mqtt_steer_builder_alloc_hard_event(ow_hs_mqtt_t *m)
{
    return ow_hs_mqtt_steer_builder_alloc(m, OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD);
}

static const char *ow_hs_mqtt_steer_builder_event_type_to_str(const enum ow_hs_mqtt_steer_builder_event_type t)
{
    switch (t)
    {
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT:
            return "soft";
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD:
            return "hard";
    }
    return "";
}

void ow_hs_mqtt_steer_builder_set_mac(ow_hs_mqtt_steer_builder_t *builder, const struct osw_hwaddr *mac_address)
{
    if (builder == NULL || builder->m == NULL || mac_address == NULL) return;
    builder->mac = *mac_address;
}

static bool ow_hs_mqtt_steer_builder_set_btm_summary(
        Hotspot__BtmSummary *btm_summary_set,
        protobuf_c_boolean *has_btm_response,
        uint32_t *dot11_btm_response_set,
        Hotspot__BtmSummary btm_summary_to_set,
        uint32_t dot11_btm_response_to_set)
{
    if (btm_summary_set == NULL || has_btm_response == NULL || dot11_btm_response_set == NULL) return false;

    switch (*btm_summary_set)
    {
        case HOTSPOT__BTM_SUMMARY__NOT_SENT:
            switch (btm_summary_to_set)
            {
                case HOTSPOT__BTM_SUMMARY__NOT_SENT:
                    *has_btm_response = false;
                    break;
                case HOTSPOT__BTM_SUMMARY__SENT_BUT_NO_RESPONSE:
                    *btm_summary_set = btm_summary_to_set;
                    *has_btm_response = false;
                    return true;
                case HOTSPOT__BTM_SUMMARY__SENT_AND_GOT_RESPONSE:
                    *btm_summary_set = btm_summary_to_set;
                    *has_btm_response = true;
                    *dot11_btm_response_set = dot11_btm_response_to_set;
                    break;
                default:
                    break;
            }
            break;
        case HOTSPOT__BTM_SUMMARY__SENT_BUT_NO_RESPONSE:
            switch (btm_summary_to_set)
            {
                case HOTSPOT__BTM_SUMMARY__NOT_SENT:
                case HOTSPOT__BTM_SUMMARY__SENT_BUT_NO_RESPONSE:
                    *has_btm_response = false;
                    break;
                case HOTSPOT__BTM_SUMMARY__SENT_AND_GOT_RESPONSE:
                    *btm_summary_set = btm_summary_to_set;
                    *has_btm_response = true;
                    *dot11_btm_response_set = dot11_btm_response_to_set;
                    return true;
                default:
                    break;
            }
            break;
        case HOTSPOT__BTM_SUMMARY__SENT_AND_GOT_RESPONSE:
            switch (btm_summary_to_set)
            {
                case HOTSPOT__BTM_SUMMARY__NOT_SENT:
                case HOTSPOT__BTM_SUMMARY__SENT_BUT_NO_RESPONSE:
                    break;
                case HOTSPOT__BTM_SUMMARY__SENT_AND_GOT_RESPONSE:
                    *has_btm_response = true;
                    *dot11_btm_response_set = dot11_btm_response_to_set;
                    return true;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    return false;
}

static char *ow_hs_mqtt_steer_builder_get_btm_summary_name(Hotspot__BtmSummary btm_summary)
{
    switch (btm_summary)
    {
        case HOTSPOT__BTM_SUMMARY__NOT_SENT:
            return "NOT_SENT";
        case HOTSPOT__BTM_SUMMARY__SENT_BUT_NO_RESPONSE:
            return "SENT_BUT_NO_RESPONSE";
        case HOTSPOT__BTM_SUMMARY__SENT_AND_GOT_RESPONSE:
            return "SENT_AND_GOT_RESPONSE";
        default:
            return "UNKNOWN";
    }
}

bool ow_hs_mqtt_steer_builder_btm_sent(ow_hs_mqtt_steer_builder_t *builder)
{
    if (builder == NULL || builder->m == NULL) return false;
    if (builder->hard_event == NULL && builder->soft_event == NULL) return false;
    ev_timer_stop(builder->loop, &builder->submit_timer);
    ev_timer_set(&builder->submit_timer, OW_HS_MQTT_TIME_TO_SUBMIT_SEC, 0.0);
    ev_timer_start(builder->loop, &builder->submit_timer);
    bool status = false;
    Hotspot__BtmSummary prev_btm_summary = HOTSPOT__BTM_SUMMARY__NOT_SENT;

    switch (builder->type)
    {
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT:
            prev_btm_summary = builder->soft_event->btm_summary;
            status = ow_hs_mqtt_steer_builder_set_btm_summary(
                    &builder->soft_event->btm_summary,
                    &builder->soft_event->has_dot11_btm_response,
                    &builder->soft_event->dot11_btm_response,
                    HOTSPOT__BTM_SUMMARY__SENT_BUT_NO_RESPONSE,
                    0);
            break;
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD:
            prev_btm_summary = builder->hard_event->btm_summary;
            status = ow_hs_mqtt_steer_builder_set_btm_summary(
                    &builder->hard_event->btm_summary,
                    &builder->hard_event->has_dot11_btm_response,
                    &builder->hard_event->dot11_btm_response,
                    HOTSPOT__BTM_SUMMARY__SENT_BUT_NO_RESPONSE,
                    0);
            break;
    }
    if (status == false)
    {
        LOGW(LOG_PREFIX_MQTT_MAC(
                builder->m,
                &builder->mac,
                "btm_sent: cannot set btm_summary HOTSPOT__BTM_SUMMARY__SENT_BUT_NO_RESPONSE, prev=%s",
                ow_hs_mqtt_steer_builder_get_btm_summary_name(prev_btm_summary)));
        return false;
    }
    return true;
}

bool ow_hs_mqtt_steer_builder_btm_submitted(ow_hs_mqtt_steer_builder_t *builder)
{
    if (builder == NULL || builder->m == NULL) return false;
    if (builder->hard_event == NULL && builder->soft_event == NULL) return false;
    builder->btm_submitted_ts_ms = clock_real_ms();
    return true;
}

bool ow_hs_mqtt_steer_builder_set_snr_on_start(
        ow_hs_mqtt_steer_builder_t *builder,
        const struct osw_hwaddr *bssid,
        const struct osw_hwaddr *sta_addr,
        uint32_t link_snr,
        uint32_t channel_mhz)
{
    if (WARN_ON(bssid == NULL) || WARN_ON(sta_addr == NULL)) return false;
    if (builder == NULL || builder->m == NULL) return true;
    if (builder->hard_event == NULL && builder->soft_event == NULL) return true;
    Hotspot__AssocSnapshot *assoc_on_start = NULL;
    switch (builder->type)
    {
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT:
            assoc_on_start = builder->soft_event->assoc_on_start;
            break;
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD:
            assoc_on_start = builder->hard_event->assoc_on_start;
            break;
    }
    Hotspot__LinkSnr *snr = ow_hs_mqtt_link_snr_find(assoc_on_start, bssid, sta_addr);
    if (snr == NULL)
    {
        Hotspot__LinkSnr **new_snr_ptr = ow_hs_mqtt_link_snr_grow(assoc_on_start);
        *new_snr_ptr = ow_hs_mqtt_link_snr_alloc();
        snr = *new_snr_ptr;
        snr->link_address.data = MEMNDUP(sta_addr, OSW_HWADDR_LEN);
        snr->link_address.len = OSW_HWADDR_LEN;
        snr->bssid.data = MEMNDUP(bssid, OSW_HWADDR_LEN);
        snr->bssid.len = OSW_HWADDR_LEN;
    }
    snr->snr = link_snr;
    if (channel_mhz > 0)
    {
        snr->has_primary_channel_frequency_mhz = true;
        snr->primary_channel_frequency_mhz = channel_mhz;
    }
    return true;
}

bool ow_hs_mqtt_steer_builder_btm_response(ow_hs_mqtt_steer_builder_t *builder, uint32_t dot11_btm_response)
{
    if (builder == NULL || builder->m == NULL) return false;
    if (builder->hard_event == NULL && builder->soft_event == NULL) return false;
    WARN_ON(builder->btm_submitted_ts_ms
            == 0);  // Ensure that BTM was "submitted to send" to driver before response is recorded
    bool status = false;
    Hotspot__BtmSummary prev_btm_summary = HOTSPOT__BTM_SUMMARY__NOT_SENT;

    switch (builder->type)
    {
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT:
            prev_btm_summary = builder->soft_event->btm_summary;
            status = ow_hs_mqtt_steer_builder_set_btm_summary(
                    &builder->soft_event->btm_summary,
                    &builder->soft_event->has_dot11_btm_response,
                    &builder->soft_event->dot11_btm_response,
                    HOTSPOT__BTM_SUMMARY__SENT_AND_GOT_RESPONSE,
                    dot11_btm_response);

            break;
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD:
            prev_btm_summary = builder->hard_event->btm_summary;
            status = ow_hs_mqtt_steer_builder_set_btm_summary(
                    &builder->hard_event->btm_summary,
                    &builder->hard_event->has_dot11_btm_response,
                    &builder->hard_event->dot11_btm_response,
                    HOTSPOT__BTM_SUMMARY__SENT_AND_GOT_RESPONSE,
                    dot11_btm_response);
            break;
    }
    if (status == false)
    {
        LOGW(LOG_PREFIX_MQTT_MAC(
                builder->m,
                &builder->mac,
                "cannot set BTM summary from %s to SENT_AND_GOT_RESPONSE",
                ow_hs_mqtt_steer_builder_get_btm_summary_name(prev_btm_summary)));
        return false;
    }
    return true;
}

void ow_hs_mqtt_steer_builder_sta_disconnected(ow_hs_mqtt_steer_builder_t *builder)
{
    const bool status = ow_hs_mqtt_set_steering_result(builder, HOTSPOT__STEER_RESULT__STA_IS_DISCONNECTED);
    if (status == false) return;
    builder->sta_disconnected_ts_ms = clock_real_ms();
}

void ow_hs_mqtt_steer_builder_event_preempted_by_soft_steer(ow_hs_mqtt_steer_builder_t *builder)
{
    ow_hs_mqtt_set_steering_result(builder, HOTSPOT__STEER_RESULT__PREEMPTED_BY_SOFT_STEER);
}

void ow_hs_mqtt_steer_builder_event_preempted_by_hard_steer(ow_hs_mqtt_steer_builder_t *builder)
{
    ow_hs_mqtt_set_steering_result(builder, HOTSPOT__STEER_RESULT__PREEMPTED_BY_HARD_STEER);
}

void ow_hs_mqtt_steer_builder_event_preempted_by_link_is_good(ow_hs_mqtt_steer_builder_t *builder)
{
    ow_hs_mqtt_set_steering_result(builder, HOTSPOT__STEER_RESULT__PREEMPTED_BY_LINK_IS_GOOD);
}

static void ow_hs_mqtt_steer_builder_set_mbo(uint32_t *mbo, protobuf_c_boolean *has_mbo, uint32_t mbo_to_set)
{
    if (mbo == NULL || has_mbo == NULL) return;
    if (mbo_to_set == 1 || mbo_to_set == 2)
    {
        *mbo = mbo_to_set;
        *has_mbo = true;
        return;
    }
    *has_mbo = false;
}

void ow_hs_mqtt_steer_builder_set_cell_mbo(ow_hs_mqtt_steer_builder_t *builder, uint32_t dot11_mbo_state)
{
    if (builder == NULL || builder->m == NULL) return;
    if (builder->hard_event == NULL && builder->soft_event == NULL) return;
    switch (builder->type)
    {
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT:
            ow_hs_mqtt_steer_builder_set_mbo(
                    &builder->soft_event->assoc_on_start->dot11_mbo_cell_state,
                    &builder->soft_event->assoc_on_start->has_dot11_mbo_cell_state,
                    dot11_mbo_state);
            break;
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD:
            ow_hs_mqtt_steer_builder_set_mbo(
                    &builder->hard_event->assoc_on_start->dot11_mbo_cell_state,
                    &builder->hard_event->assoc_on_start->has_dot11_mbo_cell_state,
                    dot11_mbo_state);
            break;
    }
}

void ow_hs_mqtt_steer_builder_drop(ow_hs_mqtt_steer_builder_t **builder_ptr)
{
    if (builder_ptr == NULL || *builder_ptr == NULL) return;
    ow_hs_mqtt_steer_builder_t *builder = *builder_ptr;
    hotspot__hard_steer_event__free_unpacked(builder->hard_event, NULL);
    hotspot__soft_steer_event__free_unpacked(builder->soft_event, NULL);
    ev_timer_stop(builder->loop, &builder->submit_timer);
    ow_hs_mqtt_unregister_builder(builder->m, builder);
    FREE(builder);
    *builder_ptr = NULL;
}

static void ow_hs_mqtt_steer_builder_notify_mqtt_dropped(ow_hs_mqtt_steer_builder_t *builder)
{
    if (builder == NULL || builder->m == NULL) return;
    builder->m = NULL;
}

void ow_hs_mqtt_steer_builder_deauth_sent(ow_hs_mqtt_steer_builder_t *builder)
{
    if (builder == NULL || builder->m == NULL) return;
    if (builder->hard_event == NULL || builder->type != OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD) return;

    builder->hard_event->deauth_sent = true;
}

static void ow_hs_mqtt_find_client_telemetry(
        Hotspot__ClientTelemetry **ct_out,
        const ow_hs_mqtt_t *m,
        const struct osw_hwaddr *mac_address)
{
    *ct_out = NULL;
    if (osw_hwaddr_is_zero(mac_address) || m == NULL || m->hm == NULL) return;

    Hotspot__HotspotMessage *hm = m->hm;
    for (size_t i = 0; i < hm->n_client_telemetry; i++)
    {
        Hotspot__ClientTelemetry *ct = hm->client_telemetry[i];
        const struct osw_hwaddr *dest = osw_hwaddr_from_cptr(ct->mac_address.data, ct->mac_address.len);
        if (osw_hwaddr_is_equal(dest, mac_address))
        {
            *ct_out = ct;
            break;
        }
    }
}

static bool ow_hs_mqtt_get_client_telemetry(
        Hotspot__ClientTelemetry **ct_ptr,
        ow_hs_mqtt_t *m,
        const struct osw_hwaddr *mac_address)
{
    if (osw_hwaddr_is_zero(mac_address)) return false;
    ow_hs_mqtt_find_client_telemetry(ct_ptr, m, mac_address);
    if (*ct_ptr == NULL)
    {
        Hotspot__ClientTelemetry **new_ct_ptr = ow_hs_mqtt_client_telemetry_grow(ow_hs_mqtt_get_alloc_root(m));
        *new_ct_ptr = ow_hs_mqtt_client_telemetry_alloc();
        Hotspot__ClientTelemetry *new_ct = *new_ct_ptr;
        new_ct->mac_address.data = MEMNDUP(mac_address, OSW_HWADDR_LEN);
        new_ct->mac_address.len = OSW_HWADDR_LEN;
        *ct_ptr = new_ct;
    }
    return true;
}

static bool ow_hs_mqtt_report_submit(ow_hs_mqtt_steer_builder_t *builder)
{
    Hotspot__ClientTelemetry *ct = NULL;
    const bool ct_found = ow_hs_mqtt_get_client_telemetry(&ct, builder->m, &builder->mac);
    if (ct_found == false) return false;
    Hotspot__ClientEvent **new_ce_ptr = ow_hs_mqtt_client_event_grow(ct);
    *new_ce_ptr = ow_hs_mqtt_client_event_alloc();
    Hotspot__ClientEvent *ce = *new_ce_ptr;

    switch (builder->type)
    {
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT:
            ce->soft_steer_event = builder->soft_event;
            builder->soft_event = NULL;
            break;
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD:
            ce->hard_steer_event = builder->hard_event;
            builder->hard_event = NULL;
            break;
    }
    return true;
}

static bool ow_hs_mqtt_is_valid_report(ow_hs_mqtt_steer_builder_t *builder)
{
    if (WARN_ON(osw_hwaddr_is_zero(&builder->mac))) return false;
    Hotspot__AssocSnapshot *assoc_on_start = NULL;
    Hotspot__BtmSummary btm_summary;
    switch (builder->type)
    {
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT:
            assoc_on_start = builder->soft_event->assoc_on_start;
            btm_summary = builder->soft_event->btm_summary;
            break;
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD:
            assoc_on_start = builder->hard_event->assoc_on_start;
            btm_summary = builder->hard_event->btm_summary;
            /* BTM request must be "submitted to send" if deauth was sent */
            if (WARN_ON(builder->hard_event->deauth_sent == true && builder->btm_submitted_ts_ms == 0)) return false;
            break;
    }
    if (WARN_ON(assoc_on_start->n_link_snr == 0)) return false;
    /* If BTM response was received, BTM request must have been "submitted to send" */
    if (WARN_ON(btm_summary == HOTSPOT__BTM_SUMMARY__SENT_AND_GOT_RESPONSE && builder->btm_submitted_ts_ms == 0))
        return false;
    return true;
}

static bool ow_hs_mqtt_calculate_duration(ow_hs_mqtt_steer_builder_t *builder)
{
    if (WARN_ON(builder->btm_submitted_ts_ms == 0)) return false;

    const bool sta_disconnected_before_btm = builder->sta_disconnected_ts_ms < builder->btm_submitted_ts_ms;
    const bool sta_has_disconnected = builder->sta_disconnected_ts_ms > 0;
    const uint64_t duration_ms =
            sta_has_disconnected ? builder->sta_disconnected_ts_ms - builder->btm_submitted_ts_ms : 0;

    switch (builder->type)
    {
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_SOFT:
            if (sta_disconnected_before_btm && sta_has_disconnected) break;
            builder->soft_event->started_on_ms = builder->btm_submitted_ts_ms;
            builder->soft_event->disconnected_after_ms = duration_ms;
            builder->soft_event->has_disconnected_after_ms = sta_has_disconnected;
            return true;
        case OW_HS_MQTT_STEER_BUILDER_EVENT_TYPE_HARD:
            if (sta_disconnected_before_btm) break;
            builder->hard_event->started_on_ms = builder->btm_submitted_ts_ms;
            builder->hard_event->disconnected_after_ms = duration_ms;
            builder->hard_event->has_disconnected_after_ms = sta_has_disconnected;
            return true;
    }
    LOGW(LOG_PREFIX_MQTT_MAC(
            builder->m,
            &builder->mac,
            "Not sending report: type: %s: STA disconnected before BTM has been submitted "
            "(sta_disconnected_ts_ms=%llu < btm_submitted_ts_ms=%llu)",
            ow_hs_mqtt_steer_builder_event_type_to_str(builder->type),
            (unsigned long long)builder->sta_disconnected_ts_ms,
            (unsigned long long)builder->btm_submitted_ts_ms));
    return false;
}

bool ow_hs_mqtt_steer_builder_report_submit(ow_hs_mqtt_steer_builder_t *builder)
{
    if (builder == NULL || WARN_ON(builder->m == NULL)) return false;

    if (builder->hard_event == NULL && builder->soft_event == NULL) return true;

    const bool is_calculation_ts_ok = ow_hs_mqtt_calculate_duration(builder);
    if (is_calculation_ts_ok == false) return false;
    const bool is_valid_report = ow_hs_mqtt_is_valid_report(builder);
    if (is_valid_report == false) return false;

    ev_timer_stop(builder->loop, &builder->submit_timer);
    const bool status = ow_hs_mqtt_report_submit(builder);
    return status;
}

ow_hs_mqtt_t *ow_hs_mqtt_alloc(void)
{
    ow_hs_mqtt_t *m = CALLOC(1, sizeof(*m));
    m->loop = EV_DEFAULT;
    ev_periodic_init(&m->periodic_try_send, ow_hs_mqtt_try_send_cb, 0.0, 0.0, NULL);
    m->periodic_try_send.data = m;
    ds_dlist_init(&m->builders, ow_hs_mqtt_steer_builder_t, node_m);
    LOGI(LOG_PREFIX_MQTT(m, "allocated"));
    return m;
}

void ow_hs_mqtt_set_interval_secs(ow_hs_mqtt_t *m, double interval_secs)
{
    if (m == NULL) return;
    const double current_interval = (double)m->periodic_try_send.interval;
    if (current_interval == interval_secs) return;
    if (interval_secs != 0.0)
        LOGI(LOG_PREFIX_MQTT(m, "stats enabled, set interval: %.1f seconds", interval_secs));
    else
        LOGI(LOG_PREFIX_MQTT(m, "stats disabled"));
    ev_periodic_stop(m->loop, &m->periodic_try_send);
    ev_periodic_set(&m->periodic_try_send, 0.0, interval_secs, NULL);
    ev_periodic_start(m->loop, &m->periodic_try_send);
}

void ow_hs_mqtt_drop(ow_hs_mqtt_t *m)
{
    if (m == NULL) return;
    LOGI(LOG_PREFIX_MQTT(m, "dropping"));
    ev_periodic_stop(m->loop, &m->periodic_try_send);
    ow_hs_mqtt_report_drop(m);

    ow_hs_mqtt_steer_builder_t *builder;
    ds_dlist_foreach (&m->builders, builder)
    {
        ow_hs_mqtt_steer_builder_notify_mqtt_dropped(builder);
        ds_dlist_remove(&m->builders, builder);
    }
    FREE(m->topic);
    FREE(m->node_id);
    FREE(m);
}

void ow_hs_mqtt_report_drop(ow_hs_mqtt_t *m)
{
    if (m == NULL) return;
    hotspot__hotspot_message__free_unpacked(m->hm, NULL);
    m->hm = NULL;
}

void ow_hs_mqtt_set_node_id(ow_hs_mqtt_t *m, const char *node_id)
{
    if (m == NULL) return;
    if (m->node_id == NULL && node_id == NULL) return;
    if (m->node_id != NULL && node_id != NULL && strcmp(m->node_id, node_id) == 0) return;
    LOGI(LOG_PREFIX_MQTT(m, "node_id: '%s' -> '%s'", m->node_id ?: "(null)", node_id ?: "(null)"));
    FREE(m->node_id);
    m->node_id = node_id ? STRDUP(node_id) : NULL;
}

void ow_hs_mqtt_set_topic(ow_hs_mqtt_t *m, const char *topic)
{
    if (m == NULL) return;
    if (m->topic == NULL && topic == NULL) return;
    if (m->topic != NULL && topic != NULL && strcmp(m->topic, topic) == 0) return;
    LOGI(LOG_PREFIX_MQTT(m, "topic: '%s' -> '%s'", m->topic ?: "(null)", topic ?: "(null)"));
    FREE(m->topic);
    m->topic = topic ? STRDUP(topic) : NULL;
}
