/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "iotm.h"
#include "iotm_tag.h"
#include "iotm_tag_private.h"


void tag_print_tree(struct iotm_tree_t *tags)
{
    printf("%s: Current tag tree: -\n", __func__);

    iotm_tree_foreach_value(tags, iotm_print_value, NULL);
}

// Check if provided string includes tag references
bool str_has_template(const char *str)
{
    const char          *s;

    if (str)
    {
        s = str;
        while((s = strchr(s, TEMPLATE_VAR_CHAR)))
        {
            s++;
            if (*s == TEMPLATE_TAG_BEGIN)
            {
                if (strchr(s, TEMPLATE_TAG_END)) return true;
            }
            else if (*s == TEMPLATE_GROUP_BEGIN)
            {
                if (strchr(s, TEMPLATE_GROUP_END)) return true;
            }
        }
    }

    return false;
}

int tag_extract_all_vars(
        const char *rule,
        iotm_list_t *vars)
{
    return tag_extract_vars(
            rule,
            TEMPLATE_VAR_CHAR,
            TEMPLATE_TAG_BEGIN,
            TEMPLATE_TAG_END,
            OM_TLE_FLAG_GROUP,
            vars);
}

// Detect vars within the rule
int tag_extract_vars(
        const char *rule,
        char var_chr,
        char begin,
        char end,
        uint8_t base_flags,
        iotm_list_t *vars)
{
    uint8_t     flag;
    char        *mrule    = NULL;
    char        *p;
    char        *s;
    int        ret       = 1;

    // Make a copy of the rule we can modify
    mrule = strdup(rule);

    // Detect tags
    p = mrule;
    s = p;
    while((s = strchr(s, var_chr))) {
        s++;
        if (*s != begin) {
            continue;
        }
        s++;

        flag = base_flags;
        if (*s == TEMPLATE_DEVICE_CHAR) {
            s++;
            flag |= OM_TLE_FLAG_DEVICE;
        }
        else if (*s == TEMPLATE_CLOUD_CHAR) {
            s++;
            flag |= OM_TLE_FLAG_CLOUD;
        }
        if (!(p = strchr(s, end))) {
            printf("Template Flow has malformed (no ending '%c')",
                    end);
            continue;
        }
        *p++ = '\0';

        LOGD("Template Flow detected %s'%s'\n",
                (flag == OM_TLE_FLAG_DEVICE) ? "device " :
                (flag == OM_TLE_FLAG_CLOUD) ? "cloud " : "",
                s);

        iotm_set_add_str(vars, s);

        s = p;
    }

exit:
    free(mrule);
    return ret;
}

int remove_tag_from_tree(
        iotm_tree_t *tree,
        struct schema_Openflow_Tag *row)
{
    return iotm_tree_remove_list(tree, row->name);
}

int add_tag_to_tree(
        iotm_tree_t *tree,
        struct schema_Openflow_Tag *row)
{
    if (tree == NULL) return -1;
    if (row == NULL) return -1;

    int err = -1;
    int i = 0;

    for ( i = 0; i < row->device_value_len; i++ )
    {
        err = iotm_tree_set_add_str(
                tree,
                row->name,
                row->device_value[i]);

        if ( err ) goto err_cleanup;
    }

    for ( i = 0; i < row->cloud_value_len; i++ )
    {
        err = iotm_tree_set_add_str(
                tree,
                row->name,
                row->cloud_value[i]);

        if ( err ) goto err_cleanup;
    }

    return 0;

err_cleanup:
    remove_tag_from_tree(tree, row);
    return -1;
}

void change_passed_key(ds_list_t *dl, struct iotm_value_t *value, void *ctx)
{
    struct key_update_t *update = (struct key_update_t *) ctx;
    if (update == NULL)
    {
        LOGE("%s: Could not extract update, exiting\n", __func__);
        return;
    }

    struct iotm_value_t diff_val =
    {
        .key = update->newk,
        .value = value->value,
    };
    update->cb(dl, &diff_val, update->ctx);
}

// expand a tag into all of it's values
void expand_tags(
        char *tag,
        char *key,
        void(*cb)(ds_list_t *, struct iotm_value_t *, void *),
        void *ctx)
{
    struct iotm_mgr *mgr = iotm_get_mgr();
    struct iotm_tree_t *tags = mgr->tags;

    // get the list of tags matching the current found value
    struct iotm_list_t *current = iotm_tree_get(tags, tag);

    struct key_update_t update =
    {
        .newk = key,
        .cb = cb,
        .ctx = ctx,
    };
    iotm_list_foreach(current, change_passed_key, &update);
}
