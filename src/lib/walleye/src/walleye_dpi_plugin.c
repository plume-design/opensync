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

#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <errno.h>

/* To load signature file */
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "const.h"
#include "ds_tree.h"
#include "log.h"
#include "walleye_dpi_plugin.h"
#include "assert.h"
#include "json_util.h"
#include "ovsdb.h"
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "net_header_parse.h"
#include "network_metadata_report.h"
#include "qm_conn.h"
#include "fsm_dpi_utils.h"
#include "fsm_policy.h"
#include "osp_objm.h"
#include "memutil.h"
#include "dpi_stats.h"
#include "kconfig.h"
#include "fsm_fn_trace.h"

/* Walleye library config */
extern int rts_handle_isolate;
extern int rts_handle_memory_size;
extern int rts_handle_dict_hash_expiry;
extern int rts_handle_dict_hash_bucket;

/* Mapping of scan errors */
#define SCAN_ERROR_INCOMPLETE (1 << 0)
#define SCAN_ERROR_LENGTH     (1 << 1)
#define SCAN_ERROR_CREATE     (1 << 2)
#define SCAN_ERROR_SCAN       (1 << 3)


/* A connection, defined by 6-tuple (vlan, saddr, daddr, sport, dport, prot) */

static struct dpi_plugin_cache
cache_mgr =
{
    .initialized = false,
    .signature_loaded = false,
};


struct dpi_plugin_cache *
dpi_get_mgr(void)
{
    return &cache_mgr;
}

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
dpi_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}

static void
save_service(rts_stream_t stream, void *user, const char *key,
             uint8_t type, uint16_t length, const void *value)
{
    int64_t index;
    struct dpi_conn *dpi = user;
    enum service_level level = service_none;

    __builtin_memcpy(&index, value, length);

    if (strcmp(key + 8, "protocol") == 0) {
        level = service_protocol;
    } else if (strcmp(key + 8, "network") == 0) {
        level = service_network;
    } else if (strcmp(key + 8, "platform") == 0) {
        level = service_platform;
    } else if (strcmp(key + 8, "application") == 0) {
        level = service_application;
    } else if (strcmp(key + 8, "feature") == 0) {
        level = service_feature;
    }

    if (level >= dpi->service_level) {
        dpi->service_level = level;
        dpi->service = (uint16_t) index;
    }
}

static void
save_tag(rts_stream_t stream, void *user, const char *key,
         uint8_t type, uint16_t length, const void *value)
{
    int64_t i, index;
    struct dpi_conn *dpi = user;
    __builtin_memcpy(&index, value, length);

    for (i = 0; i < NUM_TAGS; ++i)
    {
        if (dpi->tags[i] == (uint16_t) index)
        {
            break;
        }
        else if (dpi->tags[i] == 0)
        {
            dpi->tags[i] = (uint16_t) index;
            break;
        }
    }
}

static void
save_toldata(rts_stream_t stream, void *user, const char *key,
             uint8_t type, uint16_t length, const void *value)
{
    struct net_md_stats_accumulator *acc;
    struct dpi_conn *dpi;

    acc = (struct net_md_stats_accumulator *)user;
    dpi = acc->dpi;
    __builtin_memcpy(&dpi->toldata, value, length);
}


static void
save_tcp_syn_delay(rts_stream_t stream, void *user, const char *key,
                   uint8_t type, uint16_t length, const void *value)
{
    struct net_md_stats_accumulator *acc;
    struct dpi_conn *dpi;

    acc = (struct net_md_stats_accumulator *)user;
    dpi = acc->dpi;

    __builtin_memcpy(&dpi->tcp_syn_delay, value, length);
}


static void
save_tcp_ack_delay(rts_stream_t stream, void *user, const char *key,
                   uint8_t type, uint16_t length, const void *value)
{
    struct net_md_stats_accumulator *acc;
    struct dpi_conn *dpi;

    acc = (struct net_md_stats_accumulator *)user;
    dpi = acc->dpi;

    __builtin_memcpy(&dpi->tcp_ack_delay, value, length);
}


static void
save_server_name(rts_stream_t stream, void *user, const char *key,
                 uint8_t type, uint16_t length, const void *value)
{
    struct net_md_stats_accumulator *acc;
    struct dpi_conn *dpi;

    acc = (struct net_md_stats_accumulator *)user;
    dpi = acc->dpi;

    if (sizeof(dpi->server_name) <= length)
        length = sizeof(dpi->server_name) - 1;

    strncpy(dpi->server_name, value, length);
    dpi->server_name[length] = '\0';
}

static void
notify_client(rts_stream_t stream, void *user, const char *key,
              uint8_t type, uint16_t length, const void *value)
{
    struct fsm_dpi_plugin_client_pkt_info pkt_info;
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct net_md_stats_accumulator *acc;
    struct dpi_session *dpi_session;
    struct fsm_session *dpi_plugin;
    struct flow_key *fkey;
    struct dpi_conn *dpi;
    int rc;

    /* Stash flow tags. They will be processed upon the destruction of the stream */
    if (strcmp(key, "tag") == 0)
    {
        save_tag(stream, user, key, type, length, value);
        return;
    }
    else if(strcmp(key, "toldata") == 0)
    {
        save_toldata(stream, user, key, type, length, value);
        return;
    }
    else if (strcmp(key, "server.name") == 0)
    {
        save_server_name(stream, user, key, type, length, value);
        return;
    }

    else if (strncmp(key, "service", strlen("service")) == 0)
    {
        save_service(stream, user, key, type, length, value);
        return;
    }

    else if (strncmp(key, "tcp.client.syn.delay", strlen("tcp.client.syn.delay")) == 0)
    {
        save_tcp_syn_delay(stream, user, key, type, length, value);
        return;
    }

    else if (strncmp(key, "tcp.client.ack.delay", strlen("tcp.client.ack.delay")) == 0)
    {
        save_tcp_ack_delay(stream, user, key, type, length, value);
        return;
    }
    acc = user;
    dpi = acc->dpi;

    if (dpi->flow_action == FSM_DPI_DROP)
    {
        LOGN("%s: Flow action already set to drop, not processing further attributes", __func__);
        return;
    }

    dpi_session = dpi->dpi_sess;
    dpi_plugin = dpi_session->session;
    dpi_plugin_ops = &dpi_plugin->p_ops->dpi_plugin_ops;

    /**
     * net_parser details may not available if walleye destroys stream
     *  prior to callback.
     */
    pkt_info.acc = acc;
    pkt_info.parser = dpi_session->parser.net_parser;

    rc = dpi_plugin_ops->notify_client(dpi_plugin, key, type, length, value,
                                       &pkt_info);
    dpi->flow_action = rc;
    fkey = acc->fkey;
    if (rc == FSM_DPI_DROP)
    {
        LOGI(
            "%s: blocking flow src: %s, dst: %s, proto: %d, sport: %d, dport: %d",
            __func__,
            fkey->src_ip,
            fkey->dst_ip,
            fkey->protocol,
            fkey->sport,
            fkey->dport);
    }
    fsm_dpi_set_plugin_decision(dpi->dpi_sess->session, acc, rc);
}

