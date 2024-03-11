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

#include "fm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"
#include "os_util.h"
#include "tailf.h"

typedef struct
{
    int dst_fd;
    ino_t src_inode;
    ssize_t src_offset;
    tailf_t src_tf;
} livecopy_t;

typedef struct
{
    struct ev_loop *loop;
    ev_timer rotation_timer;
    ev_stat rotation_stat;
    ev_timer livecopy_timer;
    ev_stat livecopy_stat;
    ev_stat crash_bt_stat;
    livecopy_t livecopy;
    bool file_write_err;
    fm_log_type_t log_options;
} state_t;

static state_t g_state;

static void fm_do_rotation(void);
static void fm_archive_flash_file(const int src_fd);
static int fm_ramoops_write(const char *buf, size_t size);
static void fm_do_livecopy(void);
static void fm_rotation_stat_callback(struct ev_loop *loop, ev_stat *watcher, int revents);
static void fm_rotation_timer_callback(struct ev_loop *loop, ev_timer *watcher, int revents);
static void fm_livecopy_stat_callback(struct ev_loop *loop, ev_stat *watcher, int revents);
static void fm_livecopy_timer_callback(struct ev_loop *loop, ev_timer *watcher, int revents);
static void fm_backtrace_do_rotation(void);
static void fm_crash_bt_rotation_stat_callback(struct ev_loop *loop, ev_stat *watcher, int revents);
static void fm_crash_bt_update_log_options(const fm_log_type_t options);
static void fm_sync_syslog_dirs(const char *syslog_src, const char *syslog_dst, const int syslog_cnt);
static void fm_execute_syslog_rotate(const char *logs_path, const int rotation_cnt, const char *logs_location);

static void fm_execute_syslog_rotate(const char *logs_path, const int rotation_cnt, const char *logs_location)
{
    char shell_cmd[256];

    snprintf(
            shell_cmd,
            sizeof(shell_cmd),
            "sh %s/scripts/fm_syslog_rotate.sh %s %s %s %d %s",
            CONFIG_INSTALL_PREFIX,
            CONFIG_FM_LOGFILE_MONITOR,
            logs_path,
            CONFIG_FM_LOG_ARCHIVE_SUBDIRECTORY,
            rotation_cnt,
            logs_location);

    LOGI("Execute shell command: %s", shell_cmd);
    cmd_log(shell_cmd);
}

static void fm_do_rotation(void)
{
    const char *logs_path;
    char shell_cmd[256];
    int rotation_cnt;

    // Check if file was really rotated
    if (access(FM_MESSAGES_ORIGINAL_ROTATED, F_OK) != 0)
    {
        return;
    }

    LOGI("Log rotation initiated!");

    if (g_state.log_options.fm_log_flash)
    {
        logs_path = CONFIG_FM_LOG_FLASH_ARCHIVE_PATH;
        rotation_cnt = CONFIG_FM_MAX_ROTATION_SYSLOG_FLASH_CNT;
        fm_execute_syslog_rotate(logs_path, rotation_cnt, CONFIG_FM_LOG_PATH);
    }

    if (g_state.log_options.fm_log_ramoops)
    {
        logs_path = CONFIG_FM_LOG_RAM_ARCHIVE_PATH;
        rotation_cnt = CONFIG_FM_MAX_ROTATION_SYSLOG_RAM_CNT;
        fm_execute_syslog_rotate(logs_path, rotation_cnt, CONFIG_FM_LOG_PATH);
    }

    snprintf(shell_cmd, sizeof(shell_cmd), "rm %s", FM_MESSAGES_ORIGINAL_ROTATED);

    LOGI("Execute shell command: %s", shell_cmd);
    cmd_log(shell_cmd);
}

