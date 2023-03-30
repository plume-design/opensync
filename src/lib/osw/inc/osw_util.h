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

#ifndef OSW_UTIL_H_INCLUDED
#define OSW_UTIL_H_INCLUDED

#include <math.h>
#include <osw_types.h>

struct element {
    uint8_t id;
    uint8_t datalen;
    uint8_t data[];
} __attribute__ ((packed));

/* element iteration helpers */
#define for_each_ie(_elem, _data, _datalen)                                                                             \
    for (_elem = (const struct element *) (_data);                                                                  \
        (const uint8_t *) (_data) + (_datalen) - (const uint8_t *) _elem >= (int) sizeof(*_elem) &&                \
        (const uint8_t *) (_data) + (_datalen) - (const uint8_t *) _elem >= (int) sizeof(*_elem) + _elem->datalen; \
        _elem = (const struct element *) (_elem->data + _elem->datalen))

static inline const void *
osw_ie_find(const void *ies,
            size_t ies_len,
            uint8_t id,
            size_t *found_ie_len)
{
    const struct element *ie;
    for_each_ie(ie, ies, ies_len) {
        if (ie->id == id) {
            *found_ie_len = ie->datalen;
            return ie->data;
        }
    }
    *found_ie_len = 0;
    return NULL;
}

#define OSW_BOOL_FMT "%s"
#define OSW_BOOL_ARG(val) ((val) ? "true" : "false")

static inline double
osw_periodic_get_next(const double interval_seconds,
                      const double offset_seconds,
                      const double now)
{
    if (interval_seconds <= 0) return 0;
    if (fabs(offset_seconds) >= interval_seconds) return 0;
    return ((floor(now / interval_seconds) + 1) * interval_seconds) + offset_seconds;
}

static inline bool
osw_periodic_is_expired(const double at,
                        const double now)
{
    return at && at <= now;
}

static inline bool
osw_periodic_eval(double *at,
                  const double interval_seconds,
                  const double offset_seconds,
                  const double now)
{
    const bool expired = osw_periodic_is_expired(*at, now);
    *at = osw_periodic_get_next(interval_seconds, offset_seconds, now);
    return expired;
}

/* Updates the current duration if provided number of
 * seconds is smaller. This is intended to prepare timer
 * expiry offset. Values <0 are special. Start by having
 * *duration < 0 (eg. -1) and update iteratively. If
 * duration is >=0 then at least one iteration was valid and
 * >= 0.
 */
static inline void
osw_min_duration(double *duration,
                 double new_duration)
{
    if (new_duration < 0) return;
    if (*duration < 0) *duration = new_duration;
    if (*duration > new_duration) *duration = new_duration;
}

struct osw_assoc_req_info {
    bool wnm_bss_trans;
    bool rrm_neighbor_link_meas;
    bool rrm_neighbor_bcn_pas_meas;
    bool rrm_neighbor_bcn_act_meas;
    bool rrm_neighbor_bcn_tab_meas;
    bool rrm_neighbor_lci_meas;
    bool rrm_neighbor_ftm_range_rep;
    uint8_t op_class_list[256];
    unsigned int op_class_cnt;
    unsigned int op_class_parse_errors;
    uint8_t channel_list[512];
    unsigned int channel_cnt;
    int8_t min_tx_power;
    int8_t max_tx_power;
    bool ht_caps_present;
    bool ht_caps_40;
    uint8_t ht_caps_smps;
    uint8_t ht_caps_rx_mcs[10];
    bool vht_caps_present;
    uint8_t vht_caps_sup_chan_w_set;
    bool vht_caps_mu_beamformee;
    uint16_t vht_caps_rx_mcs_map;
    bool he_caps_present;
    bool he_caps_2ghz_40;
    bool he_caps_40_80;
    bool he_caps_160;
    bool he_caps_160_8080;
    uint16_t he_caps_rx_mcs_map_le_80;
    bool he_caps_rx_mcs_map_160_present;
    uint16_t he_caps_rx_mcs_map_160;
    bool he_caps_rx_mcs_map_8080_present;
    uint16_t he_caps_rx_mcs_map_8080;
};

bool
osw_parse_assoc_req_ies(const void *assoc_req_ies,
                        size_t assoc_req_ies_len,
                        struct osw_assoc_req_info *info);

unsigned int
osw_vht_he_mcs_to_max_nss(const uint16_t mcs);

enum osw_channel_width
osw_assoc_req_to_max_chwidth(const struct osw_assoc_req_info *info);

unsigned int
osw_assoc_req_to_max_nss(const struct osw_assoc_req_info *info);

unsigned int
osw_assoc_req_to_max_mcs(const struct osw_assoc_req_info *info);

struct osw_circ_buf {
    size_t size;
    size_t head;
    size_t tail;
};

static inline void
osw_circ_buf_init(struct osw_circ_buf *circ_buf,
                  size_t size)
{
    assert(circ_buf != NULL);
    assert(size > 1);

    circ_buf->size = size;
    circ_buf->head = 0;
    circ_buf->tail = 0;
}

static inline bool
osw_circ_buf_is_empty(const struct osw_circ_buf *circ_buf)
{
    assert(circ_buf != NULL);
    return circ_buf->head == circ_buf->tail;
}

static inline size_t
osw_circ_buf_next(const struct osw_circ_buf *circ_buf,
                  size_t entry)
{
    assert(circ_buf != NULL);

    const size_t next = entry + 1;
    return next < circ_buf->size ? next : 0;
}

static inline bool
osw_circ_buf_is_full(const struct osw_circ_buf *circ_buf)
{
    assert(circ_buf != NULL);
    return circ_buf->head == osw_circ_buf_next(circ_buf, circ_buf->tail);
}

static inline size_t
osw_circ_buf_head(const struct osw_circ_buf *circ_buf)
{
    assert(circ_buf != NULL);
    return circ_buf->head;
}

static inline size_t
osw_circ_buf_tail(const struct osw_circ_buf *circ_buf)
{
    assert(circ_buf != NULL);
    return circ_buf->tail;
}

static inline bool
osw_circ_buf_pop(struct osw_circ_buf *circ_buf,
                 size_t *i)
{
    assert(circ_buf != NULL);
    assert(i != NULL);
    
    if (osw_circ_buf_is_empty(circ_buf) == true)
        return false;

    *i = circ_buf->head;
    circ_buf->head = osw_circ_buf_next(circ_buf, circ_buf->head);
    
    return true;
}

static inline bool
osw_circ_buf_push(struct osw_circ_buf *circ_buf,
                  size_t *i)
{
    assert(circ_buf != NULL);
    assert(i != NULL);

    if (osw_circ_buf_is_full(circ_buf) == true)
        return false;

    *i = circ_buf->tail;
    circ_buf->tail = osw_circ_buf_next(circ_buf, circ_buf->tail);

    return true;
}

static inline size_t
osw_circ_buf_push_rotate(struct osw_circ_buf *circ_buf)
{
    assert(circ_buf != NULL);

    size_t entry = circ_buf->tail;

    circ_buf->tail = osw_circ_buf_next(circ_buf, circ_buf->tail);
    if (circ_buf->head == circ_buf->tail)
        circ_buf->head = osw_circ_buf_next(circ_buf, circ_buf->head);

    return entry;
}

#define OSW_CIRC_BUF_FOREACH(circ_buf, i)       \
    for(i = osw_circ_buf_head(circ_buf);        \
        i != osw_circ_buf_tail(circ_buf);       \
        i = osw_circ_buf_next(circ_buf, i))

#endif /* OSW_UTIL_H_INCLUDED */
