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

/*
 * ===========================================================================
 *  osp_dl implementation using the cURL library
 *
 *  This module uses the cURL multi API to handle download asynchronously.
 *
 *  cURL can still block while resolving hosts via DNS, unless it is compiled
 *  with libc-ares support.
 * ===========================================================================
 */
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>

#include "log.h"
#include "memutil.h"
#include "util.h"
#include "osp_dl.h"
#include "util.h"
#include "target.h"

#define MODULE_ID LOG_MODULE_ID_COMMON

/* Speed limit in bytes/s */
#define OSP_DL_CURL_LOW_SPEED_LIMIT     128
/*
 * If the transfer is below LOW_SPEED_LIMIT for this amount of seconds,
 * abort the connection
 */
#define OSP_DL_CURL_LOW_SPEED_TIME      30

/* Connection timeout in seconds */
#define OSP_DL_CURL_CONNECT_TIMEOUT     30

struct osp_dl_curl;

/* Curl object callbacks */
typedef void osp_dl_curl_dl_fn_t(struct osp_dl_curl *dc, enum osp_dl_status status);

/*
 * Curl I/O callback.
 *
 * This function must return the number of bytes written. If this value is lower
 * than `data_sz` it assumes an error occurred and the download is aborted.
 *
 * Note: data or data_sz may be NULL or 0 respectively when downloading a file
 * of size 0 or when the callee only wishes to query the current file offset.
 *
 * Note: Returning the `off` field is required if you want to supporting download
 * resuming. In other cases this field is optional and must be set to -1 if no
 * offset data is available.
 */
typedef size_t osp_dl_curl_io_fn_t(struct osp_dl_curl *dc, void *data, size_t data_sz, off_t *off);

/*
 * Structure describing a single download
 */
struct osp_dl_curl
{
    intmax_t                dc_id;              /* Instance id, mainly used for logging */
    osp_dl_curl_dl_fn_t    *dc_dl_fn;           /* Download completion callback */
    osp_dl_curl_io_fn_t    *dc_io_fn;           /* Download I/O function */
    char                   *dc_dl_url;          /* Download URL */
    int                     dc_dl_retry;        /* Retry count */
    double                  dc_timeout;         /* Timeout */
    ev_timer                dc_timeout_timer;   /* Timeout timer */
    CURL                   *dc_curl;            /* Curl easy handle */
    CURLM                  *dc_multi;           /* Curl multi handle */
    ev_timer                dc_progress_timer;  /* Progress timer */
    ev_timer                dc_multi_timer;     /* Curl multi timer handler */
    ev_io                   dc_multi_io;        /* Curl multi I/O watcher */
    ev_async                dc_done_async;      /* Download done event */
    ev_async                dc_retry_async;     /* Download retry event */
    enum osp_dl_status      dc_status;          /* Download status */
    void                   *dc_dl_data;         /* Completion callback data */
};

/*
 * Internal structure used by the osp_dl_download() API
 */
struct osp_dl_download
{
    struct osp_dl_curl      dd_curl;
    int                     dd_dst_fd;                      /* Destination file descriptor */
    char                   *dd_dst_path;                    /* Destination path */
    osp_dl_cb               dd_dl_fn;                       /* Download complete callback */
    void                   *dd_dl_data;                     /* Data pointer for the download callback */
    enum osp_dl_status      dd_status;                      /* Download status */
};

/*
 * Main osp_dl API and support functions
 */
static bool osp_dl_download_init_paths(
        struct osp_dl_download *dd,
        const char *url,
        const char *dst_path);

static bool osp_dl_download_init_files(struct osp_dl_download *dd, const char *dst_dir);
static void osp_dl_download_cleanup(struct osp_dl_download *dd);
static osp_dl_curl_dl_fn_t osp_dl_download_fn;
static osp_dl_curl_io_fn_t osp_dl_download_io_fn;

static size_t osp_dl_download_write(
        const char *file,
        int fd,
        void *data,
        size_t data_sz,
        off_t *off);

/*
 * Internmal osp_dl_curl API and support functions
 */
