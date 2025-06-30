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
 *  osp_upg API generic implementation
 *
 *  This implementation uses a set of scripts to upgrade the system. The
 *  scripts are configurable via Kconfig.
 * ===========================================================================
 */
#include <sys/statvfs.h>

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>

#include "const.h"
#include "execsh.h"
#include "kconfig.h"
#include "log.h"
#include "osp_dl.h"
#include "util.h"

#include "osp_upg.h"

#if defined(CONFIG_OSP_UPG_CRC_NONE)
#define OSP_UPG_GEN_CRC_EXT     ".0"
#endif

#if defined(CONFIG_OSP_UPG_CRC_MD5)
#include <openssl/evp.h>
#include <openssl/md5.h>
#define OSP_UPG_GEN_CRC_EXT     ".md5"
#define OSP_UPG_GEN_MD5_HEX_LEN (MD5_DIGEST_LENGTH * 2 + 1)
#endif

#if !defined(OSP_UPG_GEN_CRC_EXT)
#error Unsupported download integrity verification method.
#endif

/* Current error status */
static osp_upg_status_t g_osp_upg_gen_errno = OSP_UPG_OK;

/* True whenever the upgrade system is busy (downloading or upgrading) */
static bool g_osp_upg_gen_in_progress = false;
/* Download completion status */
static bool g_osp_upg_gen_download_complete = false;
static enum osp_dl_status g_osp_upg_gen_download_status = OSP_DL_ERROR;
static bool g_osp_upg_gen_crc_download_complete = false;
static enum osp_dl_status g_osp_upg_gen_crc_download_status = OSP_DL_ERROR;

/* Download callback */
static osp_upg_cb g_osp_upg_gen_fn = NULL;


/* Download paths */
static char g_osp_upg_gen_img_path[C_MAXPATH_LEN];
static char g_osp_upg_gen_crc_path[C_MAXPATH_LEN + sizeof(OSP_UPG_GEN_CRC_EXT) - 1];

/* Password */
static char *g_osp_upg_gen_password = NULL;

/* Execsh object for executing external scripts */
static execsh_async_t g_osp_upg_gen_execsh;

static bool osp_upg_gen_crc_url(char *crc_url, size_t crc_sz, const char *url);
static void osp_upg_gen_download_fn(const enum osp_dl_status status, void *data);
static void osp_upg_gen_crc_download_fn(const enum osp_dl_status status, void *data);
static void osp_upg_gen_download_check(void);
static bool osp_upg_gen_filename_from_url(const char *url);
static bool osp_upg_gen_download_verify(void);
static execsh_async_fn_t osp_upg_gen_execsh_fn;
static execsh_async_io_fn_t osp_upg_gen_execsh_io_fn;

/*
 * ===========================================================================
 *  osp_upg API implementation
 * ===========================================================================
 */
bool osp_upg_check_system(void)
{
    if (g_osp_upg_gen_in_progress)
    {
        g_osp_upg_gen_errno = OSP_UPG_SU_RUN;
        return false;
    }

    /* Check available space on the filesystem where the download folder is located */
    if (CONFIG_OSP_UPG_GEN_FREE_SPACE > 0)
    {
        struct statvfs fs;

        char fspath[] = CONFIG_OSP_UPG_GEN_DIR;
        char *fsdir = dirname(fspath);

        if (statvfs(fsdir, &fs) == 0)
        {
            intmax_t fsfree = fs.f_bsize * fs.f_bavail / 1024;
            if (fsfree < CONFIG_OSP_UPG_GEN_FREE_SPACE)
            {
                LOG(ERR, "osp_upg_gen: Filesystem '%s' is low on space (available %jdkb, required %jdkb). Refusing upgrade.",
                        fsdir,
                        fsfree,
                        (intmax_t)CONFIG_OSP_UPG_GEN_FREE_SPACE);
                g_osp_upg_gen_errno = OSP_UPG_DL_NOFREE;
                return false;
            }
        }
        else
        {
            LOG(WARN, "osp_upg_gen: Unable to perform free space check on `%s`: %s",
                    fsdir,
                    strerror(errno));
        }
    }

    int rc = EXECSH_LOG(INFO, CONFIG_OSP_UPG_GEN_CHECKSYSTEM_SCRIPT);
    if (rc < 0 || rc > OSP_UPG_LAST_ERROR)
    {
        LOG(ERR, "osp_upg_gen: System check script returned invalid error code: %d", rc);
        rc = g_osp_upg_gen_errno = OSP_UPG_INTERNAL;
    }
    else if (rc > 0)
    {
        LOG(DEBUG, "osp_upg_gen: System check script returned %d.", rc);
        g_osp_upg_gen_errno = rc;
    }

    return rc == 0;
}

