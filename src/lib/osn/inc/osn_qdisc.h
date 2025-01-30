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

#ifndef OSN_QDISC_H_INCLUDED
#define OSN_QDISC_H_INCLUDED

#include <stdbool.h>

#include "ds_tree.h"

/**
 * @file osn_qdisc.h
 * @brief OpenSync qdisc API
 *
 * @addtogroup OSN
 * @{
 */

/*
 * ===========================================================================
 *  Qdisc Configuration API
 * ===========================================================================
 */

/**
 * @defgroup OSN_QDISC Qdisc
 *
 * OpenSync Linux qdisc-based QoS API
 *
 * @{
 */

/**
 * Maximum osn_qdisc_params's string representation length (including the terminating \0).
 */
#define OSN_QDISC_STR_LEN 256

/**
 * OSN qdisc_cfg object type
 *
 * This is an opaque type. The actual structure implementation is hidden.
 * A new instance of the object can be obtained by calling
 * @ref osn_qdisc_cfg_new() and must be destroyed using @ref osn_qdisc_cfg_del().
 */
typedef struct osn_qdisc_cfg osn_qdisc_cfg_t;

/**
 * Qdisc or class definition parameters.
 */
struct osn_qdisc_params
{
    const char *oq_id; /**< qdisc or class ID:
                        *   if type==qdisc then this is a "handle" in the format
                        *   "major:" which specifies the qdisc id. if type==class
                        *   then this is a "class id" in the format "major:minor". */

    const char *oq_qdisc; /**< Qdisc name (well, type) */

    const char *oq_parent_id; /**< Parent qdisc or class ID or special value “root”. */

    const char *oq_params; /**  qdisc-specific parameters */

    bool oq_is_class; /**< True if this is a class definition
                       *   for a classfull qdisc, false if this
                       *   is a qdisc definition, either classless
                       *   or classful */

    bool _configured;

    ds_tree_t oq_tnode;
};

/**
 * Create a new qdisc_cfg object for an interface @p if_name.
 *
 * @param[in]   if_name  Interface that will be subject to qdisc configuration
 *
 * @return a valid @ref osn_qdisc_cfg_t on success, or NULL on error
 */
osn_qdisc_cfg_t *osn_qdisc_cfg_new(const char *if_name);

/**
 * Add a qdisc or class definition.
 *
 * This only adds a definition. No actual configuration on system yet.
 * Call @ref osn_qdisc_cfg_apply() after you've added all qdisc/class
 * definitions for this interface.
 *
 * The qdisc or class definitions can be added in any order. The correct
 * order that matters when configuring on the system will be resolved
 * automatically at config apply.
 *
 * @param[in]   self    A valid @ref osn_qdisc_cfg_t object
 * @param[in]   qdisc   qdisc or class definition
 *
 * @return true on success
 */
bool osn_qdisc_cfg_add(osn_qdisc_cfg_t *self, const struct osn_qdisc_params *qdisc);

/**
 * Apply the configuration.
 *
 * Proper order of qdisc/class configurations is automatically resolved.
 *
 * @param[in]   self    A valid @ref osn_qdisc_cfg_t object
 *
 * @return true on success
 */
bool osn_qdisc_cfg_apply(osn_qdisc_cfg_t *self);

/**
 * Reset/clear the configuration from the system.
 *
 * @param[in]   self    A valid @ref osn_qdisc_cfg_t object
 *
 * @return true on success
 */
bool osn_qdisc_cfg_reset(osn_qdisc_cfg_t *self);

/**
 * Delete the qdisc_cfg object and any associated resources.
 *
 * If qdisc/class configuration was applied to the system, the
 * system configuration is cleared first.
 *
 * @param[in]   self    A valid @ref osn_qdisc_cfg_t object
 *
 * @return true on success
 *
 */
bool osn_qdisc_cfg_del(osn_qdisc_cfg_t *self);

/**
 * Clone a @ref osn_qdisc_params object.
 *
 * @param[in]   qdisc   The object to clone.
 *
 * @return cloned object
 */
struct osn_qdisc_params *osn_qdisc_params_clone(const struct osn_qdisc_params *qdisc);

/**
 * Delete a @ref osn_qdisc_params object.
 *
 * param[in]    qdisc   The object to delete
 */
void osn_qdisc_params_del(struct osn_qdisc_params *qdisc);

/**
 * Convert struct osn_qdisc_params to its string representation.
 *
 * Macro helpers for printf() formatting:
 *
 * See @ref PRI_osn_qdisc_params for more info.
 */
#define PRI_osn_qdisc_params    "%s"
#define FMT_osn_qdisc_params(x) (__FMT_osn_qdisc_params((char[OSN_QDISC_STR_LEN]){0}, OSN_QDISC_STR_LEN, &(x)))
char *__FMT_osn_qdisc_params(char *buf, size_t sz, const struct osn_qdisc_params *qdisc);

#endif /* OSN_QDISC_H_INCLUDED */
