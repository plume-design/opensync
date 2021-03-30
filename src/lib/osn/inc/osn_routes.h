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

#ifndef OSN_ROUTES_H_INCLUDED
#define OSN_ROUTES_H_INCLUDED

#include <stdbool.h>

#include "osn_types.h"

/**
 * @file osn_routes.h
 * @brief OpenSync Routes Interface Abstraction
 *
 * @addtogroup OSN
 * @{
 *
 * @addtogroup OSN_IPV4
 * @{
 */

/*
 * ===========================================================================
 *  IPv4 Routing API
 * ===========================================================================
 */

/**
 * @defgroup OSN_ROUTEV4 IPv4 Routing
 *
 * OpenSync IPv4 Routing API
 *
 * @note The IPv4 routing API is subject to change and may be merged with the
 * @ref osn_ip_t class in the future.
 *
 * @{
 */

/**
 * IPv4 Routing object type
 *
 * This is an opaque type. The actual structure implementation is hidden
 * and is platform dependent. A new instance of the object can be obtained by
 * calling @ref osn_route_new() and must be destroyed using
 * @ref osn_route_del().
 */
typedef struct osn_route osn_route_t;

/**
 * @brief Definition of the route for ipv4
 */
typedef struct osn_route4
{
    osn_ip_addr_t dest; //< destination addr with optional mask prefix
    bool gw_valid;      //< gateway validity flag
    osn_ip_addr_t gw;   //< gateway addr (optional)
    int metric;         //< route metric or -1 when not specified

} osn_route4_t;

/**
 * Initializer for the @ref osn_route4 structure.
 *
 * Use this macro to initialize a @ref osn_route4 structure to its
 * default values
 */
#define OSN_ROUTE4_INIT (struct osn_route4) \
{                                           \
    .dest = OSN_IP_ADDR_INIT,               \
    .gw_valid = false,                      \
    .gw = OSN_IP_ADDR_INIT,                 \
    .metric = -1,                           \
}


/**
 * Structure passed to the route state notify callback, see @ref
 * osn_route_status_fn_t
 */
struct osn_route_status
{
    osn_route4_t    rts_route;
    osn_mac_addr_t  rts_gw_hwaddr;   /**< Gateway MAC address */
};

/**
 * Initializer for the @ref osn_route_status structure.
 *
 * Use this macro to initialize a @ref osn_route_status structure to its
 * default values
 */
#define OSN_ROUTE_STATUS_INIT (struct osn_route_status) \
{                                                       \
    .rts_route = OSN_ROUTE4_INIT,                       \
    .rts_gw_hwaddr = OSN_MAC_ADDR_INIT,                 \
}

/**
 * osn_route_t status notification callback. This function will be invoked
 * whenever the osn_route_t object detects a status change and wishes to report
 * it.
 *
 * Typically this will happen whenever an routing change is detected (for
 * example, when a new route is added to the system).
 *
 * Some implementation may choose to call this function periodically even if
 * there has been no status change detected.
 *
 * @param[in]   data    Private data
 * @param[in]   rts     A pointer to a @ref osn_route_status
 * @param[in]   remove  true if the route in @p rts was removed
 */
typedef bool osn_route_status_fn_t(
        osn_route_t *self,
        struct osn_route_status *rts,
        bool remove);

/**
 * Create a new IPv4 routing object. This object can be used to add/remove
 * IPv4 routing rules. The object is bound to the interface @p ifname.
 *
 * @param[in]   ifname  Interface name to which the routing object instance
 *                      will be bound to
 *
 * @return
 * This function returns NULL if an error occurs, otherwise a valid @ref
 * osn_route_t object is returned.
 */
osn_route_t *osn_route_new(const char *ifname);

/**
 * Destroy a valid osn_route_t object.
 *
 * @param[in]   self  A valid pointer to an osn_route_t object
 *
 * @return
 * This function returns true on success. On error, false is returned.
 * The input parameter should be considered invalid after this function
 * returns, regardless of the error code.
 *
 * @note
 * All resources that were allocated during the lifetime of the object are
 * freed.
 */
bool osn_route_del(osn_route_t *self);

/**
 * Set the IPv4 status callback.
 *
 * Depending on the implementation, the status callback may be invoked
 * periodically or whenever a IPv4 status change has been detected.
 * For maximum portability, the callback implementation should assume it can
 * be called using either mode of operation.
 *
 * @param[in]   self  A valid pointer to an osn_route_t object
 * @param[in]   fn    A pointer to the function implementation
 */
bool osn_route_status_notify(osn_route_t *self, osn_route_status_fn_t *fn);

/**
 * Set user data
 *
 * @param[in]   self  A valid pointer to an osn_route_t object
 * @param[in]   data  Private data, will be passed to the callback
 */
void osn_route_data_set(osn_route_t *self, void *data);

/**
 * Get user data
 *
 * @param[in]   self  A valid pointer to an osn_route_t object
 */
void* osn_route_data_get(osn_route_t *self);

/**
 * @brief Definition of osn IPv4 static route config which
 * is responsible for adding and deletion of IPv4 static routes
 * in the OS network interface 
 */
typedef struct osn_route4_cfg osn_route4_cfg_t;

/**
 * @brief Creates new IPv4 static route configuration object for
 * specified network interface device
 * 
 * @param if_name network interface name
 * @return handle to created routes config or NULL on failure
 */
osn_route4_cfg_t *osn_route4_cfg_new(const char *if_name);

/**
 * @brief Destroys network manager for the interface. 
 * Implementations shall take care to remove all routes before
 * destroying the manager
 * 
 * @param self handle to route config
 * @return true when manager destroyed w/o problems, false otherwise
 */
bool osn_route4_cfg_del(osn_route4_cfg_t *self);

/**
 * @brief Gets interface name of this route config object
 * 
 * @param self handle to route config
 * @return the name of route config network interface
 */
const char* osn_route4_cfg_name(const osn_route4_cfg_t *self);

/**
 * @brief Adds static route to the routing table
 * 
 * @param self handle to route config, this route shall be added to
 * @param route ptr to route to be added
 * @return true when new route added; false otherwise
 */
bool osn_route_add(osn_route4_cfg_t *self, const osn_route4_t *route);

/**
 * @brief Removes static route from the routing table on basis of route definition
 * 
 * @param self handle to route config, this route shall be removed from
 * @param route ptr to route definition to be removed
 * @return true when route successfully removed
 * @return false when route removal failed; reason should be logged
 */
bool osn_route_remove(osn_route4_cfg_t *self, const osn_route4_t *route);

/**
 * @brief Applies memorized routes state in the routing table.
 * 
 * @param self handle to route config
 * @return 'true' on success; 'false' on failure
 */
bool osn_route_apply(osn_route4_cfg_t *self);

/**
 * @brief Finds network device name in the routing table to be used 
 * for redirection of packets with specified IPv4
 * 
 * @param addr IPv4 addr to find network device name for
 * @param buf buffer for found network device name
 * @param bufSize size of the buffer
 * @return true when network device name found; false otherwise
 */
bool osn_route_find_dev(osn_ip_addr_t addr, char *buf, size_t bufSize);

/** @} OSN_ROUTEV4 */
/** @} OSN_IPV4 */
/** @} OSN */

#endif /* OSN_ROUTES_H_INCLUDED */