static bool osp_dl_curl_init(struct osp_dl_curl *dc, const char *url, osp_dl_curl_dl_fn_t *fn);
static void osp_dl_curl_fini(struct osp_dl_curl *dc);
static bool osp_dl_curl_start(struct osp_dl_curl *dc);
static void osp_dl_curl_stop(struct osp_dl_curl *dc);
static void osp_dl_curl_timeout_set(struct osp_dl_curl *dc, double timeout);
static void osp_dl_curl_io_set(struct osp_dl_curl *dc, osp_dl_curl_io_fn_t *io_fn);

static void osp_dl_curl_stop_download(struct osp_dl_curl *dc);
static CURL *osp_dl_curl_get_handle(struct osp_dl_curl *dc, const char *url);
static size_t osp_dl_curl_write_fn(char *ptr, size_t size, size_t nmemb, void *userdata);
static int osp_dl_curl_socket_fn(CURL *easy, curl_socket_t sock, int what, void *userp, void *socketp);
static int osp_dl_curl_timer_fn(CURLM *multi, long timeout_ms, void *userp);
static void osp_dl_curl_multi_io_fn(struct ev_loop *loop, ev_io *w, int revent);
static void osp_dl_curl_multi_timer_fn(struct ev_loop *loop, ev_timer *w, int revent);
static void osp_dl_curl_multi_check_done(struct osp_dl_curl *dc);
static void osp_dl_curl_done_async_fn(struct ev_loop *loop, ev_async *w, int revent);
static void osp_dl_curl_retry_async_fn(struct ev_loop *loop, ev_async *w, int revent);
static void osp_dl_curl_timeout_timer_fn(struct ev_loop *loop, ev_timer *w, int revent);
static void osp_dl_curl_progress(struct osp_dl_curl *dc);
static void osp_dl_curl_progress_timer_fn(struct ev_loop *loop, ev_timer *w, int revent);

/*
 * ===========================================================================
 *  Main osp_dl interface API implementation
 * ===========================================================================
 */
bool osp_dl_download(char *url, char *dst_dir, int timeout, osp_dl_cb dl_cb, void *cb_ctx)
{
    struct osp_dl_download *dd;

    dd = CALLOC(1, sizeof(struct osp_dl_download));
    dd->dd_status = OSP_DL_OK;
    dd->dd_dl_fn = dl_cb;
    dd->dd_dl_data = cb_ctx;
    dd->dd_dst_fd = -1;

    if (!osp_dl_download_init_paths(dd, url, dst_dir))
    {
        goto error;
    }

    if (!osp_dl_download_init_files(dd, dst_dir))
    {
        goto error;
    }

    if (!osp_dl_curl_init(&dd->dd_curl, url, osp_dl_download_fn))
    {
        LOG(ERR, "curl: Error initializing download object.");
        goto error;
    }
    osp_dl_curl_timeout_set(&dd->dd_curl, (double)timeout);
    osp_dl_curl_io_set(&dd->dd_curl, osp_dl_download_io_fn);

    LOG(INFO, "Downloading `%s` to local file `%s`",
            url,
            dd->dd_dst_path);

    /* Start asynchronous downloads */
    if (!osp_dl_curl_start(&dd->dd_curl))
    {
        LOG(ERR, "curl: Error starting download of: %s", url);
        goto error;
    }

    return true;

error:
    osp_dl_download_cleanup(dd);
    return false;
}

/*
 * Initialize various path names that are calculated from the url and
 * destination folder.
 */
bool osp_dl_download_init_paths(
        struct osp_dl_download *dd,
        const char *url,
        const char *dst_dir)
{
    size_t dst_sz;
    char *base;

    if (str_endswith(url, "/"))
    {
        LOG(ERR, "curl: Invalid URL `%s` (ends with '/').", url);
        return false;
    }

    /* Extract the file name form the URL */
    char purl[strlen(url) + 1];
    STRSCPY(purl, url);
    base = basename(purl);

    /* Cut out anything after and including '?' */
    (void)strsep(&(char *){base}, "?");

    /* Construct the destination file path */
    dst_sz = strlen(dst_dir) + strlen(base) + strlen("/") + 1;
    dd->dd_dst_path = MALLOC(dst_sz);
    snprintf(dd->dd_dst_path, dst_sz, "%s/%s", dst_dir, base);

    return true;
}

/*
 * Initialize the destination folder and files
 */