bool osp_upg_dl(char *url, uint32_t timeout, osp_upg_cb dl_cb)
{
    if (g_osp_upg_gen_in_progress)
    {
        g_osp_upg_gen_errno = OSP_UPG_SU_RUN;
        return false;
    }

    /* Reset the error status since it may be left in an error state from previous calls to upg and dl functions */
    g_osp_upg_gen_errno = OSP_UPG_OK;

    char crc_url[strlen(url) + strlen(OSP_UPG_GEN_CRC_EXT) + 1];
    if (!osp_upg_gen_crc_url(crc_url, sizeof(crc_url), url))
    {
        LOG(DEBUG, "osp_upg_gen: Error generating CRC URL from: %s", url);
        g_osp_upg_gen_errno = OSP_UPG_URL;
        return false;
    }

    /* Extract the filename from URL */
    if (!osp_upg_gen_filename_from_url(url))
    {
        LOG(DEBUG, "osp_upg_dl: Error generating image path from URL: %s", url);
        g_osp_upg_gen_errno = OSP_UPG_URL;
        return false;
    }

    LOG(INFO, "osp_upg_gen: Downloading image to %s", g_osp_upg_gen_img_path);

    g_osp_upg_gen_fn = dl_cb;
    g_osp_upg_gen_download_complete = false;

    /* Start download */
    if (!osp_dl_download(url, CONFIG_OSP_UPG_GEN_DIR, timeout, osp_upg_gen_download_fn, NULL))
    {
        g_osp_upg_gen_errno = OSP_UPG_DL_FW;
        return false;
    }

    /* Start CRC/MD5 file download, if enabled */
    if (kconfig_enabled(CONFIG_OSP_UPG_CRC_NONE))
    {
        g_osp_upg_gen_crc_download_complete = true;
        g_osp_upg_gen_crc_download_status = OSP_DL_OK;
    }
    else
    {
        g_osp_upg_gen_crc_download_complete = false;
        if (!osp_dl_download(crc_url, CONFIG_OSP_UPG_GEN_DIR, timeout, osp_upg_gen_crc_download_fn, NULL))
        {
            g_osp_upg_gen_errno = OSP_UPG_DL_MD5;
            return false;
        }
    }

    g_osp_upg_gen_in_progress = true;

    return true;
}

bool osp_upg_upgrade(char *password, osp_upg_cb upg_cb)
{
    if (g_osp_upg_gen_in_progress)
    {
        g_osp_upg_gen_errno = OSP_UPG_SU_RUN;
        return false;
    }

    /* Reset the error status since it may be left in an error state from previous calls to upg and dl functions */
    g_osp_upg_gen_errno = OSP_UPG_OK;

    if (access(g_osp_upg_gen_img_path, R_OK) != 0)
    {
        LOG(DEBUG, "osp_upg_gen: upgrade: Image file does not exist.");
        g_osp_upg_gen_errno = OSP_UPG_IMG_FAIL;
        return false;
    }

    g_osp_upg_gen_fn = upg_cb;
    g_osp_upg_gen_in_progress = true;
    g_osp_upg_gen_password = (password == NULL) ? NULL : STRDUP(password);

    /*
     * Execute process in the background - the password must come last as it may
     * be NULL (NULL can be used to prematurely end the argument list)
     */
    execsh_async_init(&g_osp_upg_gen_execsh, osp_upg_gen_execsh_fn);
    execsh_async_set(&g_osp_upg_gen_execsh, NULL, osp_upg_gen_execsh_io_fn);
    execsh_async_start(
            &g_osp_upg_gen_execsh,
            CONFIG_OSP_UPG_GEN_UPGRADE_SCRIPT,
            g_osp_upg_gen_img_path,
            password);

    return true;
}

