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

#ifndef QOSM_INTERNAL_H_INCLUDED
#define QOSM_INTERNAL_H_INCLUDED

#include <stdbool.h>

#include "const.h"
#include "evx.h"
#include "log.h"
#include "osn_types.h"
#include "osn_qos.h"
#include "osn_qdisc.h"
#include "osn_adaptive_qos.h"
#include "ovsdb_table.h"
#include "reflink.h"
#include "ds_map_str.h"

#define QOSM_DEBOUNCE_MIN   0.250       /**< 250ms default/minimum timer */
#define QOSM_DEBOUNCE_MAX   3.000       /**< 3 seconds max debounce timer */

/*
 * ===========================================================================
 *  IP_Interface table
 * ===========================================================================
 */
struct qosm_ip_interface
{
    char                ipi_ifname[C_IFNAME_LEN];   /**< Interface name */
    ovs_uuid_t          ipi_uuid;                   /**< UUID of this object */
    reflink_t           ipi_reflink;                /**< Reflink of this object */
    struct qosm_interface_qos
                       *ipi_interface_qos;          /**< Interface_QoS structure */
    reflink_t           ipi_interface_qos_reflink;  /**< Reflink to Interface_QoS */
    osn_qos_t          *ipi_qos;                    /**< OSN QoS configuration object */
    osn_qdisc_cfg_t    *ipi_qdisc_cfg;              /**< OSN qdisc_cfg configuration object */
    ds_tree_node_t      ipi_tnode;                  /**< Tree node */
};

void qosm_ip_interface_init(void);
struct qosm_ip_interface *qosm_ip_interface_get(ovs_uuid_t *uuid);

/*
 * ===========================================================================
 *  Interface_QoS table
 * ===========================================================================
 */
struct qosm_interface_qos
{
    ovs_uuid_t          qos_uuid;                   /**< UUID of this object */
    reflink_t           qos_reflink;                /**< Reflink of this object */

    struct qosm_interface_queue
                      **qos_interface_queue;        /**< Array of pointers to Interface_Queue objects */
    int                 qos_interface_queue_len;    /**< Length of the queues array */
    reflink_t           qos_interface_queue_reflink;/**< Reflink to Interface_Queue */

    struct qosm_linux_queue
                      **qos_linux_queue;            /**< Array of pointers to Linux_Queue objects */
    int                 qos_linux_queue_len;        /**< Length of the lnx_queues array */
    reflink_t           qos_linux_queue_reflink;    /**< Reflink to Linux_Queue */

    ds_map_str_t       *qos_adaptive_qos;           /**< Per-interface adaptive QoS key-value map configuration */

    ds_tree_node_t      qos_tnode;                  /**< Tree node */
};

void qosm_interface_qos_init(void);
struct qosm_interface_qos *qosm_interface_qos_get(ovs_uuid_t *uuid);
bool qosm_interface_qos_set_status(struct qosm_interface_qos *qos, const char *status);

/*
 * ===========================================================================
 *  Interface_Queue table
 * ===========================================================================
 */
struct qosm_interface_queue
{
    ovs_uuid_t          que_uuid;                   /**< UUID of this object */
    reflink_t           que_reflink;                /**< Reflink of this object */
    int                 que_priority;               /**< Queue priority */
    int                 que_bandwidth;              /**< Queue bandwidth in kbit/s */
    int                 que_bandwidth_ceil;         /**< Queue ceil bandwidth in kbit/s */
    char                que_tag[32];                /**< Queue tag */
    ds_tree_node_t      que_tnode;                  /**< Tree node */
    struct osn_qos_other_config
                        que_other_config;           /**< Queue configuration */
};

/*
 * ===========================================================================
 *  Linux_Queue table
 * ===========================================================================
 */
struct qosm_linux_queue
{
    ovs_uuid_t          que_uuid;                   /**< UUID of this object */
    reflink_t           que_reflink;                /**< Reflink of this object */

    bool                que_is_class;               /**< True if this is a class definition
                                                     *   for a classfull qdisc, false if this
                                                     *   is a qdisc definition, either classless
                                                     *   or classful */

    char                que_qdisc[32];              /**< Qdisc name (well, type) */

    char                que_parent_id[32];          /**< Parent qdisc or class ID or special value “root”. */

    char                que_id[32];                 /**< qdisc or class ID:
                                                     *   if type==qdisc then this is a "handle" in the format
                                                     *   "major:" which specifies the qdisc id. if type==class
                                                     *   then this is a "class id" in the format "major:minor". */

    char                que_params[256];            /**  qdisc-specific parameters */

    bool                que_applied;                /**< Successfully aplied? */

    ds_tree_node_t      que_tnode;                  /**< Tree node */

};

#define QOSM_ADAPTIVE_QOS_MAX_REFLECTORS    64

/*
 * ===========================================================================
 *  AdaptiveQoS table  - global adaptive QoS configuration
 * ===========================================================================
 */
struct qosm_adaptive_qos_cfg
{
    osn_ipany_addr_t    reflectors[QOSM_ADAPTIVE_QOS_MAX_REFLECTORS]; /**< Custom reflectors list */

    int                 num_reflectors;     /**< Number of custom reflectors */

    bool                rand_reflectors;    /**< Enable or disable randomization of reflectors at startup */
    int                 ping_interval;      /**< Interval time for ping, milliseconds */

    int                 num_pingers;        /**< Number of pingers to maintain */

    int                 active_thresh;      /**< Threshold in Kbit/s below which dl/ul is considered idle */

    ds_map_str_t       *other_config;

};

void qosm_interface_queue_init(void);
struct qosm_interface_queue *qosm_interface_queue_get(ovs_uuid_t *uuid);
bool qosm_interface_queue_set_status(
        struct qosm_interface_queue *que,
        bool status,
        struct osn_qos_queue_status *qqs);

void qosm_linux_queue_init(void);
struct qosm_linux_queue *qosm_linux_queue_get(ovs_uuid_t *uuid);
bool qosm_linux_queue_set_status(struct qosm_linux_queue *lnx_queue, const char *status);
bool qosm_linux_queue_set_status_all(struct qosm_interface_qos *qos, const char *status);

void qosm_adaptive_qos_init(void);
struct qosm_adaptive_qos_cfg *qosm_adaptive_qos_cfg_get(void);

#endif /* QOSM_INTERNAL_H_INCLUDED */
