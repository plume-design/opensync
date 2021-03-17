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

#ifndef OSN_INET_H_INCLUDED
#define OSN_INET_H_INCLUDED

#include "osn_routes.h"

/**
 * @file osn_inet.h
 * @brief OpenSync IPv4
 *
 * @addtogroup OSN
 * @{
 */

/*
 * ===========================================================================
 *  IPv4 Configuration API
 * ===========================================================================
 */

/**
 * @defgroup OSN_IPV4 IPv4
 *
 * OpenSync IPv4 API
 *
 * @{
 */

/**
 * OSN IPv4 object type
 *
 * This is an opaque type. The actual structure implementation is hidden and is
 * platform dependent. A new instance of the object can be obtained by calling
 * @ref osn_ip_new() and must be destroyed using @ref osn_ip_del().
 */
typedef struct osn_ip osn_ip_t;

/**
 * IPv4 status structure. A structure of this type is used when reporting the
 * status of the IPv4 object. See @ref osn_ip_status_fn_t() and @ref
 * osn_ip_status_notify() for more details.
 */
struct osn_ip_status
{
    const char         *is_ifname;    /**< Interface name */
    size_t              is_addr_len;  /**< Length of is_addr array */
    osn_ip_addr_t      *is_addr;      /**< List of IPv4 addresses on interface */
    size_t              is_dns_len;   /**< Length of is_dns array */
    osn_ip_addr_t      *is_dns;       /**< List of DNS servers */
};

/**
 * osn_ip_t status notification callback type
 *
 * A function of this type, registered via @ref osn_ip_status_notify,
 * will be invoked whenever the osn_ip_t object wishes to report the
 * IPv4 status.
 *
 * Typically this will happen whenever an IPv4 status change is detected (for
 * example, when the IP of the interface changes).
 *
 * Some implementations may choose to call this function periodically even if
 * there has been no status change detected.
 *
 * @param[in]   ip      A valid pointer to an osn_ip_t object
 * @param[in]   status  A pointer to a @ref osn_ip_status
 */
typedef void osn_ip_status_fn_t(osn_ip_t *ip, struct osn_ip_status *status);

/**
 * Create a new instance of a IPv4 object.
 *
 * @param[in]   ifname  Interface name to which the IPv4 instance will be
 *                      bound to
 *
 * @return
 * This function returns NULL if an error occurs, otherwise a valid @ref
 * osn_ip_t object is returned.
 */
osn_ip_t* osn_ip_new(const char *ifname);

/**
 * Destroy a valid osn_ip_t object.
 *
 * @param[in]   ip  A valid pointer to an osn_ip_t object
 *
 * @return
 * This function returns true on success. On error, false is returned.
 * The input parameter should be considered invalid after this function
 * returns, regardless of the error code.
 *
 * @note
 * All resources that were allocated during the lifetime of the object are
 * freed.
 * The implementation may choose to remove all IPv4 addresses regardless if
 * they were added using @ref osn_ip_addr_add().
 *
 * @note
 * If osn_ip_addr_add() returns success when adding a duplicate address then
 * osn_ip_addr_del() should return success when removing an invalid address.
 */
bool osn_ip_del(osn_ip_t *ip);

/**
 * Add an IPv4 address to the IPv4 object.
 *
 * @param[in]   ip    A valid pointer to an osn_ip_t object
 * @param[in]   addr  A pointer to a valid IPv4 address (@ref osn_ip_addr_t)
 *
 * @note
 * The new configuration may not take effect until osn_ip_apply() is called.
 *
 * @note
 * If osn_ip_addr_add() returns success when adding a duplicate address then
 * osn_ip_addr_del() should return success when removing an invalid address.
 */
bool osn_ip_addr_add(osn_ip_t *ip, const osn_ip_addr_t *addr);

/**
 * Remove an IPv4 address from the IPv4 object.
 *
 * @param[in]   ip    A valid pointer to an osn_ip_t object
 * @param[in]   addr  A pointer to a valid IPv4 address (@ref osn_ip_addr_t)
 *
 * @note
 * The new configuration may not take effect until osn_ip_apply() is called.
 */