bool osp_upg_commit(void)
{
    if (g_osp_upg_gen_in_progress)
    {
        g_osp_upg_gen_errno = OSP_UPG_SU_RUN;
        LOG(DEBUG, "osp_upg_gen: commit: Upgrade already in progress.");
        return false;
    }

    int rc = EXECSH_LOG(INFO, CONFIG_OSP_UPG_GEN_COMMIT_SCRIPT);
    if (rc < 0 || rc > OSP_UPG_LAST_ERROR)
    {
        LOG(ERR, "osp_upg: Commit script returned invalid error code: %d", rc);
        rc = g_osp_upg_gen_errno = OSP_UPG_INTERNAL;
    }
    else if (rc > 0)
    {
        LOG(DEBUG, "osp_upg_gen: Commit script returned %d.", rc);
        g_osp_upg_gen_errno = rc;
    }

    return rc == OSP_UPG_OK;
}

int osp_upg_errno(void)
{
    return g_osp_upg_gen_errno;
}

/*
 * ===========================================================================
 *  Helper functions
 * ===========================================================================
 */
/* Image file download handler */
void osp_upg_gen_download_fn(const enum osp_dl_status status, void *data)
{
    (void)data;

    g_osp_upg_gen_download_status = status;
    g_osp_upg_gen_download_complete = true;

    osp_upg_gen_download_check();
}

/* CRC/MD5 file download handler */
void osp_upg_gen_crc_download_fn(const enum osp_dl_status status, void *data)
{
    (void)data;

    g_osp_upg_gen_crc_download_status = status;
    g_osp_upg_gen_crc_download_complete = true;

    osp_upg_gen_download_check();
}

/*
 * Check for download completion and report proper error status according
 * to the image and MD5 download statuses
 */
void osp_upg_gen_download_check(void)
{
    if (!g_osp_upg_gen_download_complete || !g_osp_upg_gen_crc_download_complete)
    {
        return;
    }

    if (g_osp_upg_gen_download_status != OSP_DL_OK)
    {
        g_osp_upg_gen_errno = OSP_UPG_DL_FW;
    }
    else if (g_osp_upg_gen_crc_download_status != OSP_DL_OK)
    {
        g_osp_upg_gen_errno = OSP_UPG_DL_MD5;
    }
    else
    {
        /*
         * Do the integrity verification only if both the image and the CRC
         * file have been downloaded successfully. If OSP_UPG_CRC_NONE is set,
         * the crc download status code will be set to OK from the start.
         */
        if (!osp_upg_gen_download_verify())
        {
            g_osp_upg_gen_errno = OSP_UPG_MD5_FAIL;
        }
    }

    g_osp_upg_gen_in_progress = false;
    g_osp_upg_gen_fn(OSP_UPG_DL, g_osp_upg_gen_errno, 100);
}

/*
 * Construct the target filename from `url`
 */
