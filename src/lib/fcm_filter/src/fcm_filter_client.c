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

#include "ds_tree.h"
#include "fcm_filter.h"
#include "log.h"
#include "memutil.h"

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int client_cmp(void *a, void *b)
{
    struct fcm_filter_client *client_a;
    struct fcm_filter_client *client_b;
    char *default_name = "fcm_filter_1";
    char *name_a;
    char *name_b;

    client_a = (struct fcm_filter_client *)a;
    client_b = (struct fcm_filter_client *)b;

    uintptr_t p_a = (uintptr_t)(client_a->session);
    uintptr_t p_b = (uintptr_t)(client_b->session);

    /* Discriminate sessions */
    if (p_a < p_b) return -1;
    if (p_a > p_b) return 1;

    /* Discriminate clients of the same session */
    name_a = (client_a->name == NULL ? default_name : client_a->name);
    name_b = (client_b->name == NULL ? default_name : client_b->name);

    return strcmp(name_a, name_b);
}

void fcm_filter_client_init(void)
{
    struct fcm_filter_mgr *mgr;
    ds_tree_t *tree;

    mgr = get_filter_mgr();
    tree = &mgr->clients;

    ds_tree_init(tree, client_cmp, struct fcm_filter_client, client_node);
}

/**
 * @brief walks the tree of clients
 *
 * Debug function, logs each tree entry
 */
void
fcm_walk_clients_tree(const char *caller)
{
    struct fcm_filter_client *client;
    struct fcm_filter_mgr *mgr;
    char *session_name;
    char *table_name;
    ds_tree_t *tree;

    mgr = get_filter_mgr();
    tree = &mgr->clients;

    LOGD("%s: Walking client tree", caller);
    client = ds_tree_head(tree);
    while (client != NULL)
    {
        session_name = NULL;
        if (client->session_name != NULL) session_name = client->session_name(client);

        table_name = NULL;
        if (client->table != NULL) table_name = client->table->name;
        LOGD("%s: client: %s: session: %s, table: %s", __func__,
             client->name != NULL ? client->name : "None",
             session_name != NULL ? session_name : "None",
             table_name != NULL ? table_name : "None");
        client = ds_tree_next(tree, client);
    }
}

void fcm_filter_register_client(struct fcm_filter_client *client)
{
    struct fcm_filter_client *p_client;
    char *default_name = "fcm_filter_1";
    struct fcm_filter_mgr *mgr;
    struct filter_table *table;
    char *session_name;
    char *table_name;
    ds_tree_t *tree;
    char *name;

    if (client == NULL) return;
    if (client->session == NULL) return;

    session_name = NULL;
    if (client->session_name != NULL) session_name = client->session_name(client);
    table_name = NULL;

    mgr = get_filter_mgr();
    tree = &mgr->clients;
    p_client = ds_tree_find(tree, client);

    if (p_client != NULL)
    {
        /* Update the external client's table */
        client->table = p_client->table;

        /* Update the internal client */
        p_client->update_client = client->update_client;
        p_client->session_name = client->session_name;

        LOGD("%s: updating client %s", __func__, p_client->name);

        if (client->table != NULL) table_name = client->table->name;

        LOGD("%s: client: %s: session: %s, table: %s", __func__,
             client->name ? client->name : "None",
             session_name ? session_name : "None",
             table_name ? table_name : "None");

        fcm_walk_clients_tree(__func__);
        return;
    }

    /* Allocate a client, add it to the tree */
    p_client = CALLOC(1, sizeof(*p_client));
    if (p_client == NULL) return;

    name = (client->name == NULL ? default_name : client->name);
    p_client->name = STRDUP(name);

    if (p_client->name == NULL) goto err_free_client;
    p_client->session = client->session;
    p_client->update_client = client->update_client;
    p_client->session_name = client->session_name;
    table = ds_tree_find(&mgr->fcm_filters, name);
    p_client->table = table;
    client->table = table;
    ds_tree_insert(&mgr->clients, p_client, p_client);

    LOGD("%s: registered client %s", __func__, p_client->name);

    if (client->table != NULL) table_name = client->table->name;
    LOGD("%s: client: %s: session: %s, table: %s", __func__,
         client->name ? client->name : "None",
         session_name ? session_name : "None",
         table_name ? table_name : "None");
    fcm_walk_clients_tree(__func__);
    return;

err_free_client:
    FREE(p_client);
}

void fcm_filter_deregister_client(struct fcm_filter_client *client)
{
    struct fcm_filter_client *p_client;
    struct fcm_filter_mgr *mgr;
    char *session_name;
    char *table_name;

    if (client == NULL) return;
    if (client->session == NULL) return;

    session_name = NULL;
    table_name = NULL;

    if (client->session_name != NULL) session_name = client->session_name(client);
    if (client->table != NULL) table_name = client->table->name;

    LOGD("%s: client: %s: session: %s, table: %s", __func__,
         client->name ? client->name : "None",
         session_name ? session_name : "None",
         table_name ? table_name : "None");

    mgr = get_filter_mgr();
    p_client = ds_tree_find(&mgr->clients, client);
    if (p_client == NULL) return;

    ds_tree_remove(&mgr->clients, p_client);
    FREE(p_client->name);
    FREE(p_client);

    client->table = NULL;
    client->session = NULL;
    fcm_walk_clients_tree(__func__);
}

void fcm_filter_update_clients(struct filter_table *table)
{
    char *default_name = "fcm_filter_1";
    struct fcm_filter_client *client;
    struct fcm_filter_mgr *mgr;
    ds_tree_t *tree;

    mgr = get_filter_mgr();
    tree = &mgr->clients;
    client = ds_tree_head(tree);

    LOGD("%s: Updating clients of table: %s", __func__,
         table->name == NULL ? default_name : table->name);

    while (client != NULL)
    {
        bool update;
        char *name;
        int cmp;

        name = (client->name == NULL ? default_name : client->name);
        cmp = strcmp(name, table->name);
        update = ((cmp == 0) && (client->update_client != NULL));
        if (update)
        {
            LOGD("%s: updating client %s", __func__, client->name);
            client->update_client(client->session, table);
        }
        client = ds_tree_next(tree, client);
    }
    fcm_walk_clients_tree(__func__);
}
