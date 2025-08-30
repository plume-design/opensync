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

/**
 * @brief comparator function for comparing mac address
 *
 * @params: _a: first MAC address to compare
 * @params: _b: second MAC address to compare
 *
 * @return 0 if identical, positive or negative value if different
 *         (implying the "order" between the entries)
 */
static int
fsm_dpi_adt_mac_cmp(const void *_a, const void *_b)
{
    const os_macaddr_t *a = _a;
    const os_macaddr_t *b = _b;

    return memcmp(a->addr, b->addr, sizeof(a->addr));
}

/**
 * @brief Returns the number of entries present in the cache.
 *
 * @param None
 * @return entries present in the cache
 */
uint64_t
fsm_dpi_adt_get_cache_count(void)
{
    struct fsm_dpi_adt_cache *adt_cache;

    adt_cache = fsm_dpi_adt_get_cache_mgr();
    return adt_cache->counter;
}

/**
 * @brief Decrement cache entry count by one.
 *
 * @param None
 * @return None
 */
static void
fsm_dpi_adt_dec_cache_count(void)
{
    struct fsm_dpi_adt_cache *adt_cache;

    adt_cache = fsm_dpi_adt_get_cache_mgr();
    adt_cache->counter--;
}

/**
 * @brief Increment cache entry count by one.
 *
 * @param None
 * @return None
 */
static void
fsm_dpi_adt_inc_cache_count(void)
{
    struct fsm_dpi_adt_cache *adt_cache;

    adt_cache = fsm_dpi_adt_get_cache_mgr();
    adt_cache->counter++;
}

/**
 * @brief Frees memory allocated by value entry.
 *
 * @param adt_value pointer to adt value entry
 * @return None
 */
static void
fsm_dpi_adt_free_value_entry(struct fsm_dpi_adt_value *adt_value)
{
    FREE(adt_value->value);
    fsm_dpi_adt_dec_cache_count();
}

/**
 * @brief Removes all the value entries from the cache.
 *
 * @param tree pointer to adt value tree
 * @return None
 */
static void
fsm_dpi_adt_clear_values_cache(ds_tree_t *tree)
{
    struct fsm_dpi_adt_value *adt_value, *remove;
    adt_value = ds_tree_head(tree);
    while (adt_value != NULL)
    {
        remove = adt_value;
        adt_value = ds_tree_next(tree, adt_value);
        fsm_dpi_adt_free_value_entry(remove);
        ds_tree_remove(tree, remove);
        FREE(remove);
    }
}

/**
 * @brief Frees memory allocated by key entry.
 *
 * @param adt_key pointer to adt key entry
 * @return None
 */
static void
fsm_dpi_adt_free_key_entry(struct fsm_dpi_adt_key *adt_key)
{
    FREE(adt_key->key);
    fsm_dpi_adt_dec_cache_count();
    fsm_dpi_adt_clear_values_cache(&adt_key->values);
}

/**
 * @brief Removes all the keys entries from the cache.
 *
 * @param tree pointer to adt key's tree
 * @return None
 */
static void
fsm_dpi_adt_clear_keys_cache(ds_tree_t *tree)
{
    struct fsm_dpi_adt_key *adt_key, *remove;

    adt_key = ds_tree_head(tree);
    while (adt_key != NULL)
    {
        remove = adt_key;
        adt_key = ds_tree_next(tree, adt_key);
        fsm_dpi_adt_free_key_entry(remove);
        ds_tree_remove(tree, remove);
        FREE(remove);
    }
}

/**
 * @brief Frees memory allocated by device entry.
 *
 * @param adt_dev pointer to adt device entry
 * @return None
 */
static void
fsm_dpi_adt_free_dev_entry(struct fsm_dpi_adt_device *adt_dev)
{
    FREE(adt_dev->mac);
    fsm_dpi_adt_dec_cache_count();
    fsm_dpi_adt_clear_keys_cache(&adt_dev->keys);
}

/**
 * @brief Removes all the device entries from the cache.
 *
 * @param tree pointer to adt devices's tree
 * @return None
 */
void
fsm_dpi_adt_clear_dev_cache(ds_tree_t *tree)
{
    struct fsm_dpi_adt_device *adt_dev, *remove;

    adt_dev = ds_tree_head(tree);
    while (adt_dev != NULL)
    {
        remove = adt_dev;
        adt_dev = ds_tree_next(tree, adt_dev);
        fsm_dpi_adt_free_dev_entry(remove);
        ds_tree_remove(tree, remove);
        FREE(remove);
    }
}

