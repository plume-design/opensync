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

#include <arpa/inet.h>
#include <stdint.h>
#include <stdlib.h>

#include "fsm_policy.h"
#include "gatekeeper_msg.h"
#include "gatekeeper.pb-c.h"
#include "log.h"
#include "network_metadata_report.h"
#include "os_types.h"

/**
 * @brief frees a protobuf request header
 *
 * @param header the header to free
 * @return none
 */
static void
gk_free_req_header(Gatekeeper__Southbound__V1__GatekeeperCommonRequest *header)
{
    if (header == NULL) return;

    free(header);

    return;
}


/**
 * @brief frees a protobuf fqdn request
 *
 * @param pb the fqdn request container to free
 * @return none
 */
static void
gk_free_fqdn_req(Gatekeeper__Southbound__V1__GatekeeperReq *pb)
{
    Gatekeeper__Southbound__V1__GatekeeperFqdnReq *fqdn_req;

    fqdn_req = pb->req_fqdn;
    if (fqdn_req == NULL) return;

    gk_free_req_header(fqdn_req->header);
    fqdn_req->header = NULL;
    free(fqdn_req);

    pb->req_fqdn = NULL;

    return;
}


/**
 * @brief frees a protobuf https sni request
 *
 * @param pb the https sni request container to free
 * @return none
 */
static void
gk_free_https_sni_req(Gatekeeper__Southbound__V1__GatekeeperReq *pb)
{
    Gatekeeper__Southbound__V1__GatekeeperHttpsSniReq *sni_req;

    sni_req = pb->req_https_sni;
    if (sni_req == NULL) return;

    gk_free_req_header(sni_req->header);
    sni_req->header = NULL;
    free(sni_req);

    pb->req_https_sni = NULL;

    return;
}


/**
 * @brief frees a protobuf http host request
 *
 * @param pb the http host request container to free
 * @return none
 */
static void
gk_free_http_host_req(Gatekeeper__Southbound__V1__GatekeeperReq *pb)
{
    Gatekeeper__Southbound__V1__GatekeeperHttpHostReq *host_req;

    host_req = pb->req_http_host;
    if (host_req == NULL) return;

    gk_free_req_header(host_req->header);
    host_req->header = NULL;
    free(host_req);

    pb->req_http_host = NULL;

    return;
}


/**
 * @brief frees a protobuf http url request
 *
 * @param pb the http url request container to free
 * @return none
 */
static void
gk_free_http_url_req(Gatekeeper__Southbound__V1__GatekeeperReq *pb)
{
    Gatekeeper__Southbound__V1__GatekeeperHttpUrlReq *url_req;

    url_req = pb->req_http_url;
    if (url_req == NULL) return;

    gk_free_req_header(url_req->header);
    url_req->header = NULL;
    free(url_req);

    pb->req_http_url = NULL;

    return;
}


/**
 * @brief frees a protobuf app name request
 *
 * @param pb the app name request container to free
 * @return none
 */
static void
gk_free_app_req(Gatekeeper__Southbound__V1__GatekeeperReq *pb)
{
    Gatekeeper__Southbound__V1__GatekeeperAppReq *app_req;

    app_req = pb->req_app;
    if (app_req == NULL) return;

    gk_free_req_header(app_req->header);
    app_req->header = NULL;
    free(app_req);

    pb->req_app = NULL;

    return;
}


/**
 * @brief frees a protobuf ipv4 request
 *
 * @param pb the ipv4 request to free
 * @return none
 */
static void
gk_free_ipv4_req(Gatekeeper__Southbound__V1__GatekeeperReq *pb)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv4Req *ipv4_req;

    ipv4_req = pb->req_ipv4;
    if (ipv4_req == NULL) return;

    gk_free_req_header(ipv4_req->header);
    ipv4_req->header = NULL;
    free(ipv4_req->flow_ipv4);
    ipv4_req->flow_ipv4 = NULL;

    free(ipv4_req);

    pb->req_ipv4 = NULL;

    return;
}


/**
 * @brief frees a protobuf ipv6 request
 *
 * @param pb the ipv6 request to free
 * @return none
 */
