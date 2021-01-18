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

#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_utils.h"
#include "schema.h"

#include "captive_portal.h"

/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID LOG_MODULE_ID_OVSDB

ovsdb_table_t    table_Captive_Portal;
static int cportal_cmp(void *a, void *b);
void callback_Captive_Portal(
        ovsdb_update_monitor_t *mon,
        struct schema_Captive_Portal *old,
        struct schema_Captive_Portal *new);

ds_tree_t cportal_list = DS_TREE_INIT(cportal_cmp, struct cportal, cp_tnode);

/*
 * Initialize table monitors
 */
int cportal_ovsdb_init(void)
{
    LOGI("Initializing Captive_Portal tables");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(Captive_Portal);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Captive_Portal, false);

    return 0;
}

/**
 * @brief compare portal instances
 *
 * @param a instance pointer
 * @param b instance pointer
 * @return 0 if sessions matches
 */
static int
cportal_cmp(void *a, void *b)
{
    /*
     * For now we only support one instance of
     * Captive Portal.
     */
    return 0;
}

static void
cportal_dump_map(ds_tree_t *conf)
{
    struct str_pair *pair = NULL;

    if (conf)
    {
        pair = ds_tree_head(conf);
        LOGT("%s: config key-value pairs", __func__);
        while (pair != NULL)
        {
            LOGT("%s:%s",pair->key,pair->value);
            pair = ds_tree_next(conf, pair);
        }
    }
}

static void
cportal_dump_instances(void)
{
    struct cportal *inst = ds_tree_head(&cportal_list);

    LOGT("%s: Walking cportal instances", __func__);
    while (inst != NULL)
    {
        LOGT("service name: %s, uam_url: %s",
              inst->name, inst->uam_url);
        cportal_dump_map(inst->other_config);
        cportal_dump_map(inst->additional_headers);
        inst = ds_tree_next(&cportal_list, inst);
    }
}

char *
cportal_get_other_config_val(struct cportal *inst, char *key)
{
   struct str_pair *pair;
   ds_tree_t       *tree;

   tree = inst->other_config;
   if (!tree) return NULL;

   pair = ds_tree_find(tree, key);
   if (!pair) return NULL;

   LOGT("%s: cportal other_config %s:%s",__func__, key, pair->value);
   return pair->value;
}

static void
cportal_parse_additional_hdrs(struct cportal *inst,
                              struct schema_Captive_Portal *new)
{
    ds_tree_t   *additional_headers;

    if (!inst || !new) return;

    additional_headers = schema2tree(sizeof(new->additional_headers_keys[0]),
                                  sizeof(new->additional_headers[0]),
                                  new->additional_headers_len,
                                  new->additional_headers_keys,
                                  new->additional_headers);

    if (!additional_headers) return;

    cportal_dump_map(additional_headers);

    if (inst->additional_headers) free_str_tree(inst->additional_headers);

    inst->additional_headers = additional_headers;

    return;
}

static void
cportal_parse_other_config(struct cportal *inst,
                           struct schema_Captive_Portal *new)
{
    ds_tree_t   *other_config;

    if (!inst || !new) return;

    other_config = schema2tree(sizeof(new->other_config_keys[0]),
                   sizeof(new->other_config[0]),
                   new->other_config_len,
                   new->other_config_keys,
                   new->other_config);

    if (!other_config) return;

    cportal_dump_map(other_config);

    if (inst->other_config) free_str_tree(inst->other_config);

    inst->other_config = other_config;

    inst->pkt_mark = cportal_get_other_config_val(inst, "pkt_mark");

    inst->rt_tbl_id = cportal_get_other_config_val(inst, "rt_tbl_id");

    return;
}

static void
cportal_enable_inst(struct cportal *inst)
{
    if (!inst) return;

    if (inst->enabled) return;

    if (!cportal_proxy_set(inst))
    {
        LOGE("%s: Couldn't configure proxy service for instance [%s]",__func__,inst->name);
        return;
    }

    if (!cportal_proxy_start(inst))
    {
        LOGE("%s: Couldn't launch proxy service for instance [%s]",__func__,inst->name);
        return;
    }
}

static void
cportal_disable_inst(struct cportal *inst)
{
    if (!inst->enabled) return;

    if (!cportal_proxy_stop(inst))
    {
        LOGE("%s: Couldn't stop proxy service ",__func__);
        return;
    }
}


