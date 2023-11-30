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

#ifndef OSN_MAP_V6PLUS_H_INCLUDED
#define OSN_MAP_V6PLUS_H_INCLUDED

#include "osn_map.h"

#include "const.h"

/**
 * @file osn_map_v6plus.h
 *
 * @brief OpenSync MAP "v6 Plus" API
 *
 * @addtogroup OSN
 * @{
 *
 * @addtogroup OSN_MAP
 * @{
 *
 * @defgroup OSN_MAP_V6PLUS OpenSync MAP "v6 Plus" API
 *
 * OpenSync MAP "v6 Plus" APIs.
 *
 * "v6 Plus" is the name of a service that provides connectivity to IPv4 services over IPv6
 * (IPv4 over IPv6) with MAP-E and non-standard method of MAP rules acquisition -- via a
 * MAP rules distribution server (HTTPS API endpoint).
 *
 * OpenSync provides a generic MAP-T/MAP-E implementation.
 *
 * The APIs here implement the "v6 Plus"-specific method of fetching MAP rules from a
 * HTTPS API endpoint, fetching upstream NTT HGW status and reporting operation status
 * to v6plus API endpoints.
 *
 * @{
 */

struct osn_map_v6plus_cfg
{
    char     vp_endpoint_url[1024];      /** HTTP(S) API endpoint URL */

    char     vp_user_id[64+1];           /** 32 single-byte alphanumeric character unique user ID */

};

typedef struct
{
    osn_map_rulelist_t    pl_rulelist;        /** Rule list. (parent object) */

    char                  pl_user_id[64+1];   /** 32 single-byte alphanumeric character unique user ID */

    char                 *pl_raw_str;

}  osn_map_v6plus_rulelist_t;

/**
 * NTT HGW status.
 */
typedef enum
{
    OSN_MAP_V6PLUS_UNSET,
    OSN_MAP_V6PLUS_UNKNOWN,              /** HGW status unknown, or not yet known, or no HGW detected */
    OSN_MAP_V6PLUS_MAP_OFF,              /** Running under HGW, v6plus MAP turned OFF in HGW */
    OSN_MAP_V6PLUS_MAP_ON                /** Running under HGW, v6plus MAP turned ON in HGW */

} osn_map_v6plus_hgw_status_t;

typedef enum
{
    OSN_MAP_V6PLUS_ACTION_STARTED = 1,   /** v6plus operation started/running */
    OSN_MAP_V6PLUS_ACTION_STOPPED        /** v6plus operation stopped */

} osn_map_v6plus_status_action_t;

typedef enum
{
    OSN_MAP_V6PLUS_REASON_NORMAL_OPERATION = 0,   /** Normal operation */
    OSN_MAP_V6PLUS_REASON_MANUAL_OPERATION,       /** Manual operation (operation start/stop by user) */
    OSN_MAP_V6PLUS_REASON_ADDRESS_CHANGE,         /** Address change (start/stop) */
    OSN_MAP_V6PLUS_REASON_RULE_MISMATCH,          /** MAP rule mismatch (stop) */
    OSN_MAP_V6PLUS_REASON_RUNNING_ON_HGW          /** v6plus running on HGW (stop) */

} osn_map_v6plus_status_reason_t;

/**
 * Fetch v6plus MAP rules from the MAP rules distribution server.
 *
 * @param[out]  response_code   On success, the HTTP response code will be returned here.
 *
 * @param[out]  rule_list       On success, the pointer to a valid osn_map_rulelist_t object
 *                              (containing MAP rules, or empty) will be returned here.
 *
 *                              Note: Success is returned when there was a successful connection
 *                              to the MAP server, however the HTTP response code can be 200 or 4xx
 *                              or 5xx, etc. Thus, you should check the response code as well.
 *                              Further, even if the response code is 200, the returned MAP rule
 *                              list may be empty, so you should check the list length as well.
 *
 * @param[in]   cfg             v6plus endpoint configuration parameters, mainly the endpoint URL.
 *
 *                              cfg->vp_user_id can be optionally set to the previously
 *                              acquired unique user ID (if available). Otherwise set to empty
 *                              string.
 *
 * @return                      true on success. Success means that the HTTP connection to the
 *                              MAP rule server was performed successfully, however the response_code
 *                              can be other then 200 and the returned MAP rule list may as well be
 *                              empty. On any errors and also on timeout, failure is returned.
 *
 */
bool osn_map_v6plus_fetch_rules(
    long *response_code,
    osn_map_v6plus_rulelist_t **rule_list,
    const struct osn_map_v6plus_cfg *cfg);

/**
 * Acquire NTT HGW status. That is, detect if we are running under HGW or not. And if under HGW
 * whether v6plus MAP is turned ON or OFF in it.

 * @param[out]  status          On success, the NTT HGW status will be returned here.
 *
 * @param[in]   cfg             Set to NULL to use the default endpoint URL for getting HGW status.
 *                              Or supply your custom endpoint URL in cfg->vp_endpoint_url.
 *
 * @return                      true on success.
 */
bool osn_map_v6plus_ntt_hgw_status_get(
    osn_map_v6plus_hgw_status_t *status,
    const struct osn_map_v6plus_cfg *cfg);

/**
 * Report v6plus operation status to MAP rule server API endpoint.
 *
 * @param[in]  status_action     Action being reported.
 *
 * @param[in]  status_reason     Reason being reported.
 *
 * @param[in]  cfg               cfg->vp_endpoint_url set to the base URL of the MAP rule server
 *                               acct_report API endpoint.
 *                               cfg->vp_user_id can be optionally set to the previously
 *                               acquired unique user ID (if available). Otherwise set to empty
 *                               string.
 *
 * @return                       true on success.
 */
bool osn_map_v6plus_operation_report(
    osn_map_v6plus_status_action_t status_action,
    osn_map_v6plus_status_reason_t status_reason,
    const struct osn_map_v6plus_cfg *_cfg);

/**
 * Parse the JSON string with MAP rules in the format as returned from the v6plus MAP rule server
 * into the osn_map_v6plus_rulelist_t object.
 *
 * @param[in]  rules_json_str    String with JSON with MAP rules in the format as returned by a
 *                               v6plus MAP rule server.
 *
 * @param[out] rule_list         On success, *rule_list will be set to the parsed allocated
 *                               osn_map_v6plus_rulelist_t object.
 *
 * @return                       true on success.
 */
bool osn_map_v6plus_rulelist_parse(char *rules_json_str, osn_map_v6plus_rulelist_t **rule_list);

osn_map_v6plus_rulelist_t *osn_map_v6plus_rulelist_new();

void osn_map_v6plus_rulelist_del(osn_map_v6plus_rulelist_t *rule_list);

osn_map_v6plus_rulelist_t *osn_map_v6plus_rulelist_copy(const osn_map_v6plus_rulelist_t *rule_list);

/** @} OSN_MAP_V6PLUS */
/** @} OSN_MAP */
/** @} OSN */

#endif /** OSN_MAP_V6PLUS_H_INCLUDED */