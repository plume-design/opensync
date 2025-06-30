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

#ifndef TEST_GATEKEEPER_CACHE_H
#define TEST_GATEKEEPER_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include <ev.h>

#include "ds_dlist.h"

extern const char *test_name;

#ifndef IP_STR_LEN
#define IP_STR_LEN          INET6_ADDRSTRLEN
#endif /* IP_STR_LEN */

extern size_t OVER_MAX_CACHE_ENTRIES;


struct test_timers
{
    ev_timer timeout_watcher_add;                     /* Add entries */
    ev_timer timeout_watcher_validate_add;            /* Validate added entries */
    ev_timer timeout_watcher_delete;                  /* Delete entries */
    ev_timer timeout_watcher_validate_delete;         /* Validate deleted entries */
    ev_timer timeout_watcher_update;                  /* Validate added entries */
    ev_timer timeout_watcher_validate_update;         /* Validate added entries */
    ev_timer timeout_watcher_update_add;              /* Update entries */
    ev_timer timeout_watcher_validate_update_add;     /* Validate updated entries */
    ev_timer timeout_watcher_update_delete;           /* Update entries */
    ev_timer timeout_watcher_validate_update_delete;  /* Validate updated entries */
};


struct gkc_test_mgr
{
    struct ev_loop *loop;
    ev_timer timeout_watcher;
    bool has_ovsdb;
    struct test_timers gkc_test_timers;
    double g_timeout;
    ds_dlist_t cleanup;
    struct fsm_session session;
    struct fsm_policy_client client;
};

struct gkc_ut_cleanup
{
    char *table;
    char *field;
    char *id;
};

typedef void (*cleanup_callback_t)(void);

struct gkc_tests_cleanup_entry
{
    cleanup_callback_t callback;
    ds_dlist_node_t node;
};


struct sample_attribute_entries
{
    char mac_str[18];
    int attribute_type;   /* request type */
    char attr_name[256];  /* attribute name */
    uint64_t cache_ttl;   /* TTL value that should be set */
    uint8_t action;       /* action req when adding will be set when lookup is
                            performed */
};

struct sample_flow_entries
{
    char mac_str[18];
    char src_ip_addr[IP_STR_LEN];     /* src ip in Network byte order */
    char dst_ip_addr[IP_STR_LEN];     /* dst ip in Network byte order */
    uint8_t ip_version;       /* ipv4 (4), ipv6 (6) */
    uint16_t src_port;        /* source port value */
    uint16_t dst_port;        /* dst port value */
    uint8_t protocol;         /* protocol value  */
    uint8_t direction;        /* used to check inbound/outbound cache */
    uint64_t cache_ttl;       /* TTL value that should be set */
    uint8_t action;           /* action req when adding will be set when lookup is
                                 performed */
    uint64_t hit_counter;     /* will be updated when lookup is performed */
};

extern struct sample_attribute_entries *test_attr_entries;
extern struct sample_flow_entries *test_flow_entries;

extern struct gk_attr_cache_interface *entry1, *entry2, *entry3, *entry4;
extern struct gk_attr_cache_interface *entry5, *entry6, *entry7, *entry8;
extern struct gk_attr_cache_interface *entry9, *entry10, *entry11, *entry12;
extern struct gkc_ip_flow_interface *flow_entry1, *flow_entry2, *flow_entry3, *flow_entry4;
extern struct gkc_ip_flow_interface *flow_entry5, *flow_entry6, *flow_entry7, *flow_entry8;

void free_flow_interface(struct gkc_ip_flow_interface *entry);

void run_gk_cache(void);
void run_gk_cache_flush(void);
void run_gk_cache_cmp(void);
void run_gk_cache_rework(void);
void run_gk_cache_ovsdb_app(void);
struct gkc_test_mgr *gkc_get_test_mgr(void);
void gkc_tests_register_cleanup(cleanup_callback_t cleanup);

#endif /* TEST_GATEKEEPER_CACHE_H */