static void fm_archive_flash_file(const int src_fd)
{
    char copy_buffer[FM_READ_BLOCK_SIZE];
    int archive_fd;
    char *logs_path;
    int rotation_cnt;
    ssize_t src_no;
    char shell_cmd[256];

    // Create copy of archive file (rotate livecopy)
    archive_fd = open(FM_MESSAGES_LIVECOPY_ROTATED, O_CREAT | O_RDWR, S_IROTH | S_IRUSR | S_IWUSR | S_IRGRP);
    if (archive_fd < 0)
    {
        LOGE("Failed to open flash backup destination file %s", strerror(errno));
        return;
    }

    do
    {
        // Copy file to archive
        src_no = read(src_fd, copy_buffer, FM_READ_BLOCK_SIZE);
        if (src_no < 0)
        {
            LOGE("Error reading from file %s", strerror(errno));
            return;
        }

        if (write(archive_fd, copy_buffer, src_no) < src_no)
        {
            LOGE("Write to flash archive failed %s", strerror(errno));
            return;
        }
    } while (src_no > 0);

    int err = fsync(archive_fd);
    if (err != 0)
    {
        LOGE("Error syncing archive file %s", strerror(errno));
    }

    close(archive_fd);

    if (g_state.log_options.fm_log_flash)
    {
        logs_path = CONFIG_FM_LOG_FLASH_ARCHIVE_PATH;
        rotation_cnt = CONFIG_FM_MAX_ROTATION_SYSLOG_FLASH_CNT;
        fm_execute_syslog_rotate(logs_path, rotation_cnt, logs_path);
    }

    if (g_state.log_options.fm_log_ramoops)
    {
        logs_path = CONFIG_FM_LOG_RAM_ARCHIVE_PATH;
        rotation_cnt = CONFIG_FM_MAX_ROTATION_SYSLOG_RAM_CNT;
        fm_execute_syslog_rotate(logs_path, rotation_cnt, logs_path);
    }

    snprintf(shell_cmd, sizeof(shell_cmd), "rm %s", FM_MESSAGES_LIVECOPY_ROTATED);

    LOGI("Execute shell command: %s", shell_cmd);
    cmd_log(shell_cmd);
}

static int fm_ramoops_write(const char *buf, size_t size)
{
    int ret = 0;
    int fd;
    const char *pbuf;

    static char lbuf[FM_READ_BLOCK_SIZE + sizeof(FM_RAMOOPS_HEADER)] = FM_RAMOOPS_HEADER;
    /* Position lsz to the end of the FM_RAMOOPS_HEADER string */
    static size_t lsz = sizeof(FM_RAMOOPS_HEADER) - 1;

    fd = open(CONFIG_FM_RAMOOPS_BUFFER, O_WRONLY);
    if (fd < 0)
    {
        LOGE("Open ramoops file [%s] failed", CONFIG_FM_RAMOOPS_BUFFER);
        ret = -1;
        goto exit;
    }

    /*
     * Split data into new lines and prepend it with FM_RAMOOPS_HEADER
     *
     * The strategy is to always keep FM_RAMOOPS_HEADER at the beginning of the
     * buffer and append data as we're able to split it.
     */
    while (size > 0)
    {
        size_t appendsz = 0;

        /*
         * Calculate the number of bytes to append. If we don't find a '\n'
         * character, use the whole buffer
         */
        pbuf = memchr(buf, '\n', size);
        if (pbuf == NULL)
        {
            /* \n not found, just append everything */
            appendsz = size;
        }
        else
        {
            /* Found \n, the append size is 1 byte past the \n character */
            appendsz = pbuf - buf + 1;
        }

        /*
         * Clip the maximum append size but always leave room for a potential '\n'
         * padding
         */
        if (lsz + appendsz > sizeof(lbuf) - 1)
        {
            appendsz = sizeof(lbuf) - lsz - 1;
        }

        /* Copy the data */
        memmove(lbuf + lsz, buf, appendsz);
        buf += appendsz;
        size -= appendsz;
        lsz += appendsz;

        /* Add a '\n' if its not there */
        if (lbuf[lsz - 1] != '\n')
        {
            /*
             * Buffer is not yet full and there's no '\n' at the end -- this may
             * be an incomplete line (split between two buffers). Return and
             * retry later when more data is available
             */
            if (lsz < sizeof(lbuf) - 1)
            {
                break;
            }
            lbuf[lsz++] = '\n';
        }

        /*
         * To avoid interleaving of data when multiple processes are writing
         * to /dev/fmsg0, use a single write to write the complete line
         */
        ret = write(fd, lbuf, lsz);
        if (ret < 0)
        {
            LOGE("Write to ramoops (2) failed %s", strerror(errno));
        }
        /* Reset lsz */
        lsz = sizeof(FM_RAMOOPS_HEADER) - 1;
    }

exit:
    if (fd > 0)
    {
        close(fd);
    }

    return ret;
}

