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

#ifndef OSN_PPPOE_H_INCLUDED
#define OSN_PPPOE_H_INCLUDED

#include <stdbool.h>

#include "osn_types.h"

/**
 * @file osn_pppoe.h
 * @brief OpenSync PPPoE Interface Abstraction
 *
 * @addtogroup OSN
 * @{
 *
 * @defgroup OSN_PPPOE PPPoE
 *
 * OpenSync API for managing PPPoE links
 *
 * @{
 */

/*
 * ===========================================================================
 *  PPPoE interface configuration
 * ===========================================================================
 */

/**
 * OSN PPPoE object type
 *
 * This is an opaque type. The actual structure implementation is hidden and is
 * platform dependent. A new instance of the object can be obtained by calling
 * @ref osn_pppoe_new() and must be destroyed using @ref osn_pppoe_del().
 */
typedef struct osn_pppoe osn_pppoe_t;

/**
 * Create a new instance of a PPPoE interface object.
 *
 * @param[in]   ifname  Interface name of the PPPoE link
 *
 * @return
 * This function returns NULL if an error occurs, otherwise a valid @ref
 * osn_netif_t object is returned.
 *
 * @note
 * The PPPoE interface may be created after osn_pppoe_apply() is called.
 */

osn_pppoe_t *osn_pppoe_new(const char *ifname);

/**
 * Destroy a valid osn_pppoe_t object.
 *
 * @param[in]   self  A valid pointer to an osn_pppoe_t object
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
bool osn_pppoe_del(osn_pppoe_t *self);

/**
 * Set the parent interface; this interface will be used to create the PPPoE
 * interface
 *
 * @param[in]   self  A valid pointer to an osn_pppoe_t object
 * @param[in]   parent_ifname The parent interface name
 *
 * This function must be called before osn_pppoe_apply(), otherwise the
 * PPPoE interface creation will fail.
 *
 * If this function is called multiple times, previous values are overwritten.
 *
 * @return
 * This function returns true on success. On error, false is returned.
 */
bool osn_pppoe_parent_set(
        osn_pppoe_t *self,
        const char *parent_ifname);

/**
 * Set credentials for this PPPoE connection.
 *
 * @param[in]   self  A valid pointer to an osn_pppoe_t object
 * @param[in]   username Username to be used during PAP/CHAP authentication
 * @param[in]   password Password to be used during PAP/CHAP authentication
 *
 * If this function is called multiple times, previous credentials are
 * overwritten.
 *
 * @return
 * This function returns true on success. On error, false is returned.
 */
bool osn_pppoe_secret_set(
        osn_pppoe_t *self,
        const char *username,
        const char *password);

/**
 * Apply configuration to the system.
 *
 * This function applies the PPPoE data to the running system and creates the
 * PPPoE interface.
 *
 * @note
 * When this function returns, the running system may be still in an incomplete
 * configuration state -- this function just ensures that the configuration
 * process has started.
 */
bool osn_pppoe_apply(osn_pppoe_t *self);

/*
 * ===========================================================================
 *  Utility functions
 * ===========================================================================
 */

/**
 * Set the object @p self user data.
 *
 * @param[in]   self  A valid pointer to an osn_pppoe_t object
 * @param[in]   data  Pointer to user data
 */
void osn_pppoe_data_set(osn_pppoe_t *self, void *data);

/**
 * Get the object @p self user data. If no user data was set, NULL will
 * be returned.
 *
 * @param[in]   self  A valid pointer to an osn_pppoe_t object
 *
 * @return
 * Returns a pointer to user data previously set using @ref
 * osn_pppoe_data_set().
 */
void *osn_pppoe_data_get(osn_pppoe_t *self);

/*
 * ===========================================================================
 *  PPPoE status reporting
 * ===========================================================================
 */

/**
 * PPPoE link status reporting structure. This structure is used as a parameter
 * to a @ref osn_pppoe_status_fn_t type function. A status callback is typically
 * registered with the osn_pppoe_status_notify() function.
 */
struct osn_pppoe_status
{
    const char         *ps_ifname;      /**< Interface name */
    bool                ps_exists;      /**< The PPPoE interface was created */
    bool                ps_carrier;     /**< The PPPoE interface is ready to send/receive packets */
    osn_ip_addr_t       ps_local_ip;    /**< Local IP address */
    osn_ip_addr_t       ps_remote_ip;   /**< Remote IP address */
    int                 ps_mtu;         /**< MTU of the interface */
};

/**
 * Function callback type used for PPPoE status reporting. See
 * @ref osn_pppoe_status_notify for more details.
 */
typedef void osn_pppoe_status_fn_t(
        osn_pppoe_t *self,
        struct osn_pppoe_status *status);

/**
 * Register a function callback that will be used for asynchronous PPPoE link
 * status reporting. Depending on the implementation, the callback may be
 * invoked before osn_pppoe_apply() is called (before the configuration is
 * applied to the system).
 */
void osn_pppoe_status_notify(osn_pppoe_t *self, osn_pppoe_status_fn_t *fn);

/** @} OSN_PPPOE */
/** @} OSN */

#endif /* OSN_PPPOE_H_INCLUDED */
