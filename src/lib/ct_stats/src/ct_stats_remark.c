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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ev.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "fcm.h"
#include "ct_stats.h"
#include "ct_stats_remark.h"
#include "network_metadata_report.h"
#include "network_metadata.h"
#include "fsm_dpi_utils.h"
#include "nf_utils.h"
#include "kconfig.h"
#include "gatekeeper_bulk_reply_msg.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

static char *dir2str(uint8_t direction)
{
    switch (direction)
    {
        case NET_MD_ACC_INBOUND_DIR:
            return "inbound";
        case NET_MD_ACC_OUTBOUND_DIR:
            return "outbound";
        case NET_MD_ACC_LAN2LAN_DIR:
            return "lan2lan";
        case NET_MD_ACC_UNSET_DIR:
        default:
            return "unspecified";
    }
}

static void ct_stats_walk_gkreq(struct gk_request *req)
{
    struct gk_device2app_req *gappreq;
    struct gk_bulk_request *gbreq;
    struct gk_req_header *hdr;
    union gk_data_req *udreq;
    size_t i, j;

    if (!req) return;

    udreq = &req->req;
    gbreq = &udreq->gk_bulk_req;

    LOGD("%s: ********Gatekeeper Bulk Request Dump*********", __func__);
    for (i = 0; i < gbreq->n_devices; i++)
    {
        gappreq = gbreq->devices[i];
        hdr = gappreq->header;

        LOGD("%s: ********Mac Address:  " PRI_os_macaddr_t
             " location id %s, network id: %s, node id %s, supported flags %" PRIu64,
             __func__,
             FMT_os_macaddr_pt(hdr->dev_id),
             hdr->location_id,
             hdr->network_id,
             hdr->node_id,
             hdr->supported_features);
        for (j = 0; j < gappreq->n_apps; j++)
        {
            LOGD("%s: ********appname: [%s]", __func__, gappreq->apps[j]);
        }
    }
    return;
}

static void ct_stats_walk_dev2apps(const char *caller)
{
    struct net_md_stats_accumulator *acc;
    struct ct_device2apps *d2a;
    struct ct_app2accs *a2a;
    flow_stats_t *ct_stats;
    struct flow_key *fkey;

    LOGD("In %s caller: %s", __func__, caller);
    ct_stats = ct_stats_get_active_instance();
    LOGD("****Num Devices: %zu********", ct_stats->n_device2apps);
    ds_tree_foreach (&ct_stats->device2apps, d2a)
    {
        LOGD("****Num Apps: %zu for Mac: %s********", d2a->n_apps, d2a->mac);
        ds_tree_foreach (&d2a->apps, a2a)
        {
            LOGD("app: %s", a2a->app_name);
            ds_tree_foreach (&a2a->accs, acc)
            {
                LOGD("acc: %p", acc);
                fkey = acc->fkey;
                LOGD("fkey: %p  and protocol[%d]:", acc->fkey, fkey->protocol);
            }
        }
    }
    return;
}

static int ct_net_md_acc_cmp(const void *a, const void *b)
{
    const struct flow_key *fkey_a = (struct flow_key *)a;
    const struct flow_key *fkey_b = (struct flow_key *)b;

    size_t ipl;
    int cmp;
    /* Compare ip versions */
    cmp = fkey_a->ip_version - fkey_b->ip_version;
    if (cmp != 0) return cmp;

    /* Get ip version compare length */
    ipl = (fkey_a->ip_version == 4 ? INET_ADDRSTRLEN : INET6_ADDRSTRLEN);

    /* Compare source IP addresses */
    cmp = strncmp(fkey_a->src_ip, fkey_b->src_ip, ipl);
    if (cmp != 0) return cmp;

    /* Compare dest IP addresses */
    cmp = strncmp(fkey_a->dst_ip, fkey_b->dst_ip, ipl);
    if (cmp != 0) return cmp;

    /* Compare ip protocols */
    cmp = fkey_a->protocol - fkey_b->protocol;
    if (cmp != 0) return cmp;

    /* Compare source ports */
    cmp = fkey_a->sport - fkey_b->sport;
    if (cmp != 0) return cmp;

    /* Compare dest ports */
    cmp = fkey_a->dport - fkey_b->dport;
    if (cmp != 0) return cmp;

    return cmp;
}

