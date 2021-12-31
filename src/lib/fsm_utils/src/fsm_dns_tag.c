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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ds_tree.h"
#include "fsm_dns_utils.h"
#include "log.h"
#include "memutil.h"
#include "os_types.h"
#include "os_util.h"
#include "ovsdb_sync.h"
#include "policy_tags.h"
#include "util.h"

bool
dns_updatev4_tag(struct dns_response_s *dns_response,
                 struct fsm_policy_reply *policy_reply)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct schema_Openflow_Tag *regular_tag;
    size_t max_capacity;
    size_t value_len;
    size_t elem_len;
    int tle_flag;
    bool result;
    size_t len;

    regular_tag = NULL;
    local_tag = NULL;
    max_capacity = 0;
    result = false;
    len = 0;

    if (policy_reply->action != FSM_UPDATE_TAG) return false;

    tle_flag = om_get_type_of_tag(policy_reply->updatev4_tag);

    if (tle_flag == OM_TLE_FLAG_DEVICE ||
        tle_flag == OM_TLE_FLAG_CLOUD)
    {
        regular_tag = CALLOC(1, sizeof(*regular_tag));
        if (regular_tag == NULL) return result;

        value_len = sizeof(regular_tag->device_value);
        elem_len = sizeof(regular_tag->device_value[0]);
        max_capacity = value_len / elem_len;

        len = strlen(policy_reply->updatev4_tag);
        os_util_strncpy(regular_tag->name, &policy_reply->updatev4_tag[3], len - 3);
        regular_tag->name_exists = true;
        regular_tag->name_present = true;

        result = fsm_dns_generate_update_tag(dns_response, policy_reply,
                                             regular_tag->device_value,
                                             &regular_tag->device_value_len,
                                             max_capacity, 4);
        if (result)
        {
            result = fsm_dns_upsert_regular_tag(regular_tag, ovsdb_sync_upsert);
            if (!result)
            {
                LOGT("%s: Openflow_Tag not updated for request.", __func__);
            }
        }
        else
        {
            LOGT("%s: Openflow_Tag not updated for request.", __func__);
        }
    }
    else if (tle_flag == OM_TLE_FLAG_LOCAL)
    {
        local_tag = CALLOC(1, sizeof(*local_tag));
        if (local_tag == NULL) return result;

        value_len = sizeof(local_tag->values);
        elem_len = sizeof(regular_tag->device_value[0]);
        max_capacity = value_len / elem_len;

        len = strlen(policy_reply->updatev4_tag);
        os_util_strncpy(local_tag->name, &policy_reply->updatev4_tag[3], len - 3);
        local_tag->name_exists = true;
        local_tag->name_present = true;

        result = fsm_dns_generate_update_tag(dns_response, policy_reply,
                                             local_tag->values,
                                             &local_tag->values_len,
                                             max_capacity, 4);
        if (result)
        {
            result = fsm_dns_upsert_local_tag(local_tag, ovsdb_sync_upsert);
            if (!result)
            {
                LOGT("%s: Openflow_Local_Tag not updated for request.", __func__);
            }
        }
        else
        {
            LOGT("%s: Openflow_Local_Tag not updated for request.", __func__);
        }
    }

out:
    FREE(regular_tag);
    FREE(local_tag);

    return result;
}


