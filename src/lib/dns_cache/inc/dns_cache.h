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

#ifndef DNS_CACHE_H_INCLUDED
#define DNS_CACHE_H_INCLUDED

#include <stdint.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "ds_tree.h"
#include "os.h"
#include "os_types.h"

/******************************************************************************
* Struct Declarations
*******************************************************************************/

#define SERVICE_PROVIDER_MAX_ELEMS 3

struct dns_cache_mgr
{
    bool        initialized;
    uint8_t     refcount;
    uint32_t    cache_hit_count[SERVICE_PROVIDER_MAX_ELEMS];
    ds_tree_t   ip2a_tree;
};

#define URL_REPORT_MAX_ELEMS 8

enum {
    IP2ACTION_BC_SVC,
    IP2ACTION_WP_SVC,
    IP2ACTION_GK_SVC,
};

struct ip2action_bc_info
{
    uint8_t confidence_levels[URL_REPORT_MAX_ELEMS];
    uint8_t reputation;
};

struct ip2action_wb_info
{
    uint8_t risk_level;
};

struct ip2action_gk_info
{
    uint32_t confidence_level;
    uint32_t category_id;
    char *gk_policy;
};

struct ip2action
{
    os_macaddr_t                *device_mac;
    struct sockaddr_storage     *ip_addr;
    int                         action;
    int                         cache_ttl;
    time_t                      cache_ts;
    int                         af_family;
    uint8_t                     *ip_tbl;
    uint8_t                     policy_idx;
    int                         service_id;
    uint8_t                     nelems;
    bool                        redirect_flag;
    uint8_t                     categories[URL_REPORT_MAX_ELEMS];
    bool                        cat_unknown_to_service;
    union
    {
        struct ip2action_bc_info bc_info;
        struct ip2action_wb_info wb_info;
        struct ip2action_gk_info gk_info;
    } cache_info;
#define cache_bc cache_info.bc_info
#define cache_wb cache_info.wb_info
#define cache_gk cache_info.gk_info
    ds_tree_node_t              ip2a_tnode;
};

struct ip2action_req
{
   os_macaddr_t             *device_mac;
   struct sockaddr_storage  *ip_addr;

   int                      cache_ttl;
   int                      action;
   uint8_t                  policy_idx;
   int                      service_id;
   uint8_t                  nelems;
   bool                     redirect_flag;
   uint8_t                  categories[URL_REPORT_MAX_ELEMS];
   bool                     cat_unknown_to_service;
   union
   {
        struct ip2action_bc_info bc_info;
        struct ip2action_wb_info wb_info;
        struct ip2action_gk_info gk_info;
   } cache_info;
#define cache_bc cache_info.bc_info
#define cache_wb cache_info.wb_info
#define cache_gk cache_info.gk_info
};

struct dns_cache_mgr *
dns_cache_get_mgr(void);

/**
 * @brief initialize dns_cache handle manager.
 */
void
dns_cache_init_mgr(struct dns_cache_mgr *mgr);

/**
 * @brief initialize dns_cache.
 *
 * receive: none
 *
 * @return true for success and false for failure.
 */
bool
dns_cache_init(void);

void
dns_cache_cleanup_mgr(void);

/**
 * @brief cleanup allocated memory.
 *
 * receive none
 *
 * @return void.
 */
void
dns_cache_cleanup(void);

/**
 * @brief Lookup cached action for given ip address and mac.
 *
 * receive ipaddress and mac of device.
 *
 * output action.
 *
 * @return true for success and false for failure.
 */
bool
dns_cache_ip2action_lookup(struct ip2action_req *req);

/**
 * @brief Add an fsm policy result.
 *
 * receive dns_cache_entr.
 *
 * @return true for success and false for failure.
 */
bool
dns_cache_add_entry(struct ip2action_req *to_add);

/**
 * @brief Delete cache'd fsm policy result.
 *
 * receive ip2action_req.
 *
 * @return true for success and false for failure.
 */
bool
dns_cache_del_entry(struct ip2action_req *to_del);

/**
 * @brief remove old cache entres.
 *
 * @param ttl the cache entry time to live
 */
bool
dns_cache_ttl_cleanup();

/**
 * @brief print cache'd entres.
 *
 */
void
print_dns_cache(void);

/**
 * @brief return cache size.
 *
 * @param None
 *
 * @return cache size
 */
int
dns_cache_get_size(void);

/**
 * @brief print cache size.
 *
 * @param None
 *
 * @return None
 */
void
print_dns_cache_size(void);

/**
 * @brief returns cache ref count.
 *
 * @param None
 *
 * @return cache refcount
 */
uint8_t
dns_cache_get_refcount(void);

/**
 * @brief returns cache hit count.
 *
 * @param service_id
 *
 * @return cache hit count
 */
uint32_t
dns_cache_get_hit_count(uint8_t service_id);

/**
 * @brief print cache hit count.
 *
 * @param None
 *
 * @return None
 */
void
print_dns_cache_hit_count(void);

/**
 * @brief print dns_cache details.
 *
 * @param None
 *
 * @return None
 */
void
print_dns_cache_details(void);

#endif /* DNS_CACHE_H_INCLUDED */