/**
 * @brief Checks if the cache entry is expired.
 *
 * @param insert_time time the current cache entry
 * was added to the cache.
 * @return true is the cache entry is expired else false
 */
static bool
fsm_dpi_adt_is_entry_expired(time_t insert_time)
{
    struct fsm_dpi_adt_cache *adt_cache;
    time_t now;

    now = time(NULL);
    adt_cache = fsm_dpi_adt_get_cache_mgr();
    return ((now - insert_time) > adt_cache->age_time);
}

/**
 * @brief Removes all the expired value's entries
 *
 * @param adt_key pointer to adt_key
 * @return none
 */
void
fsm_dpi_adt_removed_expired_values(struct fsm_dpi_adt_key *adt_key)
{
    struct fsm_dpi_adt_value *adt_val, *cur_val;
    ds_tree_t *tree;
    bool expired;

    tree = &adt_key->values;
    adt_val = ds_tree_head(tree);
    while (adt_val != NULL)
    {
        cur_val = adt_val;
        adt_val = ds_tree_next(tree, adt_val);
        expired = fsm_dpi_adt_is_entry_expired(cur_val->insert_time);
        if (expired)
        {
            LOGT("%s(): removing cache entry for value %s ", __func__, cur_val->value);
            fsm_dpi_adt_free_value_entry(cur_val);
            ds_tree_remove(tree, cur_val);
            FREE(cur_val);
        }
    }
}

/**
 * @brief Removes all the expired key's entries
 *
 * @param adt_dev pointer to adt_dev
 * @return none
 */
void
fsm_dpi_adt_remove_expired_keys(struct fsm_dpi_adt_device *adt_dev)
{
    struct fsm_dpi_adt_key *adt_key, *cur_key;
    ds_tree_t *tree;
    bool expired;

    tree = &adt_dev->keys;
    adt_key = ds_tree_head(tree);
    while (adt_key != NULL)
    {
        cur_key = adt_key;
        adt_key = ds_tree_next(tree, adt_key);
        expired = fsm_dpi_adt_is_entry_expired(cur_key->insert_time);
        if (expired)
        {
            LOGT("%s(): removing cache entries for key %s ", __func__, cur_key->key);
            fsm_dpi_adt_free_key_entry(cur_key);
            ds_tree_remove(tree, cur_key);
            FREE(cur_key);
            continue;
        }
        /* check and remove expired values */
        fsm_dpi_adt_removed_expired_values(cur_key);
    }
}

/**
 * @brief Removes all the expired device's entries
 *
 * @param adt_cache pointer to adt_cache
 * @return none
 */
void
fsm_dpi_adt_remove_expired_devices(struct fsm_dpi_adt_cache *adt_cache)
{
    struct fsm_dpi_adt_device *adt_dev, *cur_dev;
    ds_tree_t *tree;
    bool expired;

    tree = &adt_cache->devices;
    adt_dev = ds_tree_head(tree);
    while (adt_dev != NULL)
    {
        cur_dev = adt_dev;
        adt_dev = ds_tree_next(tree, adt_dev);
        expired = fsm_dpi_adt_is_entry_expired(cur_dev->insert_time);
        if (expired)
        {
            LOGT("%s(): removing cache entries for device " PRI_os_macaddr_lower_t, __func__,
                 FMT_os_macaddr_pt(cur_dev->mac));
            fsm_dpi_adt_free_dev_entry(cur_dev);
            ds_tree_remove(tree, cur_dev);
            FREE(cur_dev);
            continue;
        }
        /* check and remove expired keys */
        fsm_dpi_adt_remove_expired_keys(cur_dev);
    }
}

/**
 * @brief Prints the value's tree entry in the cache
 *
 * @param adt_key pointer to adt key
 * @return none
 */
static void
fsm_adt_adt_dump_values(struct fsm_dpi_adt_key *adt_key)
{
    struct fsm_dpi_adt_value *adt_value;
    ds_tree_t *tree;

    tree = &adt_key->values;
    ds_tree_foreach(tree, adt_value) LOGT("\t %s", adt_value->value);
}

/**
 * @brief Prints the key's tree entry in the cache
 *
 * @param adt_device pointer to adt device
 * @return none
 */
