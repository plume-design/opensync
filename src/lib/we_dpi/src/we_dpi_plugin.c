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

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "dpi_stats.h"
#include "log.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "nf_utils.h"
#include "os_time.h"
#include "osp_objm.h"
#include "ovsdb.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "we_dpi_conntrack.h"
#include "we_dpi_externals.h"
#include "we_dpi_plugin.h"

#include "we.h"

#define WE_AGENT_CORO_PRIV "r"
#define WE_AGENT_CORO_UPDATE "update_coroutine"
#define WE_AGENT_CORO_PERIODIC "periodic"
#define WE_AGENT_CORO_CONNTRACK_PERIODIC "conntrack_periodic"
#define WE_AGENT_CORO_CONFIG_UPDATE "handle_config_update"
#define WE_AGENT_SLEN(s) (sizeof((s)) - 1)

#define AGENT_REGISTERS_MAX 32

#define WE_AGENT_OBJ_STORE_NAME "agent"
#define WE_AGENT_BIN_PATH "usr/we/etc/agent.bin"
#define WE_AGENT_BIN_PATH_LEN (PATH_MAX + 1)

static void *we_ct_task(void *args);
void we_dpi_plugin_exit(struct fsm_session *fsm);
void we_dpi_plugin_periodic(struct fsm_session *fsm);
void we_dpi_plugin_handler(struct fsm_session *fsm, struct net_header_parser *np);
static inline int handle_we_call_result(we_state_t s, struct we_dpi_agent_userdata *user, int result);

static struct we_dpi_plugin_cache cache_mgr = {
    .initialized = false,
};

static ovsdb_table_t table_WE_Config;

struct we_dpi_plugin_cache *we_dpi_get_mgr(void)
{
    return &cache_mgr;
}

static we_state_t we_state_from_coroutine(we_state_t s, const char *name, size_t len)
{
    we_state_t coroutine;

    we_pushstr(s, len, name);
    we_get(s, 1);
    we_pushstr(s, WE_AGENT_SLEN(WE_AGENT_CORO_PRIV), WE_AGENT_CORO_PRIV);
    we_get(s, we_top(s) - 1);
    assert(we_type(s, we_top(s)) == WE_ARR);
    we_read(s, we_top(s), WE_ARR, &coroutine);
    we_pop(s); /* coroutine array */
    we_pop(s); /* coroutine struct */
    return coroutine;
}

static int coroutine_resume(we_state_t s, const char *func, size_t len, struct we_dpi_agent_userdata *user)
{
    we_state_t coroutine;
    /* Get the periodic thread and resume it */
    coroutine = we_state_from_coroutine(s, func, len);
    return we_call(&coroutine, user);
}

static bool agent_binary_path_from_store(struct fsm_object *obj, char *path, size_t pathlen, size_t *outlen)
{
    bool r;
    int len;
    char dir[PATH_MAX + 1] = {0};
    r = osp_objm_path(dir, sizeof(dir), obj->object, obj->version);
    if (!r) return false;
    len = snprintf(path, pathlen, "%s/%s", dir, WE_AGENT_BIN_PATH);
    if (outlen != NULL) *outlen = len;
    return (len > 0 && (size_t)len <= pathlen);
}

static int update_coroutine_resume(
        we_state_t s,
        char *binary_path,
        size_t pathlen,
        struct we_dpi_agent_userdata *user,
        char **version)
{
    char *str;
    int r;
    we_state_t coroutine;

    we_pushbuf(s, WE_AGENT_SLEN(WE_AGENT_CORO_UPDATE), WE_AGENT_CORO_UPDATE);
    we_get(s, 1);
    we_pushstr(s, WE_AGENT_SLEN(WE_AGENT_CORO_PRIV), WE_AGENT_CORO_PRIV); /* Private member of the coroutine struct */
    we_get(s, we_top(s) - 1);
    assert(we_type(s, we_top(s)) == WE_ARR);
    we_read(s, we_top(s), WE_ARR, &coroutine);
    /* coroutine array */
    we_pop(s);
    /* coroutine struct */
    we_pop(s);

    r = we_read(coroutine, we_top(coroutine), WE_BUF, &str);
    if (version) *version = strndup(str, r);
    /* pop the value that the coroutine yielded */
    we_pop(coroutine);
    /* push the path for the new binary */
    we_pushstr(coroutine, pathlen, binary_path);
    /* resume the coroutine
     *
     * this should return -EAGAIN, indicating that an update has occurred.
     */
    r = we_call(&coroutine, user);
    return handle_we_call_result(s, user, r);
}