bool
dns_updatev6_tag(struct dns_response_s *dns_response,
                 struct fsm_policy_reply *policy_reply)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct schema_Openflow_Tag *regular_tag;
    size_t max_capacity;
    size_t value_len;
    size_t elem_len;
    int tle_flag;
    bool result;
    size_t len;

    regular_tag = NULL;
    local_tag = NULL;
    max_capacity = 0;
    result = false;
    len = 0;

    if (policy_reply->action != FSM_UPDATE_TAG) return false;

    tle_flag = om_get_type_of_tag(policy_reply->updatev6_tag);

    if (tle_flag == OM_TLE_FLAG_DEVICE ||
        tle_flag == OM_TLE_FLAG_CLOUD)
    {
        regular_tag = CALLOC(1, sizeof(*regular_tag));
        if (regular_tag == NULL) return result;

        value_len = sizeof(regular_tag->device_value);
        elem_len = sizeof(regular_tag->device_value[0]);
        max_capacity = value_len / elem_len;

        len = strlen(policy_reply->updatev6_tag);
        os_util_strncpy(regular_tag->name, &policy_reply->updatev6_tag[3], len - 3);
        regular_tag->name_exists = true;
        regular_tag->name_present = true;

        result = fsm_dns_generate_update_tag(dns_response, policy_reply,
                                             regular_tag->device_value,
                                             &regular_tag->device_value_len,
                                             max_capacity, 6);
        if (result)
        {
            result = fsm_dns_upsert_regular_tag(regular_tag, ovsdb_sync_upsert);
            if (!result)
            {
                LOGT("%s: Openflow_Tag not updated for request.", __func__);
            }
        }
        else
        {
            LOGT("%s: Openflow_Tag not updated for request.", __func__);
        }
    }
    else if(tle_flag == OM_TLE_FLAG_LOCAL)
    {
        local_tag = CALLOC(1, sizeof(*local_tag));
        if (local_tag == NULL) return result;

        value_len = sizeof(local_tag->values);
        elem_len = sizeof(regular_tag->device_value[0]);
        max_capacity = value_len / elem_len;

        len = strlen(policy_reply->updatev6_tag);
        os_util_strncpy(local_tag->name, &policy_reply->updatev6_tag[3], len - 3);
        local_tag->name_exists = true;
        local_tag->name_present = true;

        result = fsm_dns_generate_update_tag(dns_response, policy_reply,
                                             local_tag->values,
                                             &local_tag->values_len,
                                             max_capacity, 6);
        if (result)
        {
            result = fsm_dns_upsert_local_tag(local_tag, ovsdb_sync_upsert);
            if (!result)
            {
                LOGT("%s: Openflow_Local_Tag not updated for request.", __func__);
            }
        }
        else
        {
            LOGT("%s: Openflow_Local_Tag not updated for request.", __func__);
        }
    }

out:
    FREE(regular_tag);
    FREE(local_tag);

    return result;
}

bool
is_device_excluded(char *tag, os_macaddr_t *mac)
{
    char mac_s[32] = { 0 };

    snprintf(mac_s, sizeof(mac_s), PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(mac));

    return om_tag_in(mac_s, tag);
}

void
fsm_dns_update_tag(struct fsm_dns_update_tag_param *dns_tag_param)
{
    struct dns_response_s *dns_response;
    struct fsm_policy_reply *policy_reply;
    os_macaddr_t *dev_id;
    bool rc = false;

    dns_response = dns_tag_param->dns_response;
    policy_reply = dns_tag_param->policy_reply;
    dev_id = dns_tag_param->dev_id;


    rc = is_device_excluded(policy_reply->excluded_devices, dev_id);
    if (rc)
    {
        LOGD("%s: mac " PRI_os_macaddr_lower_t " is excluded from tag updates."
             ,__func__, FMT_os_macaddr_t(*dev_id));
        return;
    }

    if (policy_reply->updatev4_tag && dns_response->ipv4_cnt != 0)
    {
        rc = dns_updatev4_tag(dns_response, policy_reply);
        if (!rc)
        {
            LOGT("%s: Failed to update ipv4 OpenFlow tags.", __func__);
        }
    }

    if (policy_reply->updatev6_tag &&
        dns_response->ipv6_cnt != 0)
    {
        rc = dns_updatev6_tag(dns_response, policy_reply);
        if (!rc)
        {
            LOGT("%s: Failed to update ipv6 OpenFlow tags.",__func__);
        }
    }
}


bool
is_in_device_set(char values[][MAX_TAG_VALUES_LEN], int values_len,
                 char *checking)
{
    int ret;
    int i;

    for(i = 0; i < values_len; i++)
    {
        ret = strcmp(values[i], checking);
        if (!ret) return true;
    }
    return false;
}


