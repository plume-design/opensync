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

#ifndef OSN_MAP_H_INCLUDED
#define OSN_MAP_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

#include "osn_types.h"
#include "ds_dlist.h"

/**
 * @file osn_map.h
 *
 * @brief OpenSync MAP (MAP-T/MAP-E) API
 *
 * @addtogroup OSN
 * @{
 *
 * @defgroup OSN_MAP OpenSync MAP-T/MAP-E
 *
 * OpenSync MAP-T/MAP-E APIs
 *
 * @{
 */

/**
 * Maximum number of MAP port sets.
 *   @see struct osn_map_portset
 */
#define OSN_MAP_PORT_SETS_MAX    256

/**
 * OSN MAP object type.
 *
 * This is an opaque type. The actual structure implementation is hidden as
 * it may depend on the platform.
 *
 * A new instance of the object can be obtained by calling
 * @ref osn_map_new() and must be destroyed using @ref
 * osn_map_del().
 */
typedef struct osn_map osn_map_t;

/**
 * MAP type.
 */
enum osn_map_type
{
    OSN_MAP_TYPE_NOT_SET,
    OSN_MAP_TYPE_MAP_T,         /** MAP-T */
    OSN_MAP_TYPE_MAP_E,         /** MAP-E */
    OSN_MAP_TYPE_MAX
};

/**
 * MAP rule.
 *
 * - The actual MAP rule triplet/fourplet,
 * - DMR
 * - and (optional) explicit port parameters.
 */
typedef struct
{
    /* MAP rule: */
    osn_ip6_addr_t    om_ipv6prefix;      /** Rule IPv6 prefix */
    osn_ip_addr_t     om_ipv4prefix;      /** Rule IPv4 prefix */
    int               om_ea_len;          /** EA-bits length   */
    int               om_psid_offset;     /** PSID offset      */

    /* Should this rule also be used for forwarding (FMR) ?    */
    bool              om_is_fmr;          /** FMR flag         */

    /* DMR (default mapping rule):
     *  - MAP-T: IPv6 prefix
     *  - MAP-E: IPv6 address of the BR (Border Relay)
     */
    osn_ip6_addr_t    om_dmr;             /** DMR              */

    /* Optional explicit ports params: */
    int               om_psid;            /** PSID             */
    int               om_psid_len;        /** PSID length      */

    ds_dlist_node_t   om_dnode;

} osn_map_rule_t;

/**
 * Initialize osn_map_rule_t object.
 */
#define OSN_MAP_RULE_INIT (osn_map_rule_t)   \
{                                            \
    .om_ea_len = -1,                         \
    .om_psid = -1,                           \
    .om_psid_len = 0,                        \
}

/**
 * MAP rule list object.
 */
typedef struct osn_map_rulelist
{
    ds_dlist_t        rl_rule_list;       /** List of (osn_map_rule_t) */

}  osn_map_rulelist_t;

/**
 * Iterate through MAP rule list.
 */
#define osn_map_rulelist_foreach(rule_list, p) \
    ds_dlist_foreach(&(rule_list)->rl_rule_list, (p))

/**
 * Create a new MAP rule list object.
 */
osn_map_rulelist_t *osn_map_rulelist_new();

/**
 * Add a MAP rule to a MAP rule list.
 */
void osn_map_rulelist_add_rule(osn_map_rulelist_t *rule_list, const osn_map_rule_t *map_rule);

/**
 * Delete the MAP rule list object (along with all its MAP rules on the list).
 */
void osn_map_rulelist_del(osn_map_rulelist_t *rule_list);

/**
 * Check if the MAP rule list object is empty.
 */
bool osn_map_rulelist_is_empty(const osn_map_rulelist_t *rule_list);

/**
 * Create a copy of the specified rule list object.
 */
osn_map_rulelist_t *osn_map_rulelist_copy(const osn_map_rulelist_t *rule_list);

/**
 * MAP port set.
 *
 * Represents one port set (one port range).
 */
struct osn_map_portset
{
    uint16_t    op_from;   /* Port number from */
    uint16_t    op_to;     /* Port number to   */
};

/**
 * Create a new @ref osn_map_t object.
 *
 * @param[in] if_name   MAP interface name and name for this MAP configuration.
 *
 *                      The string must follow the OS interface naming rules.
 *
 * @return  on success a valid @ref osn_map_t object is returned, on
 *          error NULL is returned.
 */
osn_map_t *osn_map_new(const char *if_name);

/**
 * Set MAP type.
 *
 * @param[in] self      A valid pointer to @ref osn_map_t object.
 * @param[in] map_type  MAP type
 *
 * @return true on success
 */
bool osn_map_type_set(osn_map_t *self, enum osn_map_type map_type);

/**
 * Set a list of MAP rules.
 *
 * @param[in] self           A valid @ref osn_map_t object.
 * @param[in] rule_list      A list of MAP rules.
 *
 * @return true on success
 */
bool osn_map_rule_list_set(osn_map_t *self, osn_map_rulelist_t *rule_list);

/**
 * Set one BMR MAP rule.
 * A convenience function that sets a MAP rule list with only one MAP rule.
 *
 * @param[in] self           A valid @ref osn_map_t object.
 * @param[in] bmr            A MAP rule.
 *
 * @return true on success
 */
bool osn_map_rule_set(osn_map_t *self, const osn_map_rule_t *bmr);

