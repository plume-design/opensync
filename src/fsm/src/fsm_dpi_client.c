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
#include <stdint.h>
#include <string.h>

#include "ds_tree.h"
#include "fsm.h"
#include "fsm_internal.h"
#include "log.h"
#include "memutil.h"
#include "network_metadata_report.h"
#include "policy_tags.h"
#include "util.h"

static char *FLOW_ATTRIBUTES = "flow_attributes";
static char *DPI_PLUGIN = "dpi_plugin";

/**
 * @brief check if a fsm session is a dpi client session
 *
 * @param session the session to check
 * @return true is the session is  a dpi plugin client,
 *         false otherwise.
 */
bool
fsm_is_dpi_client(struct fsm_session *session)
{
    if (session->type == FSM_DPI_PLUGIN_CLIENT) return true;

    return false;
}

/**
 * @brief process attributes which are removed from the tags
 *
 * @param client_session session associated with these tags
 *        dpi_plugin - dpi plugin session
 *        removed_attributes tag attributes removed
 */
static void
fsm_process_removed_tag(struct fsm_session *client_session,
                        struct fsm_session *dpi_plugin,
                        ds_tree_t *removed_attributes)
{
    om_tag_list_entry_t *item;

    ds_tree_foreach(removed_attributes, item)
    {
        /* Unregister the client */
        LOGI("%s: unregistering tag value %s for client %s",
             __func__, item->value, client_session->name);
        fsm_dpi_unregister_client(dpi_plugin, client_session, item->value);
    }
}

/**
 * @brief process attributes which are added to the tags
 *
 * @param client_session session associated with these tags
 * @param dpi_plugin dpi plugin session
 * @param added_attributes tag attributes added
 */
static void
fsm_process_added_tag(struct fsm_session *client_session,
                      struct fsm_session *dpi_plugin,
                      ds_tree_t *added_attributes)
{
    om_tag_list_entry_t *item;

    ds_tree_foreach(added_attributes, item)
    {
        /* Register the client */
        LOGI("%s: registering tag value %s for client %s",
             __func__, item->value, client_session->name);
        fsm_dpi_register_client(dpi_plugin, client_session, item->value);
    }
}

/**
 * @brief process the modified tag values
 *
 * @param client_session session associated with this tag
 * @param removed attributes removed from config
 * @param added attributes added to config
 * @param updated attributes updated in config
 * @return None
 */
static void
fsm_process_tags(struct fsm_session *client_session,
                 ds_tree_t *removed,
                 ds_tree_t *added,
                 ds_tree_t *updated)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct fsm_session *dpi_plugin;
    char *dpi_plugin_handler;
    ds_tree_t *sessions;
    bool ret;

    /* make sure it is dpi client session */
    ret = fsm_is_dpi_client(client_session);
    if (!ret) return;

    /* Look up the dpi plugin handler in the other_config settings */
    dpi_plugin_handler = client_session->ops.get_config(client_session, DPI_PLUGIN);
    if (dpi_plugin_handler == NULL) return;

    /* Look up the corresponding session */
    sessions = fsm_get_sessions();
    dpi_plugin = ds_tree_find(sessions, dpi_plugin_handler);
    if (dpi_plugin == NULL) return;

    /* Validate access to the dpi plugin registration callback */
    dpi_plugin_ops = &dpi_plugin->p_ops->dpi_plugin_ops;
    if (dpi_plugin_ops->register_client == NULL) return;

    if (removed != NULL)
    {
        fsm_process_removed_tag(client_session, dpi_plugin, removed);
    }

    if (added != NULL)
    {
        fsm_process_added_tag(client_session, dpi_plugin, added);
    }
}

/**
 * @brief called when tag values are updated.  Check for the tag
 *        we are interested and process the updated value
 * @param tag tag whose value is updated
 * @param removed values which are removed from this tag
 * @param added values which are added to this tag
 * @param updated values which are updated for this tag
 */
void
fsm_process_tag_update(om_tag_t *tag,
                       struct ds_tree *removed,
                       struct ds_tree *added,
                       struct ds_tree *updated)
{
    struct fsm_dpi_client_tags *dpi_tag;
    struct fsm_session *client_session;
    struct fsm_mgr *mgr;
    ds_tree_t *sessions;

