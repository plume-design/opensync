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

#ifndef TEST_DATA_REPORT_TAGS_H_INCLUDED
#define TEST_DATA_REPORT_TAGS_H_INCLUDED

#include <ev.h>

struct test_timers
{
    ev_timer timeout_watcher_add;                     /* Add entries */
    ev_timer timeout_watcher_validate_add;            /* Validate added entries */
    ev_timer timeout_watcher_delete;                  /* Delete entries */
    ev_timer timeout_watcher_validate_delete;         /* Validate added entries */
    ev_timer timeout_watcher_update;                  /* Validate added entries */
    ev_timer timeout_watcher_validate_update;         /* Validate added entries */
    ev_timer timeout_watcher_update_add;              /* Update entries */
    ev_timer timeout_watcher_validate_update_add;     /* Validate updated entries */
    ev_timer timeout_watcher_update_delete;           /* Update entries */
    ev_timer timeout_watcher_validate_update_delete;  /* Validate updated entries */
};


struct drt_test_mgr
{
    struct ev_loop *loop;
    ev_timer timeout_watcher;
    bool has_ovsdb;
    struct test_timers drt_test_timers;
    double g_timeout;
    ds_dlist_t cleanup;
};


struct drt_ut_validation
{
    char *feature;
    char *device;
    bool to_be_found;
};


struct drt_ut_cleanup
{
    char *table;
    char *field;
    char *id;
};

typedef void (*cleanup_callback_t)(void);

struct drt_tests_cleanup_entry
{
    cleanup_callback_t callback;
    ds_dlist_node_t node;
};


struct drt_test_mgr *
drt_get_test_mgr(void);

void
drt_ovsdb_monitor_tags(void);

void
run_data_report_tags_ovsdb(void);

void
run_data_report_tags_single_add(void);

void
run_data_report_tags_update_drts(void);

void
run_data_report_tags_update_tags(void);

void
drt_tests_register_cleanup(cleanup_callback_t cleanup);

void
drt_ut_validate_devices_features(struct drt_ut_validation to_validate[], size_t nelems);

#endif /* TEST_DATA_REPORT_TAGS_H_INCLUDED */
