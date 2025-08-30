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

/*
 * MDNS Plugin - Resource Record collection
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "ds.h"
#include "ds_dlist.h"
#include "ds_tree.h"
#include "fsm.h"
#include "log.h"
#include "mdns_plugin.h"
#include "mdns_records.h"
#include "memutil.h"
#include "neigh_table.h"
#include "os.h"
#include "os_nif.h"
#include "os_types.h"
#include "policy_tags.h"
#include "sockaddr_storage.h"
#include "util.h"

#define MAX_STRLEN      (512)

static mdns_records_report_data_t   g_report;

/***********************************************************************************************************
 * Helper(utility) functions
 ***********************************************************************************************************/

bool
mdns_records_str_duplicate(char *src, char **dst)
{
    if (src == NULL)
    {
        *dst = NULL;
        return true;
    }

    *dst = strndup(src, MAX_STRLEN);
    if (*dst == NULL)
    {
        LOGE("%s: could not duplicate %s", __func__, src);
        return false;
    }

    return true;
}

mdns_client_t *
mdns_records_find_client_by_macstr(ds_tree_t *clients, char *mac)
{
    if (!clients) return NULL;

    return (mdns_client_t *)ds_tree_find(clients, (char *)mac);
}

mdns_client_t *
mdns_records_find_client_by_ip_str(ds_tree_t *clients, char *ip)
{
    if (!clients) return NULL;

    return (mdns_client_t *)ds_tree_find(clients, (char *)ip);
}

static bool
mdns_records_check_duplicate(mdns_client_t *client, const struct resource *r)
{
    struct resource     *res          = NULL;
    mdns_records_t      *rec          = NULL;
    mdns_records_list_t *records_list = NULL;
    ds_dlist_iter_t     rec_iter;

    if (!client) return false;

    // iterate through each record
    records_list = &client->records_list;
    for ( rec = ds_dlist_ifirst(&rec_iter, records_list);
          rec != NULL;
          rec = ds_dlist_inext(&rec_iter) )
    {
        res = &rec->resource;
        // check if the resource record's name and type match
        if ((!strcmp(res->name, r->name)) && (res->type == r->type))
        {
            // reset the time to live and the stored timestamp
            res->ttl = r->ttl;
            rec->stored_ts = time(NULL);
            return true;
        }
    }

    return false;
}

size_t
mdns_records_get_num_clients(ds_tree_t *clients)
{
    mdns_client_t   *client      = NULL;
    size_t           num_clients = 0;

    if (!clients) return num_clients;

    ds_tree_foreach(clients, client)
    {
        num_clients++;
    }

    return num_clients;
}

void
mdns_records_set_observation_window(observation_window_t *cur, observation_window_t *rep_obw)
{
    if (!cur || !rep_obw) return;

    rep_obw->started_at = cur->started_at;
    rep_obw->ended_at   = cur->ended_at;

    return;
}

static void
mdns_records_copy_resource_details(const struct resource *src, struct resource *dst)
{
    if (!src || !dst) return;

    /* Copy the owner's name */
    (void)mdns_records_str_duplicate(src->name, &dst->name);

    /* Copy type and other details */
    dst->type  = src->type;
    dst->class = src->class;
    dst->ttl   = src->ttl;

    /* Copy the specific RR type details */
    switch (src->type)
    {
        case QTYPE_A:
            (void)mdns_records_str_duplicate(src->known.a.name, &dst->known.a.name);
            memcpy(&dst->known.a.ip, &src->known.a.ip, sizeof(struct in_addr));
            break;

        case QTYPE_NS:
            (void)mdns_records_str_duplicate(src->known.ns.name, &dst->known.ns.name);
            break;

        case QTYPE_CNAME:
            (void)mdns_records_str_duplicate(src->known.cname.name, &dst->known.cname.name);
            break;

        case QTYPE_PTR:
            (void)mdns_records_str_duplicate(src->known.ptr.name, &dst->known.ptr.name);
            break;

        case QTYPE_TXT:
            if (src->rdlength)
            {
                dst->rdata = MALLOC(src->rdlength);
                memcpy(dst->rdata, src->rdata, src->rdlength);
                dst->rdlength = src->rdlength;
            }
            else
            {
                LOGD("Resource '%s' has rdlength 0, not storing", src->name);
            }
            break;

        case QTYPE_SRV:
            (void)mdns_records_str_duplicate(src->known.srv.name, &dst->known.srv.name);
            dst->known.srv.priority = src->known.srv.priority;
            dst->known.srv.weight   = src->known.srv.weight;
            dst->known.srv.port     = src->known.srv.port;
            break;
    }

    return;
}