static bool ct_stats_update_app2accs(struct net_md_stats_accumulator *acc, struct ct_device2apps *d2a, char *app_name)
{
    struct net_md_stats_accumulator *lkp_acc;
    struct ct_app2accs *a2a;

    if (!app_name || !acc) return false;

    a2a = ds_tree_find(&d2a->apps, app_name);
    if (!a2a)
    {
        a2a = CALLOC(1, sizeof(struct ct_app2accs));
        a2a->app_name = STRDUP(app_name);
        ds_tree_init(&a2a->accs, ct_net_md_acc_cmp, struct net_md_stats_accumulator, net_md_acc_node);
        ds_tree_insert(&d2a->apps, a2a, a2a->app_name);
        d2a->n_apps++;
    }

    lkp_acc = ds_tree_find(&a2a->accs, acc->fkey);
    if (!lkp_acc)
    {
        ds_tree_insert(&a2a->accs, acc, acc->fkey);
        a2a->n_accs++;
    }
    return true;
}

static bool ct_stats_update_device2apps(struct net_md_stats_accumulator *acc, ds_tree_t *d2as, char *lkp_mac)
{
    flow_stats_t *ct_stats;
    struct ct_device2apps *d2a;
    struct flow_key *fkey;
    struct flow_tags *ftag;
    bool rc = false;

    net_md_log_acc(acc, __func__);

    if (!lkp_mac) return false;
    ct_stats = ct_stats_get_active_instance();
    d2a = ds_tree_find(d2as, lkp_mac);
    if (!d2a)
    {
        d2a = CALLOC(1, sizeof(struct ct_device2apps));
        d2a->mac = STRDUP(lkp_mac);
        ds_tree_init(&d2a->apps, ds_str_cmp, struct ct_app2accs, a2a_node);
        ds_tree_insert(d2as, d2a, d2a->mac);
        ct_stats->n_device2apps++;
    }

    fkey = acc->fkey;
    ftag = fkey->tags[0];

    rc = ct_stats_update_app2accs(acc, d2a, ftag->app_name);
    if (rc == false) return false;
    return true;
}

static void ct_stats_delete_device2apps(struct net_md_stats_accumulator *acc, ds_tree_t *d2as, char *lkp_mac)
{
    struct ct_device2apps *d2a;
    struct ct_app2accs *a2a;
    flow_stats_t *ct_stats;
    struct flow_key *fkey;
    struct flow_tags *ftag;

    net_md_log_acc(acc, __func__);

    if (!lkp_mac) return;

    d2a = ds_tree_find(d2as, lkp_mac);
    if (!d2a) return;

    fkey = acc->fkey;
    ftag = fkey->tags[0];
    ct_stats = ct_stats_get_active_instance();

    a2a = ds_tree_find(&d2a->apps, ftag->app_name);
    if (!a2a) return;
    ds_tree_remove(&a2a->accs, acc);
    a2a->n_accs--;
    if (ds_tree_is_empty(&a2a->accs))
    {
        ds_tree_remove(&d2a->apps, a2a);
        FREE(a2a->app_name);
        FREE(a2a);
        d2a->n_apps--;
    }

    if (ds_tree_is_empty(&d2a->apps))
    {
        ds_tree_remove(d2as, d2a);
        FREE(d2a->mac);
        FREE(d2a);
        ct_stats->n_device2apps--;
    }
    return;
}

