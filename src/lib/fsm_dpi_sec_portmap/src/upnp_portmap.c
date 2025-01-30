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

#include <miniupnpc/igd_desc_parse.h>
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnpdev.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "ds_tree.h"
#include "fsm.h"
#include "fsm_dpi_sec_portmap.h"
#include "log.h"
#include "memutil.h"
#include "neigh_table.h"
#include "os_nif.h"
#include "os_time.h"
#include "os_types.h"
#include "ovsdb.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"
#include "sockaddr_storage.h"
#include "upnp_portmap.h"
#include "upnp_portmap_pb.h"
#include "upnp_portmap.pb-c.h"
#include "upnp_report_aggregator.h"
#include "util.h"

void
upnp_portmap_init(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *upnp_session;

    /* Set all things up */
    upnp_session = fsm_dpi_sec_portmap_get_session(session);
    if (upnp_session == NULL)
    {
        LOGD("%s: fsm session not initialized properly", __func__);
        return;
    }

    upnp_session->aggr = upnp_report_aggregator_alloc(session);

    upnp_portmap_mapped_init(upnp_session);
    upnp_portmap_static_init(upnp_session);

    upnp_session->initialized = true;
}

void
upnp_portmap_exit(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *upnp_session;

    upnp_session = fsm_dpi_sec_portmap_get_session(session);

    upnp_portmap_mapped_exit(upnp_session);
    upnp_portmap_static_exit(upnp_session);

    upnp_report_aggregator_free(upnp_session->aggr);

    FREE(upnp_session->mqtt_topic);

    upnp_session->initialized = false;
}

void
upnp_portmap_update(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *upnp_session;

    upnp_session = fsm_dpi_sec_portmap_get_session(session);
    upnp_session->mqtt_topic = session->ops.get_config(session, "mqtt_avs");
}

bool
upnp_portmap_fetch_all(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *u_session;
    bool ret_mapped;
    bool ret_static;
    bool ret;

    ret_mapped = upnp_portmap_fetch_mapped(session);
    ret_static = upnp_portmap_fetch_static(session);
    ret = ret_mapped | ret_static;

    if (ret)
    {
        u_session = fsm_dpi_sec_portmap_get_session(session);
        upnp_report_aggregator_add_snapshot(u_session->aggr, u_session->mapped_ports);
        upnp_report_aggregator_add_snapshot(u_session->aggr, u_session->static_ports);
    }

    return ret;
}

///////////////////////////////////////////
// UPnP port mapping related stuff
///////////////////////////////////////////
void
upnp_portmap_mapped_init(struct fsm_dpi_sec_portmap_session *upnp_session)
{
    char iface_ip_addr[INET_ADDRSTRLEN];
    char lanaddr[40] = { 0 };
    struct UPNPDev *mainIGD;
    struct UPNPDev *devIter;
    unsigned char ttl = 2;
    struct UPNPUrls urls;
    struct IGDdatas data;
    os_ipaddr_t ip_addr;
    int error = 0;
    int retval;
    bool rc;

    upnp_session->mapped_ports = upnp_portmap_create_snapshot();
    upnp_session->n_mapped_ports = 0;
    upnp_session->igdControlUrl = NULL;
    upnp_session->upnpServiceType = NULL;

    /*
     * Using IP address instead to circumvent the 'br-home' and 'br-home.tx' headache
     * when broadcasting in router mode.
     * This also works in bridge mode as we still broadcast on the right interface.
     */
    rc = os_nif_ipaddr_get("br-home", &ip_addr);
    if (rc)
    {
        snprintf(iface_ip_addr, sizeof(iface_ip_addr), PRI(os_ipaddr_t), FMT(os_ipaddr_t, ip_addr));
        LOGD("%s: UPnP discover performed with IP ADDRESS %s", __func__, iface_ip_addr);
    }

    mainIGD = upnpDiscover(2000, iface_ip_addr, NULL, 0, 0, ttl, &error);
    if (mainIGD == NULL)
    {
        LOGD("%s: Nothing found on interface %s. Likely router Mode. Try 'lo'.", __func__, iface_ip_addr);

        /* Try again using lo */
        mainIGD = upnpDiscover(2000, "lo", NULL, 0, 0, ttl, &error);
        if (mainIGD == NULL)
        {
            /* There is very little left for us to try */
            LOGI("%s: Cannot connect with UPnP service. Won't report dynamic port mappings.", __func__);
            return;
        }
    }

    /*
     * There could be more than one IGD on the subnet...
     * need to display something about that, and eventually pick the one
     * we want to monitor.
     */
    if (mainIGD->pNext != NULL)
    {
        LOGD("%s: Found multiple IGDs", __func__);
        devIter = mainIGD;
        while (devIter)
        {
            LOGD("%s: IGD present @ %s", __func__, devIter->descURL);
            devIter = devIter->pNext;
        }
    }

    retval = UPNP_GetValidIGD(mainIGD, &urls, &data, lanaddr, sizeof(lanaddr));

    /* Need to work on the return value here... 1 and 2 are OK... -1, 0 and 3 are not */
    if (retval != 1 && retval != 2)
    {
        LOGI("%s: Cannot find IGD (%d)", __func__, retval);
        return;
    }

    upnp_session->igdControlUrl = STRDUP(urls.controlURL);
    upnp_session->upnpServiceType = STRDUP(data.first.servicetype);

    /* Required cleanup */
    FreeUPNPUrls(&urls);
    freeUPNPDevlist(mainIGD);
}

