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

#ifndef OSN_VLAN_H_INCLUDED
#define OSN_VLAN_H_INCLUDED

#include <stdbool.h>

#include "osn_types.h"

/**
 * @file osn_vlan.h
 *
 * @brief OpenSync VLAN Interface Abstraction
 *
 * @addtogroup OSN
 * @{
 *
 * @defgroup OSN_VLAN VLAN
 *
 * OpenSync API for managing VLAN interfaces
 *
 * @{
 */

/*
 * ===========================================================================
 *  VLAN interface configuration
 * ===========================================================================
 */

/**
 * OSN VLAN object type
 *
 * This is an opaque type. The actual structure implementation is hidden and is
 * platform dependent. A new instance of the object can be obtained by calling
 * @ref osn_vlan_new() and must be destroyed using @ref osn_vlan_del().
 */
typedef struct osn_vlan osn_vlan_t;

/**
 * Create a new instance of a VLAN interface object.
 *
 * @param[in]   ifname  VLAN interface name
 *
 * @return
 * This function returns NULL if an error occurs, otherwise a valid @ref
 * osn_vlan_t object is returned.
 *
 * @note
 * The VLAN interface may be created after osn_vlan_apply() is called.
 */

osn_vlan_t *osn_vlan_new(const char *ifname);

/**
 * Destroy a valid osn_vlan_t object.
 *
 * @param[in]   self  A valid pointer to an osn_vlan_t object
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
bool osn_vlan_del(osn_vlan_t *self);

/**
 * Apply the interface VLAN configuration to the system. If not already created,
 * this function will create the VLAN interface.
 *
 * @note
 * When this function returns, the running system may be still in an incomplete
 * configuration state -- this function just ensures that the configuration
 * process has started.
 */
bool osn_vlan_apply(osn_vlan_t *self);

/**
 * Set the parent interface; this interface will be used to create the VLAN
 * interface
 *
 * @param[in]   self  A valid pointer to an osn_vlan_t object
 * @param[in]   parent_ifname The parent interface name
 *
 * This function must be called to set the parent interface name before
 * osn_vlan_apply(), otherwise the VLAN interface creation will fail.
 *
 * If this function is called multiple times, previous values are overwritten.
 *
 * @return
 * This function returns true on success. On error, false is returned.
 */
bool osn_vlan_parent_set(
        osn_vlan_t *self,
        const char *parent_ifname);

/**
 * Set the VLAN ID of the interface.
 *
 * @param[in]   self  A valid pointer to an osn_vlan_t object
 * @param[in]   vlanid The VLAN ID of the interface
 *
 * A valid VLAN ID must be set before osn_vlan_apply() can succeed.
 *
 * @return
 * This function return true on success or false otherwise. On error, the
 * previous value of the VLAN ID will be preserved.
 */
bool osn_vlan_vid_set(
        osn_vlan_t *self,
        int vlanid);

/** @} OSN_VLAN */
/** @} OSN */

#endif /* OSN_VLAN_H_INCLUDED */
