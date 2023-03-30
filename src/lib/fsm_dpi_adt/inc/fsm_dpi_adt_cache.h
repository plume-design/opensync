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

#ifndef FSM_DPI_ADT_CACHE_H_INCLUDED
#define FSM_DPI_ADT_CACHE_H_INCLUDED

#include <time.h>
#include "os_types.h"
#include "ds_tree.h"

/* default cache expiry is 24 hrs */
#define ADT_CACHE_AGE_TIMER 86400

struct fsm_dpi_adt_value
{
    char *value;
    time_t insert_time;
    ds_tree_node_t value_node;
};

struct fsm_dpi_adt_key
{
    char *key;
    time_t insert_time;
    ds_tree_t values; // fsm_dpi_adt_value
    ds_tree_node_t key_node;
};

struct fsm_dpi_adt_device
{
    os_macaddr_t *mac;
    time_t insert_time;
    ds_tree_t keys; // fsm_dpi_adt_key
    ds_tree_node_t device_node;
};

struct fsm_dpi_adt_cache
{
    uint64_t counter;
    int age_time;
    ds_tree_t devices; // fsm_dpi_adt_device
};

/**
 * @brief Initializes the ADT cache.
 *
 * @param None
 * @return none
 */
void
fsm_dpi_adt_init_cache(void);

/**
 * @brief Adds entry to the ADT cache. Device, key
 * or value is added to the cache if they are
 * not found in the cache.
 *
 * @param mac Device MAC address
 * @param key ADT key value
 * @param value ADT value
 * @return true if entry was added to the cache successfully
 * else false
 */
bool
fsm_dpi_adt_add_to_cache(os_macaddr_t *mac, char *key, char *value);

/**
 * @brief Performs a lookup on ADT cache for the provided
 * device, key and value.
 *
 * @param mac Device MAC address
 * @param key ADT key value
 * @param value ADT value
 * @return true if entry is found else false
 */
bool
fsm_dpi_adt_cache_lookup(os_macaddr_t *mac, char *key, char *value);

/**
 * @brief Dumps the ADT cache entries.
 *
 * @param None
 * @return none
 */
void
fsm_adt_adt_dump_cache(void);

/**
 * @brief Clears all the cache entries
 *
 * @param None
 * @return None
 */
void
fsm_dpi_adt_clear_cache(void);

/**
 * @brief Removes expired entries from the cache
 *
 * @param None
 * @return None
 */
void
fsm_dpi_adt_remove_expired_entries(void);

#endif /* FSM_DPI_ADT_CACHE_H_INCLUDED */
