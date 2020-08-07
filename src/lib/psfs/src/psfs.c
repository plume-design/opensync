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

#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "log.h"
#include "osp_ps.h"
#include "util.h"
#include "const.h"

#include "psfs.h"

/*
 * ===========================================================================
 *  Private definitions
 * ===========================================================================
 */
/* Magic NUMBER */
#define PSFS_MAGIC                          0x50534653
/* Padding pattern */
#define PSFS_PADDING                        0xFF

/* CRC32 polynomial */
#define PSFS_CRC32_POLY                     0xEDB88320
/* Running a CRC32 over a "data + CRC32" buffer will always yield this number */
#define PSFS_CRC32_VERIFY                   0x2144DF1C

#define PSFS_INIT (psfs_t)      \
{                               \
    .psfs_fd = -1,              \
    .psfs_dirfd = -1            \
}

static int psfs_dir_open(bool preserve);
static bool psfs_dir_close(bool preserve);
static bool psfs_sync_append(psfs_t *ps);
static bool psfs_sync_prune(psfs_t *ps);
static bool psfs_file_lock(int fd, bool exclusive);
static bool psfs_file_unlock(int fd);
static void psfs_drop_record(psfs_t *ps, struct psfs_record *pr, ds_tree_iter_t *iter);
ssize_t psfs_record_write(int fd, struct psfs_record *pr);
ssize_t psfs_record_read(int fd, struct psfs_record *pr);
void psfs_record_init(struct psfs_record *pr, const char *key, const void *data, size_t datasz);
void psfs_record_fini(struct psfs_record *pr);

static uint32_t psfs_crc32(uint32_t crc, void *buf, ssize_t bufsz);

/*
 * ===========================================================================
 *  Public API implementation
 * ===========================================================================
 */

/**
 * Open a PSFS store. Flags are defined in the osp_ps.h header file and the
 * following values are accepted:
 *
 * @param[in]   name    Name of the store
 * @param[in]   flags   Flags as defined in osp_ps.h:
 *                          - OPS_PS_READ - Open store for reading
 *                          - OSP_PS_WRITE - Open store for writing
 *                          - OSP_PS_RDWR - Read/write
 *                          - OSP_PS_PRESERVE - Open/create the store in the
 *                            preserve area, which is preserved across system
 *                            upgrades.
 * @return
 * This function will return true on success, or false on error.
 *
 * @note
 * If the store is opened in read-only mode (OSP_PS_READ) and the file does not
 * yet exist, it will return an error. Otherwise the file store will be created.
 *
 * @note
 * Opening a file store with the same name but with or without the
 * OPS_PS_PRESERVE flag will open different stores.
 */
bool psfs_open(psfs_t *ps, const char *name, int flags)
{
    struct stat pst;
    struct stat tst;
    int oflags;

    *ps = PSFS_INIT;

    /*
     * Calculate for open()/openat() flags according to the flags passed to
     * psfs_open()
     */
    if ((flags & OSP_PS_RDWR) == 0)
    {
        LOG(ERR, "psfs: %s: Invalid flags passed to psfs_open(): %d. "
                 "Use OSP_PS_READ or OSP_PS_WRITE.",
                 name,
                 flags);
        return false;
    }

    STRSCPY(ps->psfs_name, name);
    ps->psfs_flags = flags;
    ds_tree_init(&ps->psfs_root, ds_str_cmp, struct psfs_record, pr_tnode);

    /* Open the store folder */
    ps->psfs_dirfd = psfs_dir_open(flags & OSP_PS_PRESERVE);
    if (ps->psfs_dirfd < 0)
    {
        LOG(ERR, "psfs: %s: Error opening store dir.", name);
        return false;
    }

    oflags = O_APPEND;
    oflags |= (flags & OSP_PS_WRITE) ? O_CREAT: 0;
    oflags |= (flags & OSP_PS_WRITE) ? O_RDWR : O_RDONLY;

retry:
    ps->psfs_fd = openat(ps->psfs_dirfd, name, oflags, 0600);
    if (ps->psfs_fd < 0)
    {
        LOG(DEBUG, "psfs: %s: Error opening store. Error: %s",
                ps->psfs_name,
                strerror(errno));
        goto error;
    }

    LOG(DEBUG, "psfs: %s: Acquiring lock....", ps->psfs_name);
    /* Acquire the lock, OSP_PS_WRITE will acquire an exclusive lock */
    if (!psfs_file_lock(ps->psfs_fd, flags & OSP_PS_WRITE))
    {
        LOG(ERR, "psfs: %s: Error acquiring lock.", ps->psfs_name);
        goto error;
    }

    /*
     * It may so happen that a prune operation happened while we were waiting
     * for the lock and we may be very well waiting on a lock on a stale file.
     *
     * To check for this condition, stat the file descriptor and a newly opened
     * file. If the inode numbers match, we're good.
     */
    if (fstat(ps->psfs_fd, &pst) != 0)
    {
        LOG(ERR, "psfs: %s: Error opening store -- fstat() failed. Error: %s",
                ps->psfs_name,
                strerror(errno));
        return false;
    }

    if (fstatat(ps->psfs_dirfd, name, &tst, 0) != 0)
    {
        LOG(ERR, "psfs: %s: Error opening store -- fstatat() failed. Error: %s",
                ps->psfs_name,
                strerror(errno));
        return false;
    }

    /* Compare inode numbers */
    if (pst.st_ino != tst.st_ino)
    {
        LOG(NOTICE, "psfs: %s: Pruned file detected. Retrying to acquire lock.", ps->psfs_name);
        /* Swap the file descriptors and retry acquiring the lock */
        close(ps->psfs_fd);
        goto retry;
    }

    return true;

error:
    if (ps != NULL)
    {
        if (ps->psfs_fd >= 0) close(ps->psfs_fd);
        psfs_dir_close(flags & OSP_PS_PRESERVE);
        *ps = PSFS_INIT;
    }

    return false;
}