#define CMD_LEN (C_MAXPATH_LEN * 2 + 128)

// Remove folder
static void
walleye_dpi_rmdir(char *path)
{
    char cmd[CMD_LEN];
    if (strcmp(path, "/") == 0)
    {
        LOGE("%s: removing / is not allowed: '%s'", __func__, path);
        return;
    }
    snprintf(cmd, sizeof(cmd), "rm -fr %s", path);

    LOGT("%s: rmdir: %s", __func__, cmd);
    if (cmd_log_check_safe(cmd))
    {
        return;
    }
    return;
}


// Create folder and subfolders
static bool
walleye_dpi_mkdir(char *path)
{
    char cmd[CMD_LEN];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);

    LOGT("%s: mkdir: %s", __func__, cmd);
    if (cmd_log_check_safe(cmd))
    {
        return false;
    }
    return true;
}

static int
load_signatures(struct fsm_session *session, char *store)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    int fd, res;
    struct stat sb;
    void *sig;
    const char * const path = "/usr/walleye/etc/signature.bin";
    const char * const compressed_file = "data.tar.gz";
    char * decompress_path = "/tmp/walleye";
    char signature_file[PATH_MAX+128];
    char compressed_signature[PATH_MAX+128];
    char cmd[CMD_LEN];
    bool compressed;
    int rsz;

    compressed = false;
    snprintf(signature_file, sizeof(signature_file), "%s/%s",
             store, path);
    fd = open(signature_file, O_RDONLY);
    if (fd == -1)
    {
        LOGI("%s: failed to open %s", __func__, signature_file);
        snprintf(compressed_signature, sizeof(compressed_signature), "%s/%s",
                 store, compressed_file);
        fd = open(compressed_signature, O_RDONLY);
        if (fd == -1)
        {
            LOGE("%s: failed to open %s", __func__, compressed_signature);
            return -1;
        }
        else
        {
            compressed = true;
        }
    }

    if (compressed == true)
    {
        // Create decompress directory
        if (!walleye_dpi_mkdir(decompress_path))
        {
            LOGE("%s: could not create decompression directory", __func__);
            return -1;
        }

        // Extract data under /tmp
        rsz = snprintf(cmd, sizeof(cmd), "tar -xozf %s -C %s", compressed_signature, decompress_path);
        if (rsz >= (int)sizeof(cmd))
        {
            LOGE("%s: Error extracting to storage, command line too long.", __func__);
            return -1;
        }

        LOGT("%s: Extract data command: %s", __func__, cmd);
        if (cmd_log_check_safe(cmd))
        {
            LOG(ERR, "objmfs: Extraction of data failed: %s", cmd);
            return -1;
        }
        snprintf(signature_file, sizeof(signature_file), "%s/%s",
                 "/tmp/walleye", path);
        fd = open(signature_file, O_RDONLY);
        if (fd == -1)
        {
            LOGE("%s: failed to open %s", __func__, signature_file);
        }
    }

    fstat(fd, &sb);
    sig = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (sig == MAP_FAILED)
    {
        LOGE("%s: failed to mmap signatures\n", __func__);
        close(fd);
        res = -1;
        goto cleanup;
    }

    dpi_plugin_ops = &session->p_ops->dpi_plugin_ops;
    if (dpi_plugin_ops->unregister_clients != NULL)
    {
        dpi_plugin_ops->unregister_clients(session);
    }

    if ((res = rts_load(sig, sb.st_size)) != 0)
    {
        LOGE("%s: failed to load signatures %d\n", __func__, res);
    }
    munmap(sig, sb.st_size);
    close(fd);

    if (dpi_plugin_ops->register_clients != NULL)
    {
        dpi_plugin_ops->register_clients(session);
    }

cleanup:
    walleye_dpi_rmdir("/tmp/walleye");
    return res;
}


static int
unload_signatures(void)
{
    return rts_load(NULL, 0);
}


static int
shared_free()
{
    int res;

    res = unload_signatures();

    LOGD("%s: res %d", __func__, res);
    return res;
}


static void
dpi_plugin_update(struct fsm_session *session)
{
    struct dpi_session *dpi_session;
    const char *str;
    long value;

    /* restart if rts_dict_expiry has changed */
    dpi_session = (struct dpi_session *)session->handler_ctxt;
    str = session->ops.get_config(session, "rts_dict_expiry");
    if (str != NULL)
    {
        if (dpi_session->rts_dict_expiry != (uint32_t)atoi(str))
        {
            sleep(2);
            LOGEM("%s: Walleye library config has changed. Restarting", __func__);
            exit(EXIT_SUCCESS);
        }
    }

    str = session->ops.get_config(session, "sandbox_size");
    if (str != NULL)
    {
        errno = 0;
        value = strtol(str, NULL, 10);
        if (errno == 0)
        {
            int val;

            val = (int)value * 1024 * 1024;
            if (val != rts_handle_memory_size)
            {
                sleep(2);
                LOGEM("%s: Walleye library config has changed. Restarting", __func__);
                exit(EXIT_SUCCESS);
            }
        }
    }

    fsm_set_dpi_health_stats_cfg(session);
    dpi_session->wc_topic = session->dpi_stats_report_topic;
    dpi_session->wc_interval = session->dpi_stats_report_interval;

    return;
}


