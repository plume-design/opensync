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

#include <dlfcn.h>

#include "iotm_ovsdb.h"
#include "iotm_plug_event.h"
#include "iotm_router.h"
#include "iotm_service.h"

#include "iotm_session.h"
#include "iotm_plug_event.h"


// End Private Prototypes
bool validate_schema_tree(void *converted, int len)
{
    if (len == 0) return true;
    if (converted == NULL) return false;

    return true;
}

char *iotm_get_other_config_val(struct iotm_session *session, char *key)
{
    struct iotm_session_conf *fconf;
    struct str_pair *pair;
    ds_tree_t *tree;

    if (session == NULL) return NULL;

    fconf = session->conf;
    if (fconf == NULL) return NULL;

    tree = fconf->other_config;
    if (tree == NULL) return NULL;

    pair = ds_tree_find(tree, key);
    if (pair == NULL) return NULL;

    return pair->value;
}

    void
iotm_free_session_conf(struct iotm_session_conf *conf)
{
    if (conf == NULL) return;

    free(conf->handler);
    free(conf->plugin);
    free_str_tree(conf->other_config);
    free(conf);
}

/**
 * @brief walks the tree of sessions
 *
 * Debug function, logs each tree entry
 */
void iotm_walk_sessions_tree(void)
{
    ds_tree_t *sessions = iotm_get_sessions();
    struct iotm_session *session = ds_tree_head(sessions);

    LOGT("Walking sessions tree");
    while (session != NULL)
    {
        LOGT(", handler: %s, topic: %s",
                session->name ? session->name : "None",
                session->topic ? session->topic : "None");
        session = ds_tree_next(sessions, session);
    }
}

void iotm_delete_session(struct schema_IOT_Manager_Config *conf)
{
    struct iotm_session *session;
    ds_tree_t *sessions;

    sessions = iotm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);

    if (session == NULL) return;

    ds_tree_remove(sessions, session);
    iotm_free_session(session);
    iotm_walk_sessions_tree();
}

bool iotm_session_update(struct iotm_session *session,
        struct schema_IOT_Manager_Config *conf)
{
    struct iotm_session_conf *fconf;
    ds_tree_t *other_config;
    bool check;

    fconf = session->conf;

    /* Free old conf. Could be more efficient */
    if (session->conf) {
        iotm_free_session_conf(fconf);
        session->conf = NULL;
    }

    fconf = calloc(1, sizeof(*fconf));
    if (fconf == NULL) return false;
    session->conf = fconf;

    if (strlen(conf->handler) == 0) goto err_free_fconf;

    fconf->handler = strdup(conf->handler);
    if (fconf->handler == NULL) goto err_free_fconf;

    if (strlen(conf->plugin) != 0)
    {
        fconf->plugin = strdup(conf->plugin);
        if (fconf->plugin == NULL) goto err_free_fconf;
    }

    /* Get new conf */
    other_config = schema2tree(sizeof(conf->other_config_keys[0]),
            sizeof(conf->other_config[0]),
            conf->other_config_len,
            conf->other_config_keys,
            conf->other_config);

    check = validate_schema_tree(session->conf, conf->other_config_len);
    if (!check) goto err_free_fconf;

    fconf->other_config = other_config;
    session->topic = iotm_get_other_config_val(session, "mqtt_v");

    return true;

err_free_fconf:
    iotm_free_session_conf(fconf);
    session->conf =  NULL;
    session->topic = NULL;

    return false;
}

    void
iotm_free_session(struct iotm_session *session)
{

    LOGI("%s: Cleaning the session: %s\n",
            __func__, session->name);

    if (session == NULL) return;

    /* Call the session exit routine */
    if (session->ops.exit != NULL) session->ops.exit(session);

    /* Close the dynamic library handler */
    if (session->handle != NULL) dlclose(session->handle);

    /* Free the config settings */
    iotm_free_session_conf(session->conf);

    /* Free the dso path string */
    free(session->dso);

    /* Free the session name */
    free(session->name);

    /* Finally free the session */
    free(session);
}

struct iotm_session *get_session(ds_tree_t *sessions, char *name)
{
    return ds_tree_find(sessions, name);
}

