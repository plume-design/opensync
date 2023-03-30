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

#include "fsm_dpi_adt_cache_internal.h"
#include "fsm_dpi_adt_cache.h"
#include "fsm_dpi_adt.h"

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
fsm_dpi_adt_add_to_cache(os_macaddr_t *mac, char *key, char *value)
{
    struct fsm_dpi_adt_value *adt_value;
    struct fsm_dpi_adt_device *adt_dev;
    struct fsm_dpi_adt_key *adt_key;

    if (mac == NULL) return false;
    if (key == NULL) return false;
    if (value == NULL) return false;

    adt_dev = fsm_dpi_adt_cache_add_device(mac);
    if (adt_dev == NULL) return false;

    adt_key = fsm_dpi_adt_cache_add_key(adt_dev, key);
    if (adt_key == NULL) return false;

    adt_value = fsm_dpi_adt_cache_add_value(adt_key, value);
    if (adt_value == NULL) return false;

    LOGT("%s(): Added cache entry dev: " PRI_os_macaddr_lower_t
        " key: %s, value %s", __func__, FMT_os_macaddr_pt(mac), key, value);

    return true;
}

/**
 * @brief Clears all the cache entries
 *
 * @param None
 * @return None
 */
void
fsm_dpi_adt_clear_cache(void)
{
    struct fsm_dpi_adt_cache *adt_cache;
    ds_tree_t *tree;

    adt_cache = fsm_dpi_adt_get_cache_mgr();

    tree = &adt_cache->devices;
    fsm_dpi_adt_clear_dev_cache(tree);
}

/**
 * @brief Removes expired entries from the cache
 *
 * @param None
 * @return None
 */
void
fsm_dpi_adt_remove_expired_entries(void)
{
    struct fsm_dpi_adt_cache *adt_cache;

    adt_cache = fsm_dpi_adt_get_cache_mgr();
    LOGT("%s(): removing expired cache entries", __func__);
    fsm_dpi_adt_remove_expired_devices(adt_cache);
}

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
fsm_dpi_adt_cache_lookup(os_macaddr_t *mac, char *key, char *value)
{
    struct fsm_dpi_adt_value *adt_value;
    struct fsm_dpi_adt_device *adt_dev;
    struct fsm_dpi_adt_key *adt_key;

    LOGT("%s():checking cache for dev " PRI_os_macaddr_lower_t " key: %s, value %s ", __func__,
         FMT_os_macaddr_pt(mac), key, value);

    adt_dev = fsm_dpi_adt_dev_cache_lookup(mac);
    if (adt_dev == NULL) return false;

    adt_key = fsm_dpi_adt_key_cache_lookup(adt_dev, key);
    if (adt_key == NULL) return false;

    adt_value = fsm_dpi_adt_value_cache_lookup(adt_key, value);
    if (adt_value == NULL) return false;

    LOGT("%s(): dev: " PRI_os_macaddr_lower_t " key: %s, value %s entry found in cache", __func__,
         FMT_os_macaddr_pt(mac), key, value);
    return true;
}

/**
 * @brief Initializes the ADT cache.
 *
 * @param None
 * @return none
 */
void
fsm_dpi_adt_init_cache(void)
{
    struct fsm_dpi_adt_cache *adt_cache;

    adt_cache = fsm_dpi_adt_get_cache_mgr();
    fsm_dpi_adt_init_cache_entry(adt_cache);
    return;
}

/**
 * @brief Dumps the ADT cache entries.
 *
 * @param None
 * @return none
 */
void
fsm_adt_adt_dump_cache(void)
{
    struct fsm_dpi_adt_cache *adt_cache;
    struct fsm_dpi_adt_device *adt_dev;
    ds_tree_t *tree;

    if (!LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) return;

    adt_cache = fsm_dpi_adt_get_cache_mgr();

    tree = &adt_cache->devices;
    ds_tree_foreach(tree, adt_dev)
    {
        LOGT("--------------------------------------------------------------------------------------------------");
        LOGT("Printing cache entries for device " PRI_os_macaddr_lower_t" ", FMT_os_macaddr_pt(adt_dev->mac));
        fsm_adt_adt_dump_keys(adt_dev);
        LOGT("--------------------------------------------------------------------------------------------------");
    }
}