    LOGT("%s: Looking for tag %s", __func__, tag->name);

    mgr = fsm_get_mgr();

    /* check if this tag is in the list of tags we are interested */
    dpi_tag = ds_tree_find(&mgr->dpi_client_tags_tree, tag->name);
    if (dpi_tag == NULL) goto nothing_to_process;

    /* get session associated with this tag */
    sessions = fsm_get_sessions();
    if (sessions == NULL) goto nothing_to_process;

    client_session = ds_tree_find(sessions, dpi_tag->client_plugin_name);
    if (client_session == NULL) goto nothing_to_process;

    /* process the updated values */
    fsm_process_tags(client_session, removed, added, updated);
    LOGT("%s: All attributes processed for tag %s", __func__, tag->name);

    return;

nothing_to_process:
    LOGT("%s: Nothing to process for %s", __func__, tag->name);
}

/**
 * @brief add the tag_name and the associated plugin_name for
 *        tag value updates
 * @param tag_name tag name to be monitored
 * @param plugin_name name of the plugin associated with this tag
 */
void
fsm_add_tag_for_updates(char *tag_name, char *plugin_name)
{
    struct fsm_dpi_client_tags *dpi_tag;
    struct fsm_mgr *mgr;

    LOGT("%s: Looking for tag %s", __func__, tag_name);

    mgr = fsm_get_mgr();

    /* check if the tag is in monitor list already */
    dpi_tag = ds_tree_find(&mgr->dpi_client_tags_tree, tag_name);
    if (dpi_tag != NULL) return;

    LOGI("%s: Adding tag %s (session: %s) to monitor list",
         __func__, tag_name, plugin_name);

    dpi_tag = CALLOC(1, sizeof(struct fsm_dpi_client_tags));
    dpi_tag->name = STRDUP(tag_name);
    dpi_tag->client_plugin_name = STRDUP(plugin_name);
    ds_tree_insert(&mgr->dpi_client_tags_tree, dpi_tag, dpi_tag->name);
}

/**
 * @brief initializes a dpi plugin client session
 *
 * @param session the session to initialize
 * @return true if the initialization succeeded, false otherwise
 */
bool
fsm_update_dpi_plugin_client(struct fsm_session *session)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct fsm_session *dpi_plugin;
    om_tag_list_entry_t *tag_item;
    char *dpi_plugin_handler;
    char *attributes_tag;
    ds_tree_t *sessions;
    char tag_name[256];
    ds_tree_t *tree;
    om_tag_t *tag;
    int tag_type;
    char *tag_s;

    /* Look up the dpi plugin handler in the other_config settings */
    dpi_plugin_handler = session->ops.get_config(session, DPI_PLUGIN);

    /* Bail if not provided */
    if (dpi_plugin_handler == NULL) return false;

    /* Look up the corresponding session */
    sessions = fsm_get_sessions();

    dpi_plugin = ds_tree_find(sessions, dpi_plugin_handler);
    /*
     * The dpi plugin session might not yet be configured
     * Bail now, the registration of the client plugin will resume
     * once the dpi plugin is configured.
     */
    if (dpi_plugin == NULL) return true;

    /* Validate access to the dpi plugin registration callback */
    dpi_plugin_ops = &dpi_plugin->p_ops->dpi_plugin_ops;
    if (dpi_plugin_ops->register_client == NULL) return false;

    /* Access the tag containing the attributes we care about */
    attributes_tag = session->ops.get_config(session, FLOW_ATTRIBUTES);
    /* It's acceptable to get no tag */
    if (attributes_tag == NULL) return true;

    /* Get the tag type */
    tag_type = om_get_type_of_tag(attributes_tag);
    if (tag_type == -1) return false;

    if (tag_type == OM_TLE_FLAG_NONE) tag_type = 0;

    /* Get the actual tag from its name */
    tag = om_tag_find(attributes_tag);

    /* The tag might not yet be configured. Get its name for registering updates */
    if (tag == NULL)
    {
        tag_s = attributes_tag + 2;
        if (*tag_s == TEMPLATE_DEVICE_CHAR) tag_s += 1;
        else if (*tag_s == TEMPLATE_CLOUD_CHAR) tag_s += 1;
        else if (*tag_s == TEMPLATE_LOCAL_CHAR) tag_s += 1;

        /* Copy tag name, remove end marker */
        STRSCPY_LEN(tag_name, tag_s, -1);
        tag_s = tag_name;
    }
    else
    {
        tag_s = tag->name;
    }

    /* add tag for getting updates */
    fsm_add_tag_for_updates(tag_s, session->name);

    /* If no tag found, it might not yet have been created */
    if (tag == NULL) return true;

    /* Register all attributes */
    tree = &tag->values;
    ds_tree_foreach(tree, tag_item)
    {
        /* Check for matching type */
        if (tag_type && !(tag_item->flags & tag_type)) continue;

        /* Register the client */
        fsm_dpi_register_client(dpi_plugin, session, tag_item->value);
    }

    return true;
}