static void
gk_free_ipv6_req(Gatekeeper__Southbound__V1__GatekeeperReq *pb)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv6Req *ipv6_req;

    ipv6_req = pb->req_ipv6;
    if (ipv6_req == NULL) return;

    gk_free_req_header(ipv6_req->header);
    ipv6_req->header = NULL;
    free(ipv6_req->flow_ipv6);
    ipv6_req->flow_ipv6 = NULL;

    free(ipv6_req);

    pb->req_ipv6 = NULL;

    return;
}


static void
gk_free_ipv4_tuple_req(Gatekeeper__Southbound__V1__GatekeeperReq *pb)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv4TupleReq *tuple_req;

    tuple_req = pb->req_ipv4_tuple;
    if (tuple_req == NULL) return;

    gk_free_req_header(tuple_req->header);
    tuple_req->header = NULL;
    free(tuple_req->flow_ipv4);
    tuple_req->flow_ipv4 = NULL;

    free(tuple_req);

    pb->req_ipv4_tuple = NULL;

    return;
}


static void
gk_free_ipv6_tuple_req(Gatekeeper__Southbound__V1__GatekeeperReq *pb)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv6TupleReq *tuple_req;

    tuple_req = pb->req_ipv6_tuple;
    if (tuple_req == NULL) return;

    gk_free_req_header(tuple_req->header);
    tuple_req->header = NULL;
    free(tuple_req->flow_ipv6);
    tuple_req->flow_ipv6 = NULL;

    free(tuple_req);

    pb->req_ipv6_tuple = NULL;
    return;
}


static void
gk_free_pb_request(Gatekeeper__Southbound__V1__GatekeeperReq *pb)
{
    gk_free_fqdn_req(pb);
    gk_free_https_sni_req(pb);
    gk_free_http_host_req(pb);
    gk_free_http_url_req(pb);
    gk_free_app_req(pb);
    gk_free_ipv4_req(pb);
    gk_free_ipv6_req(pb);
    gk_free_ipv4_tuple_req(pb);
    gk_free_ipv6_tuple_req(pb);
    free(pb);
}


void
gk_free_packed_buffer(struct gk_packed_buffer *buffer)
{
    if (buffer == NULL) return;

    free(buffer->buf);
    free(buffer);
}


static Gatekeeper__Southbound__V1__GatekeeperCommonRequest *
gk_set_pb_common_req(struct gk_req_header *header)
{
    Gatekeeper__Southbound__V1__GatekeeperCommonRequest *pb;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_common_request__init(pb);

    pb->request_id = header->req_id;
    pb->device_id.len = sizeof(header->dev_id->addr);
    pb->device_id.data = header->dev_id->addr;
    pb->node_id = header->node_id;
    pb->location_id = header->location_id;
    pb->policy_rule = header->policy_rule;

    return pb;
}


/**
 * @brief fills up a gatekeeper protobuf fqdn request
 *
 * @param request the external representation of a gatekeeper request
 * @return a filled up protobuf fqdn request
 */
static Gatekeeper__Southbound__V1__GatekeeperFqdnReq *
gk_set_pb_fqdn_req(struct gk_fqdn_request *fqdn_req)
{
    Gatekeeper__Southbound__V1__GatekeeperFqdnReq *pb;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_fqdn_req__init(pb);

    pb->header = gk_set_pb_common_req(fqdn_req->header);
    if (pb->header == NULL) return NULL;

    pb->fqdn = fqdn_req->fqdn;

    return pb;
}


/**
 * @brief fills up a gatekeeper protobuf sni request
 *
 * @param request the external representation of a gatekeeper sni request
 * @return a filled up protobuf https sni request
 */
static Gatekeeper__Southbound__V1__GatekeeperHttpsSniReq *
gk_set_pb_sni_req(struct gk_sni_request *sni_req)
{
    Gatekeeper__Southbound__V1__GatekeeperHttpsSniReq *pb;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_https_sni_req__init(pb);

    pb->header = gk_set_pb_common_req(sni_req->header);
    if (pb->header == NULL) return NULL;

    pb->https_sni = sni_req->sni;

    return pb;
}


