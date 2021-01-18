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

#ifndef NETWORK_METADATA_H_INCLUDED
#define NETWORK_METADATA_H_INCLUDED

#include <stdint.h>
#include <time.h>

#include "network_metadata.pb-c.h"

/**
 * @brief container of information needed to set an observation point protobuf.
 *
 * Stashes a pod's serial number and deployment location id.
 */
struct node_info
{
    char *node_id;     /*!< pod's serial number */
    char *location_id; /*!< pod's deployment location id */
};

/**
 * @brief container of information about the flow state
 *
 * Stashes information related to flow start/end
 */

struct flow_state
{
    time_t   first_obs;
    time_t   last_obs;
    bool     fstart;
    bool     fend;
    bool     report_attrs;
};

/**
 * @brief container of information needed to set a flowkey protobuf.
 *
 * Stashes information related to a network flow.
 */
struct flow_key
{
    char *smac;         /*!< source mac address of the flow */
    bool isparent_of_smac;  /*!< is this report from parent of the source mac. */
    char *dmac;         /*!< destination mac address of the flow */
    bool isparent_of_dmac;  /*!< is this report from parent of the reporting pod. */
    uint32_t vlan_id;   /*!< valid vlan id if detected, 0 otherwise */
    uint16_t ethertype; /*!< ethernet type as defined by IANA */
    uint8_t ip_version; /*!< ip version */
    char *src_ip;       /*!< source IP in string representation */
    char *dst_ip;       /*!< destination IP in string representation */
    uint8_t protocol;   /*!< transport protocol ID (udp, tcp, etc ...) */
    uint16_t sport;     /*!< transport protocol source port */
    uint16_t dport;     /*!< transport protocol destination port */
    size_t num_tags;    /*!< number of flow tags */
    struct flow_tags **tags; /*!< flow tags */
    size_t num_vendor_data;  /*!< number of vendor data containers */
    struct flow_vendor_data **vdr_data; /*!< vendor data container */
    struct flow_state state; /*!< Flow state */
};

/**
 * @brief container of information needed to set a flowcounters protobuf.
 *
 * Stashes information related to a network flow activity.
 */
struct flow_counters
{
    uint64_t packets_count;       /*!< packet count */
    uint64_t bytes_count;         /*!< bytes count */
    uint64_t payload_bytes_count; /*!< payload bytes count */
};

/**
 * @brief container of information needed to set a flowstats protobuf
 *
 * Stashes information identifying a network flow and its activity.
 * The key might be allocated or referenced, owns_key represents it.
 */
struct flow_stats
{
    bool owns_key;
    struct flow_key *key;           /*!< flow key */
    struct flow_counters *counters; /*!< flow counters */
};

/**
 * @brief container of information needed to set a flowstats protobuf
 *
 * Stashes information related to the activity of multiple network flows
 * within a time window.
 * The flow stats are presented as an array of flow stats pointers.
 */
struct flow_window
{
    uint64_t started_at;             /*!< time window start (epoch) */
    uint64_t ended_at;               /*!< time window end (epoch) */
    size_t num_stats;                /*!< # of reported flow stats containers */
    size_t provisioned_stats;        /*!< # of provisioned flow stats containers */
    size_t dropped_stats;            /*!< # of dropped flow stats containers */
    struct flow_stats **flow_stats;  /*!< array of flow stats containers */
};

/**
 * @brief container of information needed to set a flow report protobuf.
 *
 * Stashes information related to the activity of multiple network flows
 * over the course of multiple time windows.
 * The flow windows are presented as an array of flow window pointers.
 */
struct flow_report
{
    uint64_t reported_at;              /*!< report time (epoch) */
    struct node_info *node_info;       /*!< opservation point */
    size_t num_windows;                /*!< number of flow stats containers */
    struct flow_window **flow_windows; /*!< array of flow window containers */
};


/**
 * @brief container for traffic tags
 */
struct flow_tags
{
    char *vendor;
    char *app_name;
    size_t nelems;
    char **tags;
};


enum vendor_value_type
{
    NET_VENDOR_STR = 0,
    NET_VENDOR_U32,
    NET_VENDOR_U64,
};


/**
 * @brief container for <string key, string value> pair
 */
struct vendor_data_kv_pair
{
    char *key;
    uint8_t value_type;
    char *str_value;
    uint32_t u32_value;
    uint64_t u64_value;
};


/**
 * @brief container for a specific vendor data
 */
struct flow_vendor_data
{
    char *vendor;
    size_t nelems;
    struct vendor_data_kv_pair **kv_pairs;
};

/**
 * @brief container for all vendor data
 */
struct flow_vendor_data_container
{
    size_t nelems;
    struct flow_vendor_data **vdr_data;
};


/**
 * @brief Container of protobuf serialization output
 *
 * Contains the information related to a serialized protobuf
 */
struct packed_buffer
{
    size_t len; /*<! length of the serialized protobuf */
    void *buf;  /*<! dynamically allocated pointer to serialized data */
};


/**
 * @brief Frees the pointer to serialized data and container
 *
 * Frees the dynamically allocated pointer to serialized data (pb->buf)
 * and the container (pb)
 *
 * @param pb a pointer to a serialized data container
 * @return none
 */
void free_packed_buffer(struct packed_buffer *pb);