void
fsm_print_one_dpi_client(struct dpi_client* client)
{
    struct reg_client_session *one_session;

    /* Don't proceed with a loop if we are not enabled */
    if (!LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) return;

    LOGT("%s: ---> %s : %d callback", __func__, client->attr, client->num_sessions);
    ds_tree_foreach(&client->reg_sessions, one_session)
        LOGT("%s: --->---> %s", __func__, one_session->session->name);
}

void
fsm_print_dpi_clients(ds_tree_t *tree)
{
    struct dpi_client *client;

    /* Don't proceed with a loop if we are not enabled */
    if (!LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) return;

    LOGT("%s: Start", __func__);
    ds_tree_foreach(tree, client)
    {
        if (client->num_sessions) fsm_print_one_dpi_client(client);
    }
    LOGT("%s: End", __func__);
}


static int attr_cmp(const void *attr1, const void *attr2)
{
    return strcmp((const char *)attr1, (const char *)attr2);
}

/**
 * @brief registers a dpi client to a dpi plugin for a specific flow attribute
 *
 * @param dpi_plugin_session the dpi plugin to register to
 * @param dpi_client_session the registering dpi client
 * @param attr the flow attribute
 *
 * Stores the flow attribute <-> session on behalf of the dpi plugin,
 * and triggers the dpi specific binding
 */
void
fsm_dpi_register_client(struct fsm_session *dpi_plugin_session,
                        struct fsm_session *dpi_client_session,
                        char *attr)
{
    struct reg_client_session *new_client_session;
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct fsm_dpi_plugin *dpi_plugin;
    struct dpi_client *new_client;
    struct dpi_client *one_client;
    ds_tree_t *dpi_clients;
    bool register_client;

    /* Validate access to the dpi plugin registration callback */
    dpi_plugin_ops = &dpi_plugin_session->p_ops->dpi_plugin_ops;
    if (dpi_plugin_ops->register_client == NULL) return;
    if (dpi_plugin_ops->flow_attr_cmp == NULL) return;

    /* Get the dpi plugin context */
    dpi_plugin = &dpi_plugin_session->dpi->plugin;
    dpi_clients = &dpi_plugin->dpi_clients;

    new_client = NULL;
    register_client = false;

    LOGT("%s: Registering %s for plugin %s", __func__, attr, dpi_client_session->name);
    one_client = ds_tree_find(dpi_clients, attr);

    /* The attribute is not yet monitored by anyone, create entry */
    if (one_client == NULL)
    {
        new_client = CALLOC(1, sizeof(*new_client));
        if (new_client == NULL) goto err_free_attr_node;

        new_client->attr = STRDUP(attr);
        if (new_client->attr == NULL) goto err_free_attr_node;

        ds_tree_init(&new_client->reg_sessions, attr_cmp,
                     struct reg_client_session, next);
        new_client->num_sessions = 0;

        /* Now we can add the entry */
        ds_tree_insert(dpi_clients, new_client, new_client->attr);

        /* We still need to add the session to this list */
        one_client = new_client;

        /* We need to register a callback for this attribute */
        register_client = true;
    }

    /* New client session */
    new_client_session = CALLOC(1, sizeof(*new_client_session));
    if (new_client_session == NULL) goto exit_free_new_client_session;
    new_client_session->name = dpi_plugin_session->name;  /* No need for a copy */
    new_client_session->session = dpi_client_session;

    /* Now add to this client the attribute */
    ds_tree_insert(&one_client->reg_sessions, new_client_session, new_client_session->name);
    one_client->num_sessions++;

    /* Ensure we are only registering one callback per attribute */
    if (register_client)
    {
        dpi_plugin_ops->register_client(dpi_plugin_session, dpi_client_session, attr);
    }

    fsm_print_dpi_clients(dpi_clients);

    return;

exit_free_new_client_session:
    if (new_client_session) FREE(new_client_session->name);
    FREE(new_client_session);

err_free_attr_node:
    if (new_client) FREE(new_client->attr);
    FREE(new_client);
}