/**
 * Close a PSFS store. All dirty data is flushed to disk as if psfs_sync()
 * was called.
 *
 * @param[in]   ps      Store object as previously acquired by psfs_open()
 *
 * @return
 * This function returns true if the operation was successful or false otherwise.
 * In either case the resources associated with the object are freed so the
 * object is unusable after this function returns.
 */
bool psfs_close(psfs_t *ps)
{
    struct psfs_record *pr;
    ds_tree_iter_t iter;

    bool retval = true;

    /* Flush dirty data to disk */
    if (!psfs_sync(ps, false))
    {
        retval = false;
    }

    if (!psfs_file_unlock(ps->psfs_fd))
    {
        LOG(ERR, "psfs: %s: Error unlocking store.", ps->psfs_name);
        retval = false;
    }

    if (ps->psfs_fd >= 0) close(ps->psfs_fd);

    /*
     * Free all cached entries
     */
    ds_tree_foreach_iter(&ps->psfs_root, pr, &iter)
    {
        if (pr->pr_dirty)
        {
            LOG(ERR, "psfs: %s: Record '%s' is still dirty after a sync.",
                    ps->psfs_name,
                    pr->pr_key);
            retval = false;
        }

        psfs_drop_record(ps, pr, &iter);
    }

    if (!psfs_dir_close(ps->psfs_flags & OSP_PS_PRESERVE))
    {
        retval = false;
    }

    *ps = PSFS_INIT;

    return retval;
}

/**
 * Sync dirty data to physical media. If @p force_prune is set to true, always
 * sync data in prune mode. Otherwise it is up to the implementation to decide
 * when to use append or prune mode.
 *
 * @param[in]   ps      Store object as previously acquired by psfs_open()
 *
 * @return
 * This function returns true if the operation was successful or false otherwise.
 */
bool psfs_sync(psfs_t *ps, bool force_prune)
{
    double wasted_ratio;
    ssize_t wasted;
    struct stat st;

    bool prune = false;

    if ((ps->psfs_flags & OSP_PS_WRITE) == 0)
    {
        /* Read-only mode; nothing to sync */
        return true;
    }

    /* Calculate wasted space */
    if (fstat(ps->psfs_fd, &st) != 0)
    {
        LOG(ERR, "psfs: %s: sync: Unable to stat file, falling back to append strategy.", ps->psfs_name);
    }
    wasted = (st.st_size > ps->psfs_used) ? st.st_size - ps->psfs_used : 0;
    wasted_ratio = (st.st_size == 0) ? 0.0 : (double)wasted / st.st_size;

    LOG(INFO, "psfs: %s: Syncing; used bytes = %zd, wasted bytes = %zd, waste ratio = %0.2f",
            ps->psfs_name,
            ps->psfs_used,
            wasted,
            wasted_ratio);

    /* Do not use prune when the file size is below PSFS_SYNC_MIN */
    if (st.st_size > (CONFIG_PSFS_SYNC_MIN*1024))
    {
        if (wasted > (CONFIG_PSFS_SYNC_WASTED_MAX*1024))
        {
            LOG(INFO, "psfs: %s: Forcing prune mode as wasted space exceeds %dkB.",
                    ps->psfs_name,
                    CONFIG_PSFS_SYNC_WASTED_MAX);
            prune = true;
        }
        else if ((wasted_ratio * 100.0) > CONFIG_PSFS_SYNC_WASTED_RATIO_MAX)
        {
            LOG(INFO, "psfs: %s: Forcing prune mode as wasted space ratio (%0.2f) exceeds threshold %d%%.",
                    ps->psfs_name,
                    wasted_ratio,
                    CONFIG_PSFS_SYNC_WASTED_RATIO_MAX);
            prune = true;
        }
    }

    /* Heuristic to decide whether to do an append or prune operation */
    if (prune || force_prune)
    {
        return psfs_sync_prune(ps);
    }
    else
    {
        return psfs_sync_append(ps);
    }

    return true;
}