/***********************************************************************************************************
 * Getting mac address from IP using neigh_table plugin
 ***********************************************************************************************************/

static bool
mdns_records_get_mac(struct sockaddr_storage *from, char *mac_str)
{
   os_macaddr_t            mac;
   bool                    ret;

   ret = neigh_table_lookup(from, &mac);
   if (!ret)
   {
       LOGD("%s: Failed to lookup IP in neigh_table_lookup", __func__);
       return false;
   }

   ret = os_nif_macaddr_to_str(&mac, mac_str, PRI_os_macaddr_lower_t);
   if (!ret)
   {
       LOGE("%s: Failed to convert mac address to str", __func__);
       return false;
   }

   return true;
}

static void
mdns_records_get_client_macs(ds_tree_t *clients)
{
    mdns_client_t   *client           = NULL;
    char             mac[MAC_STR_LEN] = { 0 };
    bool             ret              = false;

    if (!clients) return;

    ds_tree_foreach(clients, client)
    {
        /* If this client's mac has already been retrieved,
           move on to the next one */
        if (strlen(client->mac_str))    continue;

        memset(mac, 0, sizeof(char) * MAC_STR_LEN);
        ret = mdns_records_get_mac(&client->from, mac);
        if (ret)
        {
            /* Store the MAC address */
            STRSCPY(client->mac_str, mac);
            LOGI("%s: Client with ip '%s' has mac '%s'", __func__, client->ip_str, client->mac_str);
        }
        else
        {
            LOGT("%s: Failed to get mac for client with ip '%s'", __func__, client->ip_str);
        }
    }

    return;
}

/***********************************************************************************************************
 * Tracing functions
 ***********************************************************************************************************/

static void
mdns_records_print_client_records(mdns_client_t *client)
{
    struct resource         *res          = NULL;
    mdns_records_list_t     *records_list = NULL;
    mdns_records_t          *rec          = NULL;
    ds_dlist_iter_t          rec_iter;


    LOGT("--------------------------------------------------------------------------------------------------");
    LOGT("\t \t Printing all records for mac:%s ip:%s", client->mac_str, client->ip_str);
    LOGT("--------------------------------------------------------------------------------------------------");

    records_list = &client->records_list;
    for ( rec = ds_dlist_ifirst(&rec_iter, records_list);
          rec != NULL;
          rec = ds_dlist_inext(&rec_iter))
    {
        res = &rec->resource;
        LOGT("Name: %-64s  Type: %-5d TTL: %-5lu", res->name, res->type, res->ttl);
    }

    LOGT("--------------------------------------------------------------------------------------------------");

    return;
}

static void
mdns_records_dump_clients(ds_tree_t *clients)
{
    mdns_client_t   *client = NULL;

    ds_tree_foreach(clients, client)
    {
        mdns_records_print_client_records(client);
    }

    return;
}