/**
 * @brief fills up a gatekeeper protobuf host request
 *
 * @param request the external representation of a gatekeeper host request
 * @return a filled up protobuf http host request
 */
static Gatekeeper__Southbound__V1__GatekeeperHttpHostReq *
gk_set_pb_host_req(struct gk_host_request *host_req)
{
    Gatekeeper__Southbound__V1__GatekeeperHttpHostReq *pb;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_http_host_req__init(pb);

    pb->header = gk_set_pb_common_req(host_req->header);
    if (pb->header == NULL) return NULL;

    pb->http_host = host_req->host;

    return pb;
}


/**
 * @brief fills up a gatekeeper protobuf host request
 *
 * @param request the external representation of a gatekeeper url request
 * @return a filled up protobuf http url request
 */
static Gatekeeper__Southbound__V1__GatekeeperHttpUrlReq *
gk_set_pb_url_req(struct gk_url_request *url_req)
{
    Gatekeeper__Southbound__V1__GatekeeperHttpUrlReq *pb;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_http_url_req__init(pb);

    pb->header = gk_set_pb_common_req(url_req->header);
    if (pb->header == NULL) return NULL;

    pb->http_url = url_req->url;

    return pb;
}


/**
 * @brief fills up a gatekeeper protobuf app request
 *
 * @param request the external representation of a gatekeeper app request
 * @return a filled up protobuf app request
 */
static Gatekeeper__Southbound__V1__GatekeeperAppReq *
gk_set_pb_app_req(struct gk_app_request *app_req)
{
    Gatekeeper__Southbound__V1__GatekeeperAppReq *pb;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_app_req__init(pb);

    pb->header = gk_set_pb_common_req(app_req->header);
    if (pb->header == NULL) return NULL;

    pb->app_name = app_req->appname;

    return pb;
}


/**
 * @brief retrieves the ip address to check
 */
static uint8_t *
gk_get_ip_to_check(struct gk_ip_request *ip_req)
{
    struct net_md_stats_accumulator *acc;
    struct net_md_flow_key *key;

    if (ip_req == NULL) return NULL;

    acc = ip_req->acc;
    if (acc == NULL) return NULL;

    key = acc->key;
    if (key == NULL) return NULL;

    if (acc->direction == NET_MD_ACC_OUTBOUND_DIR)
    {
        if (acc->originator == NET_MD_ACC_ORIGINATOR_SRC) return key->dst_ip;
        else if (acc->originator == NET_MD_ACC_ORIGINATOR_DST) return key->src_ip;
    }
    else if (acc->direction == NET_MD_ACC_INBOUND_DIR)
    {
        if (acc->originator == NET_MD_ACC_ORIGINATOR_SRC) return key->src_ip;
        else if (acc->originator == NET_MD_ACC_ORIGINATOR_DST) return key->dst_ip;
    }

    return NULL;
}


/**
 * @brief fills up an ipv4 flow protobuf
 *
 * @param ip_req the external representation of an gatekeeper ipv4 request
 * @return a filled up ipv4 flow protobuf
 */