/**
 * @brief free a dpi_client node
 *
 * @param dpi_client_node the client node to free
 */
static void
fsm_free_dpi_client_node(struct dpi_client *dpi_client_node)
{
    FREE(dpi_client_node->attr);
    FREE(dpi_client_node);
}

/**
 * @brief unregister all dpi clients for the given attribute
 *
 * @param dpi_plugin_session dpi plugin to unregister
 * @param dpi_client_session dpi client to unregister
 * @param attr the flow attribute
 */
void
fsm_dpi_unregister_client(struct fsm_session *dpi_plugin_session,
                          struct fsm_session *dpi_client_session,
                          char *attr)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct reg_client_session *client_session;
    struct reg_client_session *remove_session;
    struct reg_client_session *next_session;
    struct fsm_dpi_plugin *dpi_plugin;
    struct dpi_client *attr_clients;
    ds_tree_t *reg_sessions;
    ds_tree_t *attr_tree;
    int rc;

    LOGD("%s: Unregistering flow attribute %s from %s",
          __func__, attr, dpi_client_session->name);

    /* Validate access to the dpi plugin registration callback */
    dpi_plugin_ops = &dpi_plugin_session->p_ops->dpi_plugin_ops;
    if (dpi_plugin_ops->unregister_client == NULL) return;
    if (dpi_plugin_ops->flow_attr_cmp == NULL) return;

    /* Get the dpi plugin context */
    dpi_plugin = &dpi_plugin_session->dpi->plugin;
    attr_tree = &dpi_plugin->dpi_clients;

    /* Bail if the attribute is not registered */
    attr_clients = ds_tree_find(attr_tree, attr);
    if (attr_clients == NULL) return;

    /* Scan all the registered sessions, and find the one matching the client */
    reg_sessions = &attr_clients->reg_sessions;

    client_session = ds_tree_head(reg_sessions);
    while (client_session != NULL)
    {
        next_session = ds_tree_next(reg_sessions, client_session);
        remove_session = client_session;

        LOGT("%s: Checking on %s against %s",
             __func__, dpi_client_session->name, remove_session->session->name);
        rc = strcmp(dpi_client_session->name, remove_session->session->name);
        if (rc == 0)
        {
            LOGD("%s: Delete monitoring on %s for %s",
                 __func__, attr, dpi_client_session->name);
            ds_tree_remove(reg_sessions, remove_session);
            FREE(remove_session);
            attr_clients->num_sessions--;
        }

        client_session = next_session;
    }

    if (attr_clients->num_sessions != 0)
    {
        LOGD("%s: Attribute %s still registered with %d callback",
             __func__, attr, attr_clients->num_sessions);
        fsm_print_dpi_clients(attr_tree);
        return;
    }

    LOGD("%s: Delete the attribute from the registered list", __func__);

    /* unregister the client for the given attribute */
    ds_tree_remove(attr_tree, attr_clients);

    dpi_plugin_ops->unregister_client(dpi_plugin_session, attr_clients->attr);
    fsm_free_dpi_client_node(attr_clients);

    fsm_print_dpi_clients(attr_tree);
}

/**
 * @brief free memory used by client plugin name and tag name.
 *
 * @param dpi_tag pointer to struct fsm_dpi_client_tags holding
 *        session name and tag name
 */
