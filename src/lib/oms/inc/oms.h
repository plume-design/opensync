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

#ifndef OMS_H_INCLUDED
#define OMS_H_INCLUDED

#include <ev.h>
#include <stdbool.h>

#include "ds_tree.h"
#include "schema.h"


/**
 * @brief config entry
 *
 * The config entry is used by the store manager to notify the recipient manager
 * of an an object to load.
 */
struct oms_config_entry
{
    char *object;             /* Object name */
    char *version;            /* Version */
    ds_tree_t *other_config;  /* Placeholder */
    ds_tree_node_t node;
};


struct oms_state_entry
{
    char *object;             /* Object name */
    char *version;            /* Version */
    char *state;              /* Mandatory state */
    bool fw_integrated;       /* FW integrated indicator */
    char *prev_state;         /* Previous state */
    ds_tree_t *other_state;   /* placeholder */
    ds_tree_node_t node;
};


typedef bool (*accept_id)(const char *);
typedef void (*ovsdb_config_cb)(struct oms_config_entry *, int);
typedef void (*ovsdb_state_cb)(struct oms_state_entry *, int);
typedef bool (*oms_report_status_cb)(struct oms_state_entry *);
typedef int (*oms_cmp_cb)(char *, char *, char *);


/**
 * oms global context
 */
struct oms_mgr
{
    ds_tree_t config;               /* DS tree of configs */
    ds_tree_t state;                /* DS tree of states */
    ds_tree_t *mqtt_headers;        /* DS tree of mqtt info */
    char *location_id;              /* platfrom's location id */
    char *node_id;                  /* platform's node id */
    size_t num_states;              /* Number of status nodes in the DS tree */
    size_t num_reports;             /* Number of status node to report */
    accept_id accept_id;            /* Filter object id */
    ovsdb_config_cb config_cb;      /* osdb config event callback */
    ovsdb_state_cb state_cb;        /* osdb state event callback */
    oms_report_status_cb report_cb; /* status report filter */
    bool initialized;               /* Initialization completion flag */
};


/**
 * oms initialization paramaters container
 */
struct oms_ovsdb_set
{
    bool monitor_config;            /* Request to monitor oms config table */
    bool monitor_state;             /* Request to monitor oms state table */
    bool monitor_awlan;             /* Request to monitor AWLAN_Node table */
    accept_id accept_id;            /* Filter object */
    ovsdb_config_cb config_cb;      /* ovsdb config event callback */
    ovsdb_state_cb state_cb;        /* ovsdb state event callback */
    oms_report_status_cb report_cb; /* status report filter */
};

void
oms_init_manager(void);

struct oms_mgr *
oms_get_mgr(void);

void
oms_ovsdb_init(struct oms_ovsdb_set *oms_set);


/**
 * @brief delete an oms config entry
 *
 * @param entry the config entry to delete
 */
void
oms_free_config_entry(struct oms_config_entry *entry);


/**
 * @brief free all stored config entries
 */
void
oms_delete_config_entries(void);

/**
 * @param process an ovsdb add config event
 *
 * @param config the ovsdb entry to process
 * Allocates resources for the entry and stores it.
 */
void
oms_ovsdb_add_config_entry(struct schema_OMS_Config *config);

/**
 * @param process an ovsdb delete config event
 *
 * @param config the ovsdb entry to process
 * Frees resources for the entry and deletes it.
 */
void
oms_ovsdb_del_config_entry(struct schema_OMS_Config *config);

/**
 * @brief add a config entry in the ovsdb object config table
 *
 * @param entry the entry to add
 */
int
oms_add_config_entry(struct oms_config_entry *entry);

/**
 * @brief delete a config entry in the ovsdb object config table
 *
 * @param entry the entry to delete
 */
int
oms_delete_config_entry(struct oms_config_entry *entry);

/**
 * @brief delete an oms state entry
 *
 * @param entry the state entry to delete
 */
void
oms_free_state_entry(struct oms_state_entry *entry);

/**
 * @brief free all stored config entries
 */
void
oms_delete_state_entries(void);

/**
 * @brief process an ovsdb add state event
 *
 * @param state the ovsdb entry to process
 * Allocates resources for the entry and stores it.
 */
void
oms_ovsdb_add_state_entry(struct schema_Object_Store_State *state);

/**
 * @brief add a state entry in the ovsdb object state table
 *
 * @param entry the entry to add
 */
int
oms_add_state_entry(struct oms_state_entry *entry);

/**
 * @brief update a state entry in the ovsdb object state table
 *
 * @param entry the entry to update
 */
int
oms_update_state_entry(struct oms_state_entry *entry);

/**
 * @brief add a state entry in the ovsdb object state table
 *
 * @param entry the entry to add
 */
int
oms_delete_state_entry(struct oms_state_entry *entry);


struct oms_config_entry *
oms_get_highest_version(char *object, char *version_cap, oms_cmp_cb cmp_cb);

#endif /* OMS_H_INCLUDED */
