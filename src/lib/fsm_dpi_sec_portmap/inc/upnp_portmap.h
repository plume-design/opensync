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

#ifndef UPNP_PORTMAP_H_INCLUDED
#define UPNP_PORTMAP_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "ds_tree.h"
#include "fsm.h"
#include "fsm_dpi_sec_portmap.h"
#include "os_types.h"

enum upnp_protocol
{
    UPNP_MAPPING_PROTOCOL_UNSPECIFIED = 0,
    UPNP_MAPPING_PROTOCOL_TCP = 1,
    UPNP_MAPPING_PROTOCOL_UDP = 2,
};

enum upnp_capture_source
{
    UPNP_SOURCE_CAPTURE_SOURCE_UNSPECIFIED = 0,
    UPNP_SOURCE_IGD_POLL = 1,
    UPNP_SOURCE_PKT_INSPECTION_ADD = 2,
    UPNP_SOURCE_PKT_INSPECTION_DEL = 3,
    UPNP_SOURCE_OVSDB_STATIC = 4,
};

struct mapped_port_t
{
    os_macaddr_t device_id;
    struct sockaddr_storage *intClient;
    char *desc;
    int64_t captured_at_ms;
    unsigned int duration;            /* maximum is one week in seconds */
    unsigned short intPort;
    unsigned short extPort;
    enum upnp_protocol protocol;
    enum upnp_capture_source source;  /* used to indicate data provenance */
    bool enabled;

    ds_tree_node_t node;
};

struct ovsdb_portmap_cache_t
{
    ds_tree_t *store;  /* This is a container of struct mapped_port_t */
    uint8_t refcount;
    bool initialized;
};

void upnp_portmap_init(struct fsm_session *session);
void upnp_portmap_exit(struct fsm_session *session);
void upnp_portmap_update(struct fsm_session *session);

void upnp_portmap_mapped_init(struct fsm_dpi_sec_portmap_session *u_session);
void upnp_portmap_static_init(struct fsm_dpi_sec_portmap_session *u_session);

void upnp_portmap_mapped_exit(struct fsm_dpi_sec_portmap_session *u_session);
void upnp_portmap_static_exit(struct fsm_dpi_sec_portmap_session *u_session);

bool upnp_portmap_fetch_all(struct fsm_session *session);
bool upnp_portmap_fetch_mapped(struct fsm_session *session);
bool upnp_portmap_fetch_static(struct fsm_session *session);
int upnp_portmap_fetch_mapped_from_file(ds_tree_t *store, char *upnp_leases);

struct mapped_port_t *upnp_portmap_create_record(
        enum upnp_capture_source source,
        char *extPort, char *intClient, char *intPort, char *protocol,
        char *desc, char *enabled, char *rHost, char *duration);
int upnp_portmap_compare_record(const void *a, const void *b);
void upnp_portmap_delete_record(struct mapped_port_t *r);

ds_tree_t *upnp_portmap_create_snapshot(void);
bool upnp_portmap_compare_snapshot(ds_tree_t *a, ds_tree_t *b);
void upnp_portmap_delete_snapshot(ds_tree_t *snapshot);

int upnp_portmap_send_report(struct fsm_session *session);

void upnp_portmap_dump_snapshot(ds_tree_t *tree);
void upnp_portmap_dump_record(struct mapped_port_t *rec);

struct ovsdb_portmap_cache_t *get_portmap_from_ovsdb(void);

#endif /* UPNP_PORTMAP_H_INCLUDED */
