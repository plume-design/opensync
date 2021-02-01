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

#ifndef GATEKEEPER_MSG_H_INCLUDED
#define GATEKEEPER_MSG_H_INCLUDED

#include "network_metadata_report.h"
#include "os_types.h"

/**
 * @brief Container of protobuf serialization output
 *
 * Contains the information related to a serialized protobuf
 */
struct gk_packed_buffer
{
    size_t len; /*<! length of the serialized protobuf */
    void *buf;  /*<! dynamically allocated pointer to serialized data */
};


/**
 * @brief gatekeeper request header
 */
struct gk_req_header
{
    uint32_t req_id;
    os_macaddr_t *dev_id;
    char *node_id;
    char *location_id;
    char *policy_rule;
};


/**
 * @brief gatekeeper fqdn request
 */
struct gk_fqdn_request
{
    struct gk_req_header *header;
    char *fqdn;
};


/**
 * @brief gatekeeper ip threat request
 */
struct gk_ip_request
{
    struct gk_req_header *header;
    struct net_md_stats_accumulator *acc;
};


/**
 * @brief gatekeeper https sni request
 */
struct gk_sni_request
{
    struct gk_req_header *header;
    char *sni;
};


/**
 * @brief gatekeeper http host request
 */
struct gk_host_request
{
    struct gk_req_header *header;
    char *host;
};

/**
 * @brief gatekeeper http url request
 */
struct gk_url_request
{
    struct gk_req_header *header;
    char *url;
};


/**
 * @brief gatekeeper application request
 */
struct gk_app_request
{
    struct gk_req_header *header;
    char *appname;
};

/**
 * @brief gatekeeper ip flow request
 */
struct gk_ip_flow_request
{
    struct gk_req_header *header;
    struct net_md_stats_accumulator *acc;
};


/**
 * @brief union of specific requests
 */
union gk_data_req
{
    struct gk_fqdn_request gk_fqdn_req;
    struct gk_ip_request gk_ip_req;
    struct gk_sni_request gk_sni_req;
    struct gk_host_request gk_host_req;
    struct gk_url_request gk_url_req;
    struct gk_app_request gk_app_req;
    struct gk_ip_flow_request gk_ip_flow_req;
};


/**
 * @brief gatekeeper request
 */
struct gk_request
{
    int type;
    union gk_data_req req;
};


/**
 * @brief Generates a flow report serialized protobuf
 *
 * Uses the information pointed by the report parameter to generate
 * a serialized flow report buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see gk_free_packed_buffer() for this purpose.
 *
 * @param node info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct gk_packed_buffer *
gk_serialize_request(struct gk_request *request);


/**
 * @brief Frees a serialized protobuf container
 */
void
gk_free_packed_buffer(struct gk_packed_buffer *buffer);
#endif /* GATEKEEPER_MSG_H_INCLUDED */