static Gatekeeper__Southbound__V1__GatekeeperIpv4FlowTuple *
gk_set_pb_ipv4_flow(struct net_md_stats_accumulator *acc)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv4FlowTuple *pb;
    struct net_md_flow_key *key;
    uint32_t ipv4;
    uint8_t *ip;

    if (acc == NULL) return NULL;

    key = acc->key;
    if (key == NULL) return NULL;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_ipv4_flow_tuple__init(pb);

    pb->transport = key->ipprotocol;
    if (acc->direction == NET_MD_ACC_OUTBOUND_DIR)
    {
        if (acc->originator == NET_MD_ACC_ORIGINATOR_SRC)
        {
            /* prepare source ip address */
            ip = key->src_ip;
            ipv4 = *(uint32_t *)ip;
            pb->source_ipv4 = htonl(ipv4);

            /* prepare destination ip address */
            ip = key->dst_ip;
            ipv4 = *(uint32_t *)ip;
            pb->destination_ipv4 = htonl(ipv4);

            /* prepare source port */
            pb->source_port = key->sport;

            /* prepare destination port */
            pb->destination_port = key->dport;
        }
        else if (acc->originator == NET_MD_ACC_ORIGINATOR_DST)
        {
            /* prepare source ip address */
            ip = key->src_ip;
            ipv4 = *(uint32_t *)ip;
            pb->destination_ipv4 = htonl(ipv4);

            /* prepare destination ip address */
            ip = key->dst_ip;
            ipv4 = *(uint32_t *)ip;
            pb->source_ipv4 = htonl(ipv4);

            /* prepare source port */
            pb->source_port = key->dport;

            /* prepare destination port */
            pb->destination_port = key->sport;
        }
    }
    else if (acc->direction == NET_MD_ACC_INBOUND_DIR)
    {
        if (acc->originator == NET_MD_ACC_ORIGINATOR_SRC)
        {
            /* prepare source ip address */
            ip = key->src_ip;
            ipv4 = *(uint32_t *)ip;
            pb->source_ipv4 = htonl(ipv4);

            /* prepare destination ip address */
            ip = key->dst_ip;
            ipv4 = *(uint32_t *)ip;
            pb->destination_ipv4 = htonl(ipv4);

            /* prepare source port */
            pb->source_port = key->sport;

            /* prepare destination port */
            pb->destination_port = key->dport;
        }
        else if (acc->originator == NET_MD_ACC_ORIGINATOR_DST)
        {
            /* prepare source ip address */
            ip = key->src_ip;
            ipv4 = *(uint32_t *)ip;
            pb->destination_ipv4 = htonl(ipv4);

            /* prepare destination ip address */
            ip = key->dst_ip;
            ipv4 = *(uint32_t *)ip;
            pb->source_ipv4 = htonl(ipv4);

            /* prepare source port */
            pb->source_port = key->dport;

            /* prepare destination port */
            pb->destination_port = key->sport;
        }
    }

    return pb;
}


/**
 * @brief fills up an ipv6 flow protobuf
 *
 * @param ip_req the external representation of an gatekeeper ipv4 request
 * @return a filled up ipv6 flow protobuf
 */
static Gatekeeper__Southbound__V1__GatekeeperIpv6FlowTuple *
gk_set_pb_ipv6_flow(struct net_md_stats_accumulator *acc)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv6FlowTuple *pb;
    struct net_md_flow_key *key;
    uint8_t *ip;

    if (acc == NULL) return NULL;

    key = acc->key;
    if (key == NULL) return NULL;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_ipv6_flow_tuple__init(pb);

    pb->transport = key->ipprotocol;
    if (acc->direction == NET_MD_ACC_OUTBOUND_DIR)
    {
        if (acc->originator == NET_MD_ACC_ORIGINATOR_SRC)
        {
            /* prepare source ip address */
            ip = key->src_ip;
            pb->source_ipv6.data = ip;
            pb->source_ipv6.len = 16;

            /* prepare destination ip address */
            ip = key->dst_ip;
            pb->destination_ipv6.data = ip;
            pb->destination_ipv6.len = 16;

            /* prepare source port */
            pb->source_port = key->sport;

            /* prepare destination port */
            pb->destination_port = key->dport;
        }
        else if (acc->originator == NET_MD_ACC_ORIGINATOR_DST)
        {
            /* prepare source ip address */
            ip = key->src_ip;
            pb->destination_ipv6.data = ip;
            pb->destination_ipv6.len = 16;

            /* prepare destination ip address */
            ip = key->dst_ip;
            pb->source_ipv6.data = ip;
            pb->source_ipv6.len = 16;

            /* prepare source port */
            pb->destination_port = key->sport;

            /* prepare destination port */
            pb->source_port = key->dport;
        }
    }
    else if (acc->direction == NET_MD_ACC_INBOUND_DIR)
    {
        if (acc->originator == NET_MD_ACC_ORIGINATOR_SRC)
        {
            /* prepare source ip address */
            ip = key->src_ip;
            pb->source_ipv6.data = ip;
            pb->source_ipv6.len = 16;

            /* prepare destination ip address */
            ip = key->dst_ip;
            pb->destination_ipv6.data = ip;
            pb->destination_ipv6.len = 16;

            /* prepare source port */
            pb->source_port = key->sport;

            /* prepare destination port */
            pb->destination_port = key->dport;
        }
        else if (acc->originator == NET_MD_ACC_ORIGINATOR_DST)
        {
            /* prepare source ip address */
            ip = key->src_ip;
            pb->destination_ipv6.data = ip;
            pb->destination_ipv6.len = 16;

            /* prepare destination ip address */
            ip = key->dst_ip;
            pb->source_ipv6.data = ip;
            pb->source_ipv6.len = 16;

            /* prepare source port */
            pb->destination_port = key->sport;

            /* prepare destination port */
            pb->source_port = key->dport;
        }
    }

    return pb;
}


