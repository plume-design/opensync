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

#ifndef GATEKEEPER_BULK_MSG_H_INCLUDED
#define GATEKEEPER_BULK_MSG_H_INCLUDED

#include "network_metadata_report.h"
#include "gatekeeper.pb-c.h"
#include "os_types.h"

/* Forward declarations to avoid circular dependencies */
struct gk_request;
struct gk_curl_data;
struct gk_bulk_request;
struct gk_connection_info;
struct gk_reply;

/* Entry type definitions for gk_device2app_repl */
enum gk_entry_type
{
    GK_ENTRY_TYPE_APP = 1,
    GK_ENTRY_TYPE_IPV4,
    GK_ENTRY_TYPE_IPV6,
    GK_ENTRY_TYPE_URL,
    GK_ENTRY_TYPE_FQDN,
    GK_ENTRY_TYPE_HOST,
    GK_ENTRY_TYPE_SNI,
    GK_ENTRY_TYPE_TRAFFIC_CLASS,
    /*Add new entry types here*/
    GK_ENTRY_TYPE_MAX,
};

struct gk_device2app_req
{
    struct gk_req_header *header;
    size_t n_apps; /* Number of apps in the array */
    char **apps;
};

struct gk_reply_header
{
    uint32_t request_id;
    char *dev_id;
    int action;
    uint32_t ttl;
    char *policy;
    uint32_t category_id;
    uint32_t confidence_level;
    uint32_t flow_marker;
    char *network_id;
};

struct gk_device2app_repl
{
    struct gk_reply_header *header;
    char *app_name;
    char *traffic_class;
    char *url;
    char *fqdn;
    char *http_host; /* For GK_ENTRY_TYPE_HOST */
    char *https_sni; /* For GK_ENTRY_TYPE_SNI */
    uint32_t ipv4_addr;
    struct fqdn_redirect_s *fqdn_redirect;
    struct
    {
        void *data; /* IPv6 address data */
        size_t len; /* IPv6 address length */
    } ipv6_addr;
    enum gk_entry_type type;
};

struct gk_bulk_reply
{
    size_t n_devices;
    struct gk_device2app_repl **devices;
};

union gk_data_reply
{
    struct gk_bulk_reply bulk_reply;
};

struct gk_reply
{
    int type;
    union gk_data_reply data_reply;
};

bool gk_parse_reply(struct gk_reply *reply, Gatekeeper__Southbound__V1__GatekeeperReply *pb_reply);
bool gk_parse_curl_response(struct gk_reply *reply, struct gk_curl_data *data);
void gk_clear_bulk_responses(struct gk_reply *reply);
void gk_clear_bulk_requests(struct gk_request *req);
bool gk_set_pb_bulk_request(Gatekeeper__Southbound__V1__GatekeeperReq *gk_req_pb, struct gk_bulk_request *request);
Gatekeeper__Southbound__V1__GatekeeperBulkReply *gk_cache_to_bulk_reply(void);
void gk_free_bulk_req(Gatekeeper__Southbound__V1__GatekeeperReq *pb);

/**
 * @brief Perform a bulk lookup request to gatekeeper
 *
 * This is a convenience wrapper that handles the complete request/response cycle:
 * - Serializes the request
 * - Sends it to gatekeeper via curl
 * - Parses the response
 *
 * @param conn_info Connection information (curl handle, server config)
 * @param req The request to send
 * @param reply The reply structure to populate
 * @return true on success, false on failure
 */
bool gk_perform_bulk_lookup(struct gk_connection_info *conn_info, struct gk_request *req, struct gk_reply *reply);

#endif /* GATEKEEPER_BULK_MSG_H_INCLUDED */