bool ct_stats_get_dev2apps(struct net_md_stats_accumulator *acc)
{
    flow_stats_t *ct_stats;
    struct flow_key *fkey;
    bool rc = false;

    if (!acc) return false;

    LOGD("%s: Accumulator direction for acc: %p is %s", __func__, acc, dir2str(acc->direction));

    fkey = acc->fkey;
    if (!fkey) return false;
    if (!fkey->num_tags) return false;

    ct_stats = ct_stats_get_active_instance();
    rc = ct_stats_update_device2apps(acc, &ct_stats->device2apps, fkey->smac ? fkey->smac : fkey->dmac);
    if (rc == false) return false;
    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_DEBUG)) ct_stats_walk_dev2apps(__func__);
    return true;
}

void ct_stats_on_destroy_acc(struct net_md_aggregator *aggr, struct net_md_stats_accumulator *acc)
{
    flow_stats_t *ct_stats;
    struct flow_key *fkey;

    if (!acc) return;

    ct_stats = ct_stats_get_active_instance();

    LOGD("%s: Accumulator direction for acc: %p  is %s", __func__, acc, dir2str(acc->direction));

    fkey = acc->fkey;
    if (!fkey) return;
    if (!fkey->num_tags) return;

    ct_stats_delete_device2apps(acc, &ct_stats->device2apps, fkey->smac ? fkey->smac : fkey->dmac);

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_DEBUG)) ct_stats_walk_dev2apps(__func__);
    return;
}

static void ct_stats_set_dev2app_req(struct gk_device2app_req *devices, struct ct_device2apps *d2a)
{
    struct net_md_stats_accumulator *acc;
    struct flow_key *fkey;
    struct ct_app2accs *a2a;
    struct gk_req_header *hdr;
    flow_stats_t *ct_stats;
    fcm_collect_plugin_t *collector;

    ct_stats = ct_stats_get_active_instance();
    collector = ct_stats->collector;

    if (!devices || !d2a) return;
    if (ds_tree_is_empty(&d2a->apps))
    {
        LOGD("%s: Couldn't find any apps for device %s", __func__, d2a->mac);
        return;
    }
    devices->header = CALLOC(1, sizeof(struct gk_req_header));
    hdr = devices->header;
    hdr->dev_id = CALLOC(1, sizeof(*hdr->dev_id));
    hdr->supported_features = FSM_PROXIMITY_FEATURE;
    hdr->node_id = collector->get_mqtt_hdr_node_id();
    hdr->location_id = collector->get_mqtt_hdr_loc_id();

    if (!str2os_mac_ref(d2a->mac, hdr->dev_id))
    {
        LOGD("%s: Failed to convert mac address string to octets", __func__);
        return;
    }
    devices->apps = CALLOC(d2a->n_apps, sizeof(char *));
    devices->n_apps = 0;
    ds_tree_foreach (&d2a->apps, a2a)
    {
        devices->apps[devices->n_apps] = STRDUP(a2a->app_name);
        devices->n_apps++;
    }

    a2a = ds_tree_head(&d2a->apps);
    acc = ds_tree_head(&a2a->accs);

    if (acc)
    {
        fkey = acc->fkey;
        if (fkey->networkid) hdr->network_id = STRDUP(fkey->networkid);
    }
    return;
}

static struct gk_request *ct_stats_get_gk_request(struct str_set *macs, int mac_op)
{
    struct gk_device2app_req *gappreq;
    struct gk_bulk_request *gbreq;
    struct ct_device2apps *d2a;
    union gk_data_req *udreq;
    struct gk_request *req;
    flow_stats_t *ct_stats;
    size_t i = 0;

    ct_stats = ct_stats_get_active_instance();

    if (!macs->nelems) return NULL;
    if (mac_op == MAC_OP_OUT)
    {
        LOGD("%s: MAC_OP_OUT is not supported", __func__);
        return NULL;
    }
    req = CALLOC(1, sizeof(struct gk_request));
    req->type = FSM_BULK_REQ;
    udreq = &req->req;
    gbreq = &udreq->gk_bulk_req;
    gbreq->req_type = FSM_APP_REQ;
    gbreq->n_devices = 0;
    gbreq->devices = CALLOC(macs->nelems, sizeof(struct gk_device2app_req *));
    for (i = 0; i < macs->nelems; i++)
    {
        d2a = ds_tree_find(&ct_stats->device2apps, macs->array[i]);
        if (!d2a)
        {
            LOGD("%s: Couldn't find the device %s", __func__, macs->array[i]);
            continue;
        }
        gappreq = CALLOC(1, sizeof(struct gk_device2app_req));
        ct_stats_set_dev2app_req(gappreq, d2a);
        gbreq->devices[gbreq->n_devices] = gappreq;
        gbreq->n_devices++;
    }