/**
 * Load all current data from physical media to memory. This function must
 * be called before psfs_get() can be used to read stored data.
 *
 * @param[in]   ps      Store object as previously acquired by psfs_open()
 *
 * @return
 * This function returns true if the operation was successful or false otherwise.
 */
bool psfs_load(psfs_t *ps)
{
    ssize_t rc;

    struct psfs_record *pr = NULL;

    /*
     * Cache all records in the database to RAM
     */
    do
    {
        pr = calloc(1, sizeof(*pr));

        rc = psfs_record_read(ps->psfs_fd, pr);
        if (rc <= 0)
        {
            free(pr);
            continue;
        }

        struct psfs_record *opr;

        opr = ds_tree_find(&ps->psfs_root, pr->pr_key);
        if (opr != NULL)
        {
            /* Replace the old record -- remove it from the store cache */
            psfs_drop_record(ps, opr, NULL);
        }

        /* Do not cache deleted keys */
        if (pr->pr_datasz == 0)
        {
            psfs_record_fini(pr);
            free(pr);
            continue;
        }

        ds_tree_insert(&ps->psfs_root, pr, pr->pr_key);
        /* Account read data */
        ps->psfs_used += pr->pr_used;
    }
    while (rc != 0);

    return true;
}

/**
 * Erase a PSFS store (delete all keys and their values). This flags the store
 * as dirty and will be written to disk at the first psfs_sync() or psfs_close()
 * operation.
 *
 * @return
 * This function will return true on success, or false on error.
 *
 * @note
 * If the store is opened in read-only mode (OSP_PS_READ), it will return an
 * error.
 */
bool psfs_erase(psfs_t *ps)
{
    struct psfs_record *pr;
    ds_tree_iter_t iter;

    if ((ps->psfs_flags & OSP_PS_WRITE) == 0)
    {
        /* psfs_erase() not supported in read-only mode */
        return false;
    }

    /* Truncate file to 0 bytes  */
    if (ftruncate(ps->psfs_fd, 0) != 0)
    {
        LOG(ERR, "psfs: %s: Error truncating store (erase).", ps->psfs_name);
        return false;
    }

    /* Move file pointer to the beginning of the file */
    if (lseek(ps->psfs_fd, 0, SEEK_SET) != 0)
    {
        LOG(ERR, "psfs: %s: Error seeking to the beginning of file (erase).", ps->psfs_name);
        return false;
    }

    /* Drop all in-memory records */
    ds_tree_foreach_iter(&ps->psfs_root, pr, &iter)
    {
        LOG(DEBUG, "psfs: %s: Deleting record '%s'.", ps->psfs_name, pr->pr_key);
        psfs_drop_record(ps, pr, &iter);
    }

    return true;
}

/**
 * Set the value of a single key. This flags the record associated with the key
 * as dirty and will be written to disk at the first psfs_sync() or psfs_close()
 * operation.
 *
 * @param[in]   ps          Store object as previously acquired by psfs_open()
 * @param[in]   key         Unique key id
 * @param[in]   value       Data associated with key
 * @param[in]   value_sz    Size of data associated with value
 *
 * @return
 * This function returns the number of bytes stored or a negative number on
 * error.
 *
 * @note
 * A value of NULL or value_sz of 0 indicates that the key must be deleted.
 */
ssize_t psfs_set(psfs_t *ps, const char *key, const void *value, size_t value_sz)
{
    struct psfs_record *pr;

    if ((ps->psfs_flags & OSP_PS_WRITE) == 0)
    {
        /* osp_ps_set() not supported in read-only mode */
        return -1;
    }

    if (value == NULL) value_sz = 0;

    pr = ds_tree_find(&ps->psfs_root, (void *)key);
    if (pr != NULL)
    {
        /* Compare data, if it is the same do nothing */
        if (pr->pr_datasz == value_sz && memcmp(pr->pr_data, value, value_sz) == 0)
        {
            return value_sz;
        }

        /* Remove old entry */
        psfs_drop_record(ps, pr, NULL);
    }
    else if (value_sz == 0)
    {
        /* Key does not exists, and value_sz is 0 (delete key) -- nothing to do */
        return 0;
    }

    pr = calloc(1, sizeof(*pr));
    psfs_record_init(pr, key, value, value_sz);

    /* Flag this record as dirty */
    pr->pr_dirty = true;

    ds_tree_insert(&ps->psfs_root, pr, pr->pr_key);

    /* Update byte count */
    ps->psfs_used += pr->pr_used;

    return value_sz;
}