void
upnp_portmap_mapped_exit(struct fsm_dpi_sec_portmap_session *upnp_session)
{
    upnp_portmap_delete_snapshot(upnp_session->mapped_ports);
    upnp_session->n_mapped_ports = 0;
    upnp_portmap_delete_snapshot(upnp_session->static_ports);
    upnp_session->n_static_ports = 0;

    /* Cleanup the igdControlUrl and upnpServiceType in the session */
    FREE(upnp_session->upnpServiceType);
    FREE(upnp_session->igdControlUrl);
}

/*
 * format is:
 * TCP:6666:192.168.40.128:7777:0:description
 *
 * Only very limited validation is performed on the file format.
 */
int
upnp_portmap_fetch_mapped_from_file(ds_tree_t *store, char *upnp_leases)
{
    struct mapped_port_t *duplicate;
    struct mapped_port_t *new_port;
    char *intClient;
    char *duration;
    char *protocol;
    int count = 0;
    char *intPort;
    char *extPort;
    char buf[128];
    char *desc;
    char *tmp;
    FILE *fp;

    fp = fopen(upnp_leases, "r");
    if (fp == NULL) return 0;

    while (fgets(buf, sizeof(buf), fp))
    {
        tmp = buf;
        protocol = strtok_r(tmp, ":", &tmp);
        if (protocol == NULL) continue;
        extPort = strtok_r(tmp, ":", &tmp);
        if (extPort == NULL) continue;
        intClient = strtok_r(tmp, ":", &tmp);
        if (intClient == NULL) continue;
        intPort = strtok_r(tmp, ":", &tmp);
        if (intPort == NULL) continue;
        duration = strtok_r(tmp, ":", &tmp);
        if (duration == NULL) continue;
        desc = (tmp ? tmp : "");

        /* Populate one record */
        new_port = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL,
                                              extPort, intClient, intPort, protocol,
                                              desc, "1", "", duration);

        /* Check for duplicate. Should really never happen, but just in case. */
        duplicate = ds_tree_find(store, new_port);
        if (duplicate)
        {
            upnp_portmap_delete_record(new_port);
            continue;
        }

        /* Add record to next snapshot */
        ds_tree_insert(store, new_port, new_port);
        count++;
    }

    return count;
}