static void
fsm_free_plugin_tags(struct fsm_dpi_client_tags *dpi_tag)
{
    FREE(dpi_tag->client_plugin_name);
    FREE(dpi_tag->name);
}

/**
 * @brief loop through the tags tree and return the tag
 *        name with the given session name.
 *
 * @param name name of the session
 * @param dpi_client_session the registering dpi client
 * @return pointer to fsm_dpi_tag if found else NULL
 */
struct fsm_dpi_client_tags *
fsm_get_tag_by_name(struct fsm_mgr *mgr, const char *name)
{
    struct fsm_dpi_client_tags *dpi_tag;
    int rc;

    ds_tree_foreach(&mgr->dpi_client_tags_tree, dpi_tag)
    {
        rc = strcmp(dpi_tag->client_plugin_name, name);

        /* return the required tag */
        if (rc == 0) return dpi_tag;
    }
    return NULL;
}

/**
 * @brief unregisters a dpi plugin
 *
 * @param dpi_plugin_session the dpi plugin to register to

 */
void
fsm_dpi_unregister_clients(struct fsm_session *dpi_plugin_session)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct fsm_dpi_client_tags *dpi_tag;
    struct fsm_dpi_plugin *dpi_plugin;
    struct dpi_client *remove;
    struct dpi_client *client;
    struct dpi_client *next;
    struct fsm_mgr *mgr;
    ds_tree_t *tree;

    /* Validate access to the dpi plugin registration callback */
    dpi_plugin_ops = &dpi_plugin_session->p_ops->dpi_plugin_ops;
    if (dpi_plugin_ops->unregister_client == NULL) return;
    if (dpi_plugin_ops->flow_attr_cmp == NULL) return;

    /* Get the dpi plugin context */
    if (dpi_plugin_session->dpi == NULL) return;

    dpi_plugin = &dpi_plugin_session->dpi->plugin;
    if (dpi_plugin->clients_init == false) return;

    tree = &dpi_plugin->dpi_clients;

    client = ds_tree_head(tree);
    while (client != NULL)
    {
        next = ds_tree_next(tree, client);
        remove = client;
        ds_tree_remove(tree, remove);
        dpi_plugin_ops->unregister_client(dpi_plugin_session, remove->attr);
        fsm_free_dpi_client_node(remove);
        client = next;
    }

    /* get the tag name associated with this session */
    mgr = fsm_get_mgr();
    dpi_tag = fsm_get_tag_by_name(mgr, dpi_plugin_session->name);
    if (dpi_tag == NULL) return;

    ds_tree_remove(&mgr->dpi_client_tags_tree, dpi_tag);
    fsm_free_plugin_tags(dpi_tag);
    FREE(dpi_tag);
}


/**
 * @brief registers dpi clients to a dpi plugin
 *
 * @param dpi_plugin_session the dpi plugin to register to
 */
void
fsm_dpi_register_clients(struct fsm_session *dpi_plugin_session)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    union fsm_dpi_context *dpi_context;
    struct fsm_dpi_plugin *dpi_plugin;
    struct fsm_session *session;
    ds_tree_t *sessions;
    ds_tree_t *tree;
    dpi_context = dpi_plugin_session->dpi;
    if (dpi_context == NULL) return;

    /* Validate access to the dpi plugin registration callback */
    dpi_plugin_ops = &dpi_plugin_session->p_ops->dpi_plugin_ops;
    if (dpi_plugin_ops->register_client == NULL) return;
    if (dpi_plugin_ops->flow_attr_cmp == NULL) return;

    dpi_plugin = &dpi_context->plugin;
    if (!dpi_plugin->clients_init)
    {
        tree = &dpi_plugin->dpi_clients;

        ds_tree_init(tree, dpi_plugin_ops->flow_attr_cmp,
                     struct dpi_client, next);
        dpi_plugin->clients_init = true;
    }

    sessions = fsm_get_sessions();
    ds_tree_foreach(sessions, session)
    {
        bool rc;

        rc = fsm_is_dpi_client(session);
        if (!rc) continue;

        fsm_update_dpi_plugin_client(session);
    }
}