int
walleye_signature_add(struct fsm_session *session, struct fsm_object *object)
{
    struct dpi_session *dpi_session;
    struct dpi_plugin_cache *mgr;
    char store[PATH_MAX] = { 0 };
    struct fsm_object stale;
    bool ret;
    int rc;

    mgr = dpi_get_mgr();

    ret = IS_NULL_PTR(mgr->signature_version);
    /* Bail if the signature to add is already the active signature */
    if (!ret)
    {
        rc = strcmp(mgr->signature_version, object->version);
        if (!rc)
        {
            LOGI("%s: signature version already active: %s", __func__,
                 object->version);
            return 0;
        }
    }

    rc = -1;
    object->state = FSM_OBJ_LOAD_FAILED;
    ret = osp_objm_path(store, sizeof(store),
                        object->object, object->version);
    if (!ret) goto err;

    LOGI("%s: retrieved store path: %s", __func__, store);

    rc = load_signatures(session, store);
    if (rc != 0)
    {
        LOGE("%s: failed to update signatures: %d (%s)\n",
             __func__, rc, strerror((-1) * rc));
        goto err;
    }

    dpi_session = session->handler_ctxt;
    dpi_session->signature_loaded = true;
    LOGI("%s: loaded signature version: %s", __func__, object->version);
    object->state = FSM_OBJ_ACTIVE;

    ret = IS_NULL_PTR(mgr->signature_version);
    if (!ret)
    {
        stale.object = object->object;
        stale.version = mgr->signature_version;
        stale.state = FSM_OBJ_OBSOLETE;
        session->ops.state_cb(session, &stale);
        FREE(mgr->signature_version);
    }
    mgr->signature_version = STRDUP(object->version);
    LOGI("%s: %s: signature version is now %s", __func__, object->object,
         mgr->signature_version == NULL ? "None" : mgr->signature_version);
    rc = 0;

err:
    session->ops.state_cb(session, object);

    return rc;
}

bool
walleye_signature_load_last_active(struct fsm_session *session)
{
    struct fsm_object *object;
    int rc;

    /* Retrieve the latest version of the signature */
    object = session->ops.last_active_obj_cb(session, "app_signatures");
    if (object == NULL) return false;

    /* Apply the latest signature */
    rc = walleye_signature_add(session, object);
    if (rc)
    {
        return false;
    }
    LOGI("%s: installing %s version %s succeeded", __func__,
         object->object, object->version);
    FREE(object);
    return true;
}

void
walleye_signature_load_highest(struct fsm_session *session)
{
    struct fsm_object *object;
    struct fsm_object *next;
    char *version;
    bool loop;
    int rc;

    /* Retrieve the latest version of the signature */
    object = session->ops.latest_obj_cb(session, "app_signatures", NULL);
    if (object == NULL) return;

    /* Apply the latest signature */
    rc = walleye_signature_add(session, object);
    if (!rc)
    {
        LOGI("%s: installing %s version %s succeeded", __func__,
             object->object, object->version);
        FREE(object);

        return;
    }

    /* Retrieve an apply versions until success */
    loop = true;
    while (loop)
    {
        /* Use the current version as the version cap, excluded */
        version = object->version;
        next = session->ops.latest_obj_cb(session, "app_signatures", version);

        /* free the original object */
        FREE(object);

        object = next;
        /* No further version found. Bail */
        if (object == NULL) break;

        /* Attempt to load the signature */
        rc = walleye_signature_add(session, object);
        if (!rc)
        {
            LOGI("%s: installing %s version %s succeeded", __func__,
                 object->object, object->version);
            FREE(object);
            return;
        }
    }
}

void
walleye_signature_load_best(struct fsm_session *session)
{
    struct fsm_object *object;
    int cnt;
    int rc;

    /* Put a hard limit to the loop */
    cnt = 4;
    do
    {
        /* Retrieve the best version of the signature */
        object = session->ops.best_obj_cb(session, "app_signatures");
        if (object == NULL) return;

        /* Apply the best signature */
        rc = walleye_signature_add(session, object);
        if (!rc)
        {
            LOGI("%s: installing %s version %s succeeded", __func__,
                 object->object, object->version);
            FREE(object);
        }
        cnt--;
    } while ((rc != 0) && (cnt != 0));
}


void
walleye_signature_update(struct fsm_session *session,
                         struct fsm_object *object,
                         int ovsdb_event)
{
    int rc;
    struct dpi_plugin_cache *mgr;

    switch(ovsdb_event)
    {
        case OVSDB_UPDATE_NEW:
            rc = walleye_signature_add(session, object);
            if (rc != 0) walleye_signature_load_best(session);
            break;

        case OVSDB_UPDATE_DEL:
            /* If removed version was active load best available version */
            mgr = dpi_get_mgr();
            if (mgr->signature_version != NULL)
            {
                rc = strcmp(mgr->signature_version, object->version);
                if (!rc)
                {
                    LOGD("%s: active signature (%s) removed. Load best version available",
                         __func__, object->version);
                    walleye_signature_load_best(session);
                }
            }
            break;

        case OVSDB_UPDATE_MODIFY:
            break;

        default:
            return;
    }
}


/**
 * @brief compare object versions
 *
 * @param object_name the object id
 * @param version_a first operand of the comparison
 * @param version_b second operand of the comparison
 * @return (version_a - version_b)
 *
 * Assumes a version format "major.minor.micro"
 */
int
walleye_signature_cmp(char *object_name, char *version_a, char *version_b)
{
    int major_a;
    int major_b;
    int minor_a;
    int minor_b;
    int micro_a;
    int micro_b;
    int ret;

    ret = sscanf(version_a, "%d.%d.%d", &major_a, &minor_a, &micro_a);
    if (ret != 3)
    {
        LOGE("%s: scanning %s failed: %s, ret: %d", __func__,
             version_a, strerror(errno), ret);
        return 0;
    }

    ret = sscanf(version_b, "%d.%d.%d", &major_b, &minor_b, &micro_b);
    if (ret != 3)
    {
        LOGE("%s: scanning %s failed: %s", __func__,
             version_b, strerror(errno));
        return 0;
    }

    ret = (major_a - major_b);
    if (ret) return ret;

    ret = (minor_a - minor_b);
    if (ret) return ret;

    return (micro_a - micro_b);
}


