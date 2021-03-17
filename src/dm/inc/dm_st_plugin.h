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

#ifndef DM_ST_PLUGIN_H_INCLUDED
#define DM_ST_PLUGIN_H_INCLUDED

#include <stdbool.h>

#include "ds_tree.h"
#include "schema.h"

/*
 * ===========================================================================
 *  DM speedtest plug-in API
 * ===========================================================================
 */

struct dm_st_plugin;
struct dm_st_plugin_iter;
typedef struct dm_st_plugin dm_st_plugin_t;
typedef struct dm_st_plugin_iter dm_st_plugin_iter_t;
typedef bool dm_st_run_fn_t(struct schema_Wifi_Speedtest_Config *st_config);

/**
 * speedtest plugin
 *
 */
struct dm_st_plugin
{
    const char* const       st_name;        /**< speed test type name */
    dm_st_run_fn_t* const   st_run;         /**< run function */
    ds_tree_node_t          _st_node;      /* Internal: r/b tree node structure */
};

/**
 * speedtest plug-in iterator -- function used for traversing the registered plug-ins
 * list. Plug-ins are returned in a sorted order from the highest to the lowest
 * priority
 */
struct dm_st_plugin_iter
{
    ds_tree_iter_t  _st_iter;                /**< Tree iterator */
};

/**
 * Register a speedtest plug-in
 *
 * @param[in]   p  Pointer to a speedtest plugin structure
 */
void dm_st_plugin_register(struct dm_st_plugin *p);

/**
 * Unregister a speedtest plug-in
 *
 * @param[in]   p  Pointer to a speedtest plugin structure
 */
void dm_st_plugin_unregister(struct dm_st_plugin *p);

/**
 * Reset current plug-in iterator and return the head of the list (entry with
 * lowest priority).
 *
 * @return
 * Return a pointer to a plug-in structure (struct dm_st_plugin) or NULL if no
 * plug-ins are registered.
 */
struct dm_st_plugin* dm_st_plugin_first(dm_st_plugin_iter_t *iter);

/**
 * After dm_st_plugin_first() is called, return the next element in descending
 * priority * order
 */
struct dm_st_plugin* dm_st_plugin_next(dm_st_plugin_iter_t *iter);

/**
 * Find a speedtest plug-in by name
 *
 * @param[in]   name speedtest plug-in name
 *
 * @return
 * This function returns a speedtest plug-in structure on success or a NULL pointer
 * if the plug-in couldn't be found
 */
struct dm_st_plugin* dm_st_plugin_find(const char *name);


#endif /* DM_ST_PLUGIN_H_INCLUDED */