static int
fsm_dpi_action_weight[] =
{
    [FSM_DPI_CLEAR] = 0,
    [FSM_DPI_IGNORED] = 10,
    [FSM_DPI_PASSTHRU] = 20,
    [FSM_DPI_INSPECT] = 30,
    [FSM_DPI_DROP] = 40,
};


/**
 * @brief call back registered client(s)
 *
 * @param dpi_plugin_session the dpi plugin session
 * @param attr the attribute to trigger the report
 * @param value the value of the attribute
 */
int
fsm_dpi_call_client(struct fsm_session *dpi_plugin_session, const char *attr,
                    uint8_t type, uint16_t length, const void *value,
                    struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct fsm_dpi_plugin_client_ops *dpi_client_plugin_ops;
    struct reg_client_session *client_session;
    struct fsm_session *dpi_client_session;
    struct dpi_client *clients;
    int weight_max_idx;
    ds_tree_t *attrs;
    int weight;
    int ret;
    int rc;

    /* This is the default behavior */
    ret = FSM_DPI_IGNORED;
    weight = fsm_dpi_action_weight[ret];

    /* look up the client sessions */
    attrs = &dpi_plugin_session->dpi->plugin.dpi_clients;
    clients = ds_tree_find(attrs, (char *)attr);
    if (clients == NULL) return ret;
    if (clients->num_sessions == 0) return ret;

    weight_max_idx = (int)(ARRAY_SIZE(fsm_dpi_action_weight));
    ds_tree_foreach(&clients->reg_sessions, client_session)
    {
        dpi_client_session = client_session->session;

        /* Access the client call back */
        dpi_client_plugin_ops = &dpi_client_session->p_ops->dpi_plugin_client_ops;
        if (dpi_client_plugin_ops->process_attr == NULL) continue;

        LOGT("%s: Calling %s for attribute %s",
             __func__, client_session->session->name, attr);

        /* Return value needs to be somehow combined (ignore PASSTHRU, etc) */
        rc = dpi_client_plugin_ops->process_attr(dpi_client_session, attr, type,
                                                 length, value, pkt_info);
        if ((rc < 0) || (rc >= weight_max_idx)) continue;
        if (fsm_dpi_action_weight[rc] > weight)
        {
            LOGD("%s: Return value already set to %d before %s. Now %d",
                    __func__, ret, dpi_client_session->name, rc);
            weight = fsm_dpi_action_weight[rc];
            ret = rc;
        }
    }

    return ret;
}

/**
 * @brief free the dpi resources of a dpi_plugin_client session
 *
 * @param session the session to free
 */
void
fsm_free_dpi_plugin_client(struct fsm_session *session)
{
    struct fsm_dpi_client_tags *dpi_tag;
    struct fsm_session *dpi_plugin;
    om_tag_list_entry_t *tag_item;
    char *dpi_plugin_handler;
    ds_tree_t *tag_values;
    char *attributes_tag;
    ds_tree_t *sessions;
    struct fsm_mgr *mgr;
    om_tag_t *tag;

    /* Look up the dpi plugin handler in the other_config settings */
    dpi_plugin_handler = session->ops.get_config(session, DPI_PLUGIN);
    if (dpi_plugin_handler == NULL) return;

    /* Look up the corresponding session */
    sessions = fsm_get_sessions();
    dpi_plugin = ds_tree_find(sessions, dpi_plugin_handler);
    if (dpi_plugin == NULL) return;

    attributes_tag = session->ops.get_config(session, FLOW_ATTRIBUTES);
    if (attributes_tag == NULL) return;

    /* Get the actual tag from its name */
    tag = om_tag_find(attributes_tag);
    if (tag == NULL) return;

    tag_values = &tag->values;
    ds_tree_foreach(tag_values, tag_item)
    {
        fsm_dpi_unregister_client(dpi_plugin, session, tag_item->value);
    }

    /* get the tag name associated with this session */
    mgr = fsm_get_mgr();
    dpi_tag = fsm_get_tag_by_name(mgr, session->name);
    if (dpi_tag == NULL) return;

    ds_tree_remove(&mgr->dpi_client_tags_tree, dpi_tag);
    fsm_free_plugin_tags(dpi_tag);
    FREE(dpi_tag);
}
