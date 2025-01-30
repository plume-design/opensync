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
#include <latency.pb-c.h>
#include <qm_conn.h>
#include <os_time.h>
#include <ds_tree.h>

#include "sm_lat_mqtt.h"

#define LOG_PREFIX(fmt, ...) "sm: lat: mqtt: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_MQTT(m, fmt, ...) LOG_PREFIX("%p: %s: " fmt, (m), (m)->node_id ?: "", ##__VA_ARGS__)

#define LOG_PREFIX_ROLE(r, fmt, ...) LOG_PREFIX("%s: " fmt, (r)->if_name, ##__VA_ARGS__)

typedef struct sm_lat_mqtt_if_role sm_lat_mqtt_if_role_t;

struct sm_lat_mqtt
{
    struct ev_loop *loop;
    ds_tree_t if_roles;
    ev_async report_async;
    Latency__Report *r;
    char *node_id;
    char *topic;
};

struct sm_lat_mqtt_if_role
{
    ds_tree_node_t node;
    sm_lat_mqtt_t *m;
    char *if_name;
    char *if_role;
};

static sm_lat_mqtt_if_role_t *sm_lat_mqtt_if_role_alloc(sm_lat_mqtt_t *m, const char *if_name)
{
    sm_lat_mqtt_if_role_t *r = CALLOC(1, sizeof(*r));
    r->m = m;
    r->if_name = STRDUP(if_name);
    ds_tree_insert(&m->if_roles, r, r->if_name);
    LOGD(LOG_PREFIX_ROLE(r, "allocated"));
    return r;
}

static const char *sm_lat_mqtt_if_role_get(sm_lat_mqtt_t *m, const char *if_name)
{
    sm_lat_mqtt_if_role_t *r = ds_tree_find(&m->if_roles, if_name);
    if (r == NULL) return NULL;
    return r->if_role;
}

static void sm_lat_mqtt_if_role_set(sm_lat_mqtt_if_role_t *r, const char *if_role)
{
    if (r == NULL) return;
    if (r->if_role != NULL && if_role != NULL && (strcmp(r->if_role, if_role) == 0)) return;
    LOGI(LOG_PREFIX_ROLE(r, "set: '%s' -> '%s'", r->if_role ?: "", if_role ?: ""));
    FREE(r->if_role);
    r->if_role = if_role ? STRDUP(if_role) : NULL;
}

static void sm_lat_mqtt_if_role_drop(sm_lat_mqtt_if_role_t *r)
{
    if (r == NULL) return;
    LOGD(LOG_PREFIX_ROLE(r, "dropping"));
    ds_tree_remove(&r->m->if_roles, r);
    FREE(r->if_name);
    FREE(r->if_role);
    FREE(r);
}

static void sm_lat_mqtt_if_role_gc(sm_lat_mqtt_if_role_t *r)
{
    if (r == NULL) return;
    if (r->if_role != NULL) return;
    sm_lat_mqtt_if_role_drop(r);
}

static Latency__Report *sm_lat_mqtt_root(sm_lat_mqtt_t *m)
{
    if (m->r == NULL)
    {
        Latency__Report *r = MALLOC(sizeof(*r));
        latency__report__init(r);
        r->node_id = STRDUP(m->node_id ?: "");
        m->r = r;
    }
    return m->r;
}

static Latency__Host *sm_lat_mqtt_grow(sm_lat_mqtt_t *m)
{
    Latency__Report *r = sm_lat_mqtt_root(m);
    const size_t last = r->n_hosts++;
    const size_t elem_size = sizeof(r->hosts[0]);
    const size_t new_size = r->n_hosts * elem_size;
    r->hosts = REALLOC(r->hosts, new_size);
    Latency__Host **h = &r->hosts[last];
    *h = MALLOC(sizeof(**h));
    latency__host__init(*h);
    return *h;
}

