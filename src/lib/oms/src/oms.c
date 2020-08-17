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

#include <ev.h>

#include "oms.h"
#include "ovsdb_utils.h"
#include "log.h"

static struct oms_mgr mgr =
{
    .initialized = false,
};


struct oms_mgr *
oms_get_mgr(void)
{
    return &mgr;
}


/**
 * @brief compare 2 config entries
 *
 * A config entry is uniquely identified by its object id and version
 */
static int
oms_config_cmp(void *a, void *b)
{
    struct oms_config_entry *id_a;
    struct oms_config_entry *id_b;
    int ret;

    id_a = (struct oms_config_entry *)a;
    id_b = (struct oms_config_entry *)b;

    /* First compare object id */
    ret = strcmp(id_a->object, id_b->object);
    if (ret != 0) return ret;

    /* Then compare versions */
    return strcmp(id_a->version, id_b->version);
}


/**
 * @brief compare 2 state entries
 *
 * A state entry is uniquely identified by its object id and version
 */
static int
oms_state_cmp(void *a, void *b)
{
    struct oms_state_entry *id_a;
    struct oms_state_entry *id_b;
    int ret;

    id_a = (struct oms_state_entry *)a;
    id_b = (struct oms_state_entry *)b;

    ret = strcmp(id_a->object, id_b->object);
    if (ret != 0) return ret;

    /* Then compare versions */
    return strcmp(id_a->version, id_b->version);
}


/**
 * @brief delete an oms config entry
 *
 * @param entry the config entry to delete
 */
void
oms_free_config_entry(struct oms_config_entry *entry)
{
    if (entry == NULL) return;

    free(entry->object);
    free(entry->version);
    free_str_tree(entry->other_config);
    free(entry);
}


/**
 * @brief free all stored config entries
 */
void
oms_delete_config_entries(void)
{
    struct oms_config_entry *remove;
    struct oms_config_entry *entry;
    struct oms_mgr *mgr;
    ds_tree_t *tree;

    mgr = oms_get_mgr();
    tree = &mgr->config;

    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        remove = entry;
        entry = ds_tree_next(tree, entry);
        ds_tree_remove(tree, remove);
        oms_free_config_entry(remove);
    }
}


/**
 * @brief delete an oms state entry
 *
 * @param entry the state entry to delete
 */
void
oms_free_state_entry(struct oms_state_entry *entry)
{
    if (entry == NULL) return;

    free(entry->object);
    free(entry->version);
    free(entry->state);
    free(entry->prev_state);
    free_str_tree(entry->other_state);
    free(entry);
}

/**
 * @brief free all stored config entries
 */
void
oms_delete_state_entries(void)
{
    struct oms_state_entry *remove;
    struct oms_state_entry *entry;
    struct oms_mgr *mgr;
    ds_tree_t *tree;

    mgr = oms_get_mgr();
    tree = &mgr->state;

    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        remove = entry;
        entry = ds_tree_next(tree, entry);
        ds_tree_remove(tree, remove);
        oms_free_state_entry(remove);
    }
    mgr->num_states = 0;
}


/**
 * @brief return the highest version of an object
 *
 * @param object the object name
 * @param version_cap the version cap, excluded
 * @return the object with the highest version
 *
 * If @param version_cap is provided, the return shall be lesser than it or NULL
 * The caller is responsible for freeing the returned object
 */
struct oms_config_entry *
oms_get_highest_version(char *object, char *version_cap, oms_cmp_cb cmp_cb)
{
    struct oms_config_entry *latest;
    struct oms_config_entry *entry;
    struct oms_mgr *mgr;
    ds_tree_t *tree;
    int cmp;

    if (object == NULL) return NULL;
    if (cmp_cb == NULL) return NULL;

    mgr = oms_get_mgr();
    tree = &mgr->config;
    entry = ds_tree_head(tree);
    latest =  NULL;

    while (entry != NULL)
    {
        /* Ignore object mismatch */
        cmp = strcmp(object, entry->object);
        if (cmp != 0)
        {
            entry = ds_tree_next(tree, entry);
            continue;
        }

        /* Ignore version greater or equal the max version if provided */
        if (version_cap != NULL)
        {
            cmp = cmp_cb(object, entry->version, version_cap);
            if (cmp >= 0)
            {
                entry = ds_tree_next(tree, entry);
                continue;
            }
        }

        if (latest == NULL)
        {
            latest = entry;
            entry = ds_tree_next(tree, entry);
            continue;
        }

        cmp = cmp_cb(object, entry->version, latest->version);
        if (cmp > 0) latest = entry;
        entry = ds_tree_next(tree, entry);
    }

    return latest;
}


void
oms_init_manager(void)
{
    struct oms_mgr *mgr;
    ds_tree_t *tree;

    mgr = oms_get_mgr();

    /* Initialize configs container */
    tree = &mgr->config;
    ds_tree_init(tree, oms_config_cmp, struct oms_config_entry, node);

    /* Initialize state container */
    tree = &mgr->state;
    ds_tree_init(tree, oms_state_cmp, struct oms_state_entry, node);

    mgr->initialized = true;
}