static void fm_do_livecopy(void)
{
    char dst_buf[FM_READ_BLOCK_SIZE];
    char src_buf[FM_READ_BLOCK_SIZE];
    ssize_t nr_dst;
    ssize_t nr_src;
    ssize_t i;
    unsigned int block = 0;
    static int match = 1;
    int restart = 0;

    do
    {
        nr_src = 0;
        nr_dst = 0;

        i = 0;
        do
        {
            i = tailf_read(&g_state.livecopy.src_tf, src_buf + nr_src, sizeof(src_buf) - nr_src);
            if (i < 0)
            {
                LOGE("Tailf error reading from file");
                return;
            }
            nr_src += i;
            if (g_state.livecopy.src_inode != tailf_get_inode(&g_state.livecopy.src_tf))
            {
                if (match == 1)
                {
                    // We are at the beginning, no inode has been assigned yet
                    g_state.livecopy.src_inode = tailf_get_inode(&g_state.livecopy.src_tf);
                }
                else if (tailf_get_inode(&g_state.livecopy.src_tf))
                {
                    // Start from beginning in case original file changes
                    restart = 1;
                    g_state.livecopy.src_inode = tailf_get_inode(&g_state.livecopy.src_tf);
                    break;
                }
            }

        } while (i > 0 && nr_src < (ssize_t)sizeof(src_buf));

        if (g_state.log_options.fm_log_ramoops)
        {
            fm_ramoops_write(src_buf, nr_src);

            // Skip further processing if logging to flash is disabled
            if (!(g_state.log_options.fm_log_flash))
            {
                if (nr_src == 0)
                    return;
                else
                    continue;
            }
        }

        if (match)
        {
            i = 0;
            do
            {
                i = read(g_state.livecopy.dst_fd, dst_buf + nr_dst, sizeof(dst_buf) - nr_dst);
                if (i < 0)
                {
                    LOGE("Error reading from file %s", strerror(errno));
                    return;
                }
                nr_dst += i;

            } while (i > 0 && nr_dst < (ssize_t)sizeof(dst_buf));
        }

        for (i = 0; match == 1 && i < nr_src; i++)
        {
            if (i >= nr_dst)
            {
                match = 0;
                break;
            }
            else if (dst_buf[i] != src_buf[i])
            {
                match = 0;
                lseek(g_state.livecopy.dst_fd, block * FM_READ_BLOCK_SIZE + i, SEEK_SET);
                LOGI("Temporary and flash file differs, writing differences to flash");
                fm_archive_flash_file(g_state.livecopy.dst_fd);
                lseek(g_state.livecopy.dst_fd, block * FM_READ_BLOCK_SIZE + i, SEEK_SET);
                ftruncate(g_state.livecopy.dst_fd, block * FM_READ_BLOCK_SIZE + i);
                break;
            }
        }
        if (!match)
        {
            if (write(g_state.livecopy.dst_fd, src_buf + i, nr_src - i) < 0)
            {
                if (g_state.file_write_err == false)
                {
                    g_state.file_write_err = true;
                    LOGE("Error writing to file %s", strerror(errno));
                }
            }
        }
        block++;

        if (restart == 1)
        {
            // Restart of original file detected
            match = 0;
            LOGI("Restarting flash log backup file");
            lseek(g_state.livecopy.dst_fd, 0, SEEK_SET);
            ftruncate(g_state.livecopy.dst_fd, 0);
            restart = 0;
        }

    } while (nr_src > 0);
}

static void fm_rotation_stat_callback(struct ev_loop *loop, ev_stat *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;

    fm_do_rotation();
}