bool osp_upg_gen_filename_from_url(const char *url)
{
    char *bname;
    int rc;
    char *delim = "/";

    /* Parse the image filename from the URL */
    if (url == NULL || url[0] == '\0')
    {
        LOG(DEBUG, "osp_upg_url: URL is empty or not set.");
        return false;
    }

    /* Treat this as an invalid URL if it ends in '/' */
    if (str_endswith(url, "/"))
    {
        LOG(DEBUG, "osp_upg_gen: Error: URL ends with '/'.");
        return false;
    }

    char url_buf[strlen(url) + 1];
    STRSCPY(url_buf, url);

    bname = basename(url_buf);

    if (str_endswith(CONFIG_OSP_UPG_GEN_DIR, "/"))
    {
        delim = "";
    }

    rc = snprintf(g_osp_upg_gen_img_path, sizeof(g_osp_upg_gen_img_path), "%s%s%s",
            CONFIG_OSP_UPG_GEN_DIR,
            delim,
            bname);
    if (rc >= (int)sizeof(g_osp_upg_gen_img_path))
    {
        LOG(DEBUG, "osp_upg_gen: Image path too long.");
        return false;
    }

    /* Construct the CRC file path */
    rc = snprintf(g_osp_upg_gen_crc_path, sizeof(g_osp_upg_gen_crc_path), "%s%s",
            g_osp_upg_gen_img_path,
            OSP_UPG_GEN_CRC_EXT);

    return true;
}

/*
 * Derive the verification file URL from the base url `url`. Take into account
 * any parameters that might come after the filename (?& ... etc).
 *
 * For example:
 *
 * http://localhost/test.img?test=5
 *
 * Should yield
 *
 * http://localhost/test.img.md5?test=5
 */
bool osp_upg_gen_crc_url(char *crc_url, size_t crc_sz, const char *url)
{
    int rc;

    char url_buf[strlen(url) + 1];
    STRSCPY(url_buf, url);

    char *purl = url_buf;
    char *url_head = strsep(&purl, "?");
    char *url_trail = strsep(&purl, "?");

    if (url_trail == NULL) url_trail = "";

    rc = snprintf(crc_url, crc_sz, "%s%s%s%s",
            url_head,
            OSP_UPG_GEN_CRC_EXT,
            strlen(url_trail) == 0 ? "" : "?",
            url_trail);
    if ((size_t)rc >= crc_sz)
    {
        return false;
    }

    return true;
}

#if defined(CONFIG_OSP_UPG_CRC_NONE)
bool osp_upg_gen_download_verify(void)
{
    return true;
}
#endif

#if defined(CONFIG_OSP_UPG_CRC_MD5)
bool osp_upg_gen_download_verify(void)
{
    unsigned char img_sum_raw[MD5_DIGEST_LENGTH];
    char md5_sum[OSP_UPG_GEN_MD5_HEX_LEN];
    char img_sum[OSP_UPG_GEN_MD5_HEX_LEN];

    char buf[128];
    ssize_t nrd;

    int imgfd = -1;
    int md5fd = -1;
    bool retval = false;

    /*
     * Open the image file and calculate the CRC
     */
    imgfd = open(g_osp_upg_gen_img_path, O_RDONLY);
    if (imgfd < 0)
    {
        LOG(ERR, "osp_upg_gen: Error opening image file: %s: %s",
                g_osp_upg_gen_img_path,
                strerror(errno));
        goto exit;
    }


#if OPENSSL_VERSION_NUMBER >= 0x030000000  // 3.0.0
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_create();
    const EVP_MD *md = EVP_md5();
    EVP_DigestInit_ex(md_ctx, md, NULL);
#else
    MD5_CTX md5_ctx;
    MD5_Init(&md5_ctx);
#endif
    while ((nrd = read(imgfd, buf, sizeof(buf))) > 0)
    {
#if OPENSSL_VERSION_NUMBER >= 0x030000000  // 3.0.0
        EVP_DigestUpdate(md_ctx, buf, nrd);
#else
        MD5_Update(&md5_ctx, buf, nrd);
#endif
    }
    if (nrd < 0)
    {
        LOG(ERR, "osp_upg_gen: Error reading image file %s: %s",
                g_osp_upg_gen_img_path,
                strerror(errno));
        goto exit;
    }

#if OPENSSL_VERSION_NUMBER >= 0x030000000  // 3.0.0
    EVP_DigestFinal_ex(md_ctx, img_sum_raw, 0);
    EVP_MD_CTX_destroy(md_ctx);
#else
    MD5_Final(img_sum_raw, &md5_ctx);
#endif
    bin2hex(img_sum_raw, MD5_DIGEST_LENGTH, img_sum, sizeof(img_sum));

    /*
     * Open the CRC file and acquire the MD5 hash -- the beginning of the
     * file should contain the hash in hex format.
     */
    md5fd = open(g_osp_upg_gen_crc_path, O_RDONLY);
    if (md5fd < 0)
    {
        LOG(ERR, "osp_upg_gen: Error opening CRC file %s: %s",
                g_osp_upg_gen_crc_path,
                strerror(errno));
        goto exit;
    }

    nrd = read(md5fd, md5_sum, sizeof(md5_sum) - 1);
    if (nrd < 0)
    {
        LOG(ERR, "osp_upg_error: Error reading CRC file %s: %s",
                g_osp_upg_gen_crc_path,
                strerror(errno));
        goto exit;
    }
    md5_sum[nrd] = '\0';

    if (strcasecmp(md5_sum, img_sum) != 0)
    {
        LOG(ERR, "osp_upg_gen: %s MD5 integrity check FAILED (image = %s, md5 = %s)",
                g_osp_upg_gen_img_path,
                img_sum,
                md5_sum);
        goto exit;
    }

    LOG(INFO, "osp_upg_gen: Integrity check OK: %s", g_osp_upg_gen_img_path);

    retval = true;
exit:
    close(imgfd);
    close(md5fd);

    return retval;
}
#endif

