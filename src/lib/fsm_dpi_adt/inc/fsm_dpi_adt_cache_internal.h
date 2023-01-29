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


#ifndef FSM_DPI_ADT_CACHE_INTERNAL_H_INCLUDED
#define FSM_DPI_ADT_CACHE_INTERNAL_H_INCLUDED

#include "fsm_dpi_adt_cache.h"
#include "fsm_dpi_adt.h"

/**
 * @brief Removes all the expired value's entries
 *
 * @param adt_key pointer to adt_key
 * @return none
 */
void
fsm_dpi_adt_removed_expired_values(struct fsm_dpi_adt_key *adt_key);

/**
 * @brief Removes all the expired key's entries
 *
 * @param adt_dev pointer to adt_dev
 * @return none
 */
void
fsm_dpi_adt_remove_expired_keys(struct fsm_dpi_adt_device *adt_dev);

/**
 * @brief Removes all the expired device's entries
 *
 * @param adt_cache pointer to adt_cache
 * @return none
 */
void
fsm_dpi_adt_remove_expired_devices(struct fsm_dpi_adt_cache *adt_cache);

/**
 * @brief Removes all the device entries from the cache.
 *
 * @param tree pointer to adt devices's tree
 * @return None
 */
void
fsm_dpi_adt_clear_dev_cache(ds_tree_t *tree);

/**
 * @brief Prints the key's tree entry in the cache
 *
 * @param adt_device pointer to adt device
 * @return none
 */
void
fsm_adt_adt_dump_keys(struct fsm_dpi_adt_device *adt_device);

/**
 * @brief Returns the number of entries present in the cache.
 *
 * @param None
 * @return entries present in the cache
 */
uint64_t
fsm_dpi_adt_get_cache_count(void);

/**
 * @brief Adds a value entry to the cache.
 * It first checks if the entry is present.  If already
 * present, then the entry is returned.  If not present,
 * a new value entry is created and added to the cache.
 *
 * @param adt_key pointer to adt key, this value belongs to.
 * @param value pointer to adt value that is to be added.
 * @return fsm_dpi_adt_value pointer to value entry.
 */
struct fsm_dpi_adt_value *
fsm_dpi_adt_cache_add_value(struct fsm_dpi_adt_key *adt_key, const char *value);

/**
 * @brief Adds a key entry to the cache.
 * It first checks if the entry is present.  If already
 * present, then the entry is returned.  If not present,
 * a new key entry is created and added to the cache.
 *
 * @param adt_dev pointer to adt device, this key belongs to.
 * @param key pointer to adt key that is to be added.
 * @return fsm_dpi_adt_key pointer to key entry.
 */
struct fsm_dpi_adt_key *
fsm_dpi_adt_cache_add_key(struct fsm_dpi_adt_device *adt_dev, char *key);

/**
 * @brief Adds a device entry to the cache.
 * It first checks if the entry is present.  If already
 * present, then the entry is returned.  If not present,
 * a new device entry is created and added to the cache.
 *
 * @param mac pointer to adt device mac that is to be added.
 * @return fsm_dpi_adt_device pointer to device entry.
 */
struct fsm_dpi_adt_device *
fsm_dpi_adt_cache_add_device(os_macaddr_t *mac);

/**
 * @brief Performs a lookup on ADT cache for the provided
 * device.
 *
 * @param mac device mac address to look for
 * @return fsm_dpi_adt_device * pointer to adt_device if found else
 * returns NULL.
 */
struct fsm_dpi_adt_device *
fsm_dpi_adt_dev_cache_lookup(os_macaddr_t *mac);

/**
 * @brief Performs a lookup on ADT cache for the provided
 * key.
 *
 * @param key key to look for
 * @param adt_dev ADT device this key belongs to
 * @return fsm_dpi_adt_key * pointer to adt_key if found else
 * returns NULL.
 */
struct fsm_dpi_adt_key *
fsm_dpi_adt_key_cache_lookup(struct fsm_dpi_adt_device *adt_dev, const char *key);

/**
 * @brief Performs a lookup on ADT cache for the provided
 * value.
 *
 * @param value value to look for
 * @param adt_key ADT key this value belongs to
 * @return fsm_dpi_adt_value * pointer to adt_value if found else
 * returns NULL.
 */
struct fsm_dpi_adt_value *
fsm_dpi_adt_value_cache_lookup(struct fsm_dpi_adt_key *adt_key, const char *value);

/**
 * @brief Initializes ADT cache values
 *
 * @param adt_cache pointer to adt_cache
 * @return None
 */
void
fsm_dpi_adt_init_cache_entry(struct fsm_dpi_adt_cache *adt_cache);

#endif /* FSM_DPI_ADT_CACHE_INTERNAL_H_INCLUDED */
