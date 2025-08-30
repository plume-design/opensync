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

#ifndef FSM_DPI_ADT_UPNP_H_INCLUDED
#define FSM_DPI_ADT_UPNP_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

// #include "adv_data_typing.pb-c.h"
#include "ds_tree.h"
#include "fsm.h"
#include "fsm_policy.h"
#include "network_metadata_report.h"
#include "os_types.h"
#include "qm_conn.h"

/* Defines how often we'll send a probe on the subnet */
#define PROBE_UPNP (2.0 * 60)

#define FSM_UPNP_URL_MAX_SIZE 512
#define FSM_UPNP_UA_MAX_SIZE 1024

typedef enum
{
    FSM_DPI_ADT_UPNP_INIT = 0,
    FSM_DPI_ADT_UPNP_STARTED,
    FSM_DPI_ADT_UPNP_COMPLETE,
} upnp_state_t;

/* Provide some return values for some errors */
enum
{
    NO_ACC = -1,
    NO_INFO = -2,
    WRONG_IP = -3,
    NULL_PARAM = -4,
};

/* The upnp spec defines most of the fields' lengths */
struct fsm_dpi_adt_upnp_root_desc
{
    char url[FSM_UPNP_URL_MAX_SIZE];
    char dev_type[FSM_UPNP_URL_MAX_SIZE];
    char friendly_name[64];
    char manufacturer[64];
    char manufacturer_url[FSM_UPNP_URL_MAX_SIZE];
    char model_desc[128];
    char model_name[32];
    char model_num[32];
    char model_url[FSM_UPNP_URL_MAX_SIZE];
    char serial_num[64];
    char udn[164];
    char upc[12];
    struct upnp_device *udev;
    struct fsm_session *session;
    time_t timestamp;
    upnp_state_t state;
    ds_tree_node_t next_node;
};

/* Structure aggregating all the devices and their root description(s) */
struct upnp_device
{
    os_macaddr_t device_mac;  /*!< the device MAC */
    ds_tree_t descriptions;   /*!< the list of all root descriptions for the device
                                   (struct fsm_dpi_adt_upnp_root_desc) */
    ds_tree_node_t next_node;
};

/**
 * @brief Container of protobuf serialization output
 *
 * Contains the information related to a serialized protobuf
 */
struct fsm_dpi_adt_upnp_packed_buffer
{
    size_t len; /*<! length of the serialized protobuf */
    void *buf;  /*<! dynamically allocated pointer to serialized data */
};

struct fsm_dpi_adt_upnp_data_record
{
    struct net_md_flow_info info;
    uint64_t capture_time_ms;
    char *network_id;
    char *key;
    char *value;
    size_t value_len;
    uint32_t transport;
};

struct fsm_dpi_adt_upnp_report_aggregator
{
    char *node_id;
    char *location_id;

    struct fsm_dpi_adt_upnp_data_record **data;
    size_t data_idx; /*!< current number of data records */
    size_t data_prov;
    size_t data_max;

    bool initialized;

    /** Helper for UT (initialized to @see qm_conn_send_direct() */
    bool (*send_report)(qm_compress_t compress, char *topic,
                        void *data, int data_size, qm_response_t *res);
};

struct fsm_dpi_adt_upnp_session
{
    struct fsm_dpi_adt_upnp_report_aggregator *adt_upnp_aggr;
    ds_tree_t session_upnp_devices;  /*<! keep track of the devices we know about (struct upnp_device) */
    time_t last_scan;
    bool initialized;
};

/**
 * @brief Initialize all the required structures to store, serialize and
 *        send ADT reports
 *
 * @param session used to extract information about the session.
 */
int fsm_dpi_adt_upnp_init(struct fsm_session *session);
int dpi_adt_upnp_plugin_init(struct fsm_session *session);

/**
 * @brief Releases all allocated memory and un-initialize global
 *        aggregator.
 *
 * @param session uses to fetch mqtt_channel (see @remark).
 *
 * @remark An attempt to send back any ADT record still store will be
 *         performed.
 */
void fsm_dpi_adt_upnp_exit(struct fsm_session *session);

/**
 * @brief periodic routine
 *
 * @param session the fsm session to process
 */
void fsm_dpi_adt_upnp_periodic(struct fsm_session *session);

/**
 * @brief update routine
 *
 * @param session the fsm session to update
 */
void fsm_dpi_adt_upnp_update(struct fsm_session *session);

/**
 * @brief process an ADT flow attribute specifically
 *
 * @param session the fsm session
 * @param attr the attribute flow
 * @param type the value type (RTS_TYPE_BINARY, RTS_TYPE_STRING or RTS_TYPE_NUMBER)
 * @param length the length in bytes of the value
 * @param value the value itself
 * @param packet_info packet details (acc, net_parser)
 */
int fsm_dpi_adt_upnp_process_attr(struct fsm_session *session, const char *attr,
                                  uint8_t type, uint16_t length, const void *value,
                                  struct fsm_dpi_plugin_client_pkt_info *pkt_info);

/**
 * @brief Helper function fetching the ADT specific context
 *
 * @param session the fsm session
 * @return the ADT private context
 */
struct fsm_dpi_adt_upnp_session *fsm_dpi_adt_upnp_get_session(struct fsm_session *session);

/* Internal functions specific to ADT client plugin */

/**
 * @brief Upon notification from DPI engine, store the <attribute, value>
 *        pair with all relevant informations for the current flow.
 *
 * @param session
 * @param attr
 * @param value
 * @param packet_info packet details (acc, net_parser)
 */
bool dpi_adt_upnp_store(struct fsm_session *session, const char *attr,
                        uint8_t type, uint16_t length, const void *value,
                        struct fsm_dpi_plugin_client_pkt_info *pkt_info);

bool dpi_adt_upnp_send_report(struct fsm_session *session);

void dpi_adt_upnp_free_aggr_store(struct fsm_dpi_adt_upnp_report_aggregator *aggr);

void dpi_adt_upnp_process_verdict(struct fsm_policy_req *policy_request,
                                  struct fsm_policy_reply *policy_reply);

// bool dpi_adt_upnp_store2proto(struct fsm_session *session,
//                               Interfaces__adt_upnp__adt_upnpReport *report_pb);

// bool dpi_adt_upnp_serialize_report(struct fsm_session *session,
//                                    struct fsm_dpi_adt_upnp_packed_buffer *report);

void fsm_dpi_adt_free_cache(ds_tree_t *cache);

void fsm_dpi_adt_upnp_send_probe(void);

int fsm_dpi_adt_upnp_process_notify(struct fsm_session *session, char *rootdevice_url,
                                    struct fsm_dpi_plugin_client_pkt_info *pkt_info);

struct fsm_dpi_adt_upnp_root_desc *fsm_dpi_adt_upnp_get_device(struct fsm_dpi_adt_upnp_session *session,
                                                   os_macaddr_t *device_mac,
                                                   char *rootdevice_url);


/* Expose helper functions for uni-tests */
void hexdump(const char* f, const void *b, size_t l);
void dump_upnp_cache(const char *function, ds_tree_t *cache);

#endif /* FSM_DPI_ADT_UPNP_H_INCLUDED */