    return req;
}

static bool ct_stats_update_ct_mark(ds_tree_t *apps, struct gk_device2app_repl *gapprep)
{
    struct net_md_stats_accumulator *acc;
    struct ct_app2accs *a2a;
    struct gk_reply_header *hdr;

    hdr = gapprep->header;

    a2a = ds_tree_find(apps, gapprep->app_name);
    if (!a2a) return false;

    LOGD("%s: Found the app in the gk reply %s with action [%d] and flow marker[%d]",
         __func__,
         a2a->app_name,
         hdr->action,
         hdr->flow_marker);
    ds_tree_foreach (&a2a->accs, acc)
    {
        acc->flow_marker = hdr->flow_marker;
        ct_stats_update_flow(acc, hdr->action);
    }
    return true;
}

static int ct_stats_update_action(struct gk_reply *rep)
{
    struct gk_device2app_repl *gapprep;
    union gk_data_reply *udrep;
    struct gk_bulk_reply *gbrep;
    struct gk_reply_header *hdr;
    struct ct_device2apps *d2a;
    flow_stats_t *ct_stats;
    size_t i;
    int recs = 0;

    ct_stats = ct_stats_get_active_instance();
    udrep = &rep->data_reply;
    gbrep = &udrep->bulk_reply;

    for (i = 0; i < gbrep->n_devices; i++)
    {
        gapprep = gbrep->devices[i];
        hdr = gapprep->header;
        LOGD("%s: Looking for device %s in gk reply", __func__, hdr->dev_id);
        d2a = ds_tree_find(&ct_stats->device2apps, hdr->dev_id);
        if (d2a)
        {
            LOGD("%s: Found the device %s in gk reply", __func__, hdr->dev_id);
            ct_stats_update_ct_mark(&d2a->apps, gapprep);
            recs++;
        }
    }
    return recs;
}

int ct_stats_process_flush_cache(struct fsm_policy *policy)
{
    fcm_collect_plugin_t *collector;
    struct fsm_policy_rules *rules;
    flow_stats_t *ct_stats;
    struct gk_request *req;
    struct gk_reply *rep;
    struct str_set *macs;
    rules = &policy->rules;
    macs = rules->macs;
    size_t i = 0;
    int num = 0;
    bool rc = false;

    if (!rules->mac_rule_present)
    {
        LOGI("%s: No mac addresses present in flush policy", __func__);
        return 0;
    }

    for (i = 0; i < macs->nelems; i++)
        LOGD("MAC: %s", macs->array[i]);

    ct_stats = ct_stats_get_active_instance();
    if (ct_stats == NULL)
    {
        LOGD("%s: no active instance", __func__);
        return 0;
    }

    collector = ct_stats->collector;

    req = ct_stats_get_gk_request(macs, rules->mac_op);
    if (req == NULL)
    {
        LOGD("%s: Failed to set gatekeeper request", __func__);
        return 0;
    }

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_DEBUG)) ct_stats_walk_gkreq(req);
    rep = CALLOC(1, sizeof(struct gk_reply));
    rc = collector->fcm_gk_request(req, rep);
    if (rc == false)
    {
        LOGD("%s: Failed to send gatekeeper request", __func__);
        num = 0;
        goto fail_request;
    }
    num = ct_stats_update_action(rep);
fail_request:
    gk_clear_bulk_requests(req);
    FREE(req);
    gk_clear_bulk_responses(rep);
    FREE(rep);
    return num;
}