bool osn_ip_addr_del(osn_ip_t *ip, const osn_ip_addr_t *addr);

/**
 * Add an DNSv4 server IP to the IPv4 object.
 *
 * @param[in]   ip    A valid pointer to an osn_ip_t object
 * @param[in]   dns   A pointer to a valid DNSv4 address (@ref osn_ip_addr_t)
 *
 * @note
 * The new configuration may not take effect until osn_ip_apply() is called.
 */
bool osn_ip_dns_add(osn_ip_t *ip, const osn_ip_addr_t *dns);

/**
 * Remove an DNSv4 server IP from the IPv4 object.
 *
 * @param[in]   ip    A valid pointer to an osn_ip_t object
 * @param[in]   dns   A pointer to a valid DNSv4 address (@ref osn_ip_addr_t)
 *
 * @note
 * The new configuration may not take effect until osn_ip_apply() is called.
 */
bool osn_ip_dns_del(osn_ip_t *ip, const osn_ip_addr_t *dns);

/**
 * Add gateway route to IPv4 object.
 *
 * @param[in]   ip    A valid pointer to an osn_ip_t object
 * @param[in]   src   Source IPv4 subnet
 * @param[in]   gw    Gateway IPv4 address
 *
 * @note
 * The new configuration may not take effect until osn_ip_apply() is called.
 * This might be moved to the OSN_ROUTEV4 API.
 */
bool osn_ip_route_gw_add(osn_ip_t *ip, const osn_ip_addr_t *src, const osn_ip_addr_t *gw);

/**
 * Remove gateway route from IPv4 object.
 *
 * @param[in]   ip    A valid pointer to an osn_ip_t object
 * @param[in]   src   Source IPv4 subnet
 * @param[in]   gw    Gateway IPv4 address
 *
 * @note
 * The new configuration may not take effect until osn_ip_apply() is called.
 * This might be moved to the OSN_ROUTEV4 API.
 */
bool osn_ip_route_gw_del(osn_ip_t *ip, const osn_ip_addr_t *src, const osn_ip_addr_t *gw);

/**
 * Set the IPv4 user data.
 *
 * @param[in]   ip    A valid pointer to an osn_ip_t object
 * @param[in]   data  Private data
 */
void osn_ip_data_set(osn_ip_t *ip, void *data);

/**
 * Get the IPv4 user data.
 *
 * @param[in]   ip    A valid pointer to an osn_ip_t object
 *
 * @return
 * This function returns the data that was previously set using
 * @ref osn_ip_data_set().
 */
void *osn_ip_data_get(osn_ip_t *ip);

/**
 * Set the IPv4 status callback.
 *
 * Depending on the implementation, the status callback may be invoked
 * periodically or whenever a IPv4 status change has been detected.
 * For maximum portability, the callback implementation should assume it can
 * be called using either mode of operation.
 *
 * @param[in]   ip    A valid pointer to an osn_ip_t object
 * @param[in]   fn    A pointer to the function implementation
 */
void osn_ip_status_notify(osn_ip_t *ip, osn_ip_status_fn_t *fn);

/**
 * Ensure that all configuration pertaining the @p self object is applied to
 * the running system.
 *
 * How the configuration is applied to the system is highly implementation
 * dependent. Sometimes it makes sense to cluster together several
 * configuration parameters (for example, dnsmasq uses a single config file).
 *
 * osn_ip_apply() makes sure that a write operation is initiated for
 * all currently cached (dirty) configuration data.
 *
 * @note It is not guaranteed that the configuration will be applied as soon
 * as osn_ip_apply() returns -- only that the configuration process
 * will be started for all pending operations.
 */
bool osn_ip_apply(osn_ip_t *ip);

/** @} OSN_IPV4 */
/** @} OSN */

#endif /* OSN_INET_H_INCLUDED */