/**
 * Set end-user IPv6 prefix.
 *
 * Typically, but not necessarily, the end-user IPv6 prefix configured for MAP would be
 * a delegated IPv6 prefix (IA_PD).
 *
 * @param[in] self           A valid @ref osn_map_t object.
 * @param[in] ipv6_prefix    End-user IPv6 prefix to set.
 *
 * @return true on success
 */
bool osn_map_enduser_IPv6_prefix_set(osn_map_t *self, const osn_ip6_addr_t *ipv6_prefix);

/**
 * Use legacy MAP RFC draft3 when calculating MAP IPv6 address
 * (it's interface-identifier part).
 *
 * @param[in] self          A valid @ref osn_map_t object.
 * @param[in] use_draft3    If true MAP IPv6 address will be calculated according to legacy
 *                          MAP RFC draft3.
 *
 * @return  true on success
 */
bool osn_map_use_legacy_map_draft3(osn_map_t *self, bool use_draft3);

/**
 * Set this device uplink interface name.
 *
 * @param[in] self            A valid @ref osn_map_t object.
 * @param[in] uplink_if_name  Uplink interface name.
 *
 * @return true on success
 */
bool osn_map_uplink_set(osn_map_t *self, const char *uplink_if_name);

/**
 * Apply the MAP configuration parameters to the running system.
 *
 * After you set MAP configuration parameters you must call this
 * function for the configuration to take effect.
 *
 * When this function is called, first the set configuration parameters are validated:
 *   - End-user IPv6 prefix must be configured.
 *   - At least one MAP rule must be configured.
 *   - From the configured MAP rules a rule matching to End-user IPv6 prefix must be found.
 *   - If no matching MAP rule found, error is returned.
 *
 * The matching MAP rule is then used to calculate derived MAP parameters: MAP IPv4 address,
 * MAP IPv6 address, PSID, PSID length and port sets.
 *
 * For some MAP types (MAP-T) the underlying backend platform implementation may be configured as well.
 *
 * This function may also configure any additional platform settings or configurations (such us
 * IPv6 routes, NDP proxy settings etc) if needed.
 *
 * If the configuration is applied successfully, success is returned and calculated MAP parameters
 * can be obtained by @ref osn_map_psid_get(), @ref osn_map_ipv4_addr_get(),
 * @ref osn_map_ipv6_addr_get(). and @ref osn_map_port_sets_get(). The matching MAP rule can be
 * obtained by @ref osn_map_rule_matched_get().
 *
 * @param[in]  self           A valid @ref osn_map_t object
 *
 * @return true on success
 */
bool osn_map_apply(osn_map_t *self);

/**
 * Get the matching MAP rule i.e. the configured MAP rule that matched to the configured End-user
 * IPv6 prefix.
 *
 * @param[in]  self           A valid @ref osn_map_t object.
 * @param[out] bmr            The matched MAP rule is returned here.
 *
 * @return true on success (configuration applied successfully and there was a matching MAP rule)
 */
bool osn_map_rule_matched_get(osn_map_t *self, osn_map_rule_t *bmr);

/**
 * Get the PSID.
 *
 * @param[in]  self           A valid @ref osn_map_t object.
 * @param[out] psid_len       PSID length.
 * @param[out] psid           PSID.
 *
 * @return true on success (if configuration applied successfully)
 */
bool osn_map_psid_get(osn_map_t *self, int *psid_len, int *psid);

/**
 * Get the MAP IPv4 address.
 *
 * @param[in]  self           A valid @ref osn_map_t object.
 * @param[out] map_ipv4_addr  MAP IPv4 address is returned here.
 *
 * @return true on success (if configuration applied successfully and MAP IPv4 address calculated)
 */
bool osn_map_ipv4_addr_get(osn_map_t *self, osn_ip_addr_t *map_ipv4_addr);

/**
 * Get the MAP IPv6 address.
 *
 * @param[in]  self           A valid @ref osn_map_t object.
 * @param[out] map_ipv6_addr  MAP IPv6 address is returned here.
 *
 * @return true on success (if configuration applied successfully and MAP IPv6 address calculated)
 */
bool osn_map_ipv6_addr_get(osn_map_t *self, osn_ip6_addr_t *map_ipv6_addr);

/**
 * Get the port sets.
 *
 * @param[in]  self           A valid @ref osn_map_t object.
 * @param[out] portsets       An array of @ref osn_map_portset of at least @ref OSN_MAP_PORT_SETS_MAX
 *                            length. The port sets are returned here.
 * @param[out] num            The number of port sets are returned here.
 *
 * @return true on success (if configuration applied successfully and port sets calculated)
 */
bool osn_map_port_sets_get(osn_map_t *self, struct osn_map_portset *portsets, unsigned *num);

/**
 * Destroy a valid osn_map_t object.
 *
 * If this object configuration was applied and if any resources were
 * created they will be freed and removed from the system.
 *
 * @param[in] self   a valid pointer to @ref osn_map_t object
 *
 * @return true on success. This function may fail if there was an error
 *         deconfiguring MAP resources in the system.
 *
 *         After return of this function (regardless of its return value)
 *         the input parameter (osn_map_t object) should be considered invalid and
 *         must no longer be used.
 */
bool osn_map_del(osn_map_t *self);

/** @} OSN_MAP */
/** @} OSN */

#endif /* OSN_MAP_H_INCLUDED */