static Latency__Sample *sm_lat_mqtt_grow_sample(Latency__Host *h)
{
    const size_t last = h->n_samples++;
    const size_t elem_size = sizeof(h->samples[0]);
    const size_t new_size = h->n_samples * elem_size;
    h->samples = REALLOC(h->samples, new_size);
    Latency__Sample **s = &h->samples[last];
    *s = MALLOC(sizeof(**s));
    latency__sample__init(*s);
    return *s;
}

static void sm_lat_mqtt_fill_hdr(sm_lat_mqtt_t *m, Latency__Host *dst, const sm_lat_core_host_t *src)
{
    const char *if_name = (const char *)src->if_name;
    const char *if_role = sm_lat_mqtt_if_role_get(m, if_name);
    dst->has_mac_address = true;
    dst->mac_address.len = sizeof(src->mac_address);
    dst->mac_address.data = MEMNDUP(src->mac_address, dst->mac_address.len);
    dst->if_name = STRDUP(if_name);
    dst->if_role = if_role ? STRDUP(if_role) : NULL;
    switch (src->dscp)
    {
        case SM_LAT_CORE_DSCP_NONE:
            break;
        case SM_LAT_CORE_DSCP_MISSING:
            dst->has_dscp_type = true;
            dst->dscp_type = LATENCY__DSCP_TYPE__MISSING;
            break;
        default:
            dst->has_dscp_type = true;
            dst->has_dscp_value = true;
            dst->dscp_type = LATENCY__DSCP_TYPE__PRESENT;
            dst->dscp_value = src->dscp;
            break;
    }
    dst->timestamp_ms = clock_real_ms();
}

static void sm_lat_mqtt_fill_sample(Latency__Sample *dst, const sm_lat_core_sample_t *src)
{
    if (src->min_ms)
    {
        dst->has_min_ms = true;
        dst->min_ms = *src->min_ms;
    }
    if (src->max_ms)
    {
        dst->has_max_ms = true;
        dst->max_ms = *src->max_ms;
    }
    if (src->avg_sum_ms && src->avg_cnt)
    {
        dst->has_avg_ms = true;
        dst->avg_ms = (*src->avg_sum_ms) / (*src->avg_cnt);
    }
    if (src->last_ms)
    {
        dst->has_last_ms = true;
        dst->last_ms = *src->last_ms;
    }
    if (src->num_pkts)
    {
        dst->has_num_pkts = true;
        dst->num_pkts = *src->num_pkts;
    }
    if (src->timestamp_ms != 0)
    {
        dst->has_timestamp_ms = true;
        dst->timestamp_ms = src->timestamp_ms;
    }
}

static void sm_lat_mqtt_report_host(sm_lat_mqtt_t *m, const sm_lat_core_host_t *host)
{
    Latency__Host *l = sm_lat_mqtt_grow(m);
    sm_lat_mqtt_fill_hdr(m, l, host);
    size_t i;
    for (i = 0; i < host->n_samples; i++)
    {
        Latency__Sample *s = sm_lat_mqtt_grow_sample(l);
        sm_lat_mqtt_fill_sample(s, &host->samples[i]);
    }
}

static void sm_lat_mqtt_report_send(sm_lat_mqtt_t *m)
{
    if (m->r == NULL)
    {
        LOGD(LOG_PREFIX_MQTT(m, "report: cannot send: no data has been collected"));
        return;
    }

    if (m->topic == NULL)
    {
        LOGD(LOG_PREFIX_MQTT(m, "report: cannot send: topic is undefined"));
        return;
    }

    const size_t size = latency__report__get_packed_size(m->r);
    void *buf = MALLOC(size);
    const size_t len = latency__report__pack(m->r, buf);
    LOGI(LOG_PREFIX_MQTT(m, "sending %zu bytes", len));
    qm_response_t res;
    const bool sent = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, m->topic, buf, len, &res);
    WARN_ON(sent == false);
}

static void sm_lat_mqtt_report_drop(sm_lat_mqtt_t *m)
{
    if (m->r == NULL) return;
    latency__report__free_unpacked(m->r, NULL);
    m->r = NULL;
}

