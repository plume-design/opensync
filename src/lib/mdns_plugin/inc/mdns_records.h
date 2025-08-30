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

#ifndef MDNS_RECORDS_H_INCLUDED
#define MDNS_RECORDS_H_INCLUDED

#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>

#include "1035.h"
#include "ds_tree.h"
#include "ds_dlist.h"
#include "fsm.h"
#include "mdns_plugin.h"
#include "mdns_records_telemetry.pb-c.h"
#include "memutil.h"
#include "util.h"

#ifndef MAC_STR_LEN
#define MAC_STR_LEN         18
#endif /* MAC_STR_LEN */

#ifndef IP_STR_LEN
#define IP_STR_LEN          INET6_ADDRSTRLEN
#endif /* IP_STR_LEN */

#define DEFAULT_MDNS_RECORDS_REPORT_INTERVAL    (60)        // In seconds

typedef ds_dlist_t          mdns_records_list_t;

/**
 * @brief container to describe each element of a mdns record list
 *
 */
typedef struct
{
    struct resource         resource;
    bool                    staged;                         // True when the record is stage to be reported to cloud
    bool                    reported;                       // True when reported to the cloud
    time_t                  stored_ts;                      // Timestamp when the RR is received

    ds_dlist_node_t         node;
} mdns_records_t;

typedef struct
{
    char                    mac_str[MAC_STR_LEN];
    char                    ip_str[IP_STR_LEN];
    size_t                  num_records;
    struct sockaddr_storage from;

    mdns_records_list_t     records_list;

    ds_tree_node_t          dst_node;
} mdns_client_t;

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

/**
 * @brief container of information needed to set an observation window protobuf.
 *
 * Stashes the start and end times of an observation window.
 */
typedef struct
{
    uint64_t            started_at;
    uint64_t            ended_at;
} observation_window_t;

/**
 * @brief container of information needed to set a Mdns Records report protobuf
 *
 * Stashes information related to the records responded by multiple clients over the
 * course of an observation window.
 * The clients are presented as a tree of of mdns_client_t nodes.
 */
typedef struct
{
    bool                    initialized;
    node_info_t             node_info;
    observation_window_t    obs_w;

    ds_tree_t               stored_clients;
    ds_tree_t               staged_clients;
} mdns_records_report_data_t;

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
mdns_records_t *mdns_records_alloc_record(void)
{
    mdns_records_t   *rec;

    rec = CALLOC(1, sizeof(mdns_records_t));
    return rec;
}

static inline
void mdns_records_free_record(mdns_records_t *rec)
{
    FREE(rec);
}

static inline
mdns_client_t *mdns_records_alloc_client(char *ip)
{
    mdns_client_t *client;

    client = CALLOC(1, sizeof(mdns_client_t));

    /* Copy the IP address */
    STRSCPY(client->ip_str, ip);

    /* Initialize the records list */
    ds_dlist_init(&client->records_list, mdns_records_t, node);

    return client;
}

/******************************************************************************/

extern void                 mdns_records_clear_clients(ds_tree_t *clients);
extern size_t               mdns_records_get_num_clients(ds_tree_t *clients);
extern mdns_client_t       *mdns_client_find_client_by_macstr(ds_tree_t *clients, char *mac);
extern bool                 mdns_records_str_duplicate(char *src, char **dst);
extern void                 mdns_records_set_observation_window(observation_window_t *cur, observation_window_t *rep_obw);

extern void                 mdns_records_free_packed_buffer(packed_buffer_t *pb);
extern packed_buffer_t     *mdns_records_serialize_node_info(node_info_t *node);
extern packed_buffer_t     *mdns_records_serialize_window(observation_window_t *window);

extern packed_buffer_t     *mdns_records_serialize_record(mdns_records_t *rec);
extern Interfaces__MdnsRecordsTelemetry__MdnsRecord **mdns_records_set_pb_mdns_records(mdns_client_t *client);
extern void                 mdns_records_free_pb_record(Interfaces__MdnsRecordsTelemetry__MdnsRecord *pb);

extern packed_buffer_t     *mdns_records_serialize_client(mdns_client_t *client);
extern Interfaces__MdnsRecordsTelemetry__MdnsClient **mdns_records_set_pb_clients(mdns_records_report_data_t *report);
extern void                 mdns_records_free_pb_client(Interfaces__MdnsRecordsTelemetry__MdnsClient *pb);

extern packed_buffer_t     *mdns_records_serialize_report(mdns_records_report_data_t *report);

extern void                 mdns_records_collect_record(const struct resource *r, void *data, struct sockaddr_storage *from);
extern void                 mdns_records_send_records(struct mdns_session *md_session);
extern bool                 mdns_records_send_report(mdns_records_report_data_t *report, struct fsm_session *session);

extern bool                 mdns_records_init(struct mdns_session *md_session);
extern void                 mdns_records_exit(void);

/******************************************************************************/

#endif  /* MDNS_RECORDS_H_INCLUDED */
