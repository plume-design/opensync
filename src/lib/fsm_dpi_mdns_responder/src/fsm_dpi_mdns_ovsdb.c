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
#include "memutil.h"


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
fsm_dpi_mdns_service_cmp(const void *a, const void *b)
{
    return strcmp(a, b);
}

void
fsm_dpi_mdns_ovsdb_init(void)
{
    LOGI("fsm_dpi_mdns_ovsdb: Initializing mDNS tables");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(Service_Announcement);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Service_Announcement, false);

    return;
}

void
fsm_dpi_mdns_ovsdb_exit(void)
{
    LOGI("%s: Unregister OVSDB events", __func__);

    /* Deregister monitor events */
    ovsdb_unregister_update_cb(table_Service_Announcement.monitor.mon_id);

    return;
}

ds_tree_t *
fsm_dpi_mdns_get_services(void)
{
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    return &mgr->ctxt->services;
}

static ds_tree_t *
fsm_dpi_mdns_set_service_txt(struct schema_Service_Announcement *conf)
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
fsm_dpi_mdns_walk_services_txt_records(struct fsm_dpi_mdns_service *service)
{
    ds_tree_t *pairs = service->txt;
    struct str_pair *pair;

    if (!pairs) return;

    pair = ds_tree_head(pairs);

    LOGT("fsm_dpi_mdns_ovsdb: TXT records:");
    while(pair != NULL)
    {
        LOGT("fsm_dpi_mdns_ovsdb: %s=%s", pair->key, pair->value);
        pair = ds_tree_next(pairs, pair);
    }
    return;
}

void
fsm_dpi_mdns_walk_services_tree(void)
{
    ds_tree_t *services = fsm_dpi_mdns_get_services();
    struct fsm_dpi_mdns_service *service;

    if (!services) return;
    service = ds_tree_head(services);

    LOGT("fsm_dpi_mdns_ovsdb: Walking services tree");

    while(service != NULL)
    {
        LOGT("fsm_dpi_mdns_ovsdb: name: %s, type: %s",
             service->name, service->type);
        fsm_dpi_mdns_walk_services_txt_records(service);
        service = ds_tree_next(services, service);
    }
    return;
}

void
fsm_dpi_mdns_free_service(struct fsm_dpi_mdns_service *service)
{
    ds_tree_t *services = fsm_dpi_mdns_get_services();
    if (!services || !service) return;

    ds_tree_remove(services, service);
    free_str_tree(service->txt);
    FREE(service->name);
    FREE(service->type);
    FREE(service);
    return;
}

void
fsm_dpi_mdns_free_services(void)
{
    ds_tree_t *services  = fsm_dpi_mdns_get_services();
    struct fsm_dpi_mdns_service *service;
    struct fsm_dpi_mdns_service *next;
    struct fsm_dpi_mdns_service *remove;

    if (!services) return;

    service  = ds_tree_head(services);
    while(service != NULL)
    {
        next = ds_tree_next(services, service);
        remove = service;
        fsm_dpi_mdns_free_service(remove);
        service = next;
    }
    return;
}


struct fsm_dpi_mdns_service
*fsm_dpi_mdns_alloc_service(struct schema_Service_Announcement *conf)
{
    struct fsm_dpi_mdns_service *service;

    service = CALLOC(1, sizeof(struct fsm_dpi_mdns_service));

    service->name = STRDUP(conf->name);
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
    service->txt = fsm_dpi_mdns_set_service_txt(conf);
    return service;

err_free_name:
    FREE(service->name);

err_free_service:
    FREE(service);
    return NULL;
}

bool
fsm_dpi_mdns_add_service(struct schema_Service_Announcement *conf)
{
    struct fsm_dpi_mdns_service *service;
    ds_tree_t *services = fsm_dpi_mdns_get_services();

    if (!services) return false;

    service = ds_tree_find(services, conf->name);

    if (service) return true;

    /* Allocate new service, insert it to services tree */
    service = fsm_dpi_mdns_alloc_service(conf);
    if (!service)
    {
        LOGE("mdns_ovsdb: Could not allocate service for name %s",
             conf->name);
        return false;
    }
    ds_tree_insert(services, service, service->name);

    //Update the daemon records.
    if (!fsm_dpi_mdns_update_record(service))
    {
        LOGE("mdns_ovsdb: Failed to add mdns service record.");
        return false;
    }
    fsm_dpi_mdns_walk_services_tree();
    return true;
}

void
fsm_dpi_mdns_delete_service(struct schema_Service_Announcement *conf)
{
    struct fsm_dpi_mdns_service *service;
    ds_tree_t *services = fsm_dpi_mdns_get_services();

    if (!services) return;

    service = ds_tree_find(services, conf->name);
    if(!service) return;

    // Remove the record.
    fsm_dpi_mdns_remove_record(service);
    fsm_dpi_mdns_free_service(service);
    fsm_dpi_mdns_walk_services_tree();
    return;
}

bool
fsm_dpi_mdns_modify_service(struct schema_Service_Announcement *conf)
{
    struct fsm_dpi_mdns_service *service;
    ds_tree_t *services  = fsm_dpi_mdns_get_services();

    if (!services) return false;

    service = ds_tree_find(services, conf->name);
    fsm_dpi_mdns_free_service(service);

    service = fsm_dpi_mdns_alloc_service(conf);
    if (!service)
    {
        LOGE("fsm_dpi_mdns_ovsdb: Could not allocate service for name %s",
             conf->name);
        return false;
    }

    ds_tree_insert(services, service, service->name);
    fsm_dpi_mdns_update_record(service);

    fsm_dpi_mdns_walk_services_tree();
    return true;
}

void
callback_Service_Announcement(ovsdb_update_monitor_t *mon,
                              struct schema_Service_Announcement *old_rec,
                              struct schema_Service_Announcement *conf)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) fsm_dpi_mdns_add_service(conf);
    if (mon->mon_type == OVSDB_UPDATE_DEL) fsm_dpi_mdns_delete_service(old_rec);
    if (mon->mon_type == OVSDB_UPDATE_MODIFY) fsm_dpi_mdns_modify_service(conf);
}