bool
upnp_portmap_fetch_mapped(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *u_session;
    char intClient[INET_ADDRSTRLEN];
    struct mapped_port_t *new_port;
    bool need_report = false;
    ds_tree_t *new_snapshot;
    ds_tree_t *del_snapshot;
    char duration[16];
    char protocol[4];
    char idx_str[12];
    char intPort[6];
    char extPort[6];
    char enabled[4];
    char rHost[64];
    char desc[80];
    int ret = 0;
    int idx = 0;

    LOGT("%s: Getting mapped ports from IGD", __func__);

    u_session = fsm_dpi_sec_portmap_get_session(session);

    if (u_session->mapped_ports == NULL)
    {
        LOGD("%s: no IGD found, skipping", __func__);
        return need_report;
    }

    new_snapshot = CALLOC(1, sizeof(*new_snapshot));
    ds_tree_init(new_snapshot, upnp_portmap_compare_record, struct mapped_port_t, node);

    /* In case we could not communicate with the IGD, we won't have a control URL */
    if (u_session->igdControlUrl)
    {
        while (true)
        {
            /* Everything is 'text' based, including integers */
            snprintf(idx_str, sizeof(idx_str), "%u", idx);

            ret = UPNP_GetGenericPortMappingEntry(
                    u_session->igdControlUrl, u_session->upnpServiceType, idx_str,
                    extPort, intClient, intPort, protocol, desc, enabled, rHost, duration);

            if (ret != 0)
            {
                u_session->n_mapped_ports = idx;
                break;
            }

            /* Populate one record */
            new_port = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL,
                                                  extPort, intClient, intPort, protocol,
                                                  desc, enabled, rHost, duration);

            /* Add record to next snapshot */
            ds_tree_insert(new_snapshot, new_port, new_port);

            /* Move on to the next possible port mapped */
            idx++;
        }
    }

    /* Compare the snapshots so we know whether to report */
    need_report = upnp_portmap_compare_snapshot(u_session->mapped_ports, new_snapshot);

    if (need_report == false)
    {
        LOGT("%s: Nothing to be reported", __func__);
        upnp_portmap_delete_snapshot(new_snapshot);
    }
    else
    {
        /* Swap snapshots */
        del_snapshot = u_session->mapped_ports;
        upnp_portmap_delete_snapshot(del_snapshot);
        u_session->mapped_ports = new_snapshot;
    }

    return need_report;
}

struct mapped_port_t *
upnp_portmap_create_record(enum upnp_capture_source source,
                           char *extPort, char *intClient, char *intPort, char *protocol,
                           char *desc, char *enabled, char *rHost, char *duration)
{
    struct mapped_port_t *new_port;

    new_port = CALLOC(1, sizeof(*new_port));
    if (new_port == NULL) return NULL;

    new_port->source = source;

    new_port->intPort = atoi(intPort);
    new_port->extPort = atoi(extPort);

    /* Per spec it can only be 'TCP' or 'UDP' uppercase */
    if (strcmp(protocol, "TCP") == 0 || strcmp(protocol, "tcp") == 0)
        new_port->protocol = UPNP_MAPPING_PROTOCOL_TCP;
    else if (strcmp(protocol, "UDP") == 0 || strcmp(protocol, "udp") == 0)
        new_port->protocol = UPNP_MAPPING_PROTOCOL_UDP;
    else
        new_port->protocol = UPNP_MAPPING_PROTOCOL_UNSPECIFIED;

    new_port->duration = atoi(duration);
    new_port->desc = STRDUP(desc);
    if (strcmp(enabled, "1") == 0) new_port->enabled = true;

    new_port->intClient = sockaddr_storage_create(AF_INET, intClient);

    new_port->captured_at_ms = clock_real_ms();

    neigh_table_lookup(new_port->intClient, &new_port->device_id);

    return new_port;
}

struct mapped_port_t *
upnp_portmap_copy_record(struct mapped_port_t *orig)
{
    struct mapped_port_t *new_port;

    if (orig == NULL) return NULL;

    new_port = CALLOC(1, sizeof(*new_port));
    if (new_port == NULL) return NULL;

    new_port->source = orig->source;
    new_port->intPort = orig->intPort;
    new_port->extPort = orig->extPort;
    new_port->protocol = orig->protocol;
    new_port->duration = orig->duration;
    new_port->desc = STRDUP(orig->desc);
    new_port->enabled = orig->enabled;

    new_port->intClient = CALLOC(1, sizeof(*new_port->intClient));
    sockaddr_storage_copy(orig->intClient, new_port->intClient);

    memcpy(&new_port->device_id, &orig->device_id, sizeof(os_macaddr_t));

    return new_port;
}

/**
 * @brief Mapped port comparison function
 *
 * Forwarded ports are uniquely identified with <protocol, ext_port>
 */
int
upnp_portmap_compare_record(const void *a, const void *b)
{
    const struct mapped_port_t *p_a = a;
    const struct mapped_port_t *p_b = b;
    int cmp;

    cmp = p_a->protocol - p_b->protocol;
    if (cmp != 0) return cmp;

    cmp = p_a->extPort - p_b->extPort;
    if (cmp != 0) return cmp;

    return 0;
}