/**
 * Get the value of a single key. In order to retrieve data stored on the
 * physical medium, @ref psfs_load() must be called beforehand.
 *
 *
 * @param[in]   ps          Store object as previously acquired by psfs_open()
 * @param[in]   key         Unique key id
 * @param[out]  value       Data associated with key
 * @param[in]   value_sz    Maximum number of bytes available in @ref value
 *
 * @return
 * This function returns the size of the data associated with key, 0 if the key
 * was not found or a negative number on error.
 *
 * @note
 * This function returns the actual size of the record even if value_sz prevents
 * the full buffer to be copied.
 */
ssize_t psfs_get(psfs_t *ps, const char *key, void *value, size_t value_sz)
{
    struct psfs_record *pr;

    if ((ps->psfs_flags & OSP_PS_READ) == 0)
    {
        /* osp_ps_get() not supported in write-only mode */
        return -1;
    }

    pr = ds_tree_find(&ps->psfs_root, (void *)key);
    if (pr == NULL)
    {
        /* Key not found */
        return 0;
    }

    memcpy(value, pr->pr_data, pr->pr_datasz < value_sz ? pr->pr_datasz : value_sz);

    return pr->pr_datasz;
}


/*
 * ===========================================================================
 *  Private functions
 * ===========================================================================
 */

/**
 * Open the store folder as a file descriptor. The store dir file descriptor
 * is used for openat() and fdatasync() to flush file renames to disk.
 */
struct psfs_dir
{
    const char     *psd_dir;        /* Folder path */
    int             psd_ref;        /* Reference count */
    int             psd_fd;         /* File descriptor */
};

/**
 * List of predefined directories that will be used as storage
 *
 * The entry with index 0 is used as the primary storage while index 1
 * is used for the upgrade preserved storage.
 */
static struct psfs_dir psfs_dirs[2] =
{
    {
        .psd_dir = CONFIG_PSFS_DIR,
        .psd_ref = 0,
        .psd_fd = -1,
    },
    {
        .psd_dir = CONFIG_PSFS_PRESERVE_DIR,
        .psd_ref = 0,
        .psd_fd = -1,
    }
};

/**
 * Open a storage folder and return its file descriptor. This is mainly used
 * in conjunction with functions such as openat(), renamet(), fstatat() ...
 *
 * @param[in]   preserve    Set to true to use the upgrade preserved storage
 *
 * @return
 * This function returns a negative number on error or the directory file
 * descriptor.
 *
 * @note
 * This uses reference counting for keeping folders opened/closed so make sure
 * that the number of psfs_dir_open() and psfs_dir_close() calls match.
 */
int psfs_dir_open(bool preserve)
{
    struct psfs_dir *psd = &psfs_dirs[preserve ? 1 : 0];

    if (psd->psd_ref == 0)
    {
        /* Create top-level folder if it doesn't exist */
        if (mkdir(psd->psd_dir, 0700) != 0 && errno != EEXIST)
        {
            LOG(ERR, "psfs: Error creating folder: %s", psd->psd_dir);
            return -1;
        }

        /* Open the folder as a file descriptor */
        psd->psd_fd = open(psd->psd_dir, O_RDONLY);
        if (psd->psd_fd < 0)
        {
            LOG(ERR, "psfs: Error opening folder: %s. Error: %s", psd->psd_dir, strerror(errno));
            return -1;
        }
    }

    psd->psd_ref++;
    return psd->psd_fd;
}

/**
 * Close the store folder file descriptor if the reference count reaches 0.
 *
 * @param[in]   preserve    Use the same value that was used with psfs_dir_open()
 *
 * @return
 * Returns true on success or false otherwise.
 *
 * @note
 * This uses reference counting for keeping folders opened/closed so make sure
 * that the number of psfs_dir_open() and psfs_dir_close() calls match.
 */
bool psfs_dir_close(bool preserve)
{
    struct psfs_dir *psd = &psfs_dirs[preserve ? 1 : 0];

    if (psd->psd_ref == 0)
    {
        LOG(ERR, "psfs: Reference count is already 0 before store dir close. Uneven open()/close() calls: %s",
                 psd->psd_dir);
        return false;
    }

    psd->psd_ref--;

    if (psd->psd_ref > 0) return true;

    if (close(psd->psd_fd) != 0)
    {
        LOG(ERR, "psfs: Error closing store directory: %s", psd->psd_dir);
        return false;
    }

    return true;
}

