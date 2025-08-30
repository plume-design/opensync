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

#ifndef OSN_ROUTE_RULE_H_INCLUDED
#define OSN_ROUTE_RULE_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

#include "const.h"
#include "osn_types.h"

/**
 * @file osn_route_rule.h
 *
 * @brief OpenSync Policy Routing Rules API
 *
 * @addtogroup OSN
 * @{
 *
 * @defgroup OSN_ROUTE_RULE OpenSync Policy Routing Rules APIs
 *
 * OpenSync Policy Routing Rules APIs
 *
 * @{
 */

/*
 * ===========================================================================
 * OpenSync Policy Routing Rules API
 * ===========================================================================
 */

/**
 * OSN Route Rule object type.
 *
 * This is an opaque type. The actual structure implementation is hidden as
 * it depends on the platform.
 *
 * A new instance of the object can be obtained by calling
 * @ref osn_route_rule_new() and must be destroyed using @ref
 * osn_route_rule_del().
 */
typedef struct osn_route_rule osn_route_rule_t;

/**
 * Policy route rule type.
 */
enum osn_route_rule_type
{
    OSN_ROUTERULE_TYPE_UNICAST,       /** A "normal" rule. Return the route found
                                       *  in the routing table referenced by the rule. */

    OSN_ROUTERULE_TYPE_BLACKHOLE,     /** Silently drop the packet. */
    OSN_ROUTERULE_TYPE_UNREACHABLE,   /** Generate a 'Network is unreachable' error. */
    OSN_ROUTERULE_TYPE_PROHIBIT,      /** Generate 'Communication is administratively prohibited' error. */
    OSN_ROUTERULE_TYPE_MAX
};

/**
 * Policy route rule selector.
 */
struct osn_route_rule_selector
{
    bool              rs_negate_rule;   /** Match the opposite. */

    osn_ipany_addr_t  rs_src;           /** Source address or prefix to match. */
    bool              rs_src_set;

    osn_ipany_addr_t  rs_dst;           /** Destination address or prefix to match. */
    bool              rs_dst_set;

    char              rs_input_if[C_IFNAME_LEN];  /** Incoming interface to match. If the interface
                                                    * is loopback the rule only matches packets originating
                                                    * from this host. */

    char              rs_output_if[C_IFNAME_LEN]; /** Outgoing interface to match. This is only available
                                                    * for packets originating from local sockets that are bound to
                                                    * a device */

    uint32_t          rs_fwmark;        /** Select the fwmark value to match.
                                          * value=0 is reserved: no fwmark to match configured */
    bool              rs_fwmark_set;

    uint32_t          rs_fwmask;       /** Mask for netfilter mark. */

    bool              rs_fwmask_set;
};

/**
 * Policy route rule action.
 */
struct osn_route_rule_action
{
    uint32_t    ra_lookup_table;           /**  Table ID to lookup. If not set (i.e. if set to 0),
                                             *  defaults to lookup table main. */

    unsigned    ra_suppress_prefixlength;   /** Reject routing decisions that have a
                                              * prefix length of this number or less. */

    bool        ra_suppress_prefixlength_set;
};

/**
 * Policy route rule config.
 *
 * Use @ref OSN_ROUTE_RULE_CFG_INIT to initialize it.
 */
typedef struct osn_route_rule_cfg
{
    int                             rc_addr_family;   /** AF_INET (default) or AF_INET6 */

    enum osn_route_rule_type        rc_type;          /** Route rule type.
                                                        * Default: @ref OSN_ROUTERULE_TYPE_UNICAST */

    uint32_t                        rc_priority;      /** Route rule priority. A number from 1 to 2^32−1.
                                                        * 0 is highest priority, but reserved for the local
                                                        * table. If set to 0, the priority is considered unset
                                                        * and implementation may choose an arbitrary priority
                                                        * for this rule. */

    struct osn_route_rule_selector  rc_selector;      /** Policy routing rule selector. If no selector
                                                        * attributes configured, every packet matches. */

    struct osn_route_rule_action    rc_action;        /** Policy routing rule action. That is the action to
                                                        * perform if rule selector matches. */

} osn_route_rule_cfg_t;

/**
 * Initializer for a route rule config structure (@ref osn_route_rule_cfg_t)
 */
#define OSN_ROUTE_RULE_CFG_INIT (osn_route_rule_cfg_t)    \
{                                                         \
    .rc_addr_family = AF_INET,                            \
    .rc_type = OSN_ROUTERULE_TYPE_UNICAST,                \
}

/**
 * Create a new instance of osn_route_rule_t object.
 *
 * @return a valid osn_route_rule_t object or NULL on error.
 */
osn_route_rule_t *osn_route_rule_new();

/**
 * Destroy a valid osn_route_rule_t object.
 *
 * @param[in] self          A valid @ref osn_route_rule_t object
 *
 * @return true on success. Regardless of the return value after this function
 *         returns the input parameter should be considered invalid and must
 *         no longer be used.
 */
bool osn_route_rule_del(osn_route_rule_t *self);

/**
 * Add a policy routing rule.
 *
 * @note After adding rules with this function you should call @ref osn_route_rule_apply()
 * to ensure the configuration has been committed to the system.
 *
 * @param[in] self            A valid @ref osn_route_rule_t object
 *
 * @param[in] route_rule_cfg  Route rule configuration
 *
 * @return true on success
 */
bool osn_route_rule_add(osn_route_rule_t *self, const osn_route_rule_cfg_t *route_rule_cfg);

/**
 * Remove/delete a policy routing rule.
 *
 * @note After removing rules with this function you should call @ref osn_route_rule_apply()
 * to ensure the configuration has been committed to the system.
 *
 * @param[in] self            A valid @ref osn_route_rule_t object
 *
 * @param[in] route_rule_cfg  Route rule configuration
 *
 * @return true on success
 */
bool osn_route_rule_remove(osn_route_rule_t *self, const osn_route_rule_cfg_t *route_rule_cfg);

/**
 * Apply the policy routing rule configuration parameters to the running system.
 *
 * The behaviour of this function is implementation dependent. Implementation
 * may well choose to not do any buffered adding or deleting of route rules.
 * In that case this function would be a no-op.
 *
 * For portability you should call this function to ensure that route rules have
 * been added or removed from the system.
 *
 * @param[in] self           A valid @ref osn_ipsec_t object
 *
 * @return true on success
 */
bool osn_route_rule_apply(osn_route_rule_t *self);


#endif /* OSN_ROUTE_RULE_H_INCLUDED */