void
upnp_portmap_dump_record(struct mapped_port_t *rec)
{
    char ip_str[INET_ADDRSTRLEN + 10];  /* Function adds a header !! */
    char mac_str[OS_MACSTR_SZ];
    char *protocol;
    char *source;

    switch (rec->protocol)
    {
        case UPNP_MAPPING_PROTOCOL_TCP:
            protocol = "TCP";
            break;
        case UPNP_MAPPING_PROTOCOL_UDP:
            protocol = "UDP";
            break;
        case UPNP_MAPPING_PROTOCOL_UNSPECIFIED:
        default:
            protocol = "Undefined";
    }

    switch (rec->source)
    {
        case UPNP_SOURCE_IGD_POLL:
            source = "IGD";
            break;
        case UPNP_SOURCE_PKT_INSPECTION_ADD:
            source = "PKT ADD";
            break;
        case UPNP_SOURCE_PKT_INSPECTION_DEL:
            source = "PKT DEL";
            break;
        case UPNP_SOURCE_OVSDB_STATIC:
            source = "OVSDB";
            break;
        case UPNP_SOURCE_CAPTURE_SOURCE_UNSPECIFIED:
        default:
            source = "Undefined";
    }

    snprintf(mac_str, sizeof(mac_str), PRI_os_macaddr_t, FMT_os_macaddr_t(rec->device_id));

    if (rec->intClient)
        sockaddr_storage_str(rec->intClient, ip_str, sizeof(ip_str));
    else
        STRSCPY(ip_str, "NULL");

    LOGT("%s: src = %s proto = %s dev_id = %s intClient = %s intPort = %d extPort = %d "
         "duration = %d enabled = %d desc = %s",
         __func__, source, protocol, mac_str, ip_str, rec->intPort, rec->extPort,
         rec->duration, rec->enabled, rec->desc);
}

void
upnp_portmap_delete_record(struct mapped_port_t *r)
{
    if (r == NULL) return;

    FREE(r->intClient);
    FREE(r->desc);
    FREE(r);
}


ds_tree_t *
upnp_portmap_create_snapshot(void)
{
    ds_tree_t *snapshot;

    snapshot = CALLOC(1, sizeof(*snapshot));
    if (snapshot == NULL) return NULL;

    ds_tree_init(snapshot, upnp_portmap_compare_record, struct mapped_port_t, node);
    return snapshot;
}

/*
 * Compare 2 snapshots contained in ds_trees
 *
 * Since they both are build using the same comparator, the
 * order of the entries will be identical when iterating on
 * each entry. If any 2 entries differ, or if one of the
 * snapshot is longer than the other, we need to report.
 */
bool
upnp_portmap_compare_snapshot(ds_tree_t *a, ds_tree_t *b)
{
    struct mapped_port_t *it_a;
    struct mapped_port_t *it_b;

    it_a = ds_tree_head(a);
    it_b = ds_tree_head(b);
    while (it_a != NULL && it_b != NULL)
    {
        if (upnp_portmap_compare_record(it_a, it_b) != 0) return true;

        it_a = ds_tree_next(a, it_a);
        it_b = ds_tree_next(b, it_b);
    }

    if (it_a != NULL || it_b != NULL) return true;

    return false;
}

void
upnp_portmap_dump_snapshot(ds_tree_t *tree)
{
    struct mapped_port_t *iter;

    if (tree == NULL)
    {
        LOGD("%s: NULL tree", __func__);
        return;
    }

    iter = ds_tree_head(tree);
    while (iter != NULL)
    {
        upnp_portmap_dump_record(iter);
        iter = ds_tree_next(tree, iter);
    }
}

void
upnp_portmap_delete_snapshot(ds_tree_t *tree)
{
    struct mapped_port_t *remove;
    struct mapped_port_t *iter;

    if (tree == NULL) return;

    iter = ds_tree_head(tree);
    while (iter != NULL)
    {
        remove = iter;
        iter = ds_tree_next(tree, iter);

        ds_tree_remove(tree, remove);
        upnp_portmap_delete_record(remove);
    }

    FREE(tree);
}