/**
 * @brief Generates an observation point serialized protobuf
 *
 * Uses the information pointed by the info parameter to generate
 * a serialized obervation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param node info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer * serialize_node_info(struct node_info *info);


/**
 * @brief Generates a flow_key serialized protobuf
 *
 * Uses the information pointed by the key parameter to generate
 * a serialized flow key buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param key info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer * serialize_flow_key(struct flow_key *key);


/**
 * @brief Generates a flow counters serialized protobuf
 *
 * Uses the information pointed by the counter parameter to generate
 * a serialized flow counters buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param counters info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
serialize_flow_counters(struct flow_counters *counters);


/**
 * @brief Generates a flow tags serialized protobuf
 *
 * Uses the information pointed by the counter parameter to generate
 * a serialized flow tags buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param counters info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
serialize_flow_tags(struct flow_tags *tags);


/**
 * @brief Generates a vendor kvpair serialized protobuf
 *
 * Uses the information pointed by the kv pair parameter to generate
 * a serialized vendor kvpair buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param counters info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
serialize_vdr_kvpair(struct vendor_data_kv_pair *vendor_kvpair);


/**
 * @brief Generates a flow vendor data serialized protobuf
 *
 * Uses the information pointed by the kv pair parameter to generate
 * a serialized vendor kvpair buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param counters info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
serialize_vendor_data(struct flow_vendor_data *vendor_data);

struct packed_buffer *
serialize_flow_state(struct flow_state *flow_state);

/**
 * @brief Allocates and sets a flow tags protobuf.
 *
 * Uses the flow tags info to fill a dynamically allocated
 * flow key protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_flow_tags() for this purpose.
 *
 * @param counters info used to fill up the protobuf
 * @return a pointer to a flow counters protobuf structure
 */
Traffic__FlowTags *
set_flow_tags(struct flow_tags *tags);


/**
 * @brief Allocates and sets a table of tags protobufs
 *
 * Uses the key info to fill a dynamically allocated
 * table of flow stats protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param key info used to fill up the protobuf table
 * @return a flow tags protobuf pointers table
 */
Traffic__FlowTags **
set_pb_flow_tags(struct flow_key *key);


/**
 * @brief Free a flow tag protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow tag structure to free
 * @return none
 */
void free_pb_flow_tags(Traffic__FlowTags *pb);


/**
 * @brief Allocates and sets a vendor key/value protobuf.
 *
 * Uses the vendor kv_pair info to fill a dynamically allocated
 * flow key protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_vendor_kv() for this purpose.
 *
 * @param counters info used to fill up the protobuf
 * @return a pointer to a flow counters protobuf structure
 */
Traffic__VendorDataKVPair *
set_vendor_kv(struct vendor_data_kv_pair *kv_pair);


/**
 * @brief Free a flow tag protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow tag structure to free
 * @return none
 */
void free_pb_vendor_kv(Traffic__VendorDataKVPair *pb);


/**
 * @brief Allocates and sets a table of vendor_kv_pair protobufs
 *
 * Uses the key info to fill a dynamically allocated
 * table of flow stats protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param key info used to fill up the protobuf table
 * @return a flow tags protobuf pointers table
 */
Traffic__VendorDataKVPair **
set_pb_vendor_kv_pairs(struct flow_vendor_data *vdr_data);


/**
 * @brief Allocates and sets a vendor key/value protobuf.
 *
 * Uses the vendor kv_pair info to fill a dynamically allocated
 * flow key protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_vendor_kv() for this purpose.
 *
 * @param counters info used to fill up the protobuf
 * @return a pointer to a flow counters protobuf structure
 */
Traffic__VendorData *
set_vendor_data(struct flow_vendor_data *vendor_data);


/**
 * @brief Free a flow tag protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow tag structure to free
 * @return none
 */
void
free_pb_vendor_data(Traffic__VendorData *pb);


/**
 * @brief Allocates and sets a table of vendor_kv_pair protobufs
 *
 * Uses the key info to fill a dynamically allocated
 * table of flow stats protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param key info used to fill up the protobuf table
 * @return a flow tags protobuf pointers table
 */
Traffic__VendorData **
set_pb_vendor_data(struct flow_key *flow_key);


/**
 * @brief Allocates and sets table of stats protobufs
 *
 * Uses the window info to fill a dynamically allocated
 * table of flow stats protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param window info used to fill up the protobuf table
 * @return a flow stats protobuf pointers table
 */
Traffic__FlowStats **set_pb_flow_stats(struct flow_window *window);


/**
 * @brief Free a flow stats protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow stats structure to free
 * @return none
 */
void free_pb_flowstats(Traffic__FlowStats *pb);


/**
 * @brief Generates a flow stats serialized protobuf
 *
 * Uses the information pointed by the stats parameter to generate
 * a serialized obervation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param stats info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer * serialize_flow_stats(struct flow_stats *stats);


/**
 * @brief Generates an observation window serialized protobuf
 *
 * Uses the information pointed by the window parameter to generate
 * a serialized obervation window buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param window info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer * serialize_flow_window(struct flow_window *window);


/**
 * @brief Allocates and sets table of observation window protobufs
 *
 * Uses the report info to fill a dynamically allocated
 * table of observation window protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param window info used to fill up the protobuf table
 * @return a flow stats protobuf pointers table
 */
Traffic__ObservationWindow ** set_pb_windows(struct flow_report *report);

/**
 * @brief Free an observation window protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flows window structure to free
 * @return none
 */
void free_pb_window(Traffic__ObservationWindow *pb);


/**
 * @brief Generates a flow report serialized protobuf
 *
 * Uses the information pointed by the report parameter to generate
 * a serialized flow report buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param node info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer * serialize_flow_report(struct flow_report *report);


/**
 * @brief free the flow tags of a flow key
 */
void
free_flow_key_tags(struct flow_key *key);

#endif /* NETWORK_METADATA_H_INCLUDED */