static int mark_old_agent_obsolste(struct fsm_session *fsm, struct fsm_object *object, char *version)
{
    struct fsm_object stale;
    stale.object = object->object;
    stale.version = version;
    stale.state = FSM_OBJ_OBSOLETE;
    fsm->ops.state_cb(fsm, &stale);
    return 0;
}

static int update_agent(struct fsm_session *fsm, struct we_dpi_session *dpi, struct fsm_object *obj)
{
    bool r;
    char *version;
    int res = -1;
    char binary_path[WE_AGENT_BIN_PATH_LEN] = {0};
    size_t pathlen;
    we_state_t agent;
    struct we_dpi_agent_userdata user = {.fsm = fsm, .np = NULL};

    obj->state = FSM_OBJ_LOAD_FAILED;

    r = agent_binary_path_from_store(obj, binary_path, sizeof(binary_path), &pathlen);
    if (!r) goto err;

    agent = dpi->we_state;
    res = update_coroutine_resume(agent, binary_path, pathlen, &user, &version);
    if (res == 0)
    {
        obj->state = FSM_OBJ_ACTIVE;
        mark_old_agent_obsolste(fsm, obj, version);
    }
    free(version);
err:
    fsm->ops.state_cb(fsm, obj);
    return res;
}

static int load_signature_bundle(we_state_t s, const char *path)
{
    int err = 0;
    size_t rlen;
    uint8_t *bin;
    const size_t nitems = 1;
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        LOGW("%s: failed to open WE agent at %s", __func__, path);
        return -EINVAL;
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if ((err = we_pushstr(s, fsize, NULL)) < 0) goto error;
    we_read(s, we_top(s), WE_BUF, &bin);
    rlen = fread(bin, fsize, nitems, fp);
    if (rlen != nitems)
    {
        err = ferror(fp);
        goto error;
    }
error:
    fclose(fp);
    return err;
}

static int load_agent(struct we_dpi_session *dpi, char *path)
{
    int res = -1;

    if (dpi == NULL)
    {
        LOGN("%s: No dpi_session\n", __func__);
        return -1;
    }

    if (we_dpi_setup_agent_externals() != 0)
    {
        LOGE("%s: failed to setup we externals\n", __func__);
        return -1;
    }

    res = we_create(&dpi->we_state, AGENT_REGISTERS_MAX);
    if (res < 0)
    {
        LOGE("%s: failed to create we_state: %d", __func__, res);
        return res;
    }

    res = load_signature_bundle(dpi->we_state, path);
    if (res != 0)
    {
        LOGE("%s: failed to load agent bundle at %s", __func__, path);
        goto cleanup_we_state;
    }

    res = we_pushtab(dpi->we_state, NULL); /* globals */
    if (res < 0)
    {
        LOGE("%s: failed to initialize globals", __func__);
        goto cleanup_we_state;
    }
    we_pusharr(dpi->we_state, NULL); /* empty args (...) */
    if (res < 0)
    {
        LOGE("%s: failed to initialize main args", __func__);
        goto cleanup_we_state;
    }
    res = we_call(&dpi->we_state, NULL); /* start main */
    res = handle_we_call_result(dpi->we_state, NULL, res);
    if (res < 0)
    {
        LOGE("Failed to start agent");
        goto cleanup_we_state;
    }
    we_pop(dpi->we_state);
    LOGN("%s: loaded agent", __func__);
    return res;
cleanup_we_state:
    we_destroy(dpi->we_state);
    dpi->we_state = NULL;
    return res;
}

