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

#ifndef NETWORK_ZONE_INTERNALS_H_INCLUDED
#define NETWORK_ZONE_INTERNALS_H_INCLUDED

#include "os_types.h"
#include "ds_dlist.h"
#include "ds_tree.h"
#include "policy_tags.h"

enum nz_mac_type
{
    NZ_MAC = 0,
    NZ_TAG,
    NZ_GTAG,
};


struct network_zone_mac
{
    char val[64];            /* tag name or mac address */
    char *ovsdb_tag_string;  /* tag name as passed by ovsdb */
    char nz_name[64];        /* network zone name */
    int priority;            /* network zone priority */
    enum nz_mac_type type;   /* value type */
};

struct nz_cache_entry
{
    char *name;
    int priority;
    struct str_set *device_tags;
    ds_tree_node_t node;
};


struct nz_device_zone
{
    char name[64];
    int priority;
    ds_dlist_node_t node;
};


struct nz_device_entry
{
    os_macaddr_t mac;
    ds_dlist_t zones;
    ds_tree_node_t node;
};


struct nz_tag_entry
{
    char name[64];
    char zone_name[64];
    int type;
    ds_tree_node_t node;
};


struct network_zone_mgr
{
    bool initialized;
    ds_tree_t nz_cache;           /* tree of nz_cache_entry structures */
    ds_tree_t device_cache;       /* tree of nz_device_entry structures */
    ds_tree_t tags_to_monitor;    /* tree of nz_tag_entry structures */
};


struct network_zone_mgr *
network_zone_get_mgr(void);


/**
 * @brief determine the type and value of network zone mac element
 *
 * @param nz_elem the ovsdb mac element
 * @param mac the structure to be filled
 */
void
network_zone_get_mac(struct nz_cache_entry *nz,
                     char *nz_elem, struct network_zone_mac *mac);

bool
network_zone_add_tag(struct network_zone_mac *tag);

bool
network_zone_add_mac(struct network_zone_mac *mac);

void
network_zone_delete_mac(struct network_zone_mac *mac);

void
network_zone_delete_tag(struct network_zone_mac *tag);


void
network_zone_init_manager(void);

#endif /* NETWORK_ZONE_INTERNALS_H_INCLUDED */