static void fm_rotation_timer_callback(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;

    fm_do_rotation();
}

static void fm_livecopy_stat_callback(struct ev_loop *loop, ev_stat *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;

    if (!(g_state.log_options.fm_log_flash) && !(g_state.log_options.fm_log_ramoops))
    {
        return;
    }

    fm_do_livecopy();
}

static void fm_livecopy_timer_callback(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;

    if (!(g_state.log_options.fm_log_flash) && !(g_state.log_options.fm_log_ramoops))
    {
        return;
    }

    fm_do_livecopy();
}

static void fm_backtrace_do_rotation(void)
{
    char shell_cmd[256];

    snprintf(
            shell_cmd,
            sizeof(shell_cmd),
            "sh %s/scripts/fm_crash_bt_rotate.sh %s %s %s %d",
            CONFIG_INSTALL_PREFIX,
            CONFIG_FM_BT_PATH,
            CONFIG_FM_CRASH_COUNT_PATH,
            CONFIG_FM_LOG_CRASH_ARCHIVE_PATH,
            CONFIG_FM_CRASH_FILE_COUNT);

    LOGI("Execute shell command: %s", shell_cmd);
    cmd_log(shell_cmd);
}

static void fm_crash_bt_rotation_stat_callback(struct ev_loop *loop, ev_stat *watcher, int revents)
{
    (void)loop;
    (void)watcher;
    (void)revents;

    fm_backtrace_do_rotation();
}

static void fm_crash_bt_update_log_options(const fm_log_type_t options)
{
    g_state.log_options.fm_log_flash = options.fm_log_flash;
    g_state.log_options.fm_log_ramoops = options.fm_log_ramoops;

    LOGI("rotate flash logging: %s", g_state.log_options.fm_log_flash ? "enabled" : "disabled");
    LOGI("rotate ramoops logging: %s", g_state.log_options.fm_log_ramoops ? "enabled" : "disabled");
}

/******************************************************************************
 *  PUBLIC API definitions
 *****************************************************************************/

int fm_get_persistent(char *buf, const int buf_size)
{
    int ret = 0;
    int nbytes;
    int fd;

    fd = open(FM_PERSISTENT_STATE, O_RDONLY, NULL);
    if (fd < 0)
    {
        LOGI("Persistent state file not available %s", strerror(errno));
        ret = -1;
        goto exit;
    }

    nbytes = read(fd, buf, buf_size);
    if (nbytes < 0)
    {
        LOGE("Reading from persistent file failed: %s", strerror(errno));
        ret = -2;
    }

exit:
    if (fd > 0)
    {
        close(fd);
    }

    return ret;
}

int fm_set_persistent(const bool persist, const char *buf, const int buf_size)
{
    int ret = 0;
    int nbytes = 0;
    int fd = 0;

    unlink(FM_PERSISTENT_STATE);

    if (!persist)
    {
        goto exit;
    }

    fd = open(FM_PERSISTENT_STATE, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd < 0)
    {
        LOGE("Failed to open persistent file %s", strerror(errno));
        ret = -1;
        goto exit;
    }

    nbytes = write(fd, buf, buf_size);
    if (nbytes != buf_size)
    {
        LOGE("Error writing to persistent file %s nbytes/buf_size [%d/%d]", strerror(errno), nbytes, buf_size);
        ret = -2;
    }

exit:
    if (fd > 0)
    {
        close(fd);
    }

    return ret;
}

static void fm_sync_syslog_dirs(const char *syslog_src, const char *syslog_dst, const int syslog_cnt)
{
    char shell_cmd[256];

    snprintf(
            shell_cmd,
            sizeof(shell_cmd),
            "mv $(ls %s/%s* -r | head -n %d) %s",
            syslog_src,
            CONFIG_FM_LOG_PATH,
            syslog_cnt,
            syslog_dst);

    LOGD("Execute shell command: %s", shell_cmd);
    cmd_log(shell_cmd);
}

