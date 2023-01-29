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

#ifndef DATA_REPORT_TAGS_INTERNALS_H_INCLUDED
#define DATA_REPORT_TAGS_INTERNALS_H_INCLUDED

#include "os_types.h"
#include "ds_dlist.h"
#include "ds_tree.h"
#include "policy_tags.h"
#include "ovsdb_utils.h"

enum drt_mac_type
{
    DRT_UNDEFINED_MAC_TYPE = 0,
    DRT_MAC,
    DRT_TAG,
    DRT_GTAG,
};


enum drt_precedence
{
    DRT_UNDEFINED_PRECEDENCE = 0,
    DRT_INCLUDE,
    DRT_EXCLUDE,
};


enum feature_flags
{
    DRT_STRONG_INCLUDE = 1 << 0,
    DRT_STRONG_EXCLUDE = 1 << 1,
    DRT_WEAK_INCLUDE = 1 << 2,
    DRT_WEAK_EXCLUDE = 1 << 3,
    DRT_FEATURE_ENABLED = 1 << 4,
};


struct drt_features_list_entry
{
    char name[64];
    enum feature_flags flags;
    ds_dlist_node_t node;
};


struct drt_device_entry
{
    os_macaddr_t mac;
    ds_dlist_t features_list;
    struct str_set features_set;
    size_t nelems;
    ds_tree_node_t node;
};


struct drt_tag_drt_entry
{
    char drt_name[64];               /* data report tags name */
    enum drt_precedence operand;     /* Included or excluded for this drt */
    enum drt_precedence tag_op;      /* precedence for this drt */
    enum drt_mac_type type;          /* value type */
    ds_tree_node_t node;
};


struct drt_ovsdb_tag
{
    char val[64];                    /* tag name or mac address */
    char *ovsdb_tag_string;          /* tag name as passed by ovsdb */
    enum drt_mac_type type;          /* value type */
};


struct drt_tag_entry
{
    char tag_name[64];
    char *ovsdb_tag_string;          /* tag name as passed by ovsdb */
    ds_tree_t drt_entries;
    ds_tree_t *added;
    ds_tree_t *removed;
    enum drt_mac_type type;
    ds_tree_node_t node;
};


struct drt_cache_entry
{
    char drt_name[64];
    struct str_set *included_macs;
    struct str_set *excluded_macs;
    enum drt_precedence precedence;
    ds_tree_node_t node;
};


struct data_report_tags_mgr
{
    bool initialized;
    ds_tree_t drt_cache;
    ds_tree_t tags_to_monitor;
    ds_tree_t device_cache;
};


struct data_report_tags_mgr *
data_report_tags_get_mgr(void);

void
data_report_tags_init_manager(void);

void
drt_walk_caches(void);

#endif /* DATA_REPORT_TAGS_INTERNALS_H_INCLUDED */