bool osp_dl_download_init_files(struct osp_dl_download *dd, const char *dst_dir)
{
    struct stat st;

    /* Create the destination folder, if it doesn't exist */
    if (stat(dst_dir, &st) == 0 && !S_ISDIR(st.st_mode))
    {
        if (unlink(dst_dir) != 0)
        {
            LOG(ERR, "curl: Unable to replace target folder: %s", dst_dir);
            return false;
        }
    }

    /* Create the destination folder */
    if (mkdir(dst_dir, 0700) != 0 && errno != EEXIST)
    {
        LOG(ERR, "curl: Unable to create destination folder: %s", dst_dir);
        return false;
    }

    /*
     * Deleting the file is safer than using O_TRUNC as O_TRUNC may fail if the
     * file is read-only
     */
    (void)unlink(dst_dir);
    dd->dd_dst_fd = open(dd->dd_dst_path, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    if (dd->dd_dst_fd < 0)
    {
        LOG(ERR, "curl: Error creating file %s: %s",
                dst_dir,
                strerror(errno));
        return false;
    }

    return true;
}

void osp_dl_download_cleanup(struct osp_dl_download *dd)
{
    osp_dl_curl_fini(&dd->dd_curl);
    close(dd->dd_dst_fd);

    if (dd->dd_status != OSP_DL_OK)
    {
        (void)unlink(dd->dd_dst_path);
    }

    FREE(dd->dd_dst_path);
    FREE(dd);
}

void osp_dl_download_fn(struct osp_dl_curl *dc, enum osp_dl_status status)
{
    struct osp_dl_download *dd = CONTAINER_OF(dc, struct osp_dl_download, dd_curl);

    dd->dd_status = status;

    dd->dd_dl_fn(dd->dd_status, dd->dd_dl_data);
    osp_dl_download_cleanup(dd);
}

size_t osp_dl_download_io_fn(struct osp_dl_curl *dc, void *data, size_t data_sz, off_t *off)
{
    struct osp_dl_download *dd = CONTAINER_OF(dc, struct osp_dl_download, dd_curl);
    return osp_dl_download_write(dd->dd_dst_path, dd->dd_dst_fd, data, data_sz, off);
}

size_t osp_dl_download_write(
        const char *file,
        int fd,
        void *data,
        size_t data_sz,
        off_t *off)
{
    ssize_t nwr = 0;

    if (data != NULL && data_sz > 0)
    {
        nwr = write(fd, data, data_sz);
        if (nwr < 0)
        {
            LOG(ERR, "curl: Error writing file \"%s\": %s",
                    file,
                    strerror(errno));
            return 0;
        }
        else if ((size_t)nwr < data_sz)
        {
            LOG(WARN, "curl: Error writing file \"%s\": Short write.",
                    file);
        }
    }

    /* Update the offset */
    *off = lseek(fd, 0, SEEK_CUR);

    return nwr;
}

/*
 * ===========================================================================
 *  Internal osp_dl_curl API
 * ===========================================================================
 */
bool osp_dl_curl_init(struct osp_dl_curl *dc, const char *url, osp_dl_curl_dl_fn_t *fn)
{
    static intmax_t global_dc_id = 0;

    memset(dc, 0, sizeof(*dc));
    dc->dc_id = global_dc_id++;
    dc->dc_dl_fn = fn;
    dc->dc_dl_url = STRDUP(url);
    dc->dc_status = OSP_DL_ERROR;

    LOG(NOTICE, "curl[%jd]: Starting download: %s",
            dc->dc_id,
            dc->dc_dl_url);

    /* Completion handler */
    ev_async_init(&dc->dc_done_async, osp_dl_curl_done_async_fn);
    ev_async_start(EV_DEFAULT, &dc->dc_done_async);

    /* Retry handler */
    ev_async_init(&dc->dc_retry_async, osp_dl_curl_retry_async_fn);
    ev_async_start(EV_DEFAULT, &dc->dc_retry_async);

    /* Progress timer */
    ev_timer_init(
            &dc->dc_progress_timer,
            osp_dl_curl_progress_timer_fn,
            CONFIG_OSP_DL_CURL_PROGRESS_INTERVAL,
            CONFIG_OSP_DL_CURL_PROGRESS_INTERVAL);

    return true;
}

bool osp_dl_curl_start(struct osp_dl_curl *dc)
{
    CURLMcode mrc;
    CURLcode erc;

    if (!ev_is_active(&dc->dc_timeout_timer) && dc->dc_timeout > 0.0)
    {
        /* Start the download timeout handler */
        ev_timer_init(
                &dc->dc_timeout_timer,
                osp_dl_curl_timeout_timer_fn,
                (double)dc->dc_timeout, 0.0);
        ev_timer_start(EV_DEFAULT, &dc->dc_timeout_timer);
    }

    /* Acquiare a cURL easy handle -- this will be used for the main download */
    dc->dc_curl = osp_dl_curl_get_handle(dc, dc->dc_dl_url);
    if (dc->dc_curl == NULL)
    {
        goto error;
    }

    /* Install write handlers */
    erc = curl_easy_setopt(dc->dc_curl, CURLOPT_WRITEFUNCTION, osp_dl_curl_write_fn);
    if (erc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error setting write handler: %s",
                dc->dc_id,
                curl_easy_strerror(erc));
        goto error;
    }

    erc = curl_easy_setopt(dc->dc_curl, CURLOPT_WRITEDATA, dc);
    if (erc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error setting write data: %s",
                dc->dc_id,
                curl_easy_strerror(erc));
        goto error;
    }

    /*
     * Depending on the current file offset, resume the transfer
     */
    off_t off = 0;
    if (dc->dc_io_fn != NULL)
    {
        dc->dc_io_fn(dc, NULL, 0, &off);
    }

    if (off > 0)
    {
        LOG(INFO, "curl[%jd]: Restarting transfer of '%s' at offset %jd.",
                dc->dc_id,
                dc->dc_dl_url,
                (intmax_t)off);

        erc = curl_easy_setopt(dc->dc_curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)off);
        if (erc != CURLE_OK)
        {
            LOG(ERR, "curl[%jd]: Error setting resume from offset %jd.",
                    dc->dc_id,
                    (intmax_t)off);
            goto error;
        }
    }

    /*
     * Create a cURL multi handle -- multi handles are the only way cURL can
     * work truly asynchronously.
     */
    dc->dc_multi = curl_multi_init();
    if (dc->dc_multi == NULL)
    {
        LOG(ERR, "curl[%jd]: Error creating cURL multi-handle.", dc->dc_id);
        goto error;
    }

    mrc = curl_multi_add_handle(dc->dc_multi, dc->dc_curl);
    if (mrc != CURLM_OK)
    {
        LOG(ERR, "curl[%jd]: Error adding cURL easy-handle to the multi-handle object: %s",
                dc->dc_id,
                curl_multi_strerror(mrc));
        goto error;
    }

    /* Install socket handlers */
    mrc = curl_multi_setopt(dc->dc_multi, CURLMOPT_SOCKETFUNCTION, osp_dl_curl_socket_fn);
    if (mrc != CURLM_OK)
    {
        LOG(ERR, "curl[%jd]: Error assigning multi socket callback: %s",
                dc->dc_id,
                curl_multi_strerror(mrc));
        goto error;
    }

    mrc = curl_multi_setopt(dc->dc_multi, CURLMOPT_SOCKETDATA, dc);
    if (mrc != CURLM_OK)
    {
        LOG(ERR, "curl[%jd]: Error assigning multi socket data: %s",
                dc->dc_id,
                curl_multi_strerror(mrc));
        goto error;
    }

    mrc = curl_multi_setopt(dc->dc_multi, CURLMOPT_TIMERFUNCTION, osp_dl_curl_timer_fn);
    if (mrc != CURLM_OK)
    {
        LOG(ERR, "curl[%jd]: Error assigning multi timer callback: %s",
                dc->dc_id,
                curl_multi_strerror(mrc));
        goto error;
    }

    mrc = curl_multi_setopt(dc->dc_multi, CURLMOPT_TIMERDATA, dc);
    if (mrc != CURLM_OK)
    {
        LOG(ERR, "curl[%jd]: Error assigning multi timer data: %s",
                dc->dc_id,
                curl_multi_strerror(mrc));
        goto error;
    }

    /* Kick-off the transfer */
    mrc = curl_multi_socket_action(dc->dc_multi, CURL_SOCKET_TIMEOUT, 0, &(int){ 0 });
    if (mrc != CURLM_OK)
    {
        LOG(ERR, "curl[%jd]: Error starting transfer: %s",
                dc->dc_id,
                curl_multi_strerror(mrc));
        goto error;
    }

    /* Start the progress timer */
    if (CONFIG_OSP_DL_CURL_PROGRESS_INTERVAL > 0)
    {
        ev_timer_start(EV_DEFAULT, &dc->dc_progress_timer);
    }

    return true;