void
fsm_adt_adt_dump_keys(struct fsm_dpi_adt_device *adt_device)
{
    struct fsm_dpi_adt_key *adt_key;
    ds_tree_t *tree;

    tree = &adt_device->keys;
    ds_tree_foreach(tree, adt_key)
    {
        LOGT("%s", adt_key->key);
        fsm_adt_adt_dump_values(adt_key);
    }
}

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
fsm_dpi_adt_cache_add_value(struct fsm_dpi_adt_key *adt_key, const char *value)
{
    struct fsm_dpi_adt_value *adt_value;
    ds_tree_t *tree;

    tree = &adt_key->values;
    adt_value = ds_tree_find(tree, value);
    if (adt_value != NULL)
    {
        /* device entry already present, just update the time stamp */
        adt_value->insert_time = time(NULL);
        return adt_value;
    }

    adt_value = CALLOC(1, sizeof(*adt_value));
    adt_value->value = STRDUP(value);
    adt_value->insert_time = time(NULL);
    ds_tree_insert(tree, adt_value, adt_value->value);
    fsm_dpi_adt_inc_cache_count();
    return adt_value;
}

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
fsm_dpi_adt_cache_add_key(struct fsm_dpi_adt_device *adt_dev, char *key)
{
    struct fsm_dpi_adt_key *adt_key;
    ds_tree_t *tree;

    tree = &adt_dev->keys;
    adt_key = ds_tree_find(tree, key);
    if (adt_key != NULL)
    {
        /* device entry already present, just update the time stamp */
        adt_key->insert_time = time(NULL);
        return adt_key;
    }

    adt_key = CALLOC(1, sizeof(*adt_key));
    adt_key->key = STRDUP(key);
    adt_key->insert_time = time(NULL);
    ds_tree_init(&adt_key->values, ds_str_cmp, struct fsm_dpi_adt_value, value_node);

    ds_tree_insert(tree, adt_key, adt_key->key);
    fsm_dpi_adt_inc_cache_count();
    return adt_key;
}

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
fsm_dpi_adt_cache_add_device(os_macaddr_t *mac)
{
    struct fsm_dpi_adt_cache *adt_cache;
    struct fsm_dpi_adt_device *adt_dev;

    adt_dev = fsm_dpi_adt_dev_cache_lookup(mac);
    if (adt_dev != NULL)
    {
        /* device entry already present, just update the time stamp */
        adt_dev->insert_time = time(NULL);
        return adt_dev;
    }

    adt_dev = CALLOC(1, sizeof(*adt_dev));
    adt_dev->mac = CALLOC(1, sizeof(*adt_dev->mac));

    memcpy(adt_dev->mac, mac, sizeof(*adt_dev->mac));
    adt_dev->insert_time = time(NULL);

    ds_tree_init(&adt_dev->keys, ds_str_cmp, struct fsm_dpi_adt_key, key_node);

    adt_cache = fsm_dpi_adt_get_cache_mgr();
    ds_tree_insert(&adt_cache->devices, adt_dev, adt_dev->mac);
    fsm_dpi_adt_inc_cache_count();

    return adt_dev;
}

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
fsm_dpi_adt_value_cache_lookup(struct fsm_dpi_adt_key *adt_key, const char *value)
{
    return ds_tree_find(&adt_key->values, value);
}

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
fsm_dpi_adt_key_cache_lookup(struct fsm_dpi_adt_device *adt_dev, const char *key)
{
    return ds_tree_find(&adt_dev->keys, key);
}

/**
 * @brief Performs a lookup on ADT cache for the provided
 * device.
 *
 * @param mac device mac address to look for
 * @return fsm_dpi_adt_device * pointer to adt_device if found else
 * returns NULL.
 */
struct fsm_dpi_adt_device *
fsm_dpi_adt_dev_cache_lookup(os_macaddr_t *mac)
{
    struct fsm_dpi_adt_cache *adt_cache;

    adt_cache = fsm_dpi_adt_get_cache_mgr();
    return ds_tree_find(&adt_cache->devices, mac);
}

/**
 * @brief Initializes ADT cache values
 *
 * @param adt_cache pointer to adt_cache
 * @return None
 */
void
fsm_dpi_adt_init_cache_entry(struct fsm_dpi_adt_cache *adt_cache)
{
    if (adt_cache == NULL) return;

    adt_cache->age_time = ADT_CACHE_AGE_TIMER;
    ds_tree_init(&adt_cache->devices, fsm_dpi_adt_mac_cmp, struct fsm_dpi_adt_device, device_node);
}