/**
 * Do an advisory lock on a file. Wait indefinitely when waiting for a lock.
 *
 * @param[in]   fd          File descriptor
 * @param[in]   exclusive   If true, use a read-write (exclusive) lock; otherwise
 *                          a shared (read-only) lock is used.
 *
 * @return
 * This function returns true if the file was successfully locked or false
 * on error.
 *
 * @note
 * This function uses POSIX record locks and has the same drawbacks/features:
 *  - if *ANY* file descriptor is closed that references fd, all locks held
 *    by the current process on the file are released
 *
 * @note
 * Calling this function repeatedly can upgrade a lock from shared to exclusive.
 */
bool psfs_file_lock(int fd, bool exclusive)
{
    int rc;

    struct flock fl =
    {
        .l_type = exclusive ? F_WRLCK : F_RDLCK,
        /* Lock whole file */
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0,
    };

retry:
    rc = fcntl(fd, F_SETLKW, &fl);
    if (rc == EINTR)
    {
        LOG(DEBUG, "psfs: file_lock: Interrupted while trying to acquire lock, retrying... ");
        goto retry;
    }

    if (rc != 0)
    {
        LOG(ERR, "psfs: file_lock: Error locking file. Error: %s", strerror(errno));
        return false;
    }

    LOG(DEBUG, "psfs: Lock acquired, type=%s", exclusive ? "exclusive" : "shared");

    return true;
}

/**
 * Release a lock on the file that was previously acquired using
 * @ref psfs_file_lock().
 *
 * @param[in]   fd      File descriptor to unlock
 *
 * @note
 * This function uses POSIX record locks and has the same drawbacks/features:
 *  - if *ANY* file descriptor is closed that references fd, all locks held
 *    by the current process on the file are released
 */
bool psfs_file_unlock(int fd)
{
    int rc;

    struct flock fl =
    {
        .l_type = F_UNLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0,
    };

    rc = fcntl(fd, F_SETLK, &fl);
    if (rc != 0)
    {
        LOG(ERR, "psfs: file_unlock: Error unlocking file.");
        return false;
    }

    return true;
}

/**
 * Disassociate record @p pr with store @p ps.
 *
 * If an iterator is provided, this function will use ds_tree_iremove().
 * Otherwise ds_tree_remove() will be used to remove the record from the store.
 */
void psfs_drop_record(psfs_t *ps, struct psfs_record *pr, ds_tree_iter_t *iter)
{
    if (iter != NULL)
    {
        ds_tree_iremove(iter);
    }
    else
    {
        ds_tree_remove(&ps->psfs_root, pr);
    }

    ps->psfs_used -= pr->pr_used;
    psfs_record_fini(pr);
    free(pr);
}

/**
 * Initialize a record using @p key and @p data.
 *
 * psfs_record_write() doesn't free this data, for this purpose
 * psfs_record_fini() must be called after psfs_record_init().
 *
 * @param[in]   pr      Pointer to an uninitialized record structure
 * @param[in]   key     The record key
 * @param[in]   data    The record data
 * @param[in]   datasz  Data length
 */
void psfs_record_init(
        struct psfs_record *pr,
        const char *key,
        const void *data,
        size_t datasz)
{
    size_t ksz = strlen(key) + sizeof(char);

    if (data == NULL) datasz = 0;

    pr->pr_key = malloc(ksz + datasz);
    pr->pr_data = (void *)(pr->pr_key + ksz);

    memcpy(pr->pr_key, key, ksz);
    if (data != NULL)
    {
        memcpy(pr->pr_data, data, datasz);
    }
    pr->pr_datasz = datasz;

    /* Update bytes used by this record, datasize + magic number + size + crc32 */
    pr->pr_used = ksz + datasz + (3 * sizeof(uint32_t));
}

/**
 * Free all resources associated with a record
 *
 * @param[in]   pr      Pointer to a valid record data (initialized)
 */
void psfs_record_fini(struct psfs_record *pr)
{
    /*
     * No need to free pr->pr_data as it is allocated in the same buffer as
     * pr_key -- see psfs_record_init()
     */
    if (pr->pr_key != NULL) free(pr->pr_key);
}

/*
 * Write a record to the end of the current store file and flag the current
 * record as clean.
 *
 * @param[in]   fd      Valid file descriptor
 * @param[in]   pr      Initialized record structure
 *
 * @return
 * This function returns the number of bytes used by this record on physical
 * (excluding padding) media or a negative number on error.
 *
 * @note
 * Since O_APPEND has been used during open(), there's no need to lseek() to
 * the end of the file. Also, write data using a single writev() call to ensure
 * friendliness with file systems mounted with "-o sync".
 */
