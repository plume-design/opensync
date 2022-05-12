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

#ifndef FSM_DPI_UPNP_H_INCLUDED
#define FSM_DPI_UPNP_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "ds_tree.h"
#include "fsm.h"

enum upnp_state
{
    UNDEFINED = 0,
    BEGIN_UPNP,
    UPNP_ACTION,
    UPNP_PROTOCOL,
    UPNP_EXT_PORT,
    UPNP_INT_PORT,
    UPNP_INT_CLIENT,
    UPNP_DURATION,
    UPNP_DESC,
    END_UPNP,
};

struct fsm_upnp_record
{
    char description[80];
    char action[40];
    char int_client[16];
    char duration[16];
    char protocol[4];
    char int_port[6];
    char ext_port[6];
};

struct fsm_dpi_upnp_session
{
    bool initialized;

    char *igdControlUrl;
    char *upnpServiceType;
    time_t last_snapshot_fetch;
    time_t last_report_sent;
    size_t n_mapped_ports;
    size_t n_static_ports;
    ds_tree_t *mapped_ports;
    ds_tree_t *static_ports;
    struct upnp_report_aggregator_t *aggr;
    char *mqtt_topic;
    time_t snapshot_interval;
    time_t report_interval;
    size_t snapshot_max_entries;

    struct fsm_upnp_record curr_rec_processed;

    ds_tree_node_t next;
};

/**
 * @brief reset UPnP record
 */
void fsm_dpi_upnp_reset_state(struct fsm_session *session);

/**
 * @brief Initialize all the required structures
 *
 * @param session used to extract information about the session.
 */
int fsm_dpi_upnp_init(struct fsm_session *session);
int dpi_upnp_plugin_init(struct fsm_session *session);

/**
 * @brief Releases all allocated memory and un-initialize global
 *        aggregator.
 *
 * @param session
 */
void fsm_dpi_upnp_exit(struct fsm_session *session);

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm_dpi_dns session to update
 */
void fsm_dpi_upnp_update(struct fsm_session *session);

/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm_dpi_dns session to process
 */
void fsm_dpi_upnp_periodic(struct fsm_session *session);

/**
 * @brief process specifically a UPnP flow attribute
 *
 * @param session the fsm session
 * @param attr the attribute flow
 * @param type the value type (RTS_TYPE_BINARY, RTS_TYPE_STRING or RTS_TYPE_NUMBER)
 * @param length the length in bytes of the value
 * @param value the value itself
 * @param packet_info packet details (acc, net_parser)
 */
int fsm_dpi_upnp_process_attr(struct fsm_session *session, const char *attr,
                              uint8_t type, uint16_t length, const void *value,
                              struct fsm_dpi_plugin_client_pkt_info *pkt_info);

struct fsm_dpi_upnp_session *fsm_dpi_upnp_get_session(struct fsm_session *session);

/**
 * @brief Check record validity
 *
 * @remark This is exposed for UT's benefit
 */
bool
fsm_dpi_upnp_check_record(struct fsm_upnp_record *rec);

#endif /* FSM_DPI_UPNP_H_INCLUDED */