static void
mdns_records_dump_report(mdns_records_report_data_t *report)
{
    node_info_t           *node_info = NULL;
    observation_window_t  *window    = NULL;
    ds_tree_t             *clients   = NULL;

    if (!report) return;

    node_info = &report->node_info;
    window    = &report->obs_w;

    LOGT("Location id: %s \t NodeId: %s", node_info->location_id, node_info->node_id);
    LOGT("Observation Window started at: %" PRIu64" \t ended at: %" PRIu64"", window->started_at, window->ended_at);

    clients = &report->stored_clients;
    if (mdns_records_get_num_clients(clients))
    {
        LOGT(" ------ ------ ------ ------ Stored MDNS resource records ------ ------ ------ ------ ------ -- ");
        mdns_records_dump_clients(clients);
    }

    clients = &report->staged_clients;
    if (mdns_records_get_num_clients(clients))
    {
        LOGT(" ------ ------ ------ ------ Staged MDNS resource records ------ ------ ------ ------ ------ -- ");
        mdns_records_dump_clients(clients);
    }

    return;
}

/***********************************************************************************************************
 * Checking macs in tag lists
 ***********************************************************************************************************/
static bool
mdns_records_find_mac_in_tag(char *mac, char *tag)
{
    bool rc  = false;
    int  ret = 0;

    if (!mac || !tag)   return false;

    rc = om_tag_in(mac, tag);
    if (rc)    return true;

    /* The value passed to the device by the controller, can be either a tag or
       a mac address. Hence this comparison is required */
    ret = strncmp(mac, tag, strlen(mac));
    return (ret == 0);
}

static bool
mdns_records_include_client(char *mac, struct mdns_session *md_session)
{
    bool    targeted_devices = false;
    bool    excluded_devices = false;
    bool    ret              = false;

    if (!mac)           return false;
    if (!md_session)    return false;

    if (md_session->targeted_devices)   targeted_devices = true;
    if (md_session->excluded_devices)   excluded_devices = true;

    /* No inclusion or exclusion tag provided by cloud */
    if (!targeted_devices && !excluded_devices) return false;

    /* Only inclusion list is specified */
    if (targeted_devices && !excluded_devices)
    {
        ret = mdns_records_find_mac_in_tag(mac, md_session->targeted_devices);
        return ret;
    }

    /* Only exclusion list is specified */
    if (excluded_devices && !targeted_devices)
    {
        ret = mdns_records_find_mac_in_tag(mac, md_session->excluded_devices);
        /* mac found in excluded_devices list. Return false for exclusion */
        if (ret)    return false;
    }

    /* Both inclusion and exclusion lists are specified */
    if (targeted_devices && excluded_devices)
    {
        /* In this case, just check if the mac is not present in the excluded list */
        ret = mdns_records_find_mac_in_tag(mac, md_session->excluded_devices);
        if (ret)    return false;
    }

    return true;
}

/***********************************************************************************************************
 * Report staging
 ***********************************************************************************************************/
static void
mdns_records_mark_records_as_reported(void)
{
    ds_tree_t           *stored_clients = &g_report.stored_clients;

    mdns_client_t       *client         = NULL;
    mdns_records_list_t *records_list   = NULL;
    mdns_records_t      *rec            = NULL;
    ds_dlist_iter_t      rec_iter;
    size_t               num_clients    = 0;

    num_clients = mdns_records_get_num_clients(stored_clients);
    if (num_clients == 0)   return;

    ds_tree_foreach(stored_clients, client)
    {
        records_list = &client->records_list;
        for ( rec = ds_dlist_ifirst(&rec_iter, records_list);
              rec != NULL;
              rec = ds_dlist_inext(&rec_iter))
        {
            if (rec->staged)
            {
                rec->reported = true;
            }
        }
    }

    return;
}

static void
mdns_records_stage_record(mdns_client_t *client, mdns_records_t *rec)
{
    ds_tree_t       *clients    = &g_report.staged_clients;
    mdns_client_t   *new_client = NULL;
    struct resource *res = NULL, *new_res = NULL;

    mdns_records_list_t *records_list = NULL;
    mdns_records_t      *new_rec      = NULL;

    if (!client || !rec) return;

    new_client = mdns_records_find_client_by_ip_str(clients, client->ip_str);
    if (!new_client)
    {
        new_client = mdns_records_alloc_client(client->ip_str);
        STRSCPY(new_client->mac_str, client->mac_str);
        ds_tree_insert(clients, new_client, new_client->ip_str);
    }

    records_list = &new_client->records_list;

    res = &rec->resource;

    new_rec = mdns_records_alloc_record();
    new_res = &new_rec->resource;
    mdns_records_copy_resource_details(res, new_res);

    ds_dlist_insert_tail(records_list, new_rec);
    new_client->num_records++;

    return;
}

