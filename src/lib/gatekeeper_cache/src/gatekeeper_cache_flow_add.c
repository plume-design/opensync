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

#include "memutil.h"

#include "gatekeeper_cache.h"
#include "memutil.h"

/**
 * @brief create a new flow entry initializing with 5-tuple values
 *        from input.
 * @params: req: interface structure specifying the attribute request
 *
 * @return ip_flow_cache pointer on success or NULL on failure
 */
struct ip_flow_cache *
gkc_new_flow_entry(struct gkc_ip_flow_interface *req)
{
    struct ip_flow_cache *flow_entry;
    size_t ip_len;

    if (req->src_ip_addr == NULL || req->dst_ip_addr == NULL) return NULL;

    flow_entry = CALLOC(1, sizeof(*flow_entry));
    if (flow_entry == NULL) return NULL;

    /* set src ip address */
    flow_entry->ip_version = req->ip_version;

    ip_len = (req->ip_version == 4 ? 4 : 16);

    flow_entry->src_ip_addr = CALLOC(1, ip_len);
    if (flow_entry->src_ip_addr == NULL) goto err_free_fentry;

    memcpy(flow_entry->src_ip_addr, req->src_ip_addr, ip_len);

    /* set dst ip address */
    flow_entry->dst_ip_addr = CALLOC(1, ip_len);
    if (flow_entry->dst_ip_addr == NULL) goto err_free_sip;

    memcpy(flow_entry->dst_ip_addr, req->dst_ip_addr, ip_len);

    flow_entry->original_ts  = time(NULL);
    flow_entry->cache_ts  = time(NULL);
    flow_entry->cache_ttl = req->cache_ttl;
    flow_entry->src_port  = req->src_port;
    flow_entry->dst_port  = req->dst_port;
    flow_entry->protocol  = req->protocol;
    flow_entry->cache_ttl = req->cache_ttl;
    flow_entry->action    = req->action;
    flow_entry->is_private_ip = req->is_private_ip;

    if (req->gk_policy)
    {
        flow_entry->gk_policy = STRDUP(req->gk_policy);
    }

    if (req->network_id)
    {
        flow_entry->network_id = STRDUP(req->network_id);
    }

    flow_entry->hit_count.total = 1;  /* We count the insertion as a hit */

    return flow_entry;

err_free_sip:
    FREE(flow_entry->src_ip_addr);

err_free_fentry:
    FREE(flow_entry);

    return NULL;
}

/**
 * @brief validate the request input.
 *
 * @params: req: interface structure specifying the attribute request
 * @return true for success and false for failure.
 */
bool
gkc_is_flow_valid(struct gkc_ip_flow_interface *req)
{
    if (!req || !req->device_mac) return false;

    if (req->direction != GKC_FLOW_DIRECTION_INBOUND
        && req->direction != GKC_FLOW_DIRECTION_OUTBOUND)
        return false;

    if (req->ip_version != 4 && req->ip_version != 6) return false;

    if (req->src_ip_addr == NULL) return false;
    if (req->dst_ip_addr == NULL) return false;

    return true;
}

/**
 * @brief add the given attribute to the device cache.
 *
 * @params: req: interface structure specifying the attribute request
 * @params: pdevice pointer to device tree to which the
 *          attribute is added
 * @return true for success and false for failure.
 */
bool
gkc_add_flow_tree(struct per_device_cache *pdevice,
                  struct gkc_ip_flow_interface *req)
{
    struct ip_flow_cache *flow_entry;
    struct gk_cache_mgr *mgr;
    int ret;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return false;

    ret = gkc_is_flow_valid(req);
    if (ret == false) return false;

    flow_entry = gkc_new_flow_entry(req);
    if (flow_entry == NULL) return false;

    if (req->direction == GKC_FLOW_DIRECTION_INBOUND)
    {
        ds_tree_insert(&pdevice->inbound_tree, flow_entry, flow_entry);
    }
    else if (req->direction == GKC_FLOW_DIRECTION_OUTBOUND)
    {
        ds_tree_insert(&pdevice->outbound_tree, flow_entry, flow_entry);
    }

    return true;
}
