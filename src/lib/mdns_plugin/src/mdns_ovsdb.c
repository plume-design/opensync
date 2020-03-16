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

#include "target.h"
#include "target_common.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"

#include "mdns_plugin.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB

ovsdb_table_t table_Service_Announcement;

/**
 * @brief compare sessions
 *
 * @param a service pointer
 * @param b service pointer
 * @return 0 if services matches
 */
int
mdnsd_service_cmp(void *a, void *b)
{
    return strcmp(a, b);
}

void
mdns_ovsdb_init(void)
{
    LOGI("mdnsd_ovsdb: Initializing mDNS tables");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(Service_Announcement);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Service_Announcement, false);

    return;
}

ds_tree_t *
mdnsd_get_services(void)
{
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    return &mgr->ctxt->services;
}

static ds_tree_t *
mdnsd_set_service_txt(struct schema_Service_Announcement *conf)
{
    ds_tree_t *txt = NULL;

    if (!conf) return NULL;
    txt = schema2tree(sizeof(conf->txt_keys[0]),
                               sizeof(conf->txt[0]),
                               conf->txt_len,
                               conf->txt_keys,
                               conf->txt);
    if (!txt) return NULL;
    return txt;
}


void
mdnsd_walk_services_txt_records(struct mdnsd_service *service)
{
    ds_tree_t *pairs = service->txt;
    struct str_pair *pair;

    if (!pairs) return;

    pair = ds_tree_head(pairs);

    LOGT("mdnsd_ovsdb: TXT records:");
    while(pair != NULL)
    {
        LOGT("mdnsd_ovsdb: %s=%s", pair->key, pair->value);
        pair = ds_tree_next(pairs, pair);
    }
    return;
}

void
mdnsd_walk_services_tree(void)
{
    ds_tree_t *services = mdnsd_get_services();
    struct mdnsd_service *service;

    if (!services) return;
    service = ds_tree_head(services);

    LOGT("mdnsd_ovsdb: Walking services tree");

    while(service != NULL)
    {
        LOGT("mdnsd_ovsdb: name: %s, type: %s",
             service->name, service->type);
        mdnsd_walk_services_txt_records(service);
        service = ds_tree_next(services, service);
    }
    return;
}

void
mdnsd_free_service(struct mdnsd_service *service)
{
    ds_tree_t *services = mdnsd_get_services();
    if (!services || !service) return;

    ds_tree_remove(services, service);
    free_str_tree(service->txt);
    free(service->name);
    free(service->type);
    free(service);
    return;
}

void
mdnsd_free_services(void)
{
    ds_tree_t *services  = mdnsd_get_services();
    struct mdnsd_service *service;
    struct mdnsd_service *next;
    struct mdnsd_service *remove;

    if (!services) return;

    service  = ds_tree_head(services);
    while(service != NULL)
    {
        next = ds_tree_next(services, service);
        remove = service;
        mdnsd_free_service(remove);
        service = next;
    }
    return;
}


struct mdnsd_service
*mdnsd_alloc_service(struct schema_Service_Announcement *conf)
{
    struct mdnsd_service *service;

    service = calloc(1, sizeof(struct mdnsd_service));
    if (!service) return NULL;

    service->name = strdup(conf->name);
    if (!service->name) goto err_free_service;

    if (conf->protocol_present)
    {
        service->type = strdup(conf->protocol);
    }
    else
    {
        // Default is http service.
        service->type = strdup("_http._tcp");
    }

    if (!service->type) goto err_free_name;

    service->port = conf->port;
    service->txt = mdnsd_set_service_txt(conf);
    return service;

err_free_name:
    free(service->name);

err_free_service:
    free(service);
    return NULL;
}

bool
mdnsd_add_service(struct schema_Service_Announcement *conf)
{
    struct mdnsd_service *service;
    ds_tree_t *services = mdnsd_get_services();

    if (!services) return false;

    service = ds_tree_find(services, conf->name);

    if (service) return true;

    /* Allocate new service, insert it to services tree */
    service = mdnsd_alloc_service(conf);
    if (!service)
    {
        LOGE("mdns_ovsdb: Could not allocate service for name %s",
             conf->name);
        return false;
    }
    ds_tree_insert(services, service, service->name);

    //Update the daemon records.
    if (!mdnsd_update_record(service))
    {
        LOGE("mdns_ovsdb: Failed to add mdns service record.");
        return false;
    }
    mdnsd_walk_services_tree();
    return true;
}

void
mdnsd_delete_service(struct schema_Service_Announcement *conf)
{
    struct mdnsd_service *service;
    ds_tree_t *services = mdnsd_get_services();

    if (!services) return;

    service = ds_tree_find(services, conf->name);
    if(!service) return;

    // Remove the record.
    mdnsd_remove_record(service);
    mdnsd_free_service(service);
    mdnsd_walk_services_tree();
    return;
}

bool
mdnsd_modify_service(struct schema_Service_Announcement *conf)
{
    struct mdnsd_service *service;
    ds_tree_t *services  = mdnsd_get_services();

    if (!services) return false;

    service = ds_tree_find(services, conf->name);
    mdnsd_free_service(service);

    service = mdnsd_alloc_service(conf);
    if (!service)
    {
        LOGE("mdnsd_ovsdb: Could not allocate service for name %s",
             conf->name);
        return false;
    }

    ds_tree_insert(services, service, service->name);
    mdnsd_update_record(service);

    mdnsd_walk_services_tree();
    return true;
}

void
callback_Service_Announcement(ovsdb_update_monitor_t *mon,
                              struct schema_Service_Announcement *old_rec,
                              struct schema_Service_Announcement *conf)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) mdnsd_add_service(conf);
    if (mon->mon_type == OVSDB_UPDATE_DEL) mdnsd_delete_service(old_rec);
    if (mon->mon_type == OVSDB_UPDATE_MODIFY) mdnsd_modify_service(conf);
}