/**
 * @brief fills up a gatekeeper protobuf ipv6 request
 *
 * @param ip_req the external representation of an gatekeeper ipv6 request
 * @return a filled up protobuf ipv4 request
 */
static Gatekeeper__Southbound__V1__GatekeeperIpv4Req *
gk_set_pb_ipv4_req(struct gk_ip_request *ip_req)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv4FlowTuple *flow_pb;
    Gatekeeper__Southbound__V1__GatekeeperIpv4Req *pb;
    struct net_md_stats_accumulator *acc;
    uint32_t ipv4;
    uint8_t *ip;

    if (ip_req == NULL) return NULL;

    acc = ip_req->acc;
    if (acc == NULL) return NULL;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_ipv4_req__init(pb);

    /* Add request header */
    pb->header = gk_set_pb_common_req(ip_req->header);
    if (pb->header == NULL) goto out_err;

    /* Add ipv4 address to check */
    ip = gk_get_ip_to_check(ip_req);
    if (ip == NULL) goto out_err;

    ipv4 = *(uint32_t *)ip;
    pb->addr_ipv4 = htonl(ipv4);

    /* Add flow information */
    flow_pb = gk_set_pb_ipv4_flow(ip_req->acc);
    if (flow_pb == NULL) goto out_err;

    pb->flow_ipv4 = flow_pb;

    pb->flow_direction = acc->direction;
    pb->flow_originator = acc->originator;

    return pb;

out_err:
    free(pb->flow_ipv4);
    gk_free_req_header(pb->header);
    free(pb);
    return NULL;
}


/**
 * @brief fills up a gatekeeper protobuf ipv4 request
 *
 * @param ip_req the external representation of an gatekeeper ipv6 request
 * @return a filled up protobuf ipv6 request
 */
static Gatekeeper__Southbound__V1__GatekeeperIpv6Req *
gk_set_pb_ipv6_req(struct gk_ip_request *ip_req)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv6FlowTuple *flow_pb;
    Gatekeeper__Southbound__V1__GatekeeperIpv6Req *pb;
    struct net_md_stats_accumulator *acc;
    uint8_t *ip;

    if (ip_req == NULL) return NULL;

    acc = ip_req->acc;
    if (acc == NULL) return NULL;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_ipv6_req__init(pb);

    pb->header = gk_set_pb_common_req(ip_req->header);
    if (pb->header == NULL) goto out_err;

    ip = gk_get_ip_to_check(ip_req);
    if (ip == NULL) goto out_err;

    pb->addr_ipv6.data = ip;
    pb->addr_ipv6.len = 16;

    /* Add flow information */
    flow_pb = gk_set_pb_ipv6_flow(ip_req->acc);
    if (flow_pb == NULL) goto out_err;

    pb->flow_ipv6 = flow_pb;

    pb->flow_direction = acc->direction;
    pb->flow_originator = acc->originator;

    return pb;

out_err:
    free(pb->flow_ipv6);
    gk_free_req_header(pb->header);
    free(pb);
    return NULL;
}

/**
 * @brief fills up a gatekeeper protobuf ipv4 or ipv6 request
 *
 * @param ip_req the external representation of an gatekeeper ipv4/ipv6 request
 * @return a filled up protobuf ipv4/ipv6 request
 */
