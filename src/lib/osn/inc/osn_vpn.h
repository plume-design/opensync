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

#ifndef OSN_VPN_H_INCLUDED
#define OSN_VPN_H_INCLUDED

#include <stdbool.h>
#include <string.h>
#include "osn_types.h"

/**
 * @file osn_vpn.h
 *
 * @brief OpenSync Virtual Private Networks Common Types
 *
 * @addtogroup OSN
 * @{
 *
 * @defgroup OSN_VPN OpenSync Virtual Private Networks
 *
 * OpenSync VPN APIs
 *
 * @{
 */

/*
 * ===========================================================================
 * Common OpenSync VPN types and functions
 * ===========================================================================
 */

/**
 * OSN VPN tunnel object type.
 *
 * This is an opaque type. The actual structure implementation is hidden as
 * it may depend on the platform.
 *
 * A new instance of the object can be obtained by calling
 * @ref osn_vpn_new() and must be destroyed using @ref
 * osn_vpn_del().
 */
typedef struct osn_vpn osn_vpn_t;

/**
 * VPN tunnel connection state.
 *
 * The "up" state means the VPN tunnel is up. Note that whether traffic would be
 * able to actually flow through the tunnel may depend on other conditions
 * (for instance routes) as well.
 *
 * The "connecting" state may not be reported by all VPN types. It would
 * usually mean that the connection has initiated but it is stuck in
 * connecting phase (maybe due to network conditions).
 */
enum osn_vpn_conn_state
{
    OSN_VPN_CONN_STATE_DOWN,         /** VPN tunnel down */
    OSN_VPN_CONN_STATE_CONNECTING,   /** VPN tunnel connecting */
    OSN_VPN_CONN_STATE_UP,           /** VPN tunnel up */
    OSN_VPN_CONN_STATE_ERROR,        /** Error */
    OSN_VPN_CONN_STATE_MAX
};

/**
 * VPN tunnel healthcheck status.
 */
enum osn_vpn_health_status
{
    OSN_VPN_HEALTH_STATUS_NA,       /** VPN health status not available */
    OSN_VPN_HEALTH_STATUS_OK,       /** VPN health status OK */
    OSN_VPN_HEALTH_STATUS_NOK,      /** VPN health status NOT OK */
    OSN_VPN_HEALTH_STATUS_ERR,      /** VPN health status ERROR */
    OSN_VPN_HEALTH_STATUS_MAX,
};

#define OSN_VPN_HEALTH_MIN_INTERVAL             5     /** Minimum allowed healthcheck interval */
#define OSN_VPN_HEALTH_MIN_TIMEOUT              10    /** Minimum allowed healthcheck timeout */

/** Check if a VPN tunnel name is valid */
#define OSN_VPN_IS_VALID_TUNNEL_NAME(name)      (strlen(name) > 0 && strchr(name, '/') == NULL)

/** Healthcheck status notification callback function type.
 *
 * A function of this type can be registered via
 * @ref osn_vpn_healthcheck_notify_status_set()
 * and will be invoked whenever the osn_vpn_t object wishes to report the
 * VPN tunnel health status. This would typically be only at health status
 * changes, but may also be called for initial status updates.
 *
 * @param[in]  self   a pointer to osn_vpn_t object
 * @param[in]  status the new/current health status of the VPN tunnel
 *
 */
typedef void osn_vpn_health_status_fn_t(osn_vpn_t *self, enum osn_vpn_health_status status);

/**
 * Create a new instance of an osn_vpn_t client object.
 *
 * @param[in]   name  a name for this VPN. Should be unique. Any specific VPN
 *                    type config attached to it should be created with the same
 *                    name.
 *
 * @return
 * A valid @ref osn_vpn_t object or NULL on error.
 */
osn_vpn_t *osn_vpn_new(const char *name);

/**
 * Administratively enable or disable this VPN.
 *
 * @param[in] self     a pointer to a valid @ref osn_vpn_t object
 * @param[in] enable   enable or disable this VPN.
 */
bool osn_vpn_enable_set(osn_vpn_t *self, bool enable);

/**
 * @defgroup OSN_VPN_HEALTHCHECK OpenSync VPN Healthcheck
 *
 * OpenSync VPN Healthcheck APIs
 *
 * @{
 */
/*
 * ===========================================================================
 * OpenSync VPN Healthcheck API
 * ===========================================================================
 */

/**
 * Configure VPN healthcheck IP.
 *
 * VPN healthcheck IP should be an IP address at remote VPN side reachable
 * through the VPN tunnel. It will be regularly pinged.
 *
 * @param[in] self  a pointer to a valid @ref osn_vpn_t object
 * @param[in] ip    healthcheck IP address to configure. This can be IPv4 or
 *                  IPv6 address. The address type support for healthcheck IP
 *                  may depend on the actual VPN type of this VPN tunnel and
 *                  its implementation and configuration.
 *
 * @return true on success
 *
 */