error:
    osp_dl_curl_stop(dc);
    return false;
}

void osp_dl_curl_stop_download(struct osp_dl_curl *dc)
{
    ev_timer_stop(EV_DEFAULT, &dc->dc_progress_timer);
    ev_io_stop(EV_DEFAULT, &dc->dc_multi_io);
    ev_timer_stop(EV_DEFAULT, &dc->dc_multi_timer);

    /*
     * The proper teardown sequence of a multi curl handle is as follows:
     *
     * - remove all easy handles using curl_multi_remove_handle()
     * - destroy the multi handle using curl_multi_cleanup()
     * - destroy individual easy handles using curl_easy_cleanup()
     */
    if (dc->dc_multi != NULL)
    {
        /*
         * The easy handle (dc_curl) must exists before the multi handle
         * (dc_multi) can even be created. It is safe to assume that
         * dc->dc_curl exists if dc->dc_multi is not NULL.
         */
        curl_multi_remove_handle(dc->dc_multi, dc->dc_curl);
        curl_multi_cleanup(dc->dc_multi);
        dc->dc_multi = NULL;
    }

    if (dc->dc_curl != NULL)
    {
        curl_easy_cleanup(dc->dc_curl);
        dc->dc_curl = NULL;
    }

}

void osp_dl_curl_stop(struct osp_dl_curl *dc)
{
    osp_dl_curl_stop_download(dc);
    ev_async_stop(EV_DEFAULT, &dc->dc_done_async);
    ev_async_stop(EV_DEFAULT, &dc->dc_retry_async);
    ev_timer_stop(EV_DEFAULT, &dc->dc_timeout_timer);
}