static void
mdns_records_stage_report(struct mdns_session *md_session)
{
    ds_tree_t           *stored_clients = &g_report.stored_clients;

    mdns_records_list_t *records_list   = NULL;
    mdns_records_t      *rec            = NULL;
    ds_dlist_iter_t      rec_iter;

    mdns_client_t       *client         = NULL;
    size_t               num_clients    = 0;
    bool                 include        = false;

    num_clients = mdns_records_get_num_clients(stored_clients);
    if (num_clients == 0)
    {
        LOGT("%s: No clients stored, nothing to report", __func__);
        return;
    }

    ds_tree_foreach(stored_clients, client)
    {
        /* If a client's mac address was not retrieved from neigh_table,
           skip reporting it to the cloud */
        if (strlen(client->mac_str) == 0)    continue;

        /* Check if this client should be included or excluded from
           being reported to the cloud */
        include = mdns_records_include_client(client->mac_str, md_session);
        if (!include)   continue;

        records_list = &client->records_list;
        for ( rec = ds_dlist_ifirst(&rec_iter, records_list);
              rec != NULL;
              rec = ds_dlist_inext(&rec_iter))
        {
            if (!rec->reported)
            {
                /* Mark the records as 'staged'. Once they are successfully
                   reported to the cloud, they will be marked as 'reported' */
                mdns_records_stage_record(client, rec);
                rec->staged = true;
            }
        }
    }
}

/***********************************************************************************************************
 * Cleaning
 ***********************************************************************************************************/
static void
mdns_records_free_resource(struct resource *res)
{
    if (!res)   return;

    switch (res->type)
    {
        case QTYPE_A:
            FREE(res->known.a.name);
            break;

        case QTYPE_NS:
            FREE(res->known.ns.name);
            break;

        case QTYPE_CNAME:
            FREE(res->known.cname.name);
            break;

        case QTYPE_PTR:
            FREE(res->known.ptr.name);
            break;

        case QTYPE_TXT:
            if (res->rdlength)
            {
                FREE(res->rdata);
                res->rdlength = 0;
            }
            break;

        case QTYPE_SRV:
            FREE(res->known.srv.name);
            break;

        default:
            break;
    }

    FREE(res->name);

    return;
}

void
mdns_records_clear_clients(ds_tree_t *clients)
{
    mdns_records_list_t     *records_list;
    mdns_client_t           *client;
    mdns_records_t          *rec;
    struct resource         *res;

    ds_tree_iter_t          client_iter;
    ds_dlist_iter_t         rec_iter;

    if (!clients) return;

    client = ds_tree_ifirst(&client_iter, clients);
    while (client)
    {
        records_list = &client->records_list;

        for ( rec = ds_dlist_ifirst(&rec_iter, records_list);
              rec != NULL;
              rec = ds_dlist_inext(&rec_iter))
        {
            res = &rec->resource;
            mdns_records_free_resource(res);

            ds_dlist_iremove(&rec_iter);
            mdns_records_free_record(rec);

            rec = NULL;
        }

        ds_tree_iremove(&client_iter);
        FREE(client);
        client = NULL;

        client = ds_tree_inext(&client_iter);
    }

    return;
}

