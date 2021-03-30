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

#ifndef OSN_NETIF_H_INCLUDED
#define OSN_NETIF_H_INCLUDED

#include <stdbool.h>

#include "osn_types.h"

/**
 * @file osn_netif.h
 * @brief Network Interface L2 Abstraction
 *
 * @addtogroup OSN
 * @{
 *
 * @defgroup OSN_L2 L2 Interface
 *
 * OpenSync L2 Interface Management API
 *
 * This API provides management of L2 Ethernet-like interfaces.
 *
 * @{
 */

/*
 * ===========================================================================
 *  Network Interface Configuration API
 * ===========================================================================
 */

/**
 * @defgroup OSN_NETIF Ethernet Interface
 *
 * Ethernet Interface (L2) API
 *
 * @{
 */

/**
 * OSN NETIF object type
 * 
 * This is an opaque type. The actual structure implementation is hidden and is
 * platform dependent. A new instance of the object can be obtained by calling
 * @ref osn_netif_new() and must be destroyed using @ref osn_netif_del().
 */
typedef struct osn_netif osn_netif_t;

/**
 * Network interface status structure. A structure of this type is used when
 * reporting the status of the network interface. See @ref
 * osn_netif_status_fn_t and @ref osn_netif_status_notify() for more details.
 *
 * @note
 * If the @ref ns_exists field is false, all subsequent fields should be
 * considered undefined.
 */
struct osn_netif_status
{
    const char         *ns_ifname;      /**< Interface name */
    bool                ns_exists;      /**< True if interface exists --
                                             Subsequent fields should be considered
                                             undefined if this is false */
    osn_mac_addr_t      ns_hwaddr;      /**< Interface hardware address */
    bool                ns_up;          /**< True if interface is UP */
    bool                ns_carrier;     /**< True if carrier was detected (RUNNING) */
    int                 ns_mtu;         /**< MTU */
};

/**
 * osn_netif_t status notification callback type
 * 
 * A function of this type, registered via @ref osn_netif_status_notify,
 * will be invoked whenever the osn_netif_t object wishes to report the status
 * of the network interface.
 *
 * Typically this will happen whenever a status change is detected (for
 * example, when carrier is detected).
 *
 * Some implementations may choose to call this function periodically even if
 * there has been no status change detected.
 *
 * @param[in]   self    The object that is reporting the status
 * @param[in]   status  A pointer to a @ref osn_netif_status structure
 */
typedef void osn_netif_status_fn_t(osn_netif_t *self, struct osn_netif_status *status);

/**
 * Create a new instance of a network interface object.
 *
 * @param[in]   ifname  Interface name to which the netif instance will be
 *                      bound to
 *
 * @return
 * This function returns NULL if an error occurs, otherwise a valid @ref
 * osn_netif_t object is returned.
 *
 * @note
 * If the interface doesn't exist yet, this function may return success. The
 * actual interface existence will be reported to the status callback.
 */
osn_netif_t *osn_netif_new(const char *ifname);

/**
 * Destroy a valid osn_netif_t object.
 *
 * @param[in]   self  A valid pointer to an osn_netif_t object
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
bool osn_netif_del(osn_netif_t *self);

/**
 * Set the object @p self user data.
 *
 * @param[in]   self  A valid pointer to an osn_netif_t object
 * @param[in]   data  Pointer to user data
 */
void osn_netif_data_set(osn_netif_t *self, void *data);

/**
 * Get the object @p self user data. If no user data was set, NULL will
 * be returned.
 *
 * @param[in]   self  A valid pointer to an osn_netif_t object
 *
 * @return
 *
 * Returns a pointer to user data previously set using @ref
 * osn_netif_data_set().
 */
void *osn_netif_data_get(osn_netif_t *self);

/**
 * Set the NETIF status callback.
 *
 * Depending on the implementation, the status callback may be invoked
 * periodically or whenever an interface status change has been detected.
 * For maximum portability, the callback implementation should assume it can
 * be called using either mode of operation.
 *
 * @param[in]   self  A valid pointer to an osn_netif_t object
 * @param[in]   fn    A pointer to the netif status callback handler
 *
 * @note
 * The status change callback does not require a call to osn_netif_apply().
 * The callback handler must assume that it can be called any time in between
 * osn_netif_status_notify() and osn_netif_del().
 */
void osn_netif_status_notify(osn_netif_t *self, osn_netif_status_fn_t *fn);

/**
 * Ensure that all configuration pertaining the @p self object is applied to
 * the running system.
 *
 * How the configuration is applied to the system is highly implementation
 * dependent. Sometimes it makes sense to cluster together several
 * configuration parameters.
 *
 * osn_netif_apply() makes sure that a write operation is initiated for
 * all currently cached (dirty) configuration data.
 *
 * @note It is not guaranteed that the configuration will be applied as soon
 * as osn_netif_apply() returns -- only that the configuration process
 * will be started for all pending operations.
 */
bool osn_netif_apply(osn_netif_t *self);

/**
 * Set the interface state. If @p up is set to true, the interface will be
 * brought UP, otherwise it will be brought DOWN.
 *
 * @param[in]   self A valid pointer to an osn_netif_t object
 * @param[in]   up True if the interface state should be set to UP; for down
 *              use false
 *
 * @return
 * Returns true if the state option was successfully set.
 *
 * @note
 * A call to osn_netif_apply() may be required before the change can take
 * effect.
 */
bool osn_netif_state_set(osn_netif_t *self, bool up);

/**
 * Set the interface MTU.
 *
 * @param[in]   self A valid pointer to an osn_netif_t object
 * @param[in]   mtu New MTU value
 *
 * @return
 * Returns true if the MTU was successfully set. Failing a valid range check
 * may result in a return status of false.
 *
 * @note
 * A call to osn_netif_apply() may be required before the change can take
 * effect.
 */
bool osn_netif_mtu_set(osn_netif_t *self, int mtu);

/**
 * Set the interface hardware address.
 *
 * @param[in]   self A valid pointer to an osn_eth_t object
 * @param[in]   hwaddr The new interface hardware address
 *
 * @return
 * Returns true if the hardware address was successfully set.
 *
 * @note
 * A call to osn_netif_apply() may be required before the change can take
 * effect.
 */
bool osn_netif_hwaddr_set(osn_netif_t *self, osn_mac_addr_t hwaddr);

/** @} OSN_NETIF */
/** @} OSN_L2 */
/** @} OSN */

#endif /* OSN_NETIF_H_INCLUDED */
