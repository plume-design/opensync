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

#ifndef GATEKEEPER_HERO_STATS_H_INCLUDED
#define GATEKEEPER_HERO_STATS_H_INCLUDED

#include "fsm.h"
#include "network_metadata_report.h"
#include "os_types.h"
#include "qm_conn.h"
#include "gatekeeper_msg.h"
#include "gatekeeper_hero_stats.pb-c.h"

/**
 * @brief  Aggregator for the intermediate protobufs to be
 *         used when creating the hero cache reports.
 */
struct gkc_report_aggregator
{
    char *node_id;
    char *location_id;
    size_t report_max_size;
    size_t header_size;
    time_t start_observation_window;
    time_t end_observation_window;
    size_t num_observation_windows; /*!< actual number of obs windows */
    Gatekeeper__HeroStats__HeroObservationWindow **windows;
    size_t windows_idx; /*!< current number of obs windows fragments */
    size_t windows_prov;
    size_t windows_max;
    Gatekeeper__HeroStats__HeroStats **stats;
    size_t stats_idx;
    size_t stats_prov;
    size_t stats_max;
    bool initialized;

    /** Helper for UT initialized to @see qm_conn_send_direct */
    bool (*send_report)(qm_compress_t compress, char *topic,
                        void *data, int data_size, qm_response_t *res);
};

/**
 * @brief Get a handle on the aggregator used to serialize and report
 * the hero cache
 *
 * @return a pointer to the aggregator
 */
struct gkc_report_aggregator *
gkhc_get_aggregator(void);

/**
 * @brief Initializes the global aggregator for hero cache
 *
 * @param aggr a pointer to the aggregator to initialize
 * @param session contains the @see node_id and @see location_id
 *                used in the final reporting
 * @return true if properly (or already initialized)
 *         false if anything when wrong
 *
 * @remark while the aggregator should always be a singleton,
 *         it is preferred to not use it directly .
 */
bool
gkhc_init_aggregator(struct gkc_report_aggregator *aggr,
                     struct fsm_session *session);

/**
 * @brief Releases and un-initializes the global allocator.
 *        After the call, all allocated memory has been released.
 *
 * @param aggr a pointer to the aggregator to initialize
 *
  @remark while the aggregator should always be a singleton,
 *         it is preferred to not use it directly .
 */
void
gkhc_release_aggregator(struct gkc_report_aggregator *aggr);

/**
 * @brief Starts a new observation window
 *
 * @param aggr a pointer to the initialized global aggregator
 */
void
gkhc_activate_window(struct gkc_report_aggregator *aggr);

/**
 * @brief Ends a observation window, and builds the corresponding
 *        data structure for this obs window.
 *
 * @param aggr a pointer to the initialized global aggregator
 */
void
gkhc_close_window(struct gkc_report_aggregator *aggr);

/**
 * @brief Generates a set of serialized reports, each with at
 *        most MAX_RECORDS entries.
 *
 * @param args structure with the information related to the report
 *
 * @return true if the report was successfully generated
 */
bool
gkhc_serialize_cache_entries(struct gkc_report_aggregator *aggr);

/**
 * @brief Allowing to change number of records per cache report
 *
 * @param aggr a pointer to the initialized global aggregator
 * @param n new number of records for each cache report
 */
void
gkhc_set_records_per_report(struct gkc_report_aggregator *aggr, size_t n);

/**
 * @brief Allowing to change maximum size of report (in bytes)
 *
 * @param aggr a pointer to the initialized global aggregator
 * @param n new maximum size for each serialized cache report
 */
void
gkhc_set_max_record_size(struct gkc_report_aggregator *aggr, size_t n);

/**
 * @brief Allowing to change number of anticipated observation windows
 *
 * @param aggr a pointer to the initialized global aggregator
 * @param n new number of observation windows to handle
 */
void
gkhc_set_number_obs_windows(struct gkc_report_aggregator *aggr, size_t n);

/**
 * @brief send report for hero cache
 *
 * @param aggr a pointer to the initialized global aggregator
 * @param mqtt_channel the name of the MQTT topic to report to
 *
 * @return number of successfully sent reports (possibly 0) or -1 in case of error.
 */
int
gkhc_build_and_send_report(struct gkc_report_aggregator *aggr, char *mqtt_topic);

/**
 * @brief send report for hero cache
 *
 * @param session pointer to the matching session
 * @param interval the minimum number of seconds between reports
 *
 * @return number of successfully sent reports (possibly 0) or -1 in case of error.
 */
int
gkhc_send_report(struct fsm_session *session, long interval);


#endif /* GATEKEEPER_HERO_STATS_H_INCLUDED */