static bool
walleye_dpi_register_client(struct fsm_session *dpi_plugin,
                            struct fsm_session *dpi_client,
                            char *flow_attribute)
{
    LOGI("%s: dpi plugin: %s, client: %s, registering flow attribute: %s", __func__,
         dpi_plugin->name, dpi_client->name, flow_attribute);

    rts_subscribe(flow_attribute, notify_client);

    return true;
}


static bool
walleye_dpi_unregister_client(struct fsm_session *dpi_plugin,
                              char *flow_attribute)
{
    LOGI("%s: dpi plugin: %s, unregistering flow attribute: %s", __func__,
         dpi_plugin->name, flow_attribute);

    rts_subscribe(flow_attribute, NULL);

    return true;
}


static int
walleye_dpi_attr_cmp(const void *a, const void *b)
{
    const char *str_a;
    const char *str_b;
    int cmp;

    str_a = a;
    str_b = b;

    cmp = strcmp(str_a, str_b);
    return cmp;
}


static int
walleye_dpi_get_sandbox_size(struct fsm_session *session)
{
    int default_size;;
    long value;
    char *str;
    int ret;

    /* Compute the default sandbox size */
    default_size = CONFIG_WALLEYE_DPI_ENGINE_SANDBOX_SIZE * 1024 * 1024;

    /* Check if the ovsdb config provides the size to use */
    str = session->ops.get_config(session, "sandbox_size");

    if (str == NULL) return default_size;

    errno = 0;
    value = strtol(str, NULL, 10);
    if (errno != 0) return default_size;

    /* Check the value against FSM allowed memory usage */
    if (value >= CONFIG_FSM_MEM_MAX / 2) return default_size;

    ret = (int)value * 1024 * 1024;
    return ret;
}


/**
 * @brief Free plugin resources
 *
 * This api is added in order to free the resources/flows held
 * by walleye dpi plugin whenever fsm receives the delete
 * session for dpi dispatch session earlier compared delete
 * session for walleye dpi plugin.
 * @param dpi_plugin the walleye dpi plugin context
 */
void walleye_dpi_free_resources(struct fsm_session *dpi_plugin_session)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct dpi_session *dpi_session;
    struct fsm_session *dpi_plugin;
    struct dpi_plugin_cache *mgr;
    ds_tree_t *sessions;

    mgr = dpi_get_mgr();
    sessions = &mgr->fsm_sessions;

    dpi_session = ds_tree_find(sessions, dpi_plugin_session);
    if (dpi_session == NULL) return;

    dpi_plugin = dpi_session->session;
    dpi_plugin_ops = &dpi_plugin->p_ops->dpi_plugin_ops;
    if (dpi_plugin_ops->unregister_clients != NULL)
    {
        dpi_plugin_ops->unregister_clients(dpi_plugin);
    }
}


/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the packet parsing handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
walleye_dpi_plugin_init(struct fsm_session *session)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct dpi_session *dpi_session;
    struct dpi_plugin_cache *mgr;
    struct timespec now;
    const char *str;
    int res;

    if (session == NULL) return -1;

    mgr = dpi_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(&mgr->fsm_sessions, dpi_session_cmp,
                     struct dpi_session, session_node);
        mgr->initialized = true;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    mgr->periodic_ts = now.tv_sec;
    /* Look up the fsm session */
    dpi_session = dpi_lookup_session(session);
    if (dpi_session == NULL)
    {
        LOGE("%s: could not allocate dpi_parser", __func__);
        return -1;
    }

    /* Bail if the session is already initialized */
    if (dpi_session->initialized) return 0;

    /* Set the fsm session */
    session->ops.periodic = dpi_plugin_periodic;
    session->ops.update = dpi_plugin_update;
    session->ops.exit = dpi_plugin_exit;
    session->ops.object_cb = walleye_signature_update;
    session->ops.version_cmp_cb = walleye_signature_cmp;
    session->ops.dpi_free_conn_ctxt = dpi_plugin_free_conn_ctxt;
    session->handler_ctxt = dpi_session;

    /* Set the plugin specific ops */
    dpi_plugin_ops = &session->p_ops->dpi_plugin_ops;
    FSM_FN_MAP(dpi_plugin_handler);
    dpi_plugin_ops->handler = dpi_plugin_handler;
    dpi_plugin_ops->register_client = walleye_dpi_register_client;
    dpi_plugin_ops->unregister_client = walleye_dpi_unregister_client;
    dpi_plugin_ops->flow_attr_cmp = walleye_dpi_attr_cmp;
    dpi_plugin_ops->dpi_free_resources = walleye_dpi_free_resources;

    /* Set the extended dpi stats control */
    str = session->ops.get_config(session, "scan_dbg_en");
    if (str != NULL && !strcmp(str, "true")) {
        dpi_session->scan_dbg_enable = true;
    }

    rts_handle_memory_size = walleye_dpi_get_sandbox_size(session);
    LOGI("%s: sandbox size set to %dMB", __func__,
         rts_handle_memory_size / (1024 * 1024));

    /* Wrap up the session initialization */
    dpi_session->session = session;

    rts_handle_isolate = 1;

    /* Configurable dns expiry (default to 30 seconds) */
    dpi_session->rts_dict_expiry = 30;

    str = session->ops.get_config(session, "rts_dict_expiry");
    if (str != NULL) {
        dpi_session->rts_dict_expiry = atoi(str);
    }

    rts_handle_dict_hash_expiry = dpi_session->rts_dict_expiry * 1000;
    rts_handle_dict_hash_bucket = dpi_session->rts_dict_expiry * 30;

    if ((res = rts_handle_create(&dpi_session->handle)) != 0)
    {
        LOGE("%s: failed to allocate dpi handle: %d\n", __func__, res);
        goto error;
    }

    fsm_set_dpi_health_stats_cfg(session);

    dpi_session->wc_topic = session->dpi_stats_report_topic;
    dpi_session->wc_interval = session->dpi_stats_report_interval;

    dpi_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    if (session->ops.best_obj_cb == NULL) return 0;

    /* Register app_signatures object monitoring */
    session->ops.monitor_object(session, "app_signatures");

    /* Load best available signature version */
    walleye_signature_load_best(session);
    return 0;