bool osn_vpn_healthcheck_ip_set(osn_vpn_t *self, osn_ipany_addr_t *ip);

/**
 * Configure VPN healthcheck interval.
 *
 * VPN healthcheck action (ping) will be performed every interval seconds.
 *
 * @param[in] self      a pointer to a valid @ref osn_vpn_t object
 * @param[in] interval  VPN healthcheck interval to configure.
 *                      Must be at least @ref OSN_VPN_HEALTH_MIN_INTERVAL
 *
 * @return true on success
 */
bool osn_vpn_healthcheck_interval_set(osn_vpn_t *self, int interval);

/**
 * Configure VPN healthcheck timeout.
 *
 * VPN healthcheck timeout is a time period of unsuccessful health checks
 * after which healthcheck failure is declared.
 *
 * @param[in] self     a pointer to a valid @ref osn_vpn_t object
 * @param[in] timeout  VPN healthcheck timeout to configure.
 *                     Must be at least @ref OSN_VPN_HEALTH_MIN_TIMEOUT
 *
 * @return true on success
 */
bool osn_vpn_healthcheck_timeout_set(osn_vpn_t *self, int timeout);

/**
 * Configure an *optional* VPN healthcheck source i.e. a source IP or
 * source interface to ping from.
 *
 * @param[in] self    a pointer to a valid @ref osn_vpn_t object
 * @param[in] src     source IP address string (IPv4 or IPv6 -- the address
 *                    type supported may depend on the specific VPN type and
 *                    configuration of this tunnel) or an interface name string.
 *
 * @return true on success
 *
 * @note While all the other VPN healthcheck parameters are mandatory,
 *       healthcheck source is optional as it may only be needed in some
 *       configurations.
 *
 * @return true on success
 */
bool osn_vpn_healthcheck_src_set(osn_vpn_t *self, const char *src);

/**
 * Enable or disable VPN healthcheck for this VPN.
 *
 * @param[in] self   a pointer to a valid @ref osn_vpn_t object
 * @param[in] enable enable or disable healthcheck for this VPN
 *
 * @return  true on success
 */
bool osn_vpn_healthcheck_enable_set(osn_vpn_t *self, bool enable);

/**
 * Configure a healthcheck status update notification callback.
 *
 * @param[in] self   a pointer to a valid @ref osn_vpn_t object
 * @param[in] status_cb  a pointer to a valid callback function of type
 *                       @ref osn_vpn_health_status_fn_t. This function will
 *                       be invoked whenever healthcheck status of this VPN
 *                       tunnel changes. Set to NULL to deconfigure.
 *
 * @return true on success
 */
bool osn_vpn_healthcheck_notify_status_set(osn_vpn_t *self, osn_vpn_health_status_fn_t *status_cb);

/**
 * Apply the VPN healthcheck configuration parameters to the running system.
 *
 * After you configure VPN healthcheck parameters you must call this function
 * to ensure the healthcheck configuration to take effect.
 *
 * @param[in] self  a pointer to a valid @ref osn_vpn_t object
 *
 * @return true on success
 */
bool osn_vpn_healthcheck_apply(osn_vpn_t *self);

/**
 * Destroy a valid osn_vpn_t object.
 *
 * If VPN healthcheck was configured, it will be stopped first.
 *
 * @param[in] self  a pointer to a valid @ref osn_vpn_t object.
 *
 * @return true on success. After return of this function the input parameter
 *         should be considered invalid and must no longer be used.
 */
bool osn_vpn_del(osn_vpn_t *self);

/**
 * Get the name of this VPN tunnel.
 *
 * @param[in] self a pointer to a valid @ref osn_vpn_t object
 *
 * @return  a pointer to a string representing this VPN tunnel name.
 *          This string must not be freed and is valid only during a lifetime
 *          of an @ref osn_vpn_t object
 */
const char *osn_vpn_name_get(osn_vpn_t *self);

/*
 * Get string representation of VPN tunnel connection state.
 *
 * @param[in]  vpn_conn_state
 *
 * @return a pointer to a string representation of @ref enum osn_vpn_conn_state
 *         Note: a pointer to a statically allocated string is returned and
 *         thus must not be freed.
 */
const char *osn_vpn_conn_state_to_str(enum osn_vpn_conn_state vpn_conn_state);

/*
 * Get string representation of VPN tunnel healthcheck status
 *
 * @param[in]  vpn_health_status
 *
 * @return a pointer to a string representation of @ref enum osn_vpn_health_status
 *         Note: a pointer to a statically allocated string is returned and
 *         thus must not be freed.
 */
const char *osn_vpn_health_status_to_str(enum osn_vpn_health_status vpn_health_status);

/** @} OSN_VPN_HEALTHCHECK */
/** @} OSN_VPN */
/** @} OSN */

#endif /* OSN_VPN_H_INCLUDED */