ssize_t psfs_record_write(int fd, struct psfs_record *pr)
{
    uint8_t wpad[4];
    uint8_t wcrc[4];
    uint32_t wmagic;
    uint32_t wsz;
    struct stat st;
    ssize_t ksz;
    ssize_t bpadlen;
    ssize_t epadlen;
    ssize_t rc;

    uint32_t crc = 0x0;
    ssize_t retval = 0;

    /* Initialize the padding buffer */
    memset(wpad, PSFS_PADDING, sizeof(wpad));

    /*
     * All records must start at a 4 byte offset. If for some reason it
     * does not then pad it.
     */
    if (fstat(fd, &st) != 0)
    {
        LOG(ERR, "psfs: record_write: Error stat()ing store file. Cannot write data for record %s.",
                 pr->pr_key);
        return -1;
    }

    /* Calculate padding data */
    bpadlen = 0;
    if ((st.st_size & 0x3) != 0)
    {
        bpadlen = 4 - ((st.st_size) & 0x3);
    }

    wmagic = htonl(PSFS_MAGIC);
    ksz = strlen(pr->pr_key) + sizeof(char);
    wsz = htonl(ksz + pr->pr_datasz);

    /* Refresh CRC */
    crc = psfs_crc32(crc, &wmagic, sizeof(wmagic));
    retval += sizeof(wmagic);
    crc = psfs_crc32(crc, &wsz, sizeof(wsz));
    retval += sizeof(wsz);
    crc = psfs_crc32(crc, pr->pr_key, ksz);
    retval += ksz;
    crc = psfs_crc32(crc, pr->pr_data, pr->pr_datasz);
    retval += pr->pr_datasz;

    /*
     * The CRC must be written out in big-endian order
     */
    wcrc[0] = (crc >> 0) & 0xFF;
    wcrc[1] = (crc >> 8) & 0xFF;
    wcrc[2] = (crc >> 16) & 0xFF;
    wcrc[3] = (crc >> 24) & 0xFF;
    retval += sizeof(wcrc);

    /*
     * Calculate padding size
     */
    epadlen = 0;
    if ((retval & 0x3) != 0)
    {
        epadlen = 4 - (retval & 0x3);
    }

    struct iovec iov[] =
    {
        /* Pre-padding */
        {
            .iov_base = &wpad,
            .iov_len  = bpadlen
        },
        /* Magic number */
        {
            .iov_base = &wmagic,
            .iov_len  = sizeof(wmagic),
        },
        /* Data size */
        {
            .iov_base = &wsz,
            .iov_len  = sizeof(wsz),
        },
        /* Key */
        {
            .iov_base = pr->pr_key,
            .iov_len  = ksz,
        },
        /* Data */
        {
            .iov_base = pr->pr_data,
            .iov_len  = pr->pr_datasz,
        },
        /* CRC */
        {
            .iov_base = &wcrc,
            .iov_len  = sizeof(wcrc)
        },
        /* Padding */
        {
            .iov_base = &wpad,
            .iov_len  = epadlen,
        }
    };

    rc = writev(fd, iov, ARRAY_LEN(iov));
    if (rc < retval + epadlen + bpadlen)
    {
        LOG(ERR, "psfs: Error writing key %s to storage, error: %s", pr->pr_key, strerror(errno));
        return -1;
    }

    pr->pr_dirty = false;

    return retval;
}

/**
 * Try to read a single record from the file at the current file position.
 * If the read is successful, the file position indicator will be set to
 * the beginning of the next record.
 *
 * If the read is unsuccessful, the file position indicator is set to the next
 * potential record location.
 *
 * @param[in]   fd      File descriptor to a file in read-only access
 * @param[out]  pr      Pointer to an uninitialized record
 *
 * @return
 * This function returns the total number of bytes read, 0 on EOF, or a negative
 * number on read error.
 *
 * @note
 * A record returned by this function must be freed using psfs_record_fini()
 */