static void
cportal_free_cportal(struct cportal *inst)
{

    if (!inst) return;

    free(inst->name);
    free(inst->uam_url);
    free(inst->url->port);
    free(inst->url->domain_name);
    free(inst->url);

    free_str_tree(inst->other_config);
    free_str_tree(inst->additional_headers);
    free(inst);
    return;
}

static struct cportal *
cportal_alloc_inst(struct schema_Captive_Portal *new)
{
    struct cportal *inst;

    if (!new) return NULL;

    inst = calloc(1, sizeof(struct cportal));
    if (!inst)
    {
        LOG(ERR, "%s: Memory allocation failure\n", __func__);
        return NULL;
    }

    if (!strcmp(new->proxy_method, "forward"))
    {
        inst->proxy_method = FORWARD;
    }
    else if (!strcmp(new->proxy_method, "reverse"))
    {
        inst->proxy_method = REVERSE;
    }
    else
    {
        inst->proxy_method = REVERSE;
    }

    inst->name = strdup(new->name);
    if (!inst->name)
    {
        LOG(ERR, "%s: Couldn't allocate memory for name[%s]", __func__, new->name);
        goto err_name;
    }

    inst->uam_url = strdup(new->uam_url);
    if (!inst->uam_url)
    {
        LOG(ERR, "%s: Couldn't allocate memory for uam_url[%s]", __func__, new->uam_url);
        goto err_uam;
    }

    inst->url = calloc(1, sizeof(struct url_s));
    if (!inst->url)
    {
        LOG(ERR, "%s: Couldn't allocate memory for url", __func__);
        goto err_url;
    }


    cportal_parse_other_config(inst, new);

    cportal_parse_additional_hdrs(inst, new);

    return inst;

err_url:
    free(inst->uam_url);
err_uam:
    free(inst->name);
err_name:
    free(inst);

    return NULL;
}

static void
cportal_update(struct cportal *inst,
               struct schema_Captive_Portal *mod)
{

   if (!inst || !mod) return;

   cportal_disable_inst(inst);

   ds_tree_remove(&cportal_list, inst);

   cportal_free_cportal(inst);

   inst = cportal_alloc_inst(mod);

   ds_tree_insert(&cportal_list, inst, inst->name);

   cportal_enable_inst(inst);
   return;
}

static void
cportal_add(struct schema_Captive_Portal *new)
{
    struct cportal *inst;

    if (!new) return;

    inst = ds_tree_find(&cportal_list, new->name);
    if (inst)
    {
        LOGD("%s: Allowing only one instance of captive portal %s.",__func__,inst->name);
        return;
    }

    inst = cportal_alloc_inst(new);

    if (!inst)
    {
        LOGE("%s: Could not allocate cportal instance %s", __func__, new->name);
        return;
    }
    LOG(INFO, "%s: Created new cportal instance [%s]", __func__, inst->name);
    ds_tree_insert(&cportal_list, inst, inst->name);

    cportal_enable_inst(inst);

    LOG(INFO, "%s: Created new cportal instance [%s]", __func__, inst->name);

}

static void
cportal_mod(struct schema_Captive_Portal *mod)
{
    struct cportal *inst;

    if (!mod) return;

    inst = ds_tree_find(&cportal_list, mod->name);

    if (!inst) return;

    if (strcmp(mod->name, inst->name)) return;

    cportal_update(inst, mod);

}

static void
cportal_del(struct schema_Captive_Portal *del)
{
    struct cportal *inst;

    if (!del) return;

    inst = ds_tree_find(&cportal_list, del->name);
    if (!inst) return;

    if (strcmp(del->name, inst->name)) return;

    cportal_disable_inst(inst);

    ds_tree_remove(&cportal_list, inst);

    cportal_free_cportal(inst);
    return;
}

/*
 * OVSDB monitor update callback for Captive_Portal
 */
void callback_Captive_Portal(
        ovsdb_update_monitor_t *mon,
        struct schema_Captive_Portal *old,
        struct schema_Captive_Portal *new)
{

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        {
            cportal_add(new);
            cportal_dump_instances();
            break;
        }
        case OVSDB_UPDATE_MODIFY:
        {
            cportal_mod(new);
            cportal_dump_instances();
            break;
        }
        case OVSDB_UPDATE_DEL:
        {
            cportal_del(old);
            cportal_dump_instances();
            break;
        }
        default:
            LOGW("%s:mon upd error: %d", __func__, mon->mon_type);
            return;
    }
    return;
}