void osp_dl_curl_fini(struct osp_dl_curl *dc)
{
    osp_dl_curl_stop(dc);
    FREE(dc->dc_dl_url);
}

void osp_dl_curl_timeout_set(struct osp_dl_curl *dc, double timeout)
{
    dc->dc_timeout = timeout;
}

void osp_dl_curl_io_set(struct osp_dl_curl *dc, osp_dl_curl_io_fn_t *io_fn)
{
    dc->dc_io_fn = io_fn;
}

/*
 * ===========================================================================
 *  osp_dl_curl support functions
 * ===========================================================================
 */
/*
 * Acquire a cURL handle for url @p url
 */
CURL *osp_dl_curl_get_handle(struct osp_dl_curl *dc, const char *url)
{
    CURLcode rc;

    static bool once = true;
    CURL *curl = NULL;

    if (once)
    {
        /* Initialize curl exactly once */
        rc = curl_global_init(CURL_GLOBAL_ALL);
        if (rc != CURLE_OK)
        {
            LOG(ERR, "curl[%jd]: Error initializing CURL library: %s",
                    dc->dc_id,
                    curl_easy_strerror(rc));
            goto error;
        }

        once = false;
    }

    /* Create new cURL handle */
    curl = curl_easy_init();
    if (curl == NULL)
    {
        LOG(ERR, "curl[%jd]: Error initializing handle.\n", dc->dc_id);
        goto error;
    }

    /* Set the URL to the file being downloaded */
    rc = curl_easy_setopt(curl, CURLOPT_URL, url);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error setting URL to %s: %s",
                dc->dc_id,
                url,
                curl_easy_strerror(rc));
        goto error;
    }

    /* Allow resolving hostnames to both IPv4 and IPv6. */
    rc = curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error enabling dual-stack: %s",
                dc->dc_id,
               curl_easy_strerror(rc));
        goto error;
    }

    /*
     * This option is required in order for cURL to function properly with multiple threads.
     * However, this means that DNS lookup will not handle timeouts properly.
     *
     * A workaround for this (according to cURL documentation) is to build cURL with c-ares support.
     */
    rc = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 0);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Unable to set the NOSIGNAL option: %s",
                dc->dc_id,
                curl_easy_strerror(rc));
        goto error;
    }

    /*
     * Enforce TLSv1.2 or later
     */
    rc = curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Unable to set the CURLOPT_SSLVERSION option to TLSv1_2: %s",
                dc->dc_id,
                curl_easy_strerror(rc));
        goto error;
    }

    /* Enable keep-alive */
    rc = curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error enabling keep-alive: %s",
                dc->dc_id,
                curl_easy_strerror(rc));
        goto error;
    }

    /* Seconds before starting keep-alive probes */
    rc = curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 10L);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error setting CURLOPT_TCP_KEEPDILE: %s",
                dc->dc_id,
                curl_easy_strerror(rc));
        goto error;
    }

    /* Keep-alive interval */
    rc = curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error setting the keep-alive interval: %s",
                dc->dc_id,
                curl_easy_strerror(rc));
        goto error;
    }

    /* Fail on 4xx errors */
    rc = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error setting the fail-on-error flag: %s",
                dc->dc_id,
                curl_easy_strerror(rc));
        goto error;
    }

    /* Enable all supported built-in encodings (including compression) */
    rc = curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error enabling compression: %s",
                dc->dc_id,
                curl_easy_strerror(rc));
        goto error;
    }
    /* Set the certificate authority file */
    if(access(target_opensync_ca_filename(), F_OK) != 0 )
    {
        rc = curl_easy_setopt(curl, CURLOPT_CAINFO, target_opensync_ca_filename());
        if (rc != CURLE_OK)
        {
            LOG(ERR, "curl[%jd]: Error setting the CA file: %s",
                    dc->dc_id,
                    curl_easy_strerror(rc));
            goto error;
        }
    }

