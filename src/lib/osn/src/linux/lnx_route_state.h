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

#ifndef LNX_ROUTE_STATE_H_INCLUDED
#define LNX_ROUTE_STATE_H_INCLUDED

#include "osn_types.h"
#include "osn_routes.h"

/**
 * Linux route state reporting API.
 */

/**
 * Route state update callback function type.
 *
 * Called by @ref lnx_route_state_poll() for each route currently on the system.
 *
 * Routes from non-main routing tables are reported as well. Routes from the
 * 'local' and the 'default' routing tables are not reported.
 *
 * @param[in] if_name   Nexthop interface.
 * @param[in] rts       Other route attributes for this route reported in rts->rts_route.
 */
typedef void (lnx_route_state_update_fn_t)(const char *if_name, struct osn_route_status *rts);

/**
 * Linux route state object type.
 *
 * This is an opaque type. The actual structure implementation is hidden as
 * it depends on the platform and the underlying backend implementation chosen.
 *
 * A new instance of the object can be obtained by calling
 * @ref lnx_route_state_new() and must be destroyed using
 * @ref lnx_route_state_del().
 */
typedef struct lnx_route_state lnx_route_state_t;

/**
 * Get a lnx_route_state_t object handler.
 *
 * At the same time register for a route state update callback.
 *
 * @param[in]  rt_cache_update_fn     Route state update callback. This callback
 *                                    will be called by @ref lnx_route_state_poll()
 *                                    for each route on the system. In that callback
 *                                    the API user has a chance to update its
 *                                    cache of the routes on the system and perform
 *                                    any other necessary actions (for instance
 *                                    notify the upper layer of any new or deleted
 *                                    routes).
 *
 * @return a lnx_route_state_t object handler.
 */
lnx_route_state_t *lnx_route_state_new(lnx_route_state_update_fn_t *rt_cache_update_fn);

/**
 *  Delete a lnx_route_state_t object handler when/if no longer needed.
 *
 *  @param self  A valid @ref lnx_route_state_t object.
 *
 */
void lnx_route_state_del(lnx_route_state_t *self);

/**
 * Poll for routes state. This function will call a callback registered via
 * @ref lnx_route_state_new() for each route currently on the system.
 *
 * @param self  A valid @ref lnx_route_state_t object.
 *
 */
void lnx_route_state_poll(lnx_route_state_t *self);

#endif /* LNX_ROUTE_STATE_H_INCLUDED */