/*
 * This function is a direct replacement for the standard execsh logging
 * function. There are two main reasons to do this. The first one is to filter
 * out any passwords that may leak to the log file. The second one is that by
 * default execsh logs with a DEBUG level. Since upgrade is a rather critical
 * component, log the output from all upgrade scripts with an INFO logging level.
 */
 void osp_upg_gen_execsh_io_fn(
        execsh_async_t *esa,
        enum execsh_io type,
        const char *msg)
{
    (void)esa;
    char *ppass;

    char secure_msg[strlen(msg) + 1];
    STRSCPY(secure_msg, msg);

    if (g_osp_upg_gen_password != NULL)
    {
        if ((ppass = strstr(secure_msg, g_osp_upg_gen_password)) != NULL)
        {
            /* Replace the password with '*' */
            memset(ppass, '*', strlen(g_osp_upg_gen_password));
        }
    }

    LOG(INFO, "osp_upg_gen: %s %s",
            type == EXECSH_IO_STDOUT ? ">" : "|",
            secure_msg);
}

/*
 * Asynchronous execution callback for the `upgrade script`
 */
void osp_upg_gen_execsh_fn(execsh_async_t *esa, int exit_status)
{
    /* Clear the password before releasing it */
    if (g_osp_upg_gen_password != NULL)
    {
        memset(g_osp_upg_gen_password, 0, strlen(g_osp_upg_gen_password));
        FREE(g_osp_upg_gen_password);
        g_osp_upg_gen_password = NULL;
    }

    execsh_async_stop(esa);

    g_osp_upg_gen_in_progress = false;

    /* Set the error code from the external script */
    if (exit_status < 0 || exit_status > OSP_UPG_LAST_ERROR)
    {
        LOG(ERR, "osp_upg: Upgrade script returned invalid error code: %d", exit_status);
        exit_status = g_osp_upg_gen_errno = OSP_UPG_INTERNAL;
    }
    else if (exit_status > 0)
    {
        LOG(DEBUG, "osp_upg_gen: Upgrade script returned %d.", exit_status);
        g_osp_upg_gen_errno = exit_status;
    }

    g_osp_upg_gen_fn(OSP_UPG_UPG, exit_status, 100);
}
