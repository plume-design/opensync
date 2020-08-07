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

#ifndef INTF_STATS_H_INCLUDED
#define INTF_STATS_H_INCLUDED

#define IFNAME_LEN      (17)
#define INTF_ROLE_LEN   (64)
#define MAX_STRLEN      (256)

#include "ds.h"
#include "ds_dlist.h"
#include "interface_stats.pb-c.h"

/**
 * @brief container of information needed to set Intf Stats protobuf
 *
 * Stashes information relation to stats of an interface
 */
typedef struct 
{
    char                ifname[IFNAME_LEN];
    char                role[INTF_ROLE_LEN];

    uint64_t            tx_bytes;
    uint64_t            rx_bytes;

    uint64_t            tx_packets;
    uint64_t            rx_packets;

    ds_dlist_node_t     node;
} intf_stats_t;

/**
 * @brief container of information needed to set an observation point protobuf.
 *
 * Stashes a pod's serial number and deployment location id.
 */
typedef struct
{
    char                *node_id;
    char                *location_id;
} node_info_t;

typedef ds_dlist_t      intf_stats_list_t;

/**
 * @brief container of information needed to set a ObservationWindow protobuf
 *
 * Stashes information related to acitivity on interfaces within a time window
 * The per-interface stats are presented as an double-link list of intf stat nodes
 */
typedef struct
{
    size_t              window_idx;
    uint64_t            started_at;
    uint64_t            ended_at;
    size_t              num_intfs;

    intf_stats_list_t   intf_list;
} intf_stats_window_t;

/**
 * @brief container to desribe each element of a Observation Window list
 *
 */
typedef struct
{
    intf_stats_window_t entry;
    ds_dlist_node_t     node;
} intf_stats_window_list_t;

/**
 * @brief container of information needed to set a Intf Stats report protobuf
 *
 * Stashes information related to the activity of multiple interfaces over the
 * course of mulitple time windows.
 * The observation windows are presented as an double-linked list of Observation
 * window nodes.
 */
typedef struct
{
    uint64_t            reported_at;
    node_info_t         node_info;
    size_t              num_windows;

    intf_stats_list_t   window_list;
} intf_stats_report_data_t;

/**
 * @brief Container of protobuf serialization output
 *
 * Contains the information related to a serialized protobuf
 */
typedef struct
{
    size_t              len;    /*<! Length of the serialized protobuf */
    void                *buf;   /*<! Dynamically allocataed pointer to serialied data */
} packed_buffer_t;

static inline
intf_stats_t *intf_stats_intf_alloc(void)
{
    intf_stats_t *intf = NULL;

    intf = calloc(1, sizeof(intf_stats_t));
    return intf;
}

static inline
void intf_stats_intf_free(intf_stats_t *intf)
{
    if (intf) free(intf);
}

static inline
intf_stats_window_list_t *intf_stats_window_alloc(void)
{
    intf_stats_window_list_t *window = NULL;

    window = calloc( 1, sizeof(intf_stats_window_list_t));
    return window;
}

static inline
void intf_stats_window_free(intf_stats_window_list_t *window)
{
    if (window) free(window);
}

/******************************************************************************/

extern void                             intf_stats_dump_report(intf_stats_report_data_t *report);
extern void                             intf_stats_activate_window(intf_stats_report_data_t  *report);
extern void                             intf_stats_close_window(intf_stats_report_data_t *report);
extern intf_stats_window_t             *intf_stats_get_current_window(intf_stats_report_data_t *report);
extern void                             intf_stats_reset_report(intf_stats_report_data_t *report);

extern void                             intf_stats_free_pb_intf_stats(Intf__Stats__IntfStats *pb);
extern packed_buffer_t                 *intf_stats_serialize_intf_stats(intf_stats_t *intf);
extern packed_buffer_t                 *intf_stats_serialize_window(intf_stats_window_t *window);
extern packed_buffer_t                 *intf_stats_serialize_node_info(node_info_t *node);
extern void                             intf_stats_free_pb_window(Intf__Stats__ObservationWindow *pb);
extern packed_buffer_t                 *intf_stats_serialize_report(intf_stats_report_data_t *report);
extern bool                             intf_stats_send_report(intf_stats_report_data_t *report, char *mqtt_topic);
extern void                             intf_stats_free_packed_buffer(packed_buffer_t *pb);

extern Intf__Stats__ObservationWindow **intf_stats_set_pb_windows(intf_stats_report_data_t *report);
extern Intf__Stats__IntfStats         **intf_stats_set_pb_intf_stats(intf_stats_window_t *window);

#endif /* INTF_STATS_H_INCLUDED */