static int bootstrap_agent(struct fsm_session *fsm)
{
    struct fsm_object *obj;
    struct we_dpi_session *dpi;
    char path[WE_AGENT_BIN_PATH_LEN];
    bool got_path;
    int res = -1;

    dpi = (struct we_dpi_session *)fsm->handler_ctxt;
    if (dpi == NULL)
    {
        LOGN("%s: No dpi_session\n", __func__);
        return -1;
    }

    obj = fsm->ops.best_obj_cb(fsm, WE_AGENT_OBJ_STORE_NAME);

    if (obj == NULL) return -1;

    obj->state = FSM_OBJ_LOAD_FAILED;
    got_path = agent_binary_path_from_store(obj, path, sizeof(path), NULL);
    if (!got_path)
    {
        LOGW("%s: failed to get agent binary path for %s", __func__, WE_AGENT_OBJ_STORE_NAME);
        res = -1;
        goto cleanup;
    }

    res = load_agent(dpi, path);
    if (res == 0)
    {
        obj->state = FSM_OBJ_ACTIVE;
    }
cleanup:
    fsm->ops.state_cb(fsm, obj);
    free(obj);
    return res;
}

static void we_dpi_agent_update(struct fsm_session *fsm, struct fsm_object *obj, int event_type)
{
    struct we_dpi_session *dpi = (struct we_dpi_session *)(fsm->handler_ctxt);
    if (dpi == NULL) return;

    switch (event_type)
    {
        case OVSDB_UPDATE_NEW:
            pthread_mutex_lock(&dpi->lock);
            update_agent(fsm, dpi, obj);
            pthread_mutex_unlock(&dpi->lock);
        default:
            break;
    }
}

static void unload_agent(struct we_dpi_session *dpi)
{
    if (!dpi->we_state) return;

    pthread_mutex_lock(&dpi->lock);
    we_destroy(dpi->we_state);
    dpi->we_state = NULL;
    pthread_mutex_unlock(&dpi->lock);
    pthread_join(dpi->thread, NULL);
    dpi->initialized = false;
}

/* Configuration and OVSDB integration */

#define AGENT_CONFIG_KEY_TYPE "type"
#define AGENT_CONFIG_KEY_NEW "new"
#define AGENT_CONFIG_KEY_OLD "old"

#define AGENT_CONFIG_KEYLEN(s) (sizeof((s)) - 1)

/* Push a new configuration map into the agent */
static int set_agent_config(
        we_state_t agent,
        ovsdb_update_type_t type,
        struct schema_WE_Config *old,
        struct schema_WE_Config *new)
{
    int i;
    int err;
    we_state_t coroutine;
    coroutine = we_state_from_coroutine(agent, WE_AGENT_CORO_CONFIG_UPDATE, WE_AGENT_SLEN(WE_AGENT_CORO_CONFIG_UPDATE));

    int reg = we_pushtab(coroutine, NULL);

    we_pushbuf(coroutine, AGENT_CONFIG_KEYLEN(AGENT_CONFIG_KEY_TYPE), AGENT_CONFIG_KEY_TYPE);
    we_pushnum(coroutine, type);
    we_set(coroutine, reg);

    we_pushbuf(coroutine, AGENT_CONFIG_KEYLEN(AGENT_CONFIG_KEY_NEW), AGENT_CONFIG_KEY_NEW);
    int new_reg = we_pushtab(coroutine, NULL);
    for (i = 0; i < new->config_len; ++i)
    {
        char *key = new->config_keys[i];
        char *val = new->config[i];
        we_pushstr(coroutine, strlen(key), key);
        we_pushstr(coroutine, strlen(val), val);
        we_set(coroutine, new_reg);
    }
    we_set(coroutine, reg);

    we_pushbuf(coroutine, AGENT_CONFIG_KEYLEN(AGENT_CONFIG_KEY_OLD), AGENT_CONFIG_KEY_OLD);
    int old_reg = we_pushtab(coroutine, NULL);
    for (i = 0; i < old->config_len; ++i)
    {
        char *key = old->config_keys[i];
        char *val = old->config[i];
        we_pushstr(coroutine, strlen(key), key);
        we_pushstr(coroutine, strlen(val), val);
        we_set(coroutine, old_reg);
    }
    we_set(coroutine, reg);

    /* Replace whatever it yielded with the new config value. */
    we_popr(coroutine, we_top(coroutine) - 1);

