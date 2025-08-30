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
 * @brief check if current flow is equal to the requested flow
 *
 * @params: true is same, false if different
 */
static bool
gkc_is_flow_present(struct ip_flow_cache *cur_flow_entry,
                    struct gkc_ip_flow_interface *req)
{
    struct ip_flow_cache flow_entry;
    bool ret = false;
    int rc;

    flow_entry.ip_version  = req->ip_version;
    flow_entry.src_ip_addr = req->src_ip_addr;
    flow_entry.dst_ip_addr = req->dst_ip_addr;
    flow_entry.src_port    = req->src_port;
    flow_entry.dst_port    = req->dst_port;
    flow_entry.protocol    = req->protocol;
    flow_entry.direction   = req->direction;

    /* check if the 5-tuples are equal */
    rc = gkc_flow_entry_cmp(cur_flow_entry, &flow_entry);
    if (rc == 0)
    {
        /* found the flow entry */
        ret = true;
    }

    return ret;
}

/**
 * @brief free memory used by the flow entry
 *
 * @params: flow_tree: pointer to flow tree from which flow is to be
 *          freed
 */
void
gkc_free_flow_members(struct ip_flow_cache *flow_entry)
{
    FREE(flow_entry->gk_policy);
    FREE(flow_entry->dst_ip_addr);
    FREE(flow_entry->src_ip_addr);
    FREE(flow_entry->network_id);
}

/**
 * @brief delete the given flow from the flow tree
 *
 * @params: flow_tree: pointer to flow tree from which flow is to be
 *          removed
 * @params: req: interface structure specifying the flow request
 * @return true for success and false for failure.
 */
static bool
gkc_del_flow_from_tree(ds_tree_t *flow_tree, struct gkc_ip_flow_interface *req)
{
    struct ip_flow_cache *flow_entry, *remove;
    bool rc;

    /* loop through all flows in the tree */
    flow_entry = ds_tree_head(flow_tree);
    while (flow_entry != NULL)
    {
        remove = flow_entry;
        flow_entry = ds_tree_next(flow_tree, flow_entry);

        /* check if this is the flow that needs to be removed */
        rc = gkc_is_flow_present(remove, req);
        if (rc == false) continue;

        LOGT("%s(): deleting flow for device " PRI_os_macaddr_lower_t " ",
             __func__,
             FMT_os_macaddr_pt(req->device_mac));

        /* found the flow. Free memory used by the flow structure. */
        gkc_free_flow_members(remove);
        /* remove it from the tree */
        ds_tree_remove(flow_tree, remove);
        /* free the entry */
        FREE(remove);
        return true;
    }

    return false;
}

/**
 * @brief delete the given flow from the device cache
 *
 * @params: req: interface structure specifying the flow request
 * @params: pdevice: pointer to device from which flow is to be
 *          removed
 * @return true for success and false for failure.
 */
bool
gkc_del_flow_from_dev(struct per_device_cache *pdevice,
                      struct gkc_ip_flow_interface *req)
{
    bool ret;

    switch (req->direction)
    {
        case GKC_FLOW_DIRECTION_INBOUND:
            /* delete flow entry from inbound tree */
            ret = gkc_del_flow_from_tree(&pdevice->inbound_tree, req);
            break;

        case GKC_FLOW_DIRECTION_OUTBOUND:
            /* delete flow entry from outbound tree */
            ret = gkc_del_flow_from_tree(&pdevice->outbound_tree, req);
            break;

        default:
            ret = false;
            break;
    }

    return ret;
}

static bool
gkc_flow_ttl_expired(struct ip_flow_cache *flow_entry)
{
    time_t now;
    now = time(NULL);

    /* check if TTL is expired */
    if ((now - flow_entry->original_ts) < flow_entry->cache_ttl) return false;

    return true;
}

/**
 * @brief delete the given flow from the flow
 *        tree if TTL is expired
 *
 * @params: gk_del_info cache delete info structure
 */
void
gkc_cleanup_ttl_flow_tree(struct gkc_del_info_s *gk_del_info)
{
    struct ip_flow_cache *flow_entry, *remove;
    struct gk_cache_mgr *mgr;
    bool ttl_expired;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return;

    /* loop through all flows in the tree */
    flow_entry = ds_tree_head(gk_del_info->tree);
    while (flow_entry != NULL)
    {
        remove     = flow_entry;
        flow_entry = ds_tree_next(gk_del_info->tree, flow_entry);

        /* check if the TTL value is expired for this flow */
        ttl_expired = gkc_flow_ttl_expired(remove);
        if (ttl_expired == false) continue;

        LOGT("%s(): deleting flow for device " PRI_os_macaddr_lower_t
             " with expired TTL",
             __func__,
             FMT_os_macaddr_pt(gk_del_info->pdevice->device_mac));

        gk_del_info->flow_del_count++;

        /* decrement the cache count */
        mgr->total_entry_count--;

        /* found the flow. Free memory used by the flow structure. */
        gkc_free_flow_members(remove);
        /* remove it from the tree */
        ds_tree_remove(gk_del_info->tree, remove);
        /* free the entry */
        FREE(remove);
    }
}
