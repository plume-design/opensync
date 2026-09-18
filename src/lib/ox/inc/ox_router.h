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

#ifndef OX_ROUTER_H_INCLUDED
#define OX_ROUTER_H_INCLUDED

#include <ox_route.h>

bool ox_router_init(ox_router_t *router);

void ox_router_fini(ox_router_t *router);

/**
 * Add a single route to the router.
 *
 * The `routes` is sentinel-terminated. A sentinel is `ox_route_t` which is
 * invalid, ie. ox_route_is_valid() returns false. This is true for
 * `ox_route_t` that is zero-initialized, so a common pattern is to define
 * routes as static const arrays with a zero-initialized entry at the end, eg.
 *
 * ```
 * static const ox_route_t my_routes[] = {
 *    { .table = { .tr181_table = "Device.MyTable.", ... } },
 *    { .param = { .tr181_param = "Device.MyParam", ... } },
 *    {}, // the sentinel
 * };
 * ```
 */
bool ox_router_add_routes(ox_router_t *router, const ox_route_t *routes);

ox_table_ctx_t *ox_router_find_longest_parent_table_route(ox_router_t *router, const ox_route_table_t *route);

/**
 * Find the instance with the given uuid in the table identified by
 * tr181_table. Returns NULL, if the table isn't registered or has
 * no instance with that uuid.
 */
ox_table_instance_t *ox_router_find_table_instance_by_uuid(
        ox_router_t *router,
        const char *tr181_table,
        const char *uuid);

#endif /* OX_ROUTER_H_INCLUDED */