static bool
gk_set_pb_ip_req(Gatekeeper__Southbound__V1__GatekeeperReq *gk_req_pb,
                 struct gk_ip_request *gk_ip_req)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv6Req *ipv6_pb;
    Gatekeeper__Southbound__V1__GatekeeperIpv4Req *ipv4_pb;
    struct net_md_stats_accumulator *acc;
    struct net_md_flow_key *key;
    bool rc;

    if (gk_req_pb == NULL) return false;
    if (gk_ip_req == NULL) return false;

    acc = gk_ip_req->acc;
    if (acc == NULL) return false;

    key = acc->key;
    if (key == NULL) return false;

    rc = (key->ip_version == 4);
    if (!rc) rc = (key->ip_version == 6);
    if (!rc) return false;

    if (key->ip_version == 4)
    {
        ipv4_pb = gk_set_pb_ipv4_req(gk_ip_req);
        if (ipv4_pb == NULL) return false;

        gk_req_pb->req_ipv4 = ipv4_pb;
    }
    else
    {
        ipv6_pb = gk_set_pb_ipv6_req(gk_ip_req);
        if (ipv6_pb == NULL) return false;

        gk_req_pb->req_ipv6 = ipv6_pb;
    }

    return true;
}


static Gatekeeper__Southbound__V1__GatekeeperIpv4TupleReq *
gk_set_pb_ipv4_flow_req(struct gk_ip_flow_request *gk_ip_flow_req)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv4FlowTuple *flow_pb;
    Gatekeeper__Southbound__V1__GatekeeperIpv4TupleReq *pb;
    struct net_md_stats_accumulator *acc;

    if (gk_ip_flow_req == NULL) return NULL;

    acc = gk_ip_flow_req->acc;
    if (acc == NULL) return NULL;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_ipv4_tuple_req__init(pb);

    pb->header = gk_set_pb_common_req(gk_ip_flow_req->header);
    if (pb->header == NULL) goto out_err;

    /* Add flow information */
    flow_pb = gk_set_pb_ipv4_flow(gk_ip_flow_req->acc);
    if (flow_pb == NULL) goto out_err;

    pb->flow_ipv4 = flow_pb;

    pb->flow_direction = acc->direction;
    pb->flow_originator = acc->originator;

    return pb;

out_err:
    free(pb->flow_ipv4);
    gk_free_req_header(pb->header);
    free(pb);
    return NULL;
}


static Gatekeeper__Southbound__V1__GatekeeperIpv6TupleReq *
gk_set_pb_ipv6_flow_req(struct gk_ip_flow_request *gk_ip_flow_req)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv6FlowTuple *flow_pb;
    Gatekeeper__Southbound__V1__GatekeeperIpv6TupleReq *pb;
    struct net_md_stats_accumulator *acc;

    if (gk_ip_flow_req == NULL) return NULL;

    acc = gk_ip_flow_req->acc;
    if (acc == NULL) return NULL;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_ipv6_tuple_req__init(pb);

    pb->header = gk_set_pb_common_req(gk_ip_flow_req->header);
    if (pb->header == NULL) goto out_err;

    /* Add flow information */
    flow_pb = gk_set_pb_ipv6_flow(gk_ip_flow_req->acc);
    if (flow_pb == NULL) goto out_err;

    pb->flow_ipv6 = flow_pb;

    pb->flow_direction = acc->direction;
    pb->flow_originator = acc->originator;

    return pb;

out_err:
    free(pb->flow_ipv6);
    gk_free_req_header(pb->header);
    free(pb);
    return NULL;
}


static bool
gk_set_pb_ip_flow_req(Gatekeeper__Southbound__V1__GatekeeperReq *gk_req_pb,
                      struct gk_ip_flow_request *gk_ip_flow_req)
{
    Gatekeeper__Southbound__V1__GatekeeperIpv6TupleReq *ipv6f_pb;
    Gatekeeper__Southbound__V1__GatekeeperIpv4TupleReq *ipv4f_pb;
    struct net_md_stats_accumulator *acc;
    struct net_md_flow_key *key;
    bool rc;

    if (gk_req_pb == NULL) return false;
    if (gk_ip_flow_req == NULL) return false;

    acc = gk_ip_flow_req->acc;
    if (acc == NULL) return false;

    key = acc->key;
    if (key == NULL) return false;

    rc = (key->ip_version == 4);
    if (!rc) rc = (key->ip_version == 6);
    if (!rc) return false;

