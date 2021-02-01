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

struct dns_cache_mgr
{
    bool        initialized;
    ds_tree_t   ip2a_tree;
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
    ds_tree_node_t              ip2a_tnode;
};

struct ip2action_req
{
   os_macaddr_t             *device_mac;
   struct sockaddr_storage  *ip_addr;

   int                      cache_ttl;
   int                      action;
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
 * @brief print cache size..
 *
 */
void
print_dns_cache_size(void);

#endif /* DNS_CACHE_H_INCLUDED */