#if defined(CONFIG_OSP_DL_CURL_ALLOW_UNTRUSTED_CONNECTIONS)
    rc = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error setting insecure SSL option: %s",
                dc->dc_id,
                curl_easy_strerror(rc));
        goto error;
    }
#endif

#if defined(CONFIG_OSP_DL_CURL_DETECT_STALLED)
    rc = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, OSP_DL_CURL_CONNECT_TIMEOUT);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error setting connection timeout option: %s",
                dc->dc_id,
                curl_easy_strerror(rc));
        goto error;
    }

    rc = curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, OSP_DL_CURL_LOW_SPEED_LIMIT);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error setting low speed limit option: %s",
                dc->dc_id,
                curl_easy_strerror(rc));
        goto error;
    }

    rc = curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, OSP_DL_CURL_LOW_SPEED_TIME);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "curl[%jd]: Error setting low speed time option: %s",
                dc->dc_id,
                curl_easy_strerror(rc));
        goto error;
    }
#endif

    return curl;

error:
    if (curl != NULL)
    {
        curl_easy_cleanup(curl);
    }

    return NULL;
}

/*
 * CURLOPT_WRITEFUNCTION callback
 */
size_t osp_dl_curl_write_fn(
        char *ptr,
        size_t size,
        size_t nmemb,
        void *userdata)
{
    struct osp_dl_curl *dc = userdata;

    if (dc->dc_io_fn == NULL)
    {
        /* If no I/O function is set just return success */
        return size * nmemb;
    }

    return dc->dc_io_fn(dc, ptr, size * nmemb, &(off_t){ 0});
}

/*
 * CURLMOPT_SOCKETFUNCTION callback -- this callback is used to set/unset
 * socket actions.
 */
int osp_dl_curl_socket_fn(
        CURL *easy,
        curl_socket_t sock,
        int what,
        void *userp,
        void *socketp)
{
    (void)easy;
    (void)sock;
    (void)userp;
    (void)socketp;

    struct osp_dl_curl *dc = userp;
    int revent = 0;

    ev_io_stop(EV_DEFAULT, &dc->dc_multi_io);

    switch (what)
    {
        case CURL_POLL_IN:
            revent = EV_READ;
            break;

        case CURL_POLL_OUT:
            revent = EV_WRITE;
            break;

        case CURL_POLL_INOUT:
            revent = EV_READ | EV_WRITE;
            break;

        case CURL_POLL_REMOVE:
            return 0;

        default:
            return -1;
    }

    ev_io_init(&dc->dc_multi_io, osp_dl_curl_multi_io_fn, sock, revent);
    ev_io_start(EV_DEFAULT, &dc->dc_multi_io);

    return 0;
}