static void sm_lat_mqtt_report_async_cb(struct ev_loop *l, ev_async *a, int mask)
{
    sm_lat_mqtt_t *m = a->data;
    sm_lat_mqtt_report_send(m);
    sm_lat_mqtt_report_drop(m);
}

static void sm_lat_mqtt_report_schedule(sm_lat_mqtt_t *m)
{
    ev_async_send(m->loop, &m->report_async);
}

void sm_lat_mqtt_report(sm_lat_mqtt_t *m, const sm_lat_core_host_t *const *hosts, size_t count)
{
    LOGI(LOG_PREFIX_MQTT(m, "reporting %zu hosts", count));
    for (; count > 0; count--, hosts++)
    {
        if (WARN_ON(*hosts == NULL)) continue;
        sm_lat_mqtt_report_host(m, *hosts);
    }
    sm_lat_mqtt_report_schedule(m);
}

sm_lat_mqtt_t *sm_lat_mqtt_alloc(void)
{
    sm_lat_mqtt_t *m = CALLOC(1, sizeof(*m));
    ds_tree_init(&m->if_roles, ds_str_cmp, sm_lat_mqtt_if_role_t, node);
    m->loop = EV_DEFAULT;
    ev_async_init(&m->report_async, sm_lat_mqtt_report_async_cb);
    ev_async_start(m->loop, &m->report_async);
    m->report_async.data = m;
    LOGI(LOG_PREFIX_MQTT(m, "allocated"));
    return m;
}

static void sm_lat_mqtt_drop_if_roles(sm_lat_mqtt_t *m)
{
    sm_lat_mqtt_if_role_t *r;
    while ((r = ds_tree_head(&m->if_roles)) != NULL)
    {
        sm_lat_mqtt_if_role_drop(r);
    }
}

void sm_lat_mqtt_drop(sm_lat_mqtt_t *m)
{
    if (m == NULL) return;
    LOGI(LOG_PREFIX_MQTT(m, "dropping"));
    latency__report__free_unpacked(m->r, NULL);
    ev_async_stop(m->loop, &m->report_async);
    sm_lat_mqtt_drop_if_roles(m);
    FREE(m->topic);
    FREE(m->node_id);
    FREE(m);
}

void sm_lat_mqtt_set_node_id(sm_lat_mqtt_t *m, const char *node_id)
{
    if (m == NULL) return;
    if (m->node_id == NULL && node_id == NULL) return;
    if (m->node_id != NULL && node_id != NULL && strcmp(m->node_id, node_id) == 0) return;
    LOGI(LOG_PREFIX_MQTT(m, "node_id: '%s' -> '%s'", m->node_id ?: "(null)", node_id ?: "(null)"));
    FREE(m->node_id);
    m->node_id = node_id ? STRDUP(node_id) : NULL;
}

void sm_lat_mqtt_set_topic(sm_lat_mqtt_t *m, const char *topic)
{
    if (m == NULL) return;
    if (m->topic == NULL && topic == NULL) return;
    if (m->topic != NULL && topic != NULL && strcmp(m->topic, topic) == 0) return;
    LOGI(LOG_PREFIX_MQTT(m, "topic: '%s' -> '%s'", m->topic ?: "(null)", topic ?: "(null)"));
    FREE(m->topic);
    m->topic = topic ? STRDUP(topic) : NULL;
}

void sm_lat_mqtt_set_if_role(sm_lat_mqtt_t *m, const char *if_name, const char *if_role)
{
    if (m == NULL) return;
    if (if_name == NULL) return;
    sm_lat_mqtt_if_role_t *r = ds_tree_find(&m->if_roles, if_name);
    if (r == NULL && if_role == NULL) return;
    if (r == NULL) r = sm_lat_mqtt_if_role_alloc(m, if_name);
    sm_lat_mqtt_if_role_set(r, if_role);
    sm_lat_mqtt_if_role_gc(r);
}
