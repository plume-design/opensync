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

#include "gatekeeper_cache.h"

/**
 * @brief Lookup the given flow in the flow tree.
 *
 * @params: tree: tree containing the flows
 * @params: req: interface structure specifying the attribute request
 *
 * @return true if found false if not found.
 */
static bool
gkc_lookup_flows_tree(ds_tree_t *tree, struct gkc_ip_flow_interface *req, int update_count)
{
    struct ip_flow_cache *target_entry;
    struct ip_flow_cache flow_entry;
    int ret = false;
    bool rc;

    flow_entry.ip_version  = req->ip_version;
    flow_entry.src_ip_addr = req->src_ip_addr;
    flow_entry.dst_ip_addr = req->dst_ip_addr;
    flow_entry.src_port    = req->src_port;
    flow_entry.dst_port    = req->dst_port;
    flow_entry.protocol    = req->protocol;
    flow_entry.direction   = req->direction;

    /* search if this flow entry is present in the tree */
    target_entry = ds_tree_find(tree, &flow_entry);
    if (target_entry)
    {
        /* entry found */
        ret = true;
        rc = target_entry->is_private_ip;
        if (update_count && !rc)
        {
            target_entry->hit_count.total++;
        }
        req->hit_counter = target_entry->hit_count.total;
        req->is_private_ip = target_entry->is_private_ip;
    }

    return ret;
}

/**
 * @brief Lookup the given flow in the device cache.
 *
 * @params: pdevice: tree having the device entries along with
 *         attributes and flows associated with it
 * @params: req: interface structure specifying the attribute request
 *
 * @return true if found false if not found.
 */
bool
gkc_lookup_flows_for_device(struct per_device_cache *pdevice,
                            struct gkc_ip_flow_interface *req,
                            bool update_count)
{
    int ret;

    ret = gkc_is_flow_valid(req);
    if (ret == false) return false;

    switch (req->direction)
    {
    case GKC_FLOW_DIRECTION_INBOUND:
        /* search for the given tuple in in inbound tree */
        ret = gkc_lookup_flows_tree(&pdevice->inbound_tree, req, update_count);
        break;

    case GKC_FLOW_DIRECTION_OUTBOUND:
        /* search for the given tuple in in outbound tree */
        ret = gkc_lookup_flows_tree(&pdevice->outbound_tree, req, update_count);
        break;

    default:
        ret = false;
        break;
    }

    return ret;
}