static void
mdns_records_clean_stale_records(void)
{
    ds_tree_t               *clients;
    mdns_records_list_t     *records_list;
    mdns_client_t           *client;
    mdns_records_t          *rec;
    struct resource         *res;
    time_t                   now = time(NULL);
    double                   cmp_rec;

    ds_tree_iter_t          client_iter;
    ds_dlist_iter_t         rec_iter;

    clients = &g_report.stored_clients;
    if (!clients)   return;

    client = ds_tree_ifirst(&client_iter, clients);
    while (client)
    {
        records_list = &client->records_list;

        for ( rec = ds_dlist_ifirst(&rec_iter, records_list);
              rec != NULL;
              rec = ds_dlist_inext(&rec_iter))
        {
            /* Clean the record only after it has been reported to the cloud.
               Due to reasons such as:
                    - QM erroring out
                    - MAC address of the client not retrieved from neigh_table
               the record may not have been reported before it's TTL expired. To
               avoid this, check if it has been reported here */
            if (!rec->reported) continue;

            res = &rec->resource;

            cmp_rec = now - rec->stored_ts;
            if (cmp_rec >= res->ttl)
            {
                LOGT("%s: client:%s name:%s type:%d deleting stale record", __func__, client->ip_str, res->name, res->type);
                mdns_records_free_resource(res);

                ds_dlist_iremove(&rec_iter);
                mdns_records_free_record(rec);

                rec = NULL;

                client->num_records--;
            }
        }

        if (client->num_records == 0)
        {
            LOGD("%s: client:%s has no records, deleting client", __func__, client->ip_str);
            ds_tree_iremove(&client_iter);
            FREE(client);
            client = NULL;
        }

        client = ds_tree_inext(&client_iter);
    }
}

/***********************************************************************************************************
 * Storage functions
 ***********************************************************************************************************/

static void
mdns_records_store_record(const struct resource *r, void *data, struct sockaddr_storage *from)
{
    ds_tree_t               *clients      = &g_report.stored_clients;
    mdns_client_t           *client       = NULL;
    mdns_records_list_t     *records_list = NULL;
    mdns_records_t          *rec          = NULL;
    struct resource         *resource     = NULL;

    char                    ip[INET6_ADDRSTRLEN];
    bool                    ret;
    struct sockaddr_in      *in4;
    struct sockaddr_in6     *in6;

    /* Get the IP address of the client */
    switch (from->ss_family)
    {
        case AF_INET:
            in4 = (struct sockaddr_in *)from;
            inet_ntop(AF_INET, &in4->sin_addr, ip, sizeof(ip));
            break;

        case AF_INET6:
            in6 = (struct sockaddr_in6 *)from;
            inet_ntop(AF_INET6, &in6->sin6_addr, ip, sizeof(ip));
            break;
    }

    LOGT("%s: client:%s", __func__, ip);

    client = mdns_records_find_client_by_ip_str(clients, ip);
    if (!client)
    {
        client = mdns_records_alloc_client(ip);
        if (!client)
        {
            LOGE("%s: client:%s failed to alloc memory for client", __func__, ip);
            return;
        }

        sockaddr_storage_copy(from, &client->from);

        ds_tree_insert(clients, client, client->ip_str);
        LOGI("%s: client:%s client added", __func__, client->ip_str);
    }

    /* Get the client's records_list */
    records_list = &client->records_list;

    /* Check for duplicates */
    ret = mdns_records_check_duplicate(client, r);
    if (ret)
    {
        /* Similar record already exists,  ttl and stored_ts were reset
           in mdns_records_check_duplicate. So just bail out  */
        LOGT("%s: client:%s name:%s type:%d record exists", __func__, client->ip_str, r->name, r->type);
        return;
    }

    /* Allocate the record */
    rec = mdns_records_alloc_record();
    if (!rec)
    {
        LOGE("%s: client:%s failed to allocate record entry", __func__, client->ip_str);
        return;
    }

    resource = &rec->resource;
    mdns_records_copy_resource_details(r, resource);

    /* Record the received timestamp */
    rec->stored_ts = time(NULL);

    /* Insert the record into the records_list */
    ds_dlist_insert_tail(records_list, rec);
    client->num_records++;

    LOGI("%s: client:%s name:%s type:%d record added", __func__, client->ip_str, resource->name, resource->type);

    return;
}

/***********************************************************************************************************/