void fm_set_logging(const fm_log_type_t options)
{
    char *syslog_src;
    char *syslog_dst;
    int syslog_cnt;
    char shell_cmd[256];

    g_state.log_options.fm_log_flash = false;
    g_state.log_options.fm_log_ramoops = false;

    fm_crash_bt_update_log_options(options);

    // Sync syslog directories
    if (g_state.log_options.fm_log_flash)
    {
        syslog_src = CONFIG_FM_LOG_RAM_ARCHIVE_PATH "/" CONFIG_FM_LOG_ARCHIVE_SUBDIRECTORY;
        syslog_dst = CONFIG_FM_LOG_FLASH_ARCHIVE_PATH "/" CONFIG_FM_LOG_ARCHIVE_SUBDIRECTORY;
        syslog_cnt = CONFIG_FM_MAX_ROTATION_SYSLOG_FLASH_CNT;

        fm_sync_syslog_dirs(syslog_src, syslog_dst, syslog_cnt);
    }

    if (g_state.log_options.fm_log_ramoops)
    {
        syslog_src = CONFIG_FM_LOG_FLASH_ARCHIVE_PATH "/" CONFIG_FM_LOG_ARCHIVE_SUBDIRECTORY;
        syslog_dst = CONFIG_FM_LOG_RAM_ARCHIVE_PATH "/" CONFIG_FM_LOG_ARCHIVE_SUBDIRECTORY;
        syslog_cnt = CONFIG_FM_MAX_ROTATION_SYSLOG_RAM_CNT;

        fm_sync_syslog_dirs(syslog_src, syslog_dst, syslog_cnt);
    }

    if (!g_state.log_options.fm_log_flash || !g_state.log_options.fm_log_ramoops)
    {
        snprintf(shell_cmd, sizeof(shell_cmd), "rm %s/*", syslog_src);

        LOGD("Execute shell command: %s", shell_cmd);
        cmd_log(shell_cmd);
    }
}

void fm_event_init(struct ev_loop *loop)
{
    LOGI("Initializing Log Rotate event");

    g_state.file_write_err = false;

    // Init copy data struct
    tailf_open_seek(&g_state.livecopy.src_tf, FM_MESSAGES_ORIGINAL, 0, 1);
    g_state.livecopy.dst_fd = open(FM_MESSAGES_LIVECOPY, O_CREAT | O_RDWR, S_IROTH | S_IRUSR | S_IWUSR | S_IRGRP);
    if (g_state.livecopy.dst_fd < 0)
    {
        LOGE("Failed to open log livecopy file '%s'! - %s", FM_MESSAGES_LIVECOPY, strerror(errno));
        return;
    }

    // Init stat events
    g_state.loop = loop;

    ev_stat_init(&g_state.livecopy_stat, fm_livecopy_stat_callback, FM_MESSAGES_ORIGINAL, 0.0);
    ev_stat_init(&g_state.rotation_stat, fm_rotation_stat_callback, FM_MESSAGES_ORIGINAL_ROTATED, 0.0);

    ev_stat_start(g_state.loop, &g_state.livecopy_stat);
    ev_stat_start(g_state.loop, &g_state.rotation_stat);

    ev_stat_init(&g_state.crash_bt_stat, fm_crash_bt_rotation_stat_callback, FM_CRASH_BT_MESSAGES, 0.0);
    ev_stat_start(g_state.loop, &g_state.crash_bt_stat);

    LOGI("Monitoring file %s", FM_MESSAGES_ORIGINAL);
    LOGI("Monitoring file %s", FM_MESSAGES_ORIGINAL_ROTATED);
    LOGI("Monitoring crash backtrace files %s/crashed_xx", FM_CRASH_BT_MESSAGES);

    // Init timer events (this are here as a backup)
    ev_timer_init(&g_state.livecopy_timer, fm_livecopy_timer_callback, 1.0, FM_PERIODIC_TIMER);
    ev_timer_init(&g_state.rotation_timer, fm_rotation_timer_callback, 1.0, FM_PERIODIC_TIMER);

    ev_timer_start(g_state.loop, &g_state.livecopy_timer);
    ev_timer_start(g_state.loop, &g_state.rotation_timer);
}