    /* Restart the coroutine */
    err = we_call(&coroutine, NULL);
    if (err < 0)
    {
        LOGE("%s: Failed to push a new welang config (%d: %s)\n", __func__, err, strerror(err));
    }
    return err;
}

/* The schema we use for handling configuration */
static void callback_WE_Config(ovsdb_update_monitor_t *mon, struct schema_WE_Config *old, struct schema_WE_Config *new)
{
    struct we_dpi_plugin_cache *mgr = we_dpi_get_mgr();
    we_state_t agent;

    if (!mgr->initialized) return;
    if (!mgr->dpi_session.we_state) return;

    agent = mgr->dpi_session.we_state;

    pthread_mutex_lock(&mgr->dpi_session.lock);
    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
        case OVSDB_UPDATE_DEL:
            set_agent_config(agent, mon->mon_type, old, new);
            break;
        case OVSDB_UPDATE_ERROR:
            LOGE("%s: OVSDB update error", __func__);
            break;
        default:
            LOGE("%s: unknown OVSDB event type %d", __func__, mon->mon_type);
            break;
    }
    pthread_mutex_unlock(&mgr->dpi_session.lock);
}

int we_dpi_ovsdb_init()
{
    /* Create our private configuration table */
    OVSDB_TABLE_INIT_NO_KEY(WE_Config);
    OVSDB_TABLE_MONITOR(WE_Config, false);
    return 0;
}

/* Initialization and data path */

static bool dpi_manager_init(struct we_dpi_plugin_cache *mgr)
{
    LOGD("%s: initializing dpi manager\n", __func__);

    we_dpi_ct_init();

    mgr->initialized = true;
    mgr->dpi_session.initialized = false;
    mgr->dpi_session.we_state = NULL;
    return true;
}

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the packet parsing handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int we_dpi_plugin_init(struct fsm_session *fsm)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct we_dpi_session *dpi_session;
    struct we_dpi_plugin_cache *mgr;

    if (fsm == NULL) return -1;

    mgr = we_dpi_get_mgr();

    if (mgr->initialized)
    {
        LOGD("%s: agent already initialized. Only a single instance is allowed to run", __func__);
        return -1;
    }

    if (!dpi_manager_init(mgr)) return -1;

    dpi_session = &mgr->dpi_session;

    if (dpi_session->initialized) return 0;

    fsm->ops.periodic = we_dpi_plugin_periodic;
    fsm->ops.object_cb = we_dpi_agent_update;
    fsm->ops.exit = we_dpi_plugin_exit;
    fsm->handler_ctxt = dpi_session;

    /* Set the plugin specific ops */
    dpi_plugin_ops = &fsm->p_ops->dpi_plugin_ops;
    dpi_plugin_ops->handler = we_dpi_plugin_handler;

    if (bootstrap_agent(fsm) != 0)
    {
        LOGE("%s: failed to load agent", __func__);
        return -1;
    }

    /* Must be done here to make sure we get called back with a config */
    we_dpi_ovsdb_init();

    /* Register for object callbacks to support updates */
    fsm->ops.monitor_object(fsm, WE_AGENT_OBJ_STORE_NAME);

    dpi_session->initialized = true;

    pthread_mutex_init(&dpi_session->lock, NULL);
    pthread_create(&dpi_session->thread, NULL, we_ct_task, dpi_session);

    return 0;
}

/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 */
void we_dpi_plugin_handler(struct fsm_session *fsm, struct net_header_parser *np)
{
    void *buf;
    int reg;
    int res;
    struct we_dpi_session *dpi;
    struct we_dpi_agent_userdata user = {.fsm = fsm, .np = np};

    dpi = (struct we_dpi_session *)fsm->handler_ctxt;
    if (!dpi->initialized) return;

    if (pthread_mutex_trylock(&dpi->lock) != 0) return;
    /* Pass the packet to WE, zero copy. */
    reg = we_pushbuf(dpi->we_state, np->caplen, np->start);
    /* Hold a reference to the packet in case we need to sync it */
    we_hold(dpi->we_state, reg, &buf);
    /* Resume the WE coroutine -- Process the packet. */
    res = we_call(&dpi->we_state, &user);
    if (res < 0)
    {
        // TODO: How do we handle errors in the packet path?
    }
    /* Sync the packet if necessary */
    we_sync(buf);
    /* Pop the yielded value */
    we_pop(dpi->we_state);
    pthread_mutex_unlock(&dpi->lock);
}