int
upnp_portmap_send_report(struct fsm_session *session)
{
    struct fsm_dpi_sec_portmap_session *u_session;
    const size_t buf_len = 4096;
    uint8_t pre_alloc_buf[buf_len];  // make sure we don't have to allocate in most cases
    Upnp__Portmap__Report *pb;
    char *mqtt_channel;
    int retval = -1;
    uint8_t *buf;
    size_t len;

    u_session = fsm_dpi_sec_portmap_get_session(session);

    mqtt_channel = u_session->mqtt_topic;
    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE) || mqtt_channel == NULL)
    {
        LOGD("%s: TOTAL PORTMAPS = %zu", __func__, u_session->aggr->n_ports);
        upnp_report_aggregator_dump(u_session->aggr);
    }

    /* We can't send anything, so stop right now */
    if (mqtt_channel == NULL)
    {
        upnp_report_aggregator_flush(u_session->aggr);
        return 0;
    }

    if (u_session->aggr->n_ports == 0)
    {
        LOGT("%s: No ports updated. Don't send report.", __func__);
        return 0;
    }

    /* Populate the entire protobuf now */
    pb = upnp_portmap_alloc_report(session);

    len = upnp__portmap__report__get_packed_size(pb);
    if (len == 0) goto cleanup_serialized_buf;

    /* Allocate more space if needed */
    if (len > buf_len)
    {
        buf = MALLOC(len * sizeof(*buf));
        if (buf == NULL) goto cleanup_serialized_buf;
    }
    else
    {
        buf = pre_alloc_buf;
    }

    /* Populate serialization output structure */
    len = upnp__portmap__report__pack(pb, buf);
    LOGD("%s: protobuf serialized into %zu bytes to be sent on %s",
         __func__, len, u_session->mqtt_topic);

    session->ops.send_pb_report(session, mqtt_channel, buf, len);

    /* Clear out the aggregator */
    upnp_report_aggregator_flush(u_session->aggr);

    /* Free anything that was allocated */
    if (buf != pre_alloc_buf) FREE(buf);

    retval = 0;

cleanup_serialized_buf:
    upnp_portmap_free_report(pb);

    return retval;
}

///////////////////////////////////////////
// Static port mapping related code
///////////////////////////////////////////

// Fetch statically mapped ports
ovsdb_table_t table_IP_Port_Forward;
static struct ovsdb_portmap_cache_t ovsdb_portmap_cache =
{
    .initialized = false,
    .refcount = 0,
};

struct ovsdb_portmap_cache_t *
get_portmap_from_ovsdb(void)
{
    return &ovsdb_portmap_cache;
}

struct mapped_port_t *
upnp_portmap_create_static_record(struct schema_IP_Port_Forward *new_rec,
                                  struct schema_IP_Port_Forward *old_rec)
{
    struct mapped_port_t *new_port;
    char src_port[6];
    char dst_port[6];
    char *protocol;
    char *ip_addr;

    if (new_rec == NULL) return NULL;

    protocol = new_rec->protocol;
    if (old_rec && new_rec->protocol_changed)
    {
        protocol = old_rec->protocol;
    }

    snprintf(src_port, sizeof(src_port), "%d", new_rec->src_port);
    if (old_rec && new_rec->src_port_changed)
    {
        snprintf(src_port, sizeof(src_port), "%d", old_rec->src_port);
    }

    snprintf(dst_port, sizeof(src_port), "%d", new_rec->dst_port);
    if (old_rec && new_rec->dst_port_changed)
    {
        snprintf(dst_port, sizeof(src_port), "%d", old_rec->dst_port);
    }

    ip_addr = new_rec->dst_ipaddr;
    if (old_rec && new_rec->dst_ipaddr_changed)
    {
        ip_addr = old_rec->dst_ipaddr;
    }

    new_port = upnp_portmap_create_record(UPNP_SOURCE_OVSDB_STATIC,
                                          src_port, ip_addr, dst_port, protocol,
                                          "static_port_in_ovsdb", "1", "", "0");

    return new_port;
}

/**
 * @brief IP_Port_Forward's event callbacks
 *
 * port_info contains the fully updated record as it should be used.
 * old_rec contains the prior values of the changed fields (if <FIELD>_present is true)
 */
void
callback_IP_Port_Forward(ovsdb_update_monitor_t *mon,
                         struct schema_IP_Port_Forward *old_rec,
                         struct schema_IP_Port_Forward *port_info)
{
    struct ovsdb_portmap_cache_t *cache;
    struct mapped_port_t *the_port;
    struct mapped_port_t *iter;

    cache = get_portmap_from_ovsdb();