/*
 * CURLMOPT_TIMERFUNCTION function callback -- handle transfer timeouts
 */
static int osp_dl_curl_timer_fn(
        CURLM *multi,
        long timeout_ms,
        void *userp)
{
    (void)multi;

    struct osp_dl_curl *dc = userp;

    ev_timer_stop(EV_DEFAULT, &dc->dc_multi_timer);

    if (timeout_ms < 0) return 0;

    ev_timer_init(&dc->dc_multi_timer, osp_dl_curl_multi_timer_fn, (double)timeout_ms / 1000.0, 0.0);
    ev_timer_start(EV_DEFAULT, &dc->dc_multi_timer);

    return 0;
}

void osp_dl_curl_multi_io_fn(struct ev_loop *loop, ev_io *w, int revent)
{
    (void)loop;

    CURLMcode mrc;
    int nrunning;

    struct osp_dl_curl *dc = CONTAINER_OF(w, struct osp_dl_curl, dc_multi_io);
    int curl_evmask = 0;

    curl_evmask |= (revent & EV_READ) ? CURL_CSELECT_IN : 0;
    curl_evmask |= (revent & EV_WRITE) ? CURL_CSELECT_OUT : 0;
    curl_evmask |= (revent & EV_ERROR) ? CURL_CSELECT_ERR : 0;

    mrc = curl_multi_socket_action(dc->dc_multi, w->fd, curl_evmask, &nrunning);
    if (mrc != CURLM_OK)
    {
        LOG(WARN , "curl[%jd]: Error executing socket action: %s",
                dc->dc_id,
                curl_multi_strerror(mrc));
        return;
    }

    osp_dl_curl_multi_check_done(dc);
}

void osp_dl_curl_multi_timer_fn(struct ev_loop *loop, ev_timer *w, int revent)
{
    (void)loop;
    (void)revent;

    int nrunning;
    CURLMcode mrc;

    struct osp_dl_curl *dc = CONTAINER_OF(w, struct osp_dl_curl, dc_multi_timer);

    mrc = curl_multi_socket_action(dc->dc_multi, CURL_SOCKET_TIMEOUT, 0, &nrunning);
    if (mrc != CURLM_OK)
    {
        LOG(ERR, "curl[%jd]: Error executing timer action: %s",
                dc->dc_id,
                curl_multi_strerror(mrc));
        return;
    }

    osp_dl_curl_multi_check_done(dc);
}

void osp_dl_curl_multi_check_done(struct osp_dl_curl *dc)
{
    CURLMsg *msg;

    while ((msg = curl_multi_info_read(dc->dc_multi, &(int){ 0 })))
    {
        if (msg->msg != CURLMSG_DONE) continue;

        osp_dl_curl_progress(dc);
        if (msg->data.result == CURLE_OK)
        {
            dc->dc_status = OSP_DL_OK;
            LOG(NOTICE, "curl[%jd]: Transfer complete: %s\n",
                    dc->dc_id,
                    dc->dc_dl_url);
        }
        else
        {
            dc->dc_status = OSP_DL_DOWNLOAD_FAILED;
            LOG(ERR, "curl[%jd]: Transfer error %s, code %d: %s\n",
                    dc->dc_id,
                    dc->dc_dl_url,
                    msg->data.result,
                    curl_easy_strerror(msg->data.result));
        }

        /* Retry handling */
        if (dc->dc_status != OSP_DL_OK && dc->dc_dl_retry < CONFIG_OSP_DL_CURL_RETRY)
        {
            dc->dc_dl_retry++;
            LOG(NOTICE, "curl[%jd]: Download retry %s [%d/%d].",
                    dc->dc_id,
                    dc->dc_dl_url,
                    dc->dc_dl_retry,
                    CONFIG_OSP_DL_CURL_RETRY);
            ev_async_send(EV_DEFAULT, &dc->dc_retry_async);
            return;
        }

        /*
         * It is probably not safe to destroy all the curl objects within this
         * callback so schedule an async event to do that.
         */
        ev_async_send(EV_DEFAULT, &dc->dc_done_async);
    }
}

