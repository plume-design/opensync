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

/* Walleye library config */
extern int rts_handle_isolate;
extern int rts_handle_memory_size;
extern int rts_handle_dict_hash_expiry;
extern int rts_handle_dict_hash_bucket;
extern int nfe_conntrack_tcp_timeout_est;
extern nfe_conn_t nfe_conn_lookup_by_tuple(nfe_conntrack_t conntrack,
                                           const struct nfe_tuple *tuple,
                                           uint64_t timestamp, int *dir);

/* The maximum number of tags to be reported */
#define NUM_TAGS 3

/* Mapping of scan errors */
#define SCAN_ERROR_INCOMPLETE (1 << 0)
#define SCAN_ERROR_LENGTH     (1 << 1)
#define SCAN_ERROR_CREATE     (1 << 2)
#define SCAN_ERROR_SCAN       (1 << 3)

/* Service level priorities
 * network (used for CDNs) is purposely put below protocol
 */
enum service_level {
    service_none = 0,
    service_network,
    service_protocol,
    service_platform,
    service_application,
    service_feature
};

/* A connection, defined by 6-tuple (vlan, saddr, daddr, sport, dport, prot) */
struct dpi_conn {
    /* An rts stream is connection specific context for the scan */
    rts_stream_t stream;

    /* Connection context for tracking time online */
    uint64_t toldata;

    /* The service determined from dpi. */
    uint16_t service;
    enum service_level service_level;

    uint16_t tags[NUM_TAGS];
    os_macaddr_t src_mac;
    os_macaddr_t dst_mac;

    uint32_t bytes[2];
    uint32_t packets[2];
    uint32_t data_packets[2];

    char server_name[256];
    struct net_header_parser net_hdr;
    struct dpi_session *dpi_sess;

    int flow_action;

    uint32_t scan_error;

    bool inverted;
    bool initialized;
    bool tag_flow;

    uint64_t tcp_syn_delay;
    uint64_t tcp_ack_delay;

    /* The private nfe conn data */
    unsigned char priv[] __attribute__((aligned(sizeof(ptrdiff_t))));
};

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
    struct dpi_conn *dpi = user;
    __builtin_memcpy(&dpi->toldata, value, length);
}


static void
save_tcp_syn_delay(rts_stream_t stream, void *user, const char *key,
                   uint8_t type, uint16_t length, const void *value)
{
    struct dpi_conn *dpi = user;
    __builtin_memcpy(&dpi->tcp_syn_delay, value, length);
}


static void
save_tcp_ack_delay(rts_stream_t stream, void *user, const char *key,
                   uint8_t type, uint16_t length, const void *value)
{
    struct dpi_conn *dpi = user;
    __builtin_memcpy(&dpi->tcp_ack_delay, value, length);
}


static void
save_server_name(rts_stream_t stream, void *user, const char *key,
                 uint8_t type, uint16_t length, const void *value)
{
    struct dpi_conn *dpi = user;

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

    dpi = user;

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
    pkt_info.acc = dpi->net_hdr.acc;
    pkt_info.parser = dpi_session->parser.net_parser;

    rc = dpi_plugin_ops->notify_client(dpi_plugin, key, type, length, value,
                                       &pkt_info);
    dpi->flow_action = rc;
    acc = dpi->net_hdr.acc;
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
    fsm_dpi_set_plugin_decision(dpi->dpi_sess->session, &dpi->net_hdr, rc);
}