ssize_t psfs_record_read(int fd, struct psfs_record *pr)
{
    uint32_t pr_magic;
    uint32_t pr_size;
    uint32_t pr_crc;
    ssize_t padlen;
    struct stat st;
    size_t doff;
    off_t coff;
    ssize_t rc;

    ssize_t retval = 0;
    uint32_t crc = 0;
    uint32_t wdata;

    /* Clear all data */
    pr->pr_key = NULL;
    pr->pr_data = NULL;
    pr->pr_datasz = 0;

    coff = lseek(fd, 0, SEEK_CUR);
    /*
     * Align next read offset to 4 bytes
     */
    if ((coff & 0x3) != 0)
    {
        padlen = 4 - (coff & 0x3);
        /* Align to 4 byets */
        rc = read(fd, &wdata, (size_t)padlen);
        if (rc == 0)
        {
            return 0;
        }
        else if (rc < padlen)
        {
            LOG(ERR, "psfs: record_read: Error discarding padding.");
            return -1;
        }

        coff += padlen;
    }

    rc = read(fd, &pr_magic, sizeof(pr_magic));
    if (rc == 0)
    {
        /* EOF condition, return 0 */
        return 0;
    }
    else if (rc < (ssize_t)sizeof(pr_magic))
    {
        LOG(ERR, "psfs: record_read: Short read when reading magic number.");
        return -1;
    }

    if (pr_magic != ntohl(PSFS_MAGIC))
    {
        LOG(DEBUG, "psfs: record_read: Invalid record at offset %zu, skipping.", (size_t)coff);
        return -1;
    }

    crc = psfs_crc32(crc, &pr_magic, sizeof(pr_magic));
    retval += sizeof(pr_magic);

    /*
     * Read size
     */
    rc = read(fd, &pr_size, sizeof(pr_size));
    if (rc < (ssize_t)sizeof(pr_size))
    {
        LOG(ERR, "psfs: record_read: Short read when reading size.");
        return -1;
    }

    crc = psfs_crc32(crc, &pr_size, sizeof(pr_size));
    retval += sizeof(pr_size);

    pr_size = ntohl(pr_size);

    /*
     * Read data, do sanity checks
     */

    /* Check if we're reading past the end of the file */
    if (fstat(fd, &st) != 0)
    {
        LOG(ERR, "psfs: record_read: Error stating file. Error: %s", strerror(errno));
        goto seek_on_error;
    }

    /* Compare the file size with the supposed end-of-record offset */
    if (st.st_size < (off_t)(coff +  sizeof(pr_magic) + sizeof(pr_size) + pr_size + sizeof(crc)))
    {
        LOG(ERR, "psfs: record_read: Corrupted record size points past end of file.");
        goto seek_on_error;
    }

    /*
     * Read data
     */

    /* Allocate buffer space for data and read it */
    pr->pr_key = malloc(pr_size);
    rc = read(fd, pr->pr_key, pr_size);
    if (rc < (ssize_t)pr_size)
    {
        LOG(ERR, "psfs: record_read: Corrupt record data.");
        goto seek_on_error;
    }

    crc = psfs_crc32(crc, pr->pr_key, pr_size);
    retval += pr_size;

    /* Before trying to parse the data, verify the CRC */
    rc = read(fd, &pr_crc, sizeof(pr_crc));
    if (rc < (ssize_t)sizeof(pr_crc))
    {
        LOG(ERR, "psfs: record_read: Short read while reading CRC.");
        goto seek_on_error;
    }

    crc = psfs_crc32(crc, &pr_crc, sizeof(pr_crc));
    retval += sizeof(pr_crc);

    if (crc != PSFS_CRC32_VERIFY)
    {
        LOG(ERR, "psfs: record_read: Invalid record CRC at offset %zu.", (size_t)coff);
        goto seek_on_error;
    }

    /* Get the data offset relative to the key by calculating the key length */
    doff = strnlen(pr->pr_key, pr_size);
    if (doff >= pr_size)
    {
        LOG(ERR, "psfs: record_read: Key is corrupted.");
        goto seek_on_error;
    }
    /*
     * doff points now to the '\0' of pr->pr_key. Move it by 1 to get the actual
     * data offset
     */
    doff++;

    pr->pr_data = (uint8_t *)pr->pr_key + doff;
    pr->pr_datasz = pr_size - doff;
    pr->pr_used = retval;

    return retval;

seek_on_error:
    if (pr->pr_key != NULL) free(pr->pr_key);

    if (!lseek(fd, coff + (off_t)sizeof(pr_magic), SEEK_SET))
    {
        LOG(ERR, "pspfs: record_read: Error seeking back to start of record.");
    }

    return -1;
}

/**
 * Transfer all dirty records to physical media (flush). This function works
 * in "append" mode, which just appends dirty records to the journal.
 *
 * @param[in]   ps      Pointer to a valid psfs store object
 *
 * @return
 * This function returns true on success or false on error.
 */
bool psfs_sync_append(psfs_t *ps)
{
    struct psfs_record *pr;
    ds_tree_iter_t iter;

    LOG(DEBUG, "psfs: %s: Syncing in append mode.", ps->psfs_name);

    if ((ps->psfs_flags & OSP_PS_WRITE) == 0)
    {
        LOG(ERR, "psfs: %s: Unable to sync data (append), read-only mode.", ps->psfs_name);
        return false;
    }

    ds_tree_foreach_iter(&ps->psfs_root, pr, &iter)
    {
        if (!pr->pr_dirty) continue;

        if (psfs_record_write(ps->psfs_fd, pr) <= 0)
        {
            LOG(ERR, "psfs: %s: Error writing record.", ps->psfs_name);
            return false;
        }
    }

    if (fsync(ps->psfs_fd) != 0)
    {
        LOG(WARN, "psfs: %s: Error syncing (append) storage data.", ps->psfs_name);
    }

    return true;
}

