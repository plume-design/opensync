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

#ifndef OSN_MLD_H_INCLUDED
#define OSN_MLD_H_INCLUDED

#include <stdbool.h>

#include "osn_types.h"
#include "osn_mcast.h"

/**
 * @file osn_mld.h
 *
 * @brief OpenSync MLD Configuration Abstraction
 *
 * @addtogroup OSN
 * @{
 *
 * @addtogroup OSN_MCAST
 * @{
 *
 * @defgroup OSN_MLD MLD
 *
 * OpenSync API for managing MLD configuration
 *
 * @{
 */

/*
 * ===========================================================================
 *  MLD configuration
 * ===========================================================================
 */

/**
 * OSN MLD object type
 *
 * This is an opaque type. The actual structure implementation is hidden and is
 * platform dependent. A new instance of the object can be obtained by calling
 * @ref osn_mld_new() and must be destroyed using @ref osn_mld_del().
 */
typedef struct osn_mld osn_mld_t;

/**
 * MLD version
 */
enum osn_mld_version
{
    OSN_MLDv1,
    OSN_MLDv2
};

/**
 * MLD snooping config structure. A structure of this type is used when
 * configuring snooping behavior of a bridge. See @ref osn_mld_snooping_set
 * for more details.
 */
struct osn_mld_snooping_config
{
    enum osn_mld_version        version;                /**< MLD version to use */
    bool                        enabled;                /**< Enable/Disable Snooping behavior
                                                             on the specified bridge */
    char                       *bridge;                 /**< Bridge where snooping should be used */
    char                       *static_mrouter;         /**< Specify Port where MLD reports should
                                                             be sent explicitly (even if querier is
                                                             not learned on that port) */
    char                      **mcast_exceptions;       /**< Multicast groups that should bypass
                                                             snooping behavior */
    int                         mcast_exceptions_len;   /**< Number of groups that should bypass
                                                             snooping behavior */
    enum osn_mcast_unknown_grp  unknown_group;          /**< Default forwarding behavior for mcast
                                                             groups that are not learned in snooping
                                                             table */
    int                         robustness_value;       /**< The Robustness value allows tuning
                                                             for the expected packet loss on a subnet */
    int                         max_groups;             /**< Max multicast groups that can be learned */
    int                         max_sources;            /**< Max sources allowed */
    bool                        fast_leave_enable;      /**< Immediately drop the stream when MLD
                                                             leave is received */
};

/**
 * MLD proxy config structure. A structure of this type is used when
 * configuring multicast proxy behavior. See @ref osn_mld_proxy_set for more
 * details.
 */
struct osn_mld_proxy_config
{
    bool    enabled;                /**< Enable/Disable MLD Proxy behavior */
    char   *upstream_if;            /**< Interface where MLD reports are proxied,
                                         and where multicast stream is expected */
    char   *downstream_if;          /**< Interface where Proxy is listening
                                         for MLD reports */
    char  **group_exceptions;       /**< Do not proxy specified multicast groups */
    int     group_exceptions_len;   /**< Number of not proxied multicast groups */
    char  **allowed_subnets;        /**< Proxy only Mcast groups for the
                                         specified source IP subnet */
    int     allowed_subnets_len;    /**< Number of allowed proxy subnets */
};

/**
 * MLD querier config structure. A structure of this type is used when
 * configuring a multicast querier. See @ref osn_mld_querier_set * for more
 * details.
 */
struct osn_mld_querier_config
{
    bool    enabled;                /**< Enable or disable MLD Querier */
    int     interval;               /**< The Query Interval is the interval
                                         between General Queries sent by the
                                         Querier */
    int     resp_interval;          /**< The Max Response Time inserted into
                                         the periodic General Queries */
    int     last_member_interval;   /**< The Last Member Query Interval is the Max
                                         Response Time inserted into Group-Specific
                                         Queries sent in response to Leave Group
                                         messages, and is also the amount of time
                                         between Group-Specific Query messages */
};

/**
 * Create a new instance of a MLD configuration object.
 *
 * @return
 * This function returns NULL if an error occurs, otherwise a valid @ref
 * osn_mld_t object is returned.
 */

osn_mld_t *osn_mld_new();

/**
 * Destroy a valid osn_mld_t object.
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
bool osn_mld_del(osn_mld_t *self);

/**
 * Apply the MLD configuration to the system.
 *
 * @param[in]   self    A valid pointer to an osn_mld_t object
 *
 * @return
 * This function returns true on success. On error, false is returned.
 */
bool osn_mld_apply(osn_mld_t *self);

/**
 * Set MLD Snooping configuration.
 *
 * @param[in]   self    A valid pointer to an osn_mld_t object
 * @param[in]   config  A pointer to an @ref osn_mld_snooping_config structure
 *
 * @return
 * This function returns true on success. On error, false is returned.
 */
bool osn_mld_snooping_set(
        osn_mld_t *self,
        struct osn_mld_snooping_config *config);

/**
 * Set MLD Proxy configuration.
 *
 * @param[in]   self    A valid pointer to an osn_mld_t object
 * @param[in]   config  A pointer to an @ref osn_mld_proxy_config structure
 *
 * @return
 * This function returns true on success. On error, false is returned.
 */
bool osn_mld_proxy_set(
        osn_mld_t *self,
        struct osn_mld_proxy_config *config);

/**
 * Set MLD Querier configuration.
 *
 * @param[in]   self    A valid pointer to an osn_mld_t object
 * @param[in]   config  A pointer to an @ref osn_mld_querier_config structure
 *
 * @return
 * This function returns true on success. On error, false is returned.
 */
bool osn_mld_querier_set(
        osn_mld_t *self,
        struct osn_mld_querier_config *config);

/**
 * Set other_config for platform specific multicast options; often required
 * for proper accelerator configuration.
 *
 * @param[in]   self            A valid pointer to an osn_mld_t object
 * @param[in]   other_config    Other config can specify platform specific
 *                              options for multicast. Can be NULL.
 *
 * @return
 * This function returns true on success. On error, false is returned.
 */
bool osn_mld_other_config_set(
        osn_mld_t *self,
        const struct osn_mcast_other_config *other_config);

bool osn_mld_update_iface_status(
        osn_mld_t *self,
        char *ifname,
        bool enable);

/** @} OSN_MLD */
/** @} OSN_MCAST */
/** @} OSN */

#endif /* OSN_MLD_H_INCLUDED */