error:
    dpi_delete_session(session);
    return -1;
}


/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
dpi_plugin_exit(struct fsm_session *session)
{
    struct dpi_plugin_cache *mgr;

    mgr = dpi_get_mgr();
    if (!mgr->initialized) return;

    /* Unregister app_signatures object monitoring */
    session->ops.unmonitor_object(session, "app_signatures");
    dpi_delete_session(session);
}

static struct vendor_data_kv_pair *
set_vendor_kvp_str(const char *key, const char *value)
{
    struct vendor_data_kv_pair *kvp;

    kvp = CALLOC(1, sizeof(struct vendor_data_kv_pair));
    if (kvp == NULL) return NULL;

    kvp->key = STRDUP(key);
    if (kvp->key == NULL)
    {
       FREE(kvp);
       return NULL;
    }

    kvp->value_type = NET_VENDOR_STR;
    kvp->str_value = STRDUP(value);
    if (kvp->str_value == NULL)
    {
        FREE(kvp->key);
        FREE(kvp);
        return NULL;
    }

    return kvp;
}

static struct vendor_data_kv_pair *
set_vendor_kvp_u64(const char *key, uint64_t value)
{
    struct vendor_data_kv_pair *kvp;

    kvp = CALLOC(1, sizeof(struct vendor_data_kv_pair));
    if (kvp == NULL) return NULL;

    kvp->key = STRDUP(key);
    if (kvp->key == NULL)
    {
       FREE(kvp);
       return NULL;
    }

    kvp->value_type = NET_VENDOR_U64;
    kvp->u64_value = value;

    return kvp;
}

static struct vendor_data_kv_pair *
set_vendor_kvp_u32(const char *key, uint32_t value)
{
    struct vendor_data_kv_pair *kvp;

    kvp = CALLOC(1, sizeof(struct vendor_data_kv_pair));
    if (kvp == NULL) return NULL;

    kvp->key = STRDUP(key);
    if (kvp->key == NULL)
    {
       FREE(kvp);
       return NULL;
    }

    kvp->value_type = NET_VENDOR_U32;
    kvp->u32_value = value;

    return kvp;
}

static void
add_vendor_data(struct dpi_session *dpi_session, struct flow_key *fkey,
                struct dpi_conn *dpi_conn)
{
    struct vendor_data_kv_pair **kvps;
    struct flow_vendor_data *vd;
    size_t nelems;
    unsigned i;

    free_flow_key_vdr_data(fkey);

    fkey->num_vendor_data = 1;
    fkey->vdr_data = CALLOC(fkey->num_vendor_data,
                            sizeof(*fkey->vdr_data));
    if (fkey->vdr_data == NULL) goto err_alloc_vdr_data;

    nelems = 2;
    if (dpi_conn->server_name[0] != '\0')
        nelems += 1;
    if (dpi_conn->scan_error)
        nelems += 1;
    if (dpi_session->scan_dbg_enable)
        nelems += 7;
    if (dpi_conn->tcp_syn_delay != 0)
        nelems += 1;
    if (dpi_conn->tcp_ack_delay != 0)
        nelems += 1;

    kvps = CALLOC(nelems, sizeof(struct vendor_data_kv_pair *));
    if (kvps == NULL) goto err_alloc_kvps;

    i = 0;
    kvps[i] = set_vendor_kvp_u64("TOL", dpi_conn->toldata);
    if (kvps[i] == NULL) goto err_alloc_kvp0;

    kvps[++i] = set_vendor_kvp_u32("pkts_scanned",
            dpi_conn->packets[0] + dpi_conn->packets[1]);
    if (kvps[i] == NULL) goto err_alloc_pairs;

    if (dpi_conn->server_name[0] != '\0') {
        kvps[++i] = set_vendor_kvp_str("server.name", dpi_conn->server_name);
        if (kvps[i] == NULL) goto err_alloc_pairs;
    }

    if (dpi_conn->scan_error) {
        kvps[++i] = set_vendor_kvp_u32("scan_error", dpi_conn->scan_error);
        if (kvps[i] == NULL) goto err_alloc_pairs;
    }

    if (dpi_conn->tcp_syn_delay) {
        kvps[++i] = set_vendor_kvp_u64("tcp_client_syn_delay", dpi_conn->tcp_syn_delay);
        if (kvps[i] == NULL) goto err_alloc_pairs;
    }

    if (dpi_conn->tcp_ack_delay) {
        kvps[++i] = set_vendor_kvp_u64("tcp_client_ack_delay", dpi_conn->tcp_ack_delay);
        if (kvps[i] == NULL) goto err_alloc_pairs;
    }

    if (dpi_session->scan_dbg_enable) {
        kvps[++i] = set_vendor_kvp_u32("client_bytes_scanned",
                dpi_conn->bytes[0]);
        if (kvps[i] == NULL) goto err_alloc_pairs;

        kvps[++i] = set_vendor_kvp_u32("server_bytes_scanned",
                dpi_conn->bytes[1]);
        if (kvps[i] == NULL) goto err_alloc_pairs;

        kvps[++i] = set_vendor_kvp_u32("client_data_packets_scanned",
                dpi_conn->data_packets[0]);
        if (kvps[i] == NULL) goto err_alloc_pairs;

        kvps[++i] = set_vendor_kvp_u32("server_data_packets_scanned",
                dpi_conn->data_packets[1]);
        if (kvps[i] == NULL) goto err_alloc_pairs;

        kvps[++i] = set_vendor_kvp_u32("client_packets_scanned",
                dpi_conn->packets[0]);
        if (kvps[i] == NULL) goto err_alloc_pairs;

        kvps[++i] = set_vendor_kvp_u32("server_packets_scanned",
                dpi_conn->packets[1]);
        if (kvps[i] == NULL) goto err_alloc_pairs;

        kvps[++i] = set_vendor_kvp_u32("inverted",
                dpi_conn->inverted ? 1 : 0);
        if (kvps[i] == NULL) goto err_alloc_pairs;
    }