    if (key->ip_version == 4)
    {
        ipv4f_pb = gk_set_pb_ipv4_flow_req(gk_ip_flow_req);
        if (ipv4f_pb == NULL) return false;

        gk_req_pb->req_ipv4_tuple = ipv4f_pb;
    }
    else
    {
        ipv6f_pb = gk_set_pb_ipv6_flow_req(gk_ip_flow_req);
        if (ipv6f_pb == NULL) return false;

        gk_req_pb->req_ipv6_tuple = ipv6f_pb;
    }

    return true;
}


/**
 * @brief fills up a gatekeeper protobuf request
 *
 * @param request the external representation of a gatekeeprer request
 * @return a filled up protobuf request
 */
static Gatekeeper__Southbound__V1__GatekeeperReq *
gk_set_pb_request(struct gk_request *request)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *pb;
    struct gk_ip_flow_request *gk_ip_flow_req;
    struct gk_fqdn_request *gk_fqdn_req;
    struct gk_host_request *gk_host_req;
    struct gk_sni_request *gk_sni_req;
    struct gk_url_request *gk_url_req;
    struct gk_app_request *gk_app_req;
    struct gk_ip_request *gk_ip_req;
    union gk_data_req *req_data;
    bool rc;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_req__init(pb);

    req_data = &request->req;
    switch (request->type)
    {
        case FSM_FQDN_REQ:
            gk_fqdn_req = &req_data->gk_fqdn_req;
            pb->req_fqdn = gk_set_pb_fqdn_req(gk_fqdn_req);
            if (pb->req_fqdn == NULL) goto out_err;
            break;

        case FSM_SNI_REQ:
            gk_sni_req = &req_data->gk_sni_req;
            pb->req_https_sni = gk_set_pb_sni_req(gk_sni_req);
            if (pb->req_https_sni == NULL) goto out_err;
            break;

        case FSM_HOST_REQ:
            gk_host_req = &req_data->gk_host_req;
            pb->req_http_host = gk_set_pb_host_req(gk_host_req);
            if (pb->req_http_host == NULL) goto out_err;
            break;

        case FSM_URL_REQ:
            gk_url_req = &req_data->gk_url_req;
            pb->req_http_url = gk_set_pb_url_req(gk_url_req);
            if (pb->req_http_url == NULL) goto out_err;
            break;

        case FSM_APP_REQ:
            gk_app_req = &req_data->gk_app_req;
            pb->req_app = gk_set_pb_app_req(gk_app_req);
            if (pb->req_app == NULL) goto out_err;
            break;

        case FSM_IPV4_REQ:
        case FSM_IPV6_REQ:
            gk_ip_req = &req_data->gk_ip_req;
            rc = gk_set_pb_ip_req(pb, gk_ip_req);
            if (!rc) goto out_err;
            break;

        case FSM_IPV4_FLOW_REQ:
        case FSM_IPV6_FLOW_REQ:
            gk_ip_flow_req = &req_data->gk_ip_flow_req;
            rc = gk_set_pb_ip_flow_req(pb, gk_ip_flow_req);
            if (!rc) goto out_err;
            break;
    }

    return pb;

out_err:
    gk_free_pb_request(pb);
    return NULL;
}


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
gk_serialize_request(struct gk_request *request)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *pb = NULL;
    struct gk_packed_buffer *serialized = NULL;
    void *buf = NULL;
    size_t len;

    if (request == NULL) return NULL;

    /* Allocate serialization output structure */
    serialized = calloc(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    /* Allocate and set flow report protobuf */
    pb = gk_set_pb_request(request);
    if (pb == NULL) goto out_err;

    /* Get serialization length */
    len = gatekeeper__southbound__v1__gatekeeper_req__get_packed_size(pb);
    if (len == 0) goto out_err;

    /* Allocate space for the serialized buffer */
    buf = malloc(len);
    if (buf == NULL) goto out_err;

    serialized->len = gatekeeper__southbound__v1__gatekeeper_req__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    gk_free_pb_request(pb);

    return serialized;

out_err:
    gk_free_packed_buffer(serialized);
    return NULL;
}