struct iotm_event *session_event_get(struct iotm_session *session, char *ev)
{
    if (session == NULL) return NULL;
    return ds_tree_find(session->events, ev);
}

struct pass_tag_ctx_t
{
    void *ctx;
    void (*cb)(char *key, char *value, void *ctx);
};

void pass_tag(ds_list_t *dl, struct iotm_value_t *val, void *ctx)
{
    struct pass_tag_ctx_t *tag_ctx = (struct pass_tag_ctx_t *)ctx;
    tag_ctx->cb(val->key, val->value, tag_ctx->ctx);
}
void session_foreach_tag(
        char *tag,
        void (*cb)(char *, char *, void *),
        void *ctx)
{
    struct iotm_tree_t *tags = iotm_get_tags();
    if (tags == NULL || tags->len == 0) return;

    struct pass_tag_ctx_t context =
    {
        .cb = cb,
        .ctx = ctx,
    };
    if (tag != NULL)
    {
        // trying to iterate over a specific tag list
        struct iotm_list_t *tag_list = iotm_tree_get(tags, tag);
        iotm_list_foreach(tag_list, pass_tag, &context);
    }
    else iotm_tree_foreach_value(tags, pass_tag, &context);
}

struct iotm_session *iotm_alloc_session(struct schema_IOT_Manager_Config *conf)
{
    struct iotm_session *session;
    struct iotm_mgr *mgr;
    bool ret;

    mgr = iotm_get_mgr();

    session = calloc(1, sizeof(struct iotm_session));
    if (session == NULL) return NULL;

    session->name = strdup(conf->handler);
    if (session->name == NULL) goto err_free_session;

    session->events = iotm_get_events();
    session->tl_ctx_tree = mgr->tl_ctx_tree;
    session->mqtt_headers = mgr->mqtt_headers;
    session->location_id = mgr->location_id;
    session->node_id = mgr->node_id;
    session->loop = mgr->loop;
    session->ops.send_report = iotm_send_report;
    session->ops.send_pb_report = iotm_send_pb_report;
    session->ops.get_config = iotm_get_other_config_val;
    session->ops.get_event = session_event_get;
    session->ops.update_tag = ovsdb_upsert_tag;
    session->ops.update_rules = ovsdb_upsert_rules;
    session->ops.remove_rules = ovsdb_remove_rules;
    session->ops.emit = emit;
    session->ops.plugin_event_new = plugin_event_new;
    session->ops.foreach_tag = session_foreach_tag;

    ret = iotm_session_update(session, conf);
    if (!ret) goto err_free_session;

    ret = iotm_parse_dso(session);
    if (!ret) goto err_free_session;

    return session;

err_free_session:
    if ( session->conf ) iotm_free_session_conf(session->conf);
    if (session->name) free(session->name);
    if (session) free(session);

    return NULL;
}

void iotm_modify_session(struct schema_IOT_Manager_Config *conf)
{
    struct iotm_session *session;
    ds_tree_t *sessions;

    sessions = iotm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);

    if (session == NULL) return;

    iotm_session_update(session, conf);
    if (session->ops.update != NULL) session->ops.update(session);
}

void iotm_add_session(struct schema_IOT_Manager_Config *conf)
{
    struct iotm_session *session;
    struct iotm_mgr *mgr;
    ds_tree_t *sessions;
    bool ret;

    mgr = iotm_get_mgr();
    sessions = iotm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);

    if (session != NULL) return;

    /* Allocate a new session, insert it to the sessions tree */
    session = iotm_alloc_session(conf);
    if (session == NULL)
    {
        LOGE("Could not allocate session for handler %s",
                conf->handler);
        return;
    }
    ds_tree_insert(sessions, session, session->name);

    ret = mgr->init_plugin(session);
    if (!ret)
    {
        LOGE("%s: plugin handler %s initialization failed",
                __func__, session->name);
        goto err_free_session;

    }

    iotm_walk_sessions_tree();
    return;

err_free_session:
    ds_tree_remove(sessions, session);
    iotm_free_session(session);

    return;
}
