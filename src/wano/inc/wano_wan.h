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

#ifndef WANO_WAN_H_INCLUDED
#define WANO_WAN_H_INCLUDED

#include <stdint.h>

#include "const.h"
#include "ds_dlist.h"
#include "osn_types.h"
#include "ovsdb.h"

enum wano_wan_config_status
{
    WC_STATUS_NONE = 0,
    WC_STATUS_SUCCESS = 1,
    WC_STATUS_ERROR = 2,
    WC_STATUS_LAST = WC_STATUS_ERROR
};

enum wano_wan_config_type
{
    WC_TYPE_PPPOE,
    WC_TYPE_VLAN,
    WC_TYPE_STATIC_IPV4,
    WC_TYPE_DHCP
};

struct wano_wan_config_pppoe
{
    char    wc_username[C_PASSWORD_LEN];    /**< PPPoE username */
    char    wc_password[C_PASSWORD_LEN];    /**< PPPoE password */
};

struct wano_wan_config_vlan
{
    int     wc_vlanid;                      /**< VLAN ID */
    int     wc_qos;                         /**< QoS tag */
};

struct wano_wan_config_static_ipv4
{
    osn_ip_addr_t       wc_ipaddr;          /**< IPv4 Address */
    osn_ip_addr_t       wc_netmask;         /**< IPv4 subnet */
    osn_ip_addr_t       wc_gateway;         /**< IPv4 gateway  */
    osn_ip_addr_t       wc_primary_dns;     /**< Primary DNS */
    osn_ip_addr_t       wc_secondary_dns;   /**< Secondary DNS */
};

/*
 * Structure representing single WAN configuration entry
 */
struct wano_wan_config
{
    bool                        wc_enable;          /**< True whether configuration enabled */
    int                         wc_priority;        /**< Configuration prirority, higher wins */
    enum wano_wan_config_type   wc_type;            /**< WAN configuration type */
    union
    {
        struct wano_wan_config_pppoe        wc_type_pppoe;
        struct wano_wan_config_vlan         wc_type_vlan;
        struct wano_wan_config_static_ipv4  wc_type_static_ipv4;
    };
};

/*
 * WAN object
 */
struct wano_wan
{
    int64_t             ww_priority;                    /**< Current priority */
    int64_t             ww_next_priority;               /**< Priority used for calculation of the next priority */
    bool                ww_do_pause;                    /**< Action to execute in the pause callback */
    bool                ww_is_paused;                   /**< True if paused */
    int                 ww_rollover;                    /**< Rollover count */
    ds_tree_t           ww_status_list;                 /**< List of WAN statuses */
    ds_dlist_node_t     ww_dnode;                       /**< Linked list node */
};

typedef struct wano_wan wano_wan_t;

/*
 * Initialize a WAN object
 */
void wano_wan_init(wano_wan_t *ww);

/*
 * Deinitialize a WAN object
 */
void wano_wan_fini(wano_wan_t *ww);

/*
 * Resetting an object begins processing WAN configuration type from the top
 */
void wano_wan_reset(wano_wan_t *ww);

/*
 * This is similar to a reset, except it flags the WAN configuration as being
 * rolled-over. This function is typically used when the pipeline needs to be
 * aborted, for example, when an ethernet client has been detected.
 *
 * WANO may handle rolled-over configurations somewhat differently, as this
 * means that all the current WAN configurations were exhausted and none has
 * been able to provision WAN successfully.
 */
void wano_wan_rollover(wano_wan_t *wan);

/*
 * Return the current rollover count
 */
int wano_wan_rollover_get(wano_wan_t *wan);

/*
 * Move to the next WAN configuration
 */
void wano_wan_next(wano_wan_t *ww);

/*
 * Signal whether WAN processing for the current WAN object has been stopped.
 */
void wano_wan_pause(wano_wan_t *wan, bool pause);

/*
 * Reset current WAN status
 */
void wano_wan_status_reset(wano_wan_t *ww);

/*
 * Set the status of the current WAN type
 */
void wano_wan_status_set(
        wano_wan_t *ww,
        enum wano_wan_config_type type,
        enum wano_wan_config_status status);

/*
 * Get the current WAN configuration (per type)
 */
bool wano_wan_config_get(wano_wan_t *ww, enum wano_wan_config_type type, struct wano_wan_config *wc);

#endif /* WANO_WAN_H_INCLUDED */
