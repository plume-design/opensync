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

#ifndef OW_STATS_CONF_H_INCLUDED
#define OW_STATS_CONF_H_INCLUDED

struct ow_stats_conf;
struct ow_stats_conf_entry;

enum ow_stats_conf_scan_type {
    OW_STATS_CONF_SCAN_TYPE_UNSPEC,
    OW_STATS_CONF_SCAN_TYPE_ON_CHAN,
    OW_STATS_CONF_SCAN_TYPE_OFF_CHAN,
    OW_STATS_CONF_SCAN_TYPE_FULL,
};

enum ow_stats_conf_stats_type {
    OW_STATS_CONF_STATS_TYPE_UNSPEC,
    OW_STATS_CONF_STATS_TYPE_SURVEY,
    OW_STATS_CONF_STATS_TYPE_NEIGHBOR,
    OW_STATS_CONF_STATS_TYPE_CLIENT,
};

enum ow_stats_conf_radio_type {
    OW_STATS_CONF_RADIO_TYPE_UNSPEC,
    OW_STATS_CONF_RADIO_TYPE_2G,
    OW_STATS_CONF_RADIO_TYPE_5G,
    OW_STATS_CONF_RADIO_TYPE_5GL,
    OW_STATS_CONF_RADIO_TYPE_5GU,
    OW_STATS_CONF_RADIO_TYPE_6G,
};

struct ow_stats_conf *
ow_stats_conf_get(void);

struct ow_stats_conf_entry *
ow_stats_conf_get_entry(struct ow_stats_conf *conf,
                        const char *id);

/**
 * Marks all entries for a clean up in a same way
 * ow_stats_conf_entry_reset() does.
 *
 *.Possible use case is for data models that do
 * not signal removals and provide complete
 * configurration sets every time they get
 * updated.
 */
void
ow_stats_conf_entry_reset_all(struct ow_stats_conf *conf);

/**
 * Marks an entry for removal. If any
 * ow_stats_conf_entry_set_*() is called
 * afterwards the entry will not be removed and if
 * it gets set with identical parameters prior to
 * reset it will amount to no-op as far as
 * underlying work and internal states are
 * concerned
 */
void
ow_stats_conf_entry_reset(struct ow_stats_conf_entry *e);

void
ow_stats_conf_entry_set_sampling(struct ow_stats_conf_entry *e,
                                 int seconds);

void
ow_stats_conf_entry_set_reporting(struct ow_stats_conf_entry *e,
                                  int seconds);

void
ow_stats_conf_entry_set_reporting_limit(struct ow_stats_conf_entry *e,
                                        unsigned int count);

void
ow_stats_conf_entry_set_radio_type(struct ow_stats_conf_entry *e,
                                   enum ow_stats_conf_radio_type radio_type);

void
ow_stats_conf_entry_set_scan_type(struct ow_stats_conf_entry *e,
                                   enum ow_stats_conf_scan_type scan_type);

void
ow_stats_conf_entry_set_stats_type(struct ow_stats_conf_entry *e,
                                   enum ow_stats_conf_stats_type stats_type);

void
ow_stats_conf_entry_set_channels(struct ow_stats_conf_entry *e,
                                 const int *channels,
                                 size_t n_channels);

void
ow_stats_conf_entry_set_dwell_time(struct ow_stats_conf_entry *e,
                                   unsigned int msec);

void
ow_stats_conf_entry_set_holdoff_busy(struct ow_stats_conf_entry *e,
                                     unsigned int threshold);

void
ow_stats_conf_entry_set_holdoff_delay(struct ow_stats_conf_entry *e,
                                      unsigned int seconds);

#endif /* OW_STATS_CONF_H_INCLUDED */
