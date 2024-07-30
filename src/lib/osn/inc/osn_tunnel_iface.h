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

#ifndef OSN_TUNNEL_IFACE_H_INCLUDED
#define OSN_TUNNEL_IFACE_H_INCLUDED

#include "osn_types.h"

/**
 * @file osn_tunnel_iface.h
 *
 * @brief OpenSync Tunnel Interface API
 *
 * @addtogroup OSN
 * @{
 *
 * @defgroup OSN_TUNNEL_IFACE OpenSync Tunnel Interface
 *
 * OpenSync Tunnel Interface APIs
 *
 * @{
 */

/**
 * OSN Tunnel Interface object type.
 *
 * This is an opaque type. The actual structure implementation is hidden as
 * it may depend on the platform.
 *
 * A new instance of the object can be obtained by calling
 * @ref osn_tunnel_iface_new() and must be destroyed using @ref
 * osn_tunnel_iface_del().
 */
typedef struct osn_tunnel_iface osn_tunnel_iface_t;

/**
 * OSN Tunnel Interface type
 */
enum osn_tunnel_iface_type
{
    OSN_TUNNEL_IFACE_TYPE_NOT_SET,
    OSN_TUNNEL_IFACE_TYPE_VTI,         /** Virtual Tunnel Interface IPv4 (vti) */
    OSN_TUNNEL_IFACE_TYPE_VTI6,        /** Virtual Tunnel Interface IPv6 (vti6) */
    OSN_TUNNEL_IFACE_TYPE_IP6TNL,      /** Virtual Tunnel Interface IPv4|IPv6 over IPv6 */
    OSN_TUNNEL_IFACE_TYPE_IP4GRE,      /** Virtual Tunnel Interface GRE over IPv4 */
    OSN_TUNNEL_IFACE_TYPE_IP4GRETAP,   /** Virtual L2 Tunnel Interface GRE over IPv4 */
    OSN_TUNNEL_IFACE_TYPE_IP6GRE,      /** Virtual Tunnel Interface GRE over IPv6 */
    OSN_TUNNEL_IFACE_TYPE_IP6GRETAP,   /** Virtual L2 Tunnel Interface GRE over IPv6 */
    OSN_TUNNEL_IFACE_TYPE_MAX
};

/**
 * OSN Tunnel Interface mode
 */
enum osn_tunnel_iface_mode
{
    OSN_TUNNEL_IFACE_MODE_NOT_SET,
    OSN_TUNNEL_IFACE_MODE_ANY,         /* IPv4|IPv6 over IPv6 */
    OSN_TUNNEL_IFACE_MODE_IPIP6,       /* IPv4 over IPv6 */
    OSN_TUNNEL_IFACE_MODE_IP6IP6,      /* IPv6 over IPv6 */
    OSN_TUNNEL_IFACE_MODE_MAX
};

/**
 * Create a new @ref osn_tunnel_iface_t object.
 *
 * @param[in] ifname   interface name for this tunnel interface
 *
 * @return  on success a valid @ref osn_tunnel_iface_t object is returned, on
 *          error NULL is returned.
 */
osn_tunnel_iface_t *osn_tunnel_iface_new(const char *ifname);

/**
 * Set tunnel interface type.
 *
 * @param[in] self    a valid pointer to @ref osn_tunnel_iface_t object
 * @param[in] iftype  specific tunnel interface type
 *
 * @return true on success
 */
bool osn_tunnel_iface_type_set(osn_tunnel_iface_t *self, enum osn_tunnel_iface_type iftype);

/**
 * Set tunnel interface mode.
 *
 * This function is optional. Not all tunnel interface types support or require
 * mode to be set.
 *
 * @param[in] self    a valid pointer to @ref osn_tunnel_iface_t object
 * @param[in] mode    mode for this tunnel interface
 *
 * @return true on success
 */
bool osn_tunnel_iface_mode_set(osn_tunnel_iface_t *self, enum osn_tunnel_iface_mode mode);

/**
 * Set tunnel interface local and remote endpoints.
 *
 * @param[in] self    a valid pointer to @ref osn_tunnel_iface_t object
 * @param[in] local_endpoint  IPv4 or IPv6 address for this tunnel interface
 *                            local endpoint
 * @param[in] remote_endpoint IPv4 or IPv6 address for this tunnel interface
 *                            remote endpoint
 *
 * @return true on success
 *
 * @note Both local and remote endpoints IP addresses must be of the same
 *       address type.
 *
 *       Whether a specific address type (IPv4 or IPv6) can be configured
 *       depends on the specific tunnel interface type, see @ref
 *       osn_tunnel_iface_type_set().
 */
bool osn_tunnel_iface_endpoints_set(
        osn_tunnel_iface_t *self,
        osn_ipany_addr_t local_endpoint,
        osn_ipany_addr_t remote_endpoint);

/**
 * Optional: set an XFRM mark for this tunnel interface.
 *
 * @param[in] self    a valid pointer to @ref osn_tunnel_iface_t object
 * @param[in] key     mark to set on this tunnel interface. mark==0 is reserved
 *                    and means mark will not be set.
 *
 * @return true on success
 */
bool osn_tunnel_iface_key_set(osn_tunnel_iface_t *self, int key);

/**
 * Optional: Set physical device to use for tunnel endpoint communication.
 *
 * @param[in] self          a valid pointer to @ref osn_tunnel_iface_t object
 * @param[in] dev_if_name   physical device interface name.
 *                          Set to empty string to unset.
 *
 * @return true on success
 */
bool osn_tunnel_iface_dev_set(osn_tunnel_iface_t *self, const char *dev_if_name);

/**
 * Enable or disable this tunnel interface.
 *
 * @param[in] self    a valid pointer to @ref osn_tunnel_iface_t object
 * @param[in] enable  true to enable
 *
 * @return true on success
 */
bool osn_tunnel_iface_enable_set(osn_tunnel_iface_t *self, bool enable);

/**
 * Apply all the configuration parameters on the @ref osn_tunnel_iface_t object
 * and create (or reconfigure) the tunnel interface.
 *
 * This function must be called after all the configuration parameters have
 * been set to actually create the interface on the system.
 *
 * @note: If reconfiguring an existing tunnel interface implementation may
 *        first delete the existing interface and then create a new one as
 *        the underlying kernel APIs may not support reconfiguring an
 *        already created interface.
 *
 * @return true on success
 */
bool osn_tunnel_iface_apply(osn_tunnel_iface_t *self);

/**
 * Destroy a valid osn_tunnel_iface_t object.
 *
 * If this object configuration was applied i.e if an actual interface exists
 * the interface will be removed from the system.
 *
 * @param[in] self   a valid pointer to @ref osn_tunnel_iface_t object
 *
 * @return true on success. This function may fail if there was an error
 *         deleting the interface from the system.
 *
 *         After return of this function (regardless of its return value)
 *         the input parameter should be considered invalid and must no longer
 *         be used.
 */
bool osn_tunnel_iface_del(osn_tunnel_iface_t *self);

/** @} OSN_TUNNEL_IFACE */
/** @} OSN */

#endif /* OSN_TUNNEL_IFACE_H_INCLUDED */