bool
fsm_dns_generate_update_tag(struct dns_response_s *dns_response,
                            struct fsm_policy_reply *policy_reply,
                            char values[][MAX_TAG_VALUES_LEN],
                            int *values_len, size_t max_capacity,
                            int ip_ver)
{
    bool added_information;
    om_tag_t *tag;
    char *adding;
    char name[MAX_TAG_NAME_LEN] = {0};
    int  name_len = 0;
    bool rc;
    int i;

    if (policy_reply->action != FSM_UPDATE_TAG) return false;

    added_information = false;

    if (ip_ver == 4)
    {
      name_len = strlen(policy_reply->updatev4_tag);
      os_util_strncpy(name, &policy_reply->updatev4_tag[3], name_len - 3);
      tag = om_tag_find_by_name(name, false);
    }
    else if (ip_ver == 6)
    {
      name_len = strlen(policy_reply->updatev6_tag);
      os_util_strncpy(name, &policy_reply->updatev6_tag[3], name_len - 3);
      tag = om_tag_find_by_name(name, false);
    }
    else
    {
      return false;
    }

    /* Load in current in memory tag state */
    if (tag != NULL)
    {
        om_tag_list_entry_t *iter;
        ds_tree_foreach(&tag->values, iter)
        {
            adding = iter->value;
            rc = is_in_device_set(values, *values_len, adding);
            if (!rc)
            {
                STRSCPY(values[*values_len], adding);
                *values_len = *values_len + 1;
            }
        }
    }

    if (ip_ver == 4)
    {
        for (i = 0; i < dns_response->ipv4_cnt; i++)
        {
            adding = dns_response->ipv4_addrs[i];
            rc = is_in_device_set(values, *values_len, adding);
            if (rc) continue;

            if (*values_len == (int)(max_capacity))
            {
                *values_len = *values_len % (int)max_capacity;
                LOGD("%s: tag %s max capacity reached, adding circularly at index %d",
                     __func__, name, *values_len);
            }

            STRSCPY(values[*values_len], adding);
            *values_len = *values_len + 1;
            added_information = true;
        }
    }
    else if (ip_ver == 6)
    {
        for (i = 0; i < dns_response->ipv6_cnt; i++)
        {
            adding = dns_response->ipv6_addrs[i];
            rc = is_in_device_set(values, *values_len, adding);
            if (rc) continue;

            if (*values_len == (int)(max_capacity))
            {
                *values_len = *values_len % (int)max_capacity;
                LOGD("%s: tag %s max capacity reached, adding circularly at index %d",
                     __func__, name, *values_len);
            }

            STRSCPY(values[*values_len], adding);
            *values_len = *values_len + 1;
            added_information = true;
        }
    }

    return added_information;
}

bool
fsm_dns_upsert_regular_tag(struct schema_Openflow_Tag *row,
                           dns_ovsdb_updater updater)
{
    pjs_errmsg_t err;
    json_t *jrow;
    bool ret;

    jrow = schema_Openflow_Tag_to_json(row, err);
    if (jrow == NULL)
    {
        LOGD("%s: failed to generate JSON for upsert: %s", __func__, err);

        return false;
    }

    ret = updater(SCHEMA_TABLE(Openflow_Tag), SCHEMA_COLUMN(Openflow_Tag, name),
                  row->name, jrow, NULL);
    if (ret)
    {
        LOGD("%s: Updated Openflow_Tag: %s with new IP set.", __func__,
             row->name);
        return true;
    }
    LOGD("%s: Return value from OVSDB update was: %d", __func__, ret);

    return false;
}


bool
fsm_dns_upsert_local_tag(struct schema_Openflow_Local_Tag *row,
                         dns_ovsdb_updater updater)
{
    pjs_errmsg_t err;
    json_t *jrow;
    bool ret;

    jrow = schema_Openflow_Local_Tag_to_json(row, err);
    if (jrow == NULL)
    {
        LOGD("%s: failed to generate JSON for upsert: %s", __func__, err);

        return false;
    }

    ret = updater(SCHEMA_TABLE(Openflow_Local_Tag),
                  SCHEMA_COLUMN(Openflow_Local_Tag, name),
                  row->name, jrow, NULL);
    if (ret)
    {
        LOGD("%s: Updated Openflow_Local_Tag: %s with new IP set.", __func__,
             row->name);
        return true;
    }
    LOGD("%s: Return value from OVSDB update was: %d", __func__, ret);

    return false;
}