/**
 * Transfer all dirty records to physical media (flush). This function performs
 * a prune (copy-over) operation where the full database content is dumped
 * to a temporary file, flushed to disk and then renamed to the original store
 * name,
 *
 * @param[in]   ps      Pointer to a valid psfs store object
 *
 * @return
 * This function returns true on success or false on error.
 */
bool psfs_sync_prune(psfs_t *ps)
{
    char tname[64 + 16];
    struct psfs_record *pr;
    ds_tree_iter_t iter;

    int tfd = -1;
    bool retval = false;

    if ((ps->psfs_flags & OSP_PS_WRITE) == 0)
    {
        LOG(ERR, "psfs: %s: Unable to sync data (prune), read-only mode.", ps->psfs_name);
        return false;
    }

    snprintf(tname, sizeof(tname), ".%s.tmp", ps->psfs_name);
    LOG(DEBUG, "psfs: %s: Syncing in prune mode. Temporary path: %s", ps->psfs_name, tname);

    /*
     * Create a new file
     */

    /*
     * Make sure to delete any stale files first -- although it seems that
     * O_TRUNC may take care of this, the issue is that files with invalid
     * permissions can still result in an error during openat()
     */
    (void)unlinkat(ps->psfs_dirfd, tname, 0);

    /* Use O_TRUNC just in case the file already exists */
    tfd = openat(ps->psfs_dirfd, tname, O_CREAT | O_TRUNC | O_WRONLY | O_APPEND, 0600);
    if (tfd < 0)
    {
        LOG(ERR, "psfs: %s: Error creating prune file. Error: %s.", ps->psfs_name, strerror(errno));
        goto error;
    }

    /* Acquire an exclusive lock to the temporary file */
    if (!psfs_file_lock(tfd, true))
    {
        LOG(ERR, "psfs: %s: Error acquiring lock to temporary store: %s",
                 ps->psfs_name, tname);
        goto error;
    }

    /* Write the current content of the database to file */
    ds_tree_foreach_iter(&ps->psfs_root, pr, &iter)
    {
        if (pr->pr_datasz == 0)
        {
            LOG(DEBUG, "psfs: %s: Deleting record %s.", ps->psfs_name, pr->pr_key);
            psfs_drop_record(ps, pr, &iter);
            continue;
        }

        if (psfs_record_write(tfd, pr) <= 0)
        {
            LOG(ERR, "psfs: %s: Error writing record during a prune operation.",
                     ps->psfs_name);
            continue;
        }
        pr->pr_dirty = false;
    }

    /* Flush data to storage */
    if (fsync(tfd) != 0)
    {
        LOG(ERR, "psfs: %s: Error syncing temporary storage data.", ps->psfs_name);
        goto error;
    }

    /* Rename temporary file to the real file */
    if (renameat(ps->psfs_dirfd, tname, ps->psfs_dirfd, ps->psfs_name)  != 0)
    {
        LOG(ERR, "psfs: %s: Error renaming temporary storage.", ps->psfs_name);
        goto error;
    }

    /* Sync parent folder metadata */
    if (fsync(ps->psfs_dirfd) != 0)
    {
        LOG(ERR, "psfs: %s: Error syncing store folder.", ps->psfs_name);
        goto error;
    }

    /* Close old store file descriptor ... */
    (void)psfs_file_unlock(ps->psfs_fd);
    (void)close(ps->psfs_fd);

    /* ... and replace it with the temporary file descriptor */
    ps->psfs_fd = tfd;
    tfd = -1;

    retval = true;

error:
    if (tfd >= 0) close(tfd);

    return retval;
}

/**
 * Table-less CRC32 function implementation.
 *
 * @param[in]   crc     Previous CRC value
 * @param[in]   buf     Data
 * @param[in]   bufsz   Data length
 *
 * @return
 * Returns the updated CRC value
 *
 * @note
 * By appending the CRC in big-endian order to a buffer and re-calculating the
 * CRC, this function should always yield PSFS_CRC32_VERIFY
 */
uint32_t psfs_crc32(uint32_t crc, void *buf, ssize_t bufsz)
{
    uint8_t *pbuf;
    int ii;

    crc = ~crc;
    for (pbuf = buf; bufsz-- > 0; pbuf++)
    {
        crc ^= *pbuf;
        for (ii = 0; ii < 8; ii++)
        {
            crc = (crc & 1) ? (crc >> 1) ^ PSFS_CRC32_POLY : crc >> 1;
        }
    }

    return ~crc;
}