void osp_dl_curl_done_async_fn(struct ev_loop *loop, ev_async *w, int revent)
{
    (void)loop;
    (void)revent;

    struct osp_dl_curl *dc = CONTAINER_OF(w, struct osp_dl_curl, dc_done_async);

    /* Stop the current transfer and related handlers */
    osp_dl_curl_stop(dc);

    /* Invoke the end of transfer handler */
    dc->dc_dl_fn(dc, dc->dc_status);
}

void osp_dl_curl_retry_async_fn(struct ev_loop *loop, ev_async *w, int revent)
{
    (void)loop;
    (void)revent;

    struct osp_dl_curl *dc = CONTAINER_OF(w, struct osp_dl_curl, dc_retry_async);

    osp_dl_curl_stop_download(dc);
    if (!osp_dl_curl_start(dc))
    {
        LOG(ERR, "curl[%jd]: Error restarting connection for URL: %s",
                dc->dc_id,
                dc->dc_dl_url);
        /* Nothing else we can do, just tear down everything */
        dc->dc_status = OSP_DL_ERROR;
        ev_async_send(EV_DEFAULT, &dc->dc_done_async);
    }
}

void osp_dl_curl_timeout_timer_fn(struct ev_loop *loop, ev_timer *w, int revent)
{
    (void)loop;
    (void)revent;

    struct osp_dl_curl *dc = CONTAINER_OF(w, struct osp_dl_curl, dc_timeout_timer);

    LOG(ERR, "curl[%jd]: Download timed-out: %s. Stopping transfer.",
            dc->dc_id,
            dc->dc_dl_url);

    dc->dc_status = OSP_DL_ERROR;
    ev_async_send(EV_DEFAULT, &dc->dc_done_async);
}

void osp_dl_curl_progress(struct osp_dl_curl *dc)
{
    #if LIBCURL_VERSION_NUM >= 0x073700 /* version 7.55 */
    curl_off_t content_len;
    curl_off_t dl_size;
    curl_off_t dl_speed;
    #else
    double content_len;
    double dl_size;
    double dl_speed;
    #endif
    CURLcode erc;

    int prc_done = -1;

    #if LIBCURL_VERSION_NUM >= 0x073700 /* version 7.55 */
        erc = curl_easy_getinfo(dc->dc_curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_len);
    #else
        erc = curl_easy_getinfo(dc->dc_curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_len);
    #endif
    if (erc != CURLE_OK)
    {
        content_len = -1.0;
    }

    #if LIBCURL_VERSION_NUM >= 0x073700 /* version 7.55 */
        erc = curl_easy_getinfo(dc->dc_curl, CURLINFO_SIZE_DOWNLOAD_T, &dl_size);
    #else
        erc = curl_easy_getinfo(dc->dc_curl, CURLINFO_SIZE_DOWNLOAD, &dl_size);
    #endif
    if (erc != CURLE_OK)
    {
        dl_size = -1.0;
    }

    #if LIBCURL_VERSION_NUM >= CURL_V_7_55_0
        erc = curl_easy_getinfo(dc->dc_curl, CURLINFO_SPEED_DOWNLOAD_T, &dl_speed);
    #else
        erc = curl_easy_getinfo(dc->dc_curl, CURLINFO_SPEED_DOWNLOAD, &dl_speed);
    #endif
    if (erc != CURLE_OK)
    {
        dl_speed = -1.0;
    }

    if (dl_size > 0.0 && content_len > 0.0)
    {
        prc_done = (int)100.0 * dl_size / content_len;
    }

    LOG(INFO, "curl[%jd] %3d%% %.0fkb @ %0.2fkb/s",
            dc->dc_id,
            prc_done,
            dl_size / 1024.0,
            (double)dl_speed / 1024.0);
}

void osp_dl_curl_progress_timer_fn(struct ev_loop *loop, ev_timer *w, int revent)
{
    (void)loop;
    (void)revent;

    struct osp_dl_curl *dc = CONTAINER_OF(w, struct osp_dl_curl, dc_progress_timer);

    osp_dl_curl_progress(dc);
}
