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

#include "log.h"
#include "policy_tags.h"

static const struct om_mapping_tle_flag tle_flag_map[] =
{
    {
        .flag = "device",
        .flag_enum = OM_TLE_FLAG_DEVICE,
    },
    {
        .flag = "cloud",
        .flag_enum = OM_TLE_FLAG_CLOUD,
    },
    {
        .flag = "local",
        .flag_enum = OM_TLE_FLAG_LOCAL,
    },
    {
        .flag = "group",
        .flag_enum = OM_TLE_FLAG_GROUP,
    }
};

/**
 * @brief return the flag based on its enum value
 *
 * @param flag_enum the flag represented as an integer.
 */
char *
om_tag_get_tle_flag(int flag_enum)
{
    const struct om_mapping_tle_flag *map;
    size_t nelems;
    size_t i;

    /* Walk the known flags */
    nelems = (sizeof(tle_flag_map) / sizeof(tle_flag_map[0]));
    map = tle_flag_map;
    for (i = 0; i < nelems; i++)
    {
        if (flag_enum == map->flag_enum) return map->flag;
        map++;
    }

    return NULL;
}

/**
 * @brief return the tag type based on its name
 *
 * Lets the caller know if the string is a tag, a group tag, or no tag at all.
 * @param name the string to check.
 */
int
om_tag_get_type(char *name)
{
    bool is_group;
    bool is_tag;
    size_t len;
    char last;

    if (name == NULL) return -1;

    if (name[0] != TEMPLATE_VAR_CHAR) return NOT_A_OPENSYNC_TAG;

    /* Expect at minimum $[] or ${} */
    len = strlen(name);
    if (len < 3) return NOT_A_OPENSYNC_TAG;

    is_group = false;
    is_tag = false;
    last = name[len - 1];

    is_tag = (name[1] == TEMPLATE_TAG_BEGIN);
    is_tag &= (last == TEMPLATE_TAG_END);

    is_group = (name[1] == TEMPLATE_GROUP_BEGIN);
    is_group &= (last == TEMPLATE_GROUP_END);

    if (is_tag) return OPENSYNC_TAG;
    if (is_group) return OPENSYNC_GROUP_TAG;

    return NOT_A_OPENSYNC_TAG;
}

/**
 * @brief return the tag source based on its name
 *
 * Lets the caller know if the string is a device tag, cloud tag or local tag.
 * @param name the string to check.
 */
int
om_get_type_of_tag(char *name)
{
    if (name == NULL) return -1;

    if(om_tag_get_type(name) == NOT_A_OPENSYNC_TAG) return OM_TLE_FLAG_NONE;

    if (name[2] == TEMPLATE_DEVICE_CHAR)
        return OM_TLE_FLAG_DEVICE;
    else if (name[2] == TEMPLATE_CLOUD_CHAR)
        return OM_TLE_FLAG_CLOUD;
    else if (name[2] == TEMPLATE_LOCAL_CHAR)
        return OM_TLE_FLAG_LOCAL;
    else
        return OM_TLE_FLAG_NONE;
}

/**
 * @brief checks if a string is included in an opensync tag
 *
 * The tag can be a tag or a group tag
 * @param value the string checked for inclusion
 * @param tag_name the tag name to check
 */
bool
om_tag_in(char *value, char *tag_name)
{
    om_tag_list_entry_t *e;
    int match_flags;
    char name[256];
    om_tag_t *tag;
    bool is_gtag;
    int tag_type;
    char *tag_s;

    /* Sanity checks */
    if (tag_name == NULL) return false;
    if (value == NULL) return false;

    tag_type = om_tag_get_type(tag_name);
    if (tag_type == NOT_A_OPENSYNC_TAG) return false;

    match_flags = 0;
    tag_s = tag_name + 2;
    if (*tag_s == TEMPLATE_DEVICE_CHAR)
    {
        match_flags = OM_TLE_FLAG_DEVICE;
        tag_s += 1;
    }
    else if (*tag_s == TEMPLATE_CLOUD_CHAR)
    {
        match_flags = OM_TLE_FLAG_CLOUD;
        tag_s += 1;
    }
    else if (*tag_s == TEMPLATE_LOCAL_CHAR)
    {
        match_flags = OM_TLE_FLAG_LOCAL;
        tag_s += 1;
    }

    /* Copy tag name, remove end marker */
    STRSCPY_LEN(name, tag_s, -1);

    is_gtag = (tag_type == OPENSYNC_GROUP_TAG);

    tag = om_tag_find_by_name(name, is_gtag);
    if (tag == NULL) return false;


    e = om_tag_list_entry_find_by_value(&tag->values, value);
    if (e == NULL) return false;

    if (match_flags && !(e->flags & match_flags)) return false;

    LOGT("%s: found %s in tag %s", __func__, value, tag_name);

    return true;
}