void
mdns_records_collect_record(const struct resource *r, void *data, struct sockaddr_storage *from)
{
    /* If report is not initialized, the cloud doesn't want the records to be reported.
     * (report_records is false). If not initialized, don't collect the record */
    if (!g_report.initialized) return;

    switch (r->type)
    {
        case QTYPE_A:
        case QTYPE_NS:
        case QTYPE_CNAME:
        case QTYPE_PTR:
        case QTYPE_TXT:
        case QTYPE_SRV:
        {
            mdns_records_store_record(r, data, from);
            break;
        }

        default:
            break;
    }

    return;
}

void
mdns_records_send_records(struct mdns_session *md_session)
{
    struct fsm_session      *session     = NULL;
    time_t                   now         = time(NULL);
    size_t                   num_clients = 0;
    bool                     ret         = false;
    observation_window_t     window;

    if (!md_session) return;

    /* Record the current observation window */
    window.started_at = md_session->records_report_ts;
    window.ended_at   = now;
    mdns_records_set_observation_window(&window, &g_report.obs_w);

    /* Get the client mac addresses from neigh_table */
    mdns_records_get_client_macs(&g_report.stored_clients);

    /* Mark the clients to be reported */
    mdns_records_stage_report(md_session);

    /* Dump the report */
    mdns_records_dump_report(&g_report);

    num_clients = mdns_records_get_num_clients(&g_report.staged_clients);
    if (num_clients > 0)
    {
        LOGT("%s: Sending MDNS records report", __func__);
        session = md_session->session;
        ret = mdns_records_send_report(&g_report, session);
        if (ret)
        {
            /* The records were successfully reported to the cloud.
               Marks them as 'reported' */
            mdns_records_mark_records_as_reported();
        }

        /* Clean the staging tree. In case of error in reporting the
           records to the cloud, the records will not be marked as 'reported'
           and will be staged again. So no harming in cleaning the staging
           tree always */
        mdns_records_clear_clients(&g_report.staged_clients);
    }

    mdns_records_clean_stale_records();

    /* Reset the report ts */
    md_session->records_report_ts = now;

    return;
}

bool
mdns_records_init(struct mdns_session *md_session)
{
    struct  fsm_session *session = NULL;
    node_info_t         *obs_p   = NULL;

    if (!md_session || !md_session->session) return false;
    session = md_session->session;

    /* Return if already initialized */
    if (g_report.initialized) return true;

    MEMZERO(g_report);

    /* Don't initialize the report is report_records is false */
    if (!md_session->report_records)
    {
        LOGT("%s: MDNS records reporting not enabled", __func__);
        g_report.initialized = false;
        return true;
    }

    /* Initialize the report */
    ds_tree_init(&g_report.stored_clients, (ds_key_cmp_t *)strcmp, mdns_client_t, dst_node);
    ds_tree_init(&g_report.staged_clients, (ds_key_cmp_t *)strcmp, mdns_client_t, dst_node);

    /* Get the node information (observation point) */
    obs_p = &g_report.node_info;

    if (!session->node_id)
    {
        LOGE("%s: session->node_id is NULL", __func__);
        return false;
    }
    (void)mdns_records_str_duplicate(session->node_id, &obs_p->node_id);

    if (!session->location_id)
    {
        LOGE("%s: session->location_id is NULL", __func__);
        return false;
    }
    (void)mdns_records_str_duplicate(session->location_id, &obs_p->location_id);

    g_report.initialized = true;

    return true;
}

void
mdns_records_exit(void)
{
    node_info_t *obs_p = NULL;

    /* Free the node info */
    obs_p = &g_report.node_info;
    if (obs_p->node_id)     FREE(obs_p->node_id);
    if (obs_p->location_id) FREE(obs_p->location_id);

    /* Free the stored and staged client trees */
    mdns_records_clear_clients(&g_report.stored_clients);
    mdns_records_clear_clients(&g_report.staged_clients);

    /* Reset the report */
    MEMZERO(g_report);
    g_report.initialized = false;

    return;
}