/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * Sends a flow stats report.
 * @param session the fsm session
 */
void we_dpi_plugin_periodic(struct fsm_session *fsm)
{
    struct we_dpi_session *dpi;
    int result;
    struct we_dpi_agent_userdata user = {.fsm = fsm, .np = NULL};

    dpi = (struct we_dpi_session *)fsm->handler_ctxt;
    if (dpi == NULL) return;
    if (!dpi->initialized) return;

    if (pthread_mutex_trylock(&dpi->lock) != 0) return;
    /* Resume the Welang periodic function */
    result = coroutine_resume(dpi->we_state, WE_AGENT_CORO_PERIODIC, WE_AGENT_SLEN(WE_AGENT_CORO_PERIODIC), &user);

    result = handle_we_call_result(dpi->we_state, &user, result);
    if (result < 0)
    {
        LOGE("Failed to run the agent periodic");
    }
    pthread_mutex_unlock(&dpi->lock);
}

/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the http session to delete
 */
void we_dpi_delete_session(struct fsm_session *fsm)
{
    struct we_dpi_plugin_cache *mgr;
    struct we_dpi_session *dpi_session;

    mgr = we_dpi_get_mgr();
    dpi_session = &mgr->dpi_session;

    if (dpi_session == NULL) return;

    LOGD("%s: removing fsm_session %s", __func__, fsm->name);
    unload_agent(dpi_session);
    mgr->initialized = false;
}

/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void we_dpi_plugin_exit(struct fsm_session *fsm)
{
    struct we_dpi_plugin_cache *mgr;

    mgr = we_dpi_get_mgr();
    if (!mgr->initialized) return;

    we_dpi_delete_session(fsm);
    we_dpi_ct_exit();
    fsm->ops.unmonitor_object(fsm, WE_AGENT_OBJ_STORE_NAME);
    ovsdb_unregister_update_cb(table_WE_Config.monitor.mon_id);
}

int dev_we_dpi_plugin_init(struct fsm_session *fsm)
{
    return we_dpi_plugin_init(fsm);
}

void dev_we_dpi_plugin_exit(struct fsm_session *fsm)
{
    we_dpi_plugin_exit(fsm);
}

static inline int handle_we_call_result(we_state_t s, struct we_dpi_agent_userdata *user, int result)
{
    if (result == -EAGAIN)
    {
        int again = we_call(&s, user);
        if (again < 0)
        {
            // Exit??
            return again;
        }
        we_pop(s);
        /* Unregister the current monitor, and re-monitor the table.
         * Re-monitoring the table will trigger a callback for the config table, allowing the new agent
         * to receive the current status of the table.
         */
        ovsdb_unregister_update_cb(table_WE_Config.monitor.mon_id);
        OVSDB_TABLE_MONITOR(WE_Config, false);
        return 0;
    }
    return result;
}

/**
 * @brief the thread that processes conntrack entries for the agent
 */
static void *we_ct_task(void *args)
{
    struct we_dpi_session *dpi = args;
    struct timespec start;
    struct timespec end;
    int64_t duration;
    const int64_t interval_seconds = 15;
    struct we_dpi_agent_userdata user = {.fsm = dpi->fsm, .np = NULL};

    while (true)
    {
        pthread_mutex_lock(&dpi->lock);
        if (dpi->we_state == NULL)
        {
            pthread_mutex_unlock(&dpi->lock);
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &start);
        coroutine_resume(
                dpi->we_state,
                WE_AGENT_CORO_CONNTRACK_PERIODIC,
                WE_AGENT_SLEN(WE_AGENT_CORO_CONNTRACK_PERIODIC),
                &user);
        clock_gettime(CLOCK_MONOTONIC, &end);
        pthread_mutex_unlock(&dpi->lock);
        duration = end.tv_sec - start.tv_sec;
        if (duration >= 0 && duration <= interval_seconds)
        {
            sleep(interval_seconds - duration); /* configurable? */
        }
    }
    pthread_exit(NULL);
}
