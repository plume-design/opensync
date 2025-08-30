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

#ifndef INET_ROUTES6_H_INCLUDED
#define INET_ROUTES6_H_INCLUDED

#include "inet.h"

/**
 * @brief Type of IPv6 static routes manager
 */
typedef struct inet_routes6 inet_routes6_t;

/**
 * @brief Creates 'static routes' inet object which keeps
 * and applies static routes to given network interface.
 * Object is created in 'disabled' state
 *
 * @param if_name name of the network interface (dev in linux),
 * the routes will be applied to
 * @return ptr to created object
 */
inet_routes6_t *inet_routes6_new(const char *if_name);

/**
 * @brief Deletes 'static routes' object and releases allocated
 * memory. All added routes are removed from the system before
 * object is destroyed.
 *
 * @param self ptr to inet routes object
 */
void inet_routes6_del(inet_routes6_t *self);

/**
 * @brief Adds new static route to the object. When object is enabled
 * the route is immediately applied to the network interface, otherwise
 * it is collected and will be applied when enabled
 *
 * @param self ptr to inet routes object
 * @param route IPv6 route to be added
 * @return true when route added; false when route cannot be added
 * because it was rejected by network interface
 */
bool inet_routes6_add(inet_routes6_t *self, const osn_route6_config_t *route);

/**
 * @brief Removes specified route from the object and from the network
 * interface when object is enabled.
 *
 * @param self ptr to inet routes object
 * @param route route to be removed, all route params must match the
 * route already stored in the object
 * @return true when route found and removed; false otherwise
 */
bool inet_routes6_remove(inet_routes6_t *self, const osn_route6_config_t *route);

/**
 * @brief Enables/disables the object. When enabled, all added routes are
 * applied to the network interface, when disabled all added routes are
 * deleted from network interface, but remain stored in the object.
 *
 * Repeated enable/disable requests are ignored, 'true' is returned
 *
 * New enable state is always set, no matter if underlying Linux layer
 * returned error or not. Therefore if this function returns error, recovery
 * sequence should be to wait for network interface to be ready, then toggle
 * 'enable' state until routes are properly applied / deleted in network interface.
 *
 * @param self ptr to inet routes object
 * @param enable requested object state
 * @return true request executed w/o errors, false otherwise
 */
bool inet_routes6_enable(inet_routes6_t *self, bool enable);

/**
 * @brief Returns current enable state of the object
 *
 * @param self ptr to inet routes object
 * @return object enable state
 */
bool inet_routes6_is_enabled(const inet_routes6_t *self);

/**
 * @brief This function reapplies already added routes in the routing table.
 * This is needed if kernel automatically flushes the routes and we need to
 * resynchronize routing table with inet_routes
 *
 * @param self ptr to inet routes object
 * @return false when inet routes object is disabled
 * @return true when reapply request was accepted. Note that actual routing
 * update may be asynchronous.
 */
bool inet_routes6_reapply(inet_routes6_t *self);

#endif
