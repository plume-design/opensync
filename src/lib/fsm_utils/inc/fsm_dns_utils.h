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

#ifndef FSM_DNS_UTILS_H_INCLUDED
#define FSM_DNS_UTILS_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "fsm.h"

#define MAX_TAG_VALUES_LEN 65
#define MAX_TAG_NAME_LEN 64

struct fsm_dns_update_tag_param
{
    struct dns_response_s *dns_response;
    struct fsm_policy_reply *policy_reply;
    os_macaddr_t *dev_id;
};

/**
 * @brief create updated row for OF Tag with newly matched IPs
 *
 * @param        req          request with update fields loaded
 * @param[out]   values       buffer to update values.
 * @param[out]   values_len   length of the values updated.
 * @param[in]    max_capacity the buffer maximum capacity
 * @param[in]    ip_ver       the IP protocol version
 *
 * @return true loaded correctly built struct into output
 * @return false output struct not built
 */
bool
fsm_dns_generate_update_tag(struct dns_response_s *dns_response,
                            struct fsm_policy_reply *policy_reply,
                            char values[][MAX_TAG_VALUES_LEN],
                            int *values_len, size_t max_capacity,
                            int ip_ver);

typedef bool (*dns_ovsdb_updater)(const char *, const char *,
                                  const char *, json_t *, ovs_uuid_t *);
/**
 * @brief update Openflow_Tag to map to new row
 *
 * @param       row      new row to be written to Openflow_Tag
 * @param       updater  dependency injection for updating
 *
 * @return      true     succeeded in update
 * @return      false    failed to update
 */
bool
fsm_dns_upsert_regular_tag(struct schema_Openflow_Tag *row,
                           dns_ovsdb_updater updater);

/**
 * @brief update Openflow_Tag to map to new row
 *
 * @param       row      new row to be written to Openflow_Tag
 * @param       updater  dependency injection for updating
 *
 * @return      true     succeeded in update
 * @return      false    failed to update
 */
bool
fsm_dns_upsert_local_tag(struct schema_Openflow_Local_Tag *row,
                         dns_ovsdb_updater updater);

void
fsm_dns_update_tag(struct fsm_dns_update_tag_param *dns_tag_param);

struct dns_cache_param
{
    struct fqdn_pending_req *req;
    struct fsm_policy_reply *policy_reply;
    struct sockaddr_storage *ipaddr;
    int action_by_name;
    int action;
    uint8_t direction;
    uint32_t ttl; /* this is the value that will actually be used */
    char *network_id;
};

/**
 * @brief Adds one more entry to the DNS cache in use.
 *
 * @param param contains all the required information
 *
 * @return true for success and false for failure.
 *
 * @remark ttl parameter will be used "as-is". It is the caller's
 *         responsibility to assign the correct value.
 */
bool
fsm_dns_cache_add_entry(struct dns_cache_param *param);

/**
 * @brief Adds one more redirect entry to the DNS cache in use.
 *
 * @param param contains all the required information
 *
 * @return true for success and false for failure.
 */

bool
fsm_dns_cache_add_redirect_entry(struct dns_cache_param *param);

/**
 * @brief Scrubs the DNS cache in use of all expired entries (based
 *        on TTL)
 *
 * @param param contains all the required information
 *
 * @return true for success and false for failure.
 *
 * @remark still un-implemented.
 */
bool
fsm_dns_cache_flush_ttl();

/**
 * @brief Prints the content of the used DNS cache.
 *
 * @param param is used to establish which cache implementation is
 *        actually used.
 */
void
fsm_dns_cache_print(struct dns_cache_param *param);

#endif /* FSM_DNS_UTILS_H_INCLUDED */