    if (++i != nelems) {
        LOGE("%s: i (%u) != nelems (%zu)", __func__, i, nelems);
        goto err_alloc_pairs;
    }

    vd = CALLOC(1, sizeof(struct flow_vendor_data));
    if (vd == NULL) goto err_alloc_vd;

    vd->vendor = STRDUP("Walleye");
    if (vd->vendor == NULL) goto err_alloc_vd_name;

    vd->nelems = nelems;
    vd->kv_pairs = kvps;

    fkey->vdr_data[0] = vd;

    return;

err_alloc_vd_name:
    FREE(vd);

err_alloc_vd:

err_alloc_pairs:
    for (i = 0; i < nelems; ++i)
    {
        if (kvps[i])
        {
            FREE(kvps[i]->key);
            FREE(kvps[i]->str_value);
            FREE(kvps[i]);
        }
    }

err_alloc_kvp0:
    FREE(kvps);

err_alloc_kvps:
    FREE(fkey->vdr_data);

err_alloc_vdr_data:
    fkey->num_vendor_data = 0;

    return;
}


static void
add_tag(struct flow_key *fkey, const char *service,
        int num_tags,
        const char *tags[])
{
    struct flow_tags **key_tags;
    struct flow_tags *tag;
    int i;

    free_flow_key_tags(fkey);

    /* Allocate one key tag container */
    key_tags = CALLOC(1, sizeof(*key_tags));
    if (key_tags == NULL) return;

    /* Allocate the one flow tag container the key will carry */
    tag = CALLOC(1, sizeof(*tag));
    if (tag == NULL) goto err_free_key_tags;

    tag->vendor = STRDUP("Walleye");
    if (tag->vendor == NULL) goto err_free_key_tags;

    tag->app_name = STRDUP(service);
    if (tag->app_name == NULL) goto err_free_vendor;

    tag->nelems = num_tags;
    tag->tags = CALLOC(tag->nelems, sizeof(tag->tags));
    if (tag->tags == NULL) goto err_free_app_name;

    for (i = 0; i < num_tags; ++i)
    {
        tag->tags[i] = STRDUP(tags[i]);
        if (tag->tags[i] == NULL)
        {
            for (; i > 0; --i)
                FREE(tag->tags[i-1]);
            goto err_free_tag_tags;
        }
    }

    (*key_tags) = tag;
    fkey->tags = key_tags;
    fkey->num_tags = 1;

    LOGD("%s: tagged connection with %s", __func__, service);

    return;

err_free_tag_tags:
    FREE(tag->tags);

err_free_app_name:
    FREE(tag->app_name);

err_free_vendor:
    FREE(tag->vendor);

err_free_key_tags:
    FREE(key_tags);
}


static void
walleye_app_check(struct fsm_session *session,
                  struct dpi_conn *dpi_conn,
                  struct net_md_stats_accumulator *acc,
                  const char *app)
{
    struct fsm_dpi_plugin_client_pkt_info pkt_info;
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct dpi_session *dpi_session;
    int action;

    /* Don't bother if the flow is to be dropped already */
    if (dpi_conn->flow_action == FSM_DPI_DROP) return;

    /* No app to check, allow the flow */
    if (app == NULL)
    {
        dpi_conn->flow_action = FSM_DPI_PASSTHRU;
        return;
    }

    dpi_session = dpi_conn->dpi_sess;
    dpi_plugin_ops = &session->p_ops->dpi_plugin_ops;

    /**
     * net_parser details may not available if walleye destroys stream
     *  prior to callback.
     */
    pkt_info.tag_flow = false;
    pkt_info.acc = acc;
    pkt_info.parser = dpi_session->parser.net_parser;

    action = dpi_plugin_ops->notify_client(session, "tag",
                                           RTS_TYPE_STRING, (uint16_t)strlen(app), app,
                                           &pkt_info);

    /* Check if the flow should be tagged */
    dpi_conn->tag_flow = pkt_info.tag_flow;

    if (action == FSM_DPI_DROP)
    {
        LOGI("%s: blocking app %s", __func__, app);
    }
    dpi_conn->flow_action = action;

    return;
}


static void
tag_session(struct dpi_session *dpi_session,
            struct dpi_conn *dpi_conn,
            struct net_md_stats_accumulator *acc)
{
    struct fsm_session *fsm_session;
    struct fsm_dpi_plugin_ops *ops;
    struct flow_key *fkey;

    const char *service = NULL;
    const char *tags[NUM_TAGS] = { NULL };
    int i, num_tags = 0;

    if (dpi_conn == NULL) return;

    fsm_session = dpi_session->session;

    /* Access the flow report key */
    fkey = acc->fkey;
    if (fkey == NULL) return;

    rts_lookup(dpi_conn->service, &service, dpi_conn->stream);
    if (service == NULL) return;

    for (i = 0; i < NUM_TAGS && dpi_conn->tags[i] > 0; ++i)
    {
        if (dpi_conn->tags[i] != dpi_conn->service)
        {
            rts_lookup(dpi_conn->tags[i], &tags[num_tags++], dpi_conn->stream);
        }
    }

    LOGD("%s: matched connection with %s (%s %s %s)", __func__,
         service, tags[0], tags[1], tags[2]);

    walleye_app_check(fsm_session, dpi_conn, acc, service);
    if (dpi_conn->tag_flow)
    {
        LOGT("%s: tagging flow", __func__);
        add_tag(fkey, service, num_tags, tags);
        add_vendor_data(dpi_session, fkey, dpi_conn);\
    }
    else LOGT("%s: Not tagging flow", __func__);

    ops = &fsm_session->p_ops->dpi_plugin_ops;
    if (ops->mark_flow) ops->mark_flow(fsm_session, acc);
}


