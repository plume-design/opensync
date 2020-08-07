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

#ifndef OSP_PS_H_INCLUDED
#define OSP_PS_H_INCLUDED

#include <sys/types.h>
#include <stdbool.h>


/// @file
/// @brief Persistent Storage API
///
/// @addtogroup OSP
/// @{


// ===========================================================================
//  Persistent Storage API
// ===========================================================================

/// @defgroup OSP_PS  Persistent Storage API
/// OpenSync Persistent Storage API
/// @{

/**
 * OSP Persistent Storage handle type
 * 
 * This is an opaque type. The actual structure implementation is hidden and is
 * platform dependent. A new instance of the object can be obtained by calling
 * @ref osp_ps_open() and must be destroyed using @ref osp_ps_close().
 */
typedef struct osp_ps osp_ps_t;

/**
 * Flags for @ref osp_ps_open()
 */
#define OSP_PS_READ     (1 << 0)        /**< Read mode */
#define OSP_PS_WRITE    (1 << 1)        /**< Write mode */
#define OSP_PS_PRESERVE (1 << 2)        /**< Preserve store across upgrades */

/** Read-write access */
#define OSP_PS_RDWR     (OSP_PS_READ | OSP_PS_WRITE)

/**
 * Open store @p store
 *
 * @param[in]   store   Store name
 * @param[in]   flags   Read/write mode -- this may determine the type
 *                      of lock used
 *
 * @return
 * Return a valid handle to a store
 *
 * @note
 * To enable concurrent access from multiple processes, the store
 * may be protected by means of global locks. This means that the
 * time between a @ref osp_ps_open() and @ref osp_ps_close() must
 * be kept at a minimum.
 */
osp_ps_t* osp_ps_open(
        const char *store,
        int flags);

/**
 * Release the @p ps handle and clean up any resources associated with it.
 * Pending data will be flushed to persistent storage.
 *
 * @param[in]       ps          Store -- valid object returned by
 *                              @ref osp_ps_open()
 *
 * @note
 * This function automatically syncs data to persistent storage as if
 * osp_ps_sync() was called.
 */
bool osp_ps_close(osp_ps_t *ps);

/**
 * Store value or delete value data associated with key @p key
 *
 * @param[in]       ps          Store -- valid object returned by
 *                              @ref osp_ps_open() with the flag
 *                              OSP_PS_WRITE
 * @param[in]       key         Key value
 * @param[in]       value       Pointer to value data to store; can be NULL
 *                              if @p value_sz is 0
 * @param[in]       value_sz    Value data length, if 0 the key is deleted
 *
 * @return
 * This function returns the number of bytes stored, <0 on error or 0 if the
 * entry was successfully deleted.
 *
 * @note
 * This function does not guarantee that the data was saved to persistent
 * store. To ensure that data hits the storage, a call to @ref osp_ps_sync()
 * or @ref osp_ps_close() is required.
 */
ssize_t osp_ps_set(
        osp_ps_t *ps,
        const char *key,
        void *value,
        size_t value_sz);

/**
 * Retrieve data associated with @p key from store
 *
 * @param[in]       ps          Store -- valid object returned by
 *                              @ref osp_ps_open() with the flag
 *                              OSP_PS_READ
 * @param[in]       key         Key to retrieve
 * @param[out]      value       Pointer to value data to store; can be NULL
 *                              if @p value_sz is 0
 * @param[in]       value_sz    Maximum length of @p value, data will be
 *                              truncated if the actual size exceeds
 *                              @p value_sz
 *
 * @return
 * Return the data size associated with the key, a value of <0 on error or 0
 * if they key was not found.
 *
 * @note
 * If @p value_sz is less than the actual key data, the data will be truncated.
 * However, the return value will still be the actual data size.
 */
ssize_t osp_ps_get(
        osp_ps_t *ps,
        const char *key,
        void *value,
        size_t value_sz);

/**
 * Erase content of store @p ps (delete all keys and their values)
 *
 * @param[in]       ps          Store -- valid object returned by
 *                              @ref osp_ps_open() with the flag
 *                              OSP_PS_WRITE
 *
 * @note
 * Stores opened with the same name but with or without the OPS_PS_PRESERVE
 * flag are different stores.
 *
 * @return
 * This function returns true on success, or false if any errors were
 * encountered. If false is returned, it should be assumed that store
 * was not erased.
 *
 * @note
 * This function does not guarantee that the data was deleted from
 * persistent store. To ensure that the change hits the storage, a
 * call to @ref osp_ps_sync() or @ref osp_ps_close() is required.
 */
bool osp_ps_erase(osp_ps_t *ps);

/**
 * Flush all dirty data to persistent storage. When this function returns,
 * the data written by @ref osp_ps_set() should be considered safely stored.
 *
 * @param[in]       ps          Store -- valid object returned by
 *                              @ref osp_ps_open() with the flag
 *                              OSP_PS_WRITE
 * @return
 * This function returns true on success, or false if any errors were
 * encountered. If false is returned, it should be assumed that data loss
 * may occur.
 */
bool osp_ps_sync(osp_ps_t *ps);


/// @} OSP_PS
/// @} OSP

#endif /* OSP_PS_H_INCLUDED */
