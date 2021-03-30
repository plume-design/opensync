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

#ifndef OSN_IPSET_H_INCLUDED
#define OSN_IPSET_H_INCLUDED

/**
 * @file osn_ipset.h
 * @brief OpenSync IPSET
 *
 * @addtogroup OSN
 * @{
 */

/*
 * ===========================================================================
 *  OpenSync Networking - IPSET API
 * ===========================================================================
 */

/**
 * @defgroup OSN_IPSET IPSET
 * OpenSync IPSET API
 * @{
 */

/**
 * OSN IPSET object type
 *
 * This is an opaque type. The actual structure implementation is hidden
 * and is platform dependent. A new instance of the object can be obtained by
 * calling @ref osn_ipset_new() and must be destroyed using
 * @ref osn_ipset_del().
 */
typedef struct osn_ipset osn_ipset_t;

/**
 * ipset type; refer to the ipset manual page for an explanation of these
 */
enum osn_ipset_type
{
    OSN_IPSET_BITMAP_IP,
    OSN_IPSET_BITMAPIP_MAC,
    OSN_IPSET_BITMAP_PORT,
    OSN_IPSET_HASH_IP,
    OSN_IPSET_HASH_MAC,
    OSN_IPSET_HASH_IP_MAC,
    OSN_IPSET_HASH_NET,
    OSN_IPSET_HASH_NET_NET,
    OSN_IPSET_HASH_IP_PORT,
    OSN_IPSET_HASH_NET_PORT,
    OSN_IPSET_HASH_IP_PORT_IP,
    OSN_IPSET_HASH_IP_PORT_NET,
    OSN_IPSET_HASH_IP_MARK,
    OSN_IPSET_HASH_NET_PORT_NET,
    OSN_IPSET_HASH_NET_IFACE,
    OSN_IPSET_LIST_SET
};

/**
 * Create a new IPSET named @p name. Some types require that options are
 * specified via the @p options command.
 *
 * @param[in]   name - ipset name
 * @param[in]   type - ipset type
 * @param[in]   options - ipset create options
 *
 * @return
 * This function returns a pointer to a freshly created OSN IPSET object, or
 * NULL on error.
 */
osn_ipset_t* osn_ipset_new(
        const char *name,
        enum osn_ipset_type type,
        const char *options);

/**
 * Delete an OSN IPSET object that was previously created with @ref
 * osn_ipset_new().
 *
 * @param[in]   self - A valid OSN IPSET object
 */
void osn_ipset_del(osn_ipset_t *self);

/**
 * Operations from osn_ipset_values_add(), osn_ipset_values_del() and
 * osn_ipset_values_set() functions may be delayed to ensure performance and
 * atomicity. This function forces any pending configuration to be applied to
 * the system.
 *
 * @param[in]   self - A valid OSN IPSET object
 *
 * @return
 * True on success, false otherwise.
 */
bool osn_ipset_apply(osn_ipset_t *self);

/**
 * Add @p values to the set @p self.
 *
 * The configuration may be cached or clustered together before it is applied to
 * the system in order to provide performance benefits and consistency
 * guarantees. osn_ipset_apply() must be called to ensure that the data from
 * this function is applied to the system.
 *
 * @param[in]   self - A valid OSN IPSET object
 * @param[in]   values - Array of values to be added to the ipset
 * @param[in]   values_len - Length of the @p values array
 *
 * @return
 * This function returns true on success, false on error. If an error occurred,
 * values may have been partially applied to the system.
 */
bool osn_ipset_values_add(osn_ipset_t *self, const char *values[], int values_len);

/**
 * Remove @p values from the set @p self.
 *
 * The configuration may be cached or clustered together before it is applied to
 * the system in order to provide performance benefits and consistency
 * guarantees. osn_ipset_apply() must be called to ensure that the data from
 * this function is applied to the system.
 *
 * @param[in]   self - A valid OSN IPSET object
 * @param[in]   values - Array of values to be removed from the ipset
 * @param[in]   values_len - Length of the @p values array
 *
 * @return
 * This function returns true on success, false on error. If an error occurred,
 * values may have been partially applied to the system.
 */
bool osn_ipset_values_del(osn_ipset_t *self, const char *values[], int values_len);

/**
 * Replace all values in the ipset with @p values.
 *
 * The configuration may be cached or clustered together before it is applied to
 * the system in order to provide performance benefits and consistency
 * guarantees. osn_ipset_apply() must be called to ensure that the data from
 * this function is applied to the system.
 *
 * @param[in]   self - A valid OSN IPSET object
 * @param[in]   values - Array of values that will replace all values in ipset
 * @param[in]   values_len - Length of the @p values array
 *
 * @return
 * This function returns true on success, false on error. If an error occurred,
 * values may have been partially applied to the system.
 */
bool osn_ipset_values_set(osn_ipset_t *self, const char *values[], int values_len);

/** @} OSN_IPSET */
/** @} OSN */

#endif /* OSN_IPSET_H_INCLUDED */