static void
dpi_plugin_tag_stream(struct dpi_session *dpi_session,
               struct net_header_parser *net_parser,
               struct dpi_conn *dpi_conn)
{
    struct flow_key *fkey;
    struct net_md_stats_accumulator *acc;
    bool rc;

    acc = net_parser->acc;
    tag_session(dpi_session, dpi_conn, net_parser->acc);

    fkey = acc->fkey;

    /*
     * fkey will be freed if FSM_DPI_DISPATCH session is deleted prior to
     *  walleye dpi session.
     */
    rc = IS_NULL_PTR(fkey);
    if (rc) return;

    /* Adjust action */
    if (dpi_conn->flow_action != FSM_DPI_DROP)
    {
        dpi_conn->flow_action = FSM_DPI_PASSTHRU;
    }

    /* We have a match. No more packets are necessary */
    fsm_dpi_set_plugin_decision(dpi_conn->dpi_sess->session,
                                acc, dpi_conn->flow_action);

    /* If the flow is to be blocked, just block it here */
    if (dpi_conn->flow_action == FSM_DPI_DROP)
    {
        LOGT(
            "%s: blocking the flow src: %s, dst: %s, proto: %d, sport: %d, dport: %d",
            __func__,
            fkey->src_ip,
            fkey->dst_ip,
            fkey->protocol,
            fkey->sport,
            fkey->dport);
    }
}


/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 */
void
dpi_plugin_handler(struct fsm_session *session,
                   struct net_header_parser *net_parser)
{
    struct net_md_stats_accumulator *acc;
    struct dpi_session *dpi_session;
    struct dpi_conn *dpi = NULL;
    struct nfe_packet *packet;
    struct timespec now;
    uint64_t timestamp;
    struct eth_header *eth;
    size_t len;
    int res, src, dst;

    dpi_session = (struct dpi_session *)session->handler_ctxt;
    if (!dpi_session->signature_loaded)
    {
        LOGI("%s: Signature not loaded for %s", __func__, session->name);
        return;
    }

    acc = net_parser->acc;
    if (acc == NULL) return;

