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

#ifndef OSN_QOS_H_INCLUDED
#define OSN_QOS_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

/**
 * @file osn_qos.h
 * @brief OpenSync QoS
 *
 * @addtogroup OSN
 * @{
 */

/*
 * ===========================================================================
 *  QoS Configuration API
 * ===========================================================================
 */

/**
 * @defgroup OSN_QOS QoS
 *
 * OpenSync QoS API
 *
 * @{
 */

/** Maximum length of a queue classify tag including the string terminator */
#define OSN_QOS_QUEUE_CLASS_LEN  32

/**
 * OSN QoS object type
 *
 * This is an opaque type. The actual structure implementation is hidden and is
 * platform dependent. A new instance of the object can be obtained by calling
 * @ref osn_qos_new() and must be destroyed using @ref osn_qos_del().
 */
typedef struct osn_qos osn_qos_t;

/**
 * Key/Value pairs for struct @ref osn_qos_other_config
 */
struct osn_qos_oc_kv_pair
{
    char    *ov_key;        /**< Value key(name) */
    char    *ov_value;      /**< Value data */
};

/**
 * Structure for passing advanced parameters to QoS queues
 */
struct osn_qos_other_config
{
    int                         oc_len;     /**< Length of the config array */
    struct osn_qos_oc_kv_pair  *oc_config;  /**< Config array */
};

/**
 * QoS queue configuration status -- this is returned by osn_qos_queue_begin()
 */

struct osn_qos_queue_status
{
    uint32_t    qqs_fwmark;                         /**< The firewall mark */
    char        qqs_class[OSN_QOS_QUEUE_CLASS_LEN]; /**< Class definiton, to be used with CLASSIFY iptables rules */
};

/**
 * Create new QoS object for interface @p ifname.
 *
 * @param[in]   ifname  Interface that will be subject to QoS configuration
 *
 * @return
 * This function returns NULL if an error occurs, otherwise a valid @ref
 * osn_qos_t object is returned.
 */
osn_qos_t* osn_qos_new(const char *ifname);

/**
 * Delete QoS object and any related resources
 *
 * @param[in]   qos     A valid pointer to an osn_qos_t object
 */
void osn_qos_del(osn_qos_t *qos);

/**
 * Apply the QoS configuration to the system
 *
 * @param[in]   qos     A valid pointer to an osn_qos_t object
 *
 * @return
 * This function returns true if the action was successful, false otherwise.
 */
bool osn_qos_apply(osn_qos_t *qos);

/**
 * Begin a QoS discipline definition
 *
 * This function starts a QoS discipline definition. For disciplines that
 * support multiple queues, they may be defined between a @p osn_qos_begin() and
 * @p osn_qos_end() functions.
 *
 * If called between a @p osn_qos_queue_begin() and @p osn_qos_queue_end() and
 * if the parent discipline supports it, the discipline is attached to the
 * parent queue.
 *
 * Disciplines cannot be nested directly, but must be attached to other queues.
 *
 * @param[in]   qos             A valid pointer to an osn_qos_t object
 * @param[in]   other_config    Other config can specify advanced/specific
 *                              configuration for the queuing discipline.
 *                              Can be NULL.
 *
 * @return
 * This function returns true on success, false otherwise.
 */
bool osn_qos_begin(osn_qos_t *qos, struct osn_qos_other_config *other_config);

/**
 * End a QoS discipline definition
 *
 * @return
 * This function returns true on success, false otherwise.
 */
bool osn_qos_end(osn_qos_t *qos);

/**
 * Start a new QoS queue definition.
 *
 * For disciplines that support queue nesting, subordinate queues may be
 * defined between a @p osn_qos_queue_begin() and @p osn_qos_queue_end()
 * function calls.
 *
 * QoS disciplines can be attached to queues, if the current QoS discipline
 * supports it.
 *
 * @param[in]   qos             A valid pointer to an osn_qos_t object
 * @param[in]   priority        Queue priority or a negative value if not
 *                              present
 * @param[in]   bandwidth       Queue bandwidth in kbit/s or a negative value if
 *                              not present
 * @param[in]   tag             Queue tag; this may be used to identify shared
 *                              queues (queues with the same tag are shared).
 *                              The implementation may choose to ignore this.
 * @param[in]   other_config    Other config can specify advanced/specific
 *                              configuration for the queuing. Can be NULL.
 *
 * @param[out]  qqs             Queue configuration status
 *
 * @note
 * For maximum portability queues must be defined from higher to lower priority.
 * This is required by some backends to properly calculate the fw mark.
 *
 * @return
 * This function returns true if successful, false otherwise. The queue status
 * is returned in the @p qqs structure.
 */
bool osn_qos_queue_begin(
        osn_qos_t *qos,
        int priority,
        int bandwidth,
        const char *tag,
        const struct osn_qos_other_config *other_config,
        struct osn_qos_queue_status *qqs);

/**
 * End a QoS queue definition
 *
 * @return
 * This function returns true on success, false otherwise.
 */
bool osn_qos_queue_end(osn_qos_t *qos);

/** @} OSN_QOS */
/** @} OSN */

#endif /* OSN_QOS_H_INCLUDED */