static int
load_signatures(struct fsm_session *session, char *store)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    int fd, res;
    struct stat sb;
    void *sig;
    const char * const path = "/usr/walleye/etc/signature.bin";
    char signature_file[PATH_MAX+128];

    dpi_plugin_ops = &session->p_ops->dpi_plugin_ops;
    if (dpi_plugin_ops->unregister_clients != NULL)
    {
        dpi_plugin_ops->unregister_clients(session);
    }

    snprintf(signature_file, sizeof(signature_file), "%s/%s",
             store, path);
    fd = open(signature_file, O_RDONLY);
    if (fd == -1)
    {
        LOGE("%s: failed to open '%s'\n", __func__, path);
        return -1;
    }
    fstat(fd, &sb);
    sig = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (sig == MAP_FAILED)
    {
        LOGE("%s: failed to mmap signatures\n", __func__);
        close(fd);
        return -1;
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
dpi_plugin_set_health_stats_cfg(struct fsm_session *session)
{
    struct dpi_session *dpi_session;
    long int interval;
    char *str;

    dpi_session = (struct dpi_session *)session->handler_ctxt;

    /* Get the health stats topic */
    str = session->ops.get_config(session, "dpi_health_stats_topic");
    dpi_session->wc_topic = str;

    /* Get the health stats reporting interval */
    str = session->ops.get_config(session, "dpi_health_stats_interval_secs");
    if (str != NULL)
    {
        errno = 0;
        interval = strtol(str, 0, 10);
        if (errno == 0) dpi_session->wc_interval = (int)interval;
    }

    LOGI("%s: wc_topic: %s, wc_interval: %d", __func__,
         dpi_session->wc_topic != NULL ? dpi_session->wc_topic : "not set",
         dpi_session->wc_interval);
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

    dpi_plugin_set_health_stats_cfg(session);

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
            free(object);
            return;
        }
    }
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
            if (rc != 0) walleye_signature_load_highest(session);
            break;

        case OVSDB_UPDATE_DEL:
            /* If removed version was active load highest available version */
            mgr = dpi_get_mgr();
            if (mgr->signature_version != NULL)
            {
                rc = strcmp(mgr->signature_version, object->version);
                if (!rc)
                {
                    LOGD("%s: active signature (%s) removed. Load newest version available",
                         __func__, object->version);
                    walleye_signature_load_highest(session);
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

    if (dpi_session->ct)
    {
        nfe_conntrack_destroy(dpi_session->ct);
        dpi_session->ct = NULL;
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
    session->handler_ctxt = dpi_session;

    /* Set the plugin specific ops */
    dpi_plugin_ops = &session->p_ops->dpi_plugin_ops;
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
    nfe_conntrack_tcp_timeout_est = 300;

    /* Configurable dns expiry (default to 30 seconds) */
    dpi_session->rts_dict_expiry = 30;

    str = session->ops.get_config(session, "rts_dict_expiry");
    if (str != NULL) {
        dpi_session->rts_dict_expiry = atoi(str);
    }

    rts_handle_dict_hash_expiry = dpi_session->rts_dict_expiry * 1000;
    rts_handle_dict_hash_bucket = dpi_session->rts_dict_expiry * 30;

    if ((res = nfe_conntrack_create(&dpi_session->ct, 8192)) != 0)
    {
        LOGE("%s: failed to allocate conntrack: %d\n", __func__, res);
        goto error;
    }

    if ((res = rts_handle_create(&dpi_session->handle)) != 0)
    {
        LOGE("%s: failed to allocate dpi handle: %d\n", __func__, res);
        goto error;
    }

    dpi_plugin_set_health_stats_cfg(session);

    dpi_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    if (session->ops.latest_obj_cb == NULL) return 0;
    if (session->ops.last_active_obj_cb == NULL) return 0;

    /* Get last active version */
    if (walleye_signature_load_last_active(session) == false)
    {
        /* If loading last active version fails then get the highest revision
         * available */
        LOGD("%s: No last active version available. Load highest version", __func__);
        walleye_signature_load_highest(session);
    }

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
    pkt_info.acc = dpi_conn->net_hdr.acc;
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
            const struct nfe_tuple *tuple,
            int dir)
{
    struct net_md_stats_accumulator *acc;
    struct fsm_session *fsm_session;
    struct fsm_dpi_plugin_ops *ops;
    struct flow_key *fkey;

    const char *service = NULL;
    const char *tags[NUM_TAGS] = { NULL };
    int i, num_tags = 0;

    if (dpi_conn == NULL) return;

    fsm_session = dpi_session->session;

    acc = dpi_conn->net_hdr.acc;

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

    walleye_app_check(fsm_session, dpi_conn, service);
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
destroy_stream(struct dpi_session *dpi_session,
               struct net_header_parser *net_parser,
               struct dpi_conn *dpi_conn,
               const struct nfe_tuple *tuple,
               int dir)
{
    struct flow_key *fkey;
    struct net_md_stats_accumulator *acc;
    bool rc;

    acc = dpi_conn->net_hdr.acc;
    tag_session(dpi_session, dpi_conn, tuple, dir);
    rts_stream_destroy(dpi_conn->stream);
    dpi_conn->stream = NULL;
    dpi_conn->dpi_sess->streams--;

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
                                &dpi_conn->net_hdr, dpi_conn->flow_action);

    /* If the flow is to be blocked, just block it here */
    if (dpi_conn->flow_action == FSM_DPI_DROP)
    {
        LOGI(
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
    struct dpi_conn *dpi = NULL;
    struct nfe_packet packet;
    nfe_conn_t conn;
    struct timespec now;
    uint64_t timestamp;
    struct dpi_session *dpi_session;
    struct eth_header *eth;
    size_t len, olen;
    int res, src, dst;
    uint16_t ethertype;

    dpi_session = (struct dpi_session *)session->handler_ctxt;
    if (!dpi_session->signature_loaded)
    {
        LOGI("%s: Signature not loaded for %s", __func__, session->name);
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    timestamp = ((uint64_t)now.tv_sec * 1000) + (now.tv_nsec / 1000000);

    ethertype = 0;
    eth = net_header_get_eth(net_parser);
    if (net_parser->source == PKT_SOURCE_NFQ)
    {
        ethertype = eth->ethertype;
    }

    res = nfe_packet_hash(&packet, ethertype, net_parser->start,
                          net_parser->caplen, timestamp);
    olen = packet.tail - packet.data;

    if (res)
    {
        LOGE("%s: failed to hash packet\n", __func__);
        return;
    }

    conn = nfe_conn_lookup(dpi_session->ct, &packet);
    if (!conn) return;

    dpi = container_of(conn, struct dpi_conn, priv);
    len = packet.tail - packet.data;

    if (!dpi->initialized)
    {
        dpi_session->parser.net_parser = net_parser;
        dpi->dpi_sess = dpi_session;

        /* Get a hold on the flow accumulator. */
        dpi->net_hdr.acc = net_parser->acc;

        /*
         * Increase acc's ref count here.
         * It is decreased in nfe_ext_conn_free()
         */
        dpi->net_hdr.acc->refcnt++;
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

        src = packet.direction;
        dst = 1 - packet.direction;
        res = rts_stream_create(&dpi->stream, dpi_session->handle,
                                packet.tuple.domain, packet.tuple.proto,
                                &packet.tuple.addr[src], packet.tuple.port[src],
                                &packet.tuple.addr[dst], packet.tuple.port[dst],
                                dpi);

        if (res)
        {
            dpi->dpi_sess->err_create++;
            dpi->scan_error |= SCAN_ERROR_CREATE;

            /* Mark the flow as passthrough as no resource is available */
            fsm_dpi_set_plugin_decision(session, net_parser, FSM_DPI_PASSTHRU);
        }
        else
        {
            dpi_session->streams++;
            if (rts_stream_matching(dpi->stream) == 0)
            {
                destroy_stream(dpi_session, net_parser, dpi,
                               &packet.tuple, packet.direction);
            }
        }
    }

    dpi->packets[packet.direction] += 1;

    if (len != olen) {
        dpi_session->err_length++;
        dpi->scan_error |= SCAN_ERROR_LENGTH;
    }

    dpi->inverted = (dpi->bytes[0] == 0 && packet.direction == 1);

    dpi->bytes[packet.direction] += len;
    dpi->data_packets[packet.direction] += 1;
    dpi->dpi_sess->parser.net_parser = net_parser;

    if (!dpi->stream) goto free_conn;

    res = rts_stream_scan(dpi->stream, packet.data, len,
                          packet.direction, timestamp);
    if (res < 0)
    {
        LOGE("%s: error %d in rts_stream_scan\n", __func__, res);
        dpi->dpi_sess->err_scan++;
        dpi->scan_error |= SCAN_ERROR_SCAN;
        goto free_conn;
    }

    /* Exit here if the flow requires more packets for classification */
    res = rts_stream_matching(dpi->stream);
    if (res != 0) goto free_conn;

    /* We are done matching, so tag it and remove context */
    destroy_stream(dpi_session, net_parser, dpi,
                   &packet.tuple, packet.direction);

free_conn:
    dpi->dpi_sess->parser.net_parser = NULL;
    dpi->dpi_sess->conn_releasing = true;
    nfe_conn_release(conn);
    dpi->dpi_sess->conn_releasing = false;
}

void
dpi_report_kpis(struct dpi_session *dpi_session)
{
    struct dpi_stats_counters *counters;
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
    struct nfe_tuple tuple;
    struct timespec now;
    nfe_conn_t conn;
    int threshold;
    uint64_t ts;
    int dir = 0;

    mgr = dpi_get_mgr();
    clock_gettime(CLOCK_MONOTONIC, &now);

    dpi_session = session->handler_ctxt;

    threshold = dpi_session->wc_interval;
    if (!threshold) threshold = WALLEYE_PERIODIC_INTERVAL;

    if ((now.tv_sec - mgr->periodic_ts) < threshold) return;

    ts = ((uint64_t)now.tv_sec * 1000) + (now.tv_nsec / 1000000);

    memset(&tuple, 0, sizeof(tuple));

    /* Expire idle nfe connections */
    tuple.proto = IPPROTO_ICMP;
    conn = nfe_conn_lookup_by_tuple(dpi_session->ct, &tuple, ts, &dir);
    nfe_conn_release(conn);

    tuple.proto = IPPROTO_TCP;
    conn = nfe_conn_lookup_by_tuple(dpi_session->ct, &tuple, ts, &dir);
    nfe_conn_release(conn);

    tuple.proto = IPPROTO_UDP;
    conn = nfe_conn_lookup_by_tuple(dpi_session->ct, &tuple, ts, &dir);
    nfe_conn_release(conn);

    LOGI("%s:%s: active connections: %u", __func__,
         session->name, dpi_session->connections);

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

    if (dpi_session->ct)
    {
        nfe_conntrack_destroy(dpi_session->ct);
        dpi_session->ct = NULL;
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

/* nfe_ext_conn_alloc()
 *
 * libnfe provides weak symbols for connection allocation and deallocation
 * through nfe_ext_conn_alloc() and nfe_ext_conn_free() making them easy to
 * intercept. In this example, the connection is expanded to contain extra
 * stuff for dpi.
 */
void *
nfe_ext_conn_alloc(size_t size, const struct nfe_tuple *tuple)
{
    struct dpi_conn *dpi;

    if (!(dpi = calloc(1, sizeof(*dpi) + size)))
        return NULL;

    dpi->initialized = false;

    return dpi->priv;
}

/* nfe_ext_conn_free()
 *
 * The counter-part to nfe_ext_conn_alloc().
 */
void
nfe_ext_conn_free(void *p, const struct nfe_tuple *tuple)
{
    struct dpi_conn *dpi = container_of(p, struct dpi_conn, priv);

    dpi->dpi_sess->connections--;
    dpi->net_hdr.acc->refcnt--;

    /* destroy dpi context */
    if (dpi->stream) {
        /* if a conn is not being released, then it expired due to a timeout */
        if (!dpi->dpi_sess->conn_releasing) {
            dpi->dpi_sess->err_incomplete++;
            dpi->scan_error |= SCAN_ERROR_INCOMPLETE;
        }
        destroy_stream(dpi->dpi_sess, NULL, dpi, tuple, 0);
    }

    free(dpi);
}