    dpi = (struct dpi_conn *)acc->dpi;
    if (dpi == NULL)
    {
        dpi = dpi_conn_alloc(dpi_session);
        if (!dpi) return;
        acc->dpi = (void *)dpi;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    timestamp = ((uint64_t)now.tv_sec * 1000) + (now.tv_nsec / 1000000);
    eth = net_header_get_eth(net_parser);

    packet = acc->packet;
    if (!packet) return;

    len = packet->tail - packet->data;

    if (!dpi->initialized)
    {
        dpi_session->parser.net_parser = net_parser;
        dpi->dpi_sess = dpi_session;

        dpi_session->connections++;

        /* NFQUEUE packets may not have src/dst mac address, hence condition */
        if (eth->srcmac)
        {
            memcpy(dpi->src_mac.addr, eth->srcmac, sizeof(dpi->src_mac.addr));
        }
        if (eth->dstmac)
        {
            memcpy(dpi->dst_mac.addr, eth->dstmac, sizeof(dpi->dst_mac.addr));
        }

        dpi->tcp_syn_delay = 0;
        dpi->tcp_ack_delay = 0;

        dpi->inverted = false;
        dpi->initialized = true;

        src = packet->direction;
        dst = 1 - packet->direction;
        res = rts_stream_create(&dpi->stream, dpi_session->handle,
                                packet->tuple.domain, packet->tuple.proto,
                                &packet->tuple.addr[src], packet->tuple.port[src],
                                &packet->tuple.addr[dst], packet->tuple.port[dst],
                                acc);

        if (res)
        {
            dpi->dpi_sess->err_create++;
            dpi->scan_error |= SCAN_ERROR_CREATE;

            /* Mark the flow as passthrough as no resource is available */
            fsm_dpi_set_plugin_decision(session, acc, FSM_DPI_PASSTHRU);
        }
        else
        {
            dpi_session->streams++;
            if (rts_stream_matching(dpi->stream) == 0)
            {
                dpi_plugin_tag_stream(dpi_session, net_parser, dpi);
                dpi_conn_free(dpi);
                acc->dpi = NULL;
                return;
            }
        }
    }

    dpi->packets[packet->direction] += 1;

    dpi->inverted = (dpi->bytes[0] == 0 && packet->direction == 1);

    dpi->bytes[packet->direction] += len;
    dpi->data_packets[packet->direction] += 1;
    dpi->dpi_sess->parser.net_parser = net_parser;

    if (!dpi->stream)
    {
        LOGT("%s: Stream not created", __func__);
        dpi_conn_free(dpi);
        acc->dpi = NULL;
        return;
    }

    res = rts_stream_scan(dpi->stream, packet->data, len,
                          packet->direction, timestamp);
    if (res < 0)
    {
        LOGE("%s: error %d in rts_stream_scan\n", __func__, res);
        dpi->dpi_sess->err_scan++;
        dpi->scan_error |= SCAN_ERROR_SCAN;
        return;
    }

    /* Exit here if the flow requires more packets for classification */
    res = rts_stream_matching(dpi->stream);
    if (res != 0)
    {
        LOGT("%s: stream matching needs more packets", __func__);
        return;
    }

    /* We are done matching, so tag it and remove context */
    dpi_plugin_tag_stream(dpi_session, net_parser, dpi);
    dpi_conn_free(dpi);
    acc->dpi = NULL;
    return;
}

void
dpi_report_kpis(struct dpi_session *dpi_session)
{
    struct dpi_engine_counters *counters;
    struct dpi_stats_packed_buffer *pb;
    struct dpi_stats_report report;
    struct fsm_session *session;
    struct rts_rusage stats;

    if (!dpi_session->initialized) return;

    session = dpi_session->session;
    if (session == NULL) return;

    memset(&stats, 0, sizeof(stats));
    rts_handle_rusage(dpi_session->handle, &stats);

    memset(&report, 0, sizeof(report));
    report.location_id = session->location_id;
    report.node_id = session->node_id;

    counters = &report.counters;
    counters->curr_alloc = stats.curr_alloc;
    counters->peak_alloc = stats.peak_alloc;
    counters->fail_alloc = stats.fail_alloc;
    counters->mpmc_events = stats.mpmc_events;
    counters->scan_started = stats.scan_started;
    counters->scan_stopped = stats.scan_stopped;
    counters->scan_bytes = stats.scan_bytes;

    counters->connections = dpi_session->connections;
    counters->streams = dpi_session->streams;

    counters->err_incomplete = dpi_session->err_incomplete;
    dpi_session->err_incomplete = 0;

    counters->err_length = dpi_session->err_length;
    dpi_session->err_length = 0;

    counters->err_create = dpi_session->err_create;
    dpi_session->err_create = 0;

    counters->err_scan = dpi_session->err_scan;
    dpi_session->err_scan = 0;

    pb = dpi_stats_serialize_report(&report);
    if (pb == NULL) return;

    session->ops.send_pb_report(session, dpi_session->wc_topic, pb->buf, pb->len);
    dpi_stats_free_packed_buffer(pb);
}


#define WALLEYE_PERIODIC_INTERVAL 120
/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * Sends a flow stats report.
 * @param session the fsm session
 */
void
dpi_plugin_periodic(struct fsm_session *session)
{
    struct dpi_session *dpi_session;
    struct dpi_plugin_cache *mgr;
    struct timespec now;
    int threshold;

    mgr = dpi_get_mgr();
    clock_gettime(CLOCK_MONOTONIC, &now);

    dpi_session = session->handler_ctxt;

    threshold = dpi_session->wc_interval;
    if (!threshold) threshold = WALLEYE_PERIODIC_INTERVAL;

    if ((now.tv_sec - mgr->periodic_ts) < threshold) return;

    LOGI("%s: %s: active connections: %u active streams: %u", __func__,
        session->name, dpi_session->connections,dpi_session->streams);

    if (dpi_session->err_incomplete > 0)
    {
        /* Incomplete scanning of a stream is not an "error" */
        LOGI("%s:%s: scan error: incomplete: %u", __func__,
             session->name, dpi_session->err_incomplete);
    }

    if (dpi_session->err_length > 0)
    {
        /* Length adjustments is not an "error" */
        LOGI("%s:%s: scan error: length adjustments: %u", __func__,
             session->name, dpi_session->err_length);
    }

    if (dpi_session->err_create > 0)
    {
        LOGE("%s:%s: scan error: rts_stream_create() failures: %u", __func__,
             session->name, dpi_session->err_create);
    }

    if (dpi_session->err_scan > 0)
    {
        LOGE("%s:%s: scan error: rts_stream_scan() failures: %u", __func__,
             session->name, dpi_session->err_scan);
    }

    dpi_report_kpis(dpi_session);

    mgr->periodic_ts = now.tv_sec;
}


/**
 * @brief free dpi resources 
 *
 * called by the fsm manager when acc is deleted.
 * @param acc of the dpi resource.
 */
void
dpi_plugin_free_conn_ctxt(struct net_md_stats_accumulator *acc)
{
    struct dpi_conn *dpi_conn;

    if (acc == NULL) return;

    dpi_conn = (struct dpi_conn *)acc->dpi;
    if (dpi_conn == NULL) return;

    LOGT("%s: freeing dpi connection %p", __func__, dpi_conn);
    /* Free the dpi connection */
    dpi_conn_free(dpi_conn);
    acc->dpi = NULL;
}


/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct dpi_session *
dpi_lookup_session(struct fsm_session *session)
{
    struct dpi_plugin_cache *mgr;
    struct dpi_session *dpi_session;
    ds_tree_t *sessions;

    mgr = dpi_get_mgr();
    sessions = &mgr->fsm_sessions;

    dpi_session = ds_tree_find(sessions, session);
    if (dpi_session != NULL) return dpi_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    dpi_session = CALLOC(1, sizeof(struct dpi_session));
    if (dpi_session == NULL) return NULL;

    dpi_session->initialized = false;
    dpi_session->signature_loaded = false;

    ds_tree_insert(sessions, dpi_session, session);

    return dpi_session;
}


/**
 * @brief Frees a session
 *
 * @param dpi_session the session to delete
 */
void
dpi_free_session(struct dpi_session *dpi_session)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct fsm_session *dpi_plugin;

    dpi_plugin = dpi_session->session;
    dpi_plugin_ops = &dpi_plugin->p_ops->dpi_plugin_ops;
    if (dpi_plugin_ops->unregister_clients != NULL)
    {
        dpi_plugin_ops->unregister_clients(dpi_plugin);
    }

    if (dpi_session->handle) rts_handle_destroy(dpi_session->handle);

    FREE(dpi_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the http session to delete
 */
void
dpi_delete_session(struct fsm_session *session)
{
    struct dpi_plugin_cache *mgr;
    struct dpi_session *dpi_session;
    ds_tree_t *sessions;

    mgr = dpi_get_mgr();
    sessions = &mgr->fsm_sessions;

    dpi_session = ds_tree_find(sessions, session);
    if (dpi_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, dpi_session);
    dpi_free_session(dpi_session);
    if (ds_tree_is_empty(&mgr->fsm_sessions))
    {
        shared_free();
        FREE(mgr->signature_version);
    }

    return;
}

/* 
 * dpi_conn_alloc()
 */
struct dpi_conn *
dpi_conn_alloc(struct dpi_session *dpi_session)
{
    struct dpi_conn *dpi;

    if (!dpi_session) return NULL;

    if (!(dpi = CALLOC(1, sizeof(*dpi)))) return NULL;

    dpi->initialized = false;
    dpi->last_updated = time(NULL);
    dpi->conn_ttl = dpi_session->rts_dict_expiry;
    return dpi;
}

/* dpi_conn_free()
 *
 * The counter-part to dpi_conn_alloc().
 */
void
dpi_conn_free(struct dpi_conn *dpi)
{
    struct dpi_session *dpi_sess;

    dpi_sess = dpi->dpi_sess;
    if (dpi->stream)
    {
        rts_stream_destroy(dpi->stream);
        dpi_sess->streams--;
    }
    dpi_sess->connections--;
    FREE(dpi);
}