    if (!cache->initialized)
    {
        LOGD("%s: local portmap cache of ovsdb not initialized", __func__);
        return;
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            the_port = upnp_portmap_create_static_record(port_info, NULL);

            /* Insert inside cache->store */
            ds_tree_insert(cache->store, the_port, the_port);

            break;

        case OVSDB_UPDATE_DEL:
            the_port = upnp_portmap_create_static_record(port_info, NULL);

            iter = ds_tree_find(cache->store, the_port);
            if (iter != NULL)
            {
                /* Delete from cache->store */
                ds_tree_remove(cache->store, iter);
                upnp_portmap_delete_record(iter);
            }
            else
            {
                LOGD("upnp_%s: not cannot find record to be deleted", __func__);
                upnp_portmap_dump_record(the_port);
            }
            upnp_portmap_delete_record(the_port);

            break;

        case OVSDB_UPDATE_MODIFY:
            /* Retrieve and delete the entry matching 'old_rec' */
            the_port = upnp_portmap_create_static_record(port_info, old_rec);

            iter = ds_tree_find(cache->store, the_port);
            if (iter != NULL)
            {
                /* Delete from cache->cache */
                ds_tree_remove(cache->store, iter);
                upnp_portmap_delete_record(iter);
            }
            else
            {
                LOGD("upnp_%s: not cannot find record to be updated", __func__);
                upnp_portmap_dump_record(the_port);
            }
            /* Clean up allocated memory */
            upnp_portmap_delete_record(the_port);


            /* Insert inside cache->store */
            the_port = upnp_portmap_create_static_record(port_info, NULL);
            ds_tree_insert(cache->store, the_port, the_port);

            break;

        default:
            break;
    }
}

void
upnp_portmap_static_init(struct fsm_dpi_sec_portmap_session *u_session)
{
    struct ovsdb_portmap_cache_t *cache;

    LOGT("%s: registering OVSDB callbacks", __func__);

    u_session->static_ports = upnp_portmap_create_snapshot();
    u_session->n_static_ports = 0;

    /* Keep data stored locally */
    cache = get_portmap_from_ovsdb();
    if (!cache->initialized) {
        cache->store = upnp_portmap_create_snapshot();

        OVSDB_TABLE_INIT_NO_KEY(IP_Port_Forward);
        OVSDB_TABLE_MONITOR(IP_Port_Forward, false);

        cache->initialized = true;
    }
    cache->refcount++;
}

void
upnp_portmap_static_exit(struct fsm_dpi_sec_portmap_session *u_session)
{
    struct ovsdb_portmap_cache_t *cache = get_portmap_from_ovsdb();

    if (!cache->initialized) return;

    /* Deregister monitor events */
    ovsdb_table_fini(&table_IP_Port_Forward);
    upnp_portmap_delete_snapshot(cache->store);
    cache->store = NULL;
    cache->initialized = false;
}

bool
upnp_portmap_fetch_static(struct fsm_session *session)
{
    struct ovsdb_portmap_cache_t *cache = get_portmap_from_ovsdb();
    struct fsm_dpi_sec_portmap_session *u_session;
    struct mapped_port_t *new_port;
    struct mapped_port_t *iter;
    ds_tree_t *new_snapshot;
    bool need_report;
    int new_count;

    LOGT("%s: Getting static ports mapped in OVSDB", __func__);

    u_session = fsm_dpi_sec_portmap_get_session(session);

    /* Compare ovsdb_portmap_cache and u_session->static_ports so we know to report changes */
    need_report = upnp_portmap_compare_snapshot(cache->store, u_session->static_ports);

    if (!need_report)
    {
        LOGT("%s: Nothing to be reported", __func__);
        return need_report;
    }

    /* Transfer the content of the local store for IP_Port_Forward into static_ports */
    new_snapshot = upnp_portmap_create_snapshot();
    new_count = 0;

    iter = ds_tree_head(cache->store);
    while (iter != NULL)
    {
        /* Copy content of iter into the new_port */
        new_port = upnp_portmap_copy_record(iter);

        ds_tree_insert(new_snapshot, new_port, new_port);
        new_count++;

        iter = ds_tree_next(cache->store, iter);
    }

    upnp_portmap_delete_snapshot(u_session->static_ports);

    u_session->static_ports = new_snapshot;
    u_session->n_static_ports = new_count;

    return need_report;
}
