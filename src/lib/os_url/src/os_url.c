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

#include <curl/curl.h>
#include <openssl/ssl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "os.h"
#include "log.h"
#include "os_url.h"

#define MODULE_ID LOG_MODULE_ID_COMMON
/**
 * User agent for Chrome 41, speedtest.net seems to reject the default user agent in libcurl
 * (although the "curl" command in Linux works)
 */
#define OS_URL_FAKE_USER_AGENT      "Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/41.0.2227.0 Safari/537.36"
#define OS_AUTHORITY                CONFIG_TARGET_PATH_OPENSYNC_CERTS"/auth.pem"
#define OS_01012016                 (1451606400)    /* Linux epoch 2016-01-01   GMT    */

static CURLcode curl_err;
static const CURLcode curl_stop_list[] =
{
    CURLE_UNSUPPORTED_PROTOCOL,
    CURLE_FAILED_INIT,
    CURLE_URL_MALFORMAT,
    CURLE_NOT_BUILT_IN,
    CURLE_WRITE_ERROR,
    CURLE_READ_ERROR,
    CURLE_OPERATION_TIMEDOUT, /* this is slippery ground */
    CURLE_BAD_DOWNLOAD_RESUME,
    CURLE_FILESIZE_EXCEEDED,
    CURLE_RECV_ERROR,
    CURLE_REMOTE_DISK_FULL,
    CURLE_REMOTE_FILE_EXISTS,
};

/**
 * Enforce libcurl cipher list
 * it is a zero terminated string holding the list of ciphers to use
 * for the SSL connection. The list must be syntactically correct, it
 * consists of one or more cipher strings separated by commas, spaces
 * or colons (preferred). !, -, + can be used as operators
 */
static const char enforce_cipher_list[] =        TLS1_TXT_DHE_DSS_WITH_AES_128_SHA256
                                              ":"TLS1_TXT_DHE_RSA_WITH_AES_128_SHA256
                                              ":"TLS1_TXT_DHE_RSA_WITH_AES_256_SHA256
                                              ":"TLS1_TXT_DHE_RSA_WITH_AES_128_GCM_SHA256
                                              ":"TLS1_TXT_DHE_DSS_WITH_AES_128_GCM_SHA256
                                              ":"TLS1_TXT_ECDHE_ECDSA_WITH_AES_128_SHA256
                                              ":"TLS1_TXT_ECDHE_ECDSA_WITH_AES_256_SHA384
                                              ":"TLS1_TXT_ECDHE_RSA_WITH_AES_128_SHA256
                                              ":"TLS1_TXT_ECDHE_RSA_WITH_AES_256_SHA384
                                              ":"TLS1_TXT_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
                                              ":"TLS1_TXT_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384
                                              ":"TLS1_TXT_ECDHE_RSA_WITH_AES_128_GCM_SHA256
                                              ":"TLS1_TXT_ECDHE_RSA_WITH_AES_256_GCM_SHA384;

/**
 * @file
 *
 * Simple download library based around cURL
 */

static CURL*        os_url_curl_gethandle(char *url);
static bool         os_url_curl_getstats(CURL* curl, struct os_url_stat *stat);


/**
 * Retrieve the URL and use the @p get_fn callback to process the received stream;
 *
 * If @p info is not NULL, return various statistics regarding the transfer
 *
 */
bool os_url_get_ex_gzip(
        char* url,
        int timeout,
        os_url_get_fn *get_fn,
        void *get_ctx,
        struct os_url_stat *stat,
        long resumefrom,
        bool insecure, bool gzip)
{
    CURLcode        rc;
    bool            status = false;
    CURL*           curl = NULL;

    curl_err = CURLE_OK;
    struct curl_slist *slist = NULL;

    curl = os_url_curl_gethandle(url);
    if (curl == NULL)
    {
        return false;
    }

    /* Setup timeout */
    rc = curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting timeout: %d.", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_TIMEOUT: %d.", timeout);
    }

    /* setup keep alive timeout  */
    rc = curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting CURLOPT_TCP_KEEPALIVE,: %d.", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_TCP_KEEPALIVE: %d.", 1);
    }

    rc = curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 10L);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting CURLOPT_TCP_KEEPIDLE: %d.", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_TCP_KEEPIDLE: %d.", 10);
    }

    rc = curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL , 15L);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting CURLOPT_TCP_KEEPINTVL: %d.", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_TCP_KEEPINTVL: %d.", 15);
    }

    /* Setup writers */
    rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, get_fn);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting the writer function: %d.", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_WRITEFUNCTION");
    }

    /* Set the context data, this will be passed to @p get_fn */
    rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, get_ctx);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting write data: %d.", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_WRITEDATA");
    }

    /* Set the fail on error for 4xx error   */
    rc = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting fail on error: %d.\n", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_FAILONERROR");
    }

    /* Enforce cipher list */
    rc = curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, enforce_cipher_list);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error enforcing cipher list: %d.\n", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_SSL_CIPHER_LIST");
    }

    LOG(DEBUG, "cURL: Secure transfer.");
    /* Set certificate authority file */
    rc = curl_easy_setopt(curl, CURLOPT_CAINFO, OS_AUTHORITY);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting authority storage: %d.\n", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_CAINFO");
    }

    /* Resume operation at given byte index, in case byte index > 0 */
    if (resumefrom > 0)
    {
        rc = curl_easy_setopt(curl, CURLOPT_RESUME_FROM, resumefrom);
        if (rc != CURLE_OK)
        {
            LOG(ERR, "cURL: Error setting offset to resume from: %d.\n", rc);
            goto exit;
        }
        else
        {
            LOG(DEBUG, "cURL: setopt CURLOPT_RESUME_FROM");
        }
    }

    if (insecure)
    {
        rc = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        if (rc != CURLE_OK)
        {
            LOG(ERR, "cURL: Error setting insecure SSL option: %d.\n", rc);
            goto exit;
        }
        else
        {
            LOG(DEBUG, "cURL: setopt CURLOPT_SSL_VERIFYPEER");
        }
    }

    if (gzip)
    {
        slist = curl_slist_append(slist, "accept-encoding: gzip");
        if (slist == NULL)
        {
            LOG(ERR, "curl_slist_append");
            goto exit;
        }
        rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
        if (rc != CURLE_OK)
        {
            LOG(ERR, "Error setting CURLOPT_HTTPHEADER");
            goto exit;
        }
        else
        {
            LOG(DEBUG, "cURL: setopt CURLOPT_HTTPHEADER: accept-encoding: gzip");
        }
    }

    /* Do it! */
    LOG(DEBUG, "Fetching URL: %s\n", url);
    rc = curl_easy_perform(curl);
    if (rc != CURLE_OK)
    {
        if (rc == CURLE_OPERATION_TIMEDOUT) {
            LOG(ERR, "cURL: Timed Out downloading: %d.\n", rc);
            status = false;
        }
        else {
            LOG(ERR, "cURL: Error downloading: %d.\n", rc);
        }
        goto exit;
    }

    if (stat != NULL)
    {
        os_url_curl_getstats(curl, stat);
    }

    status = true;

exit:
    curl_err = rc;
    if (slist != NULL)
    {
        curl_slist_free_all(slist);
    }
    if (curl != NULL)
    {
        curl_easy_cleanup(curl);
    }

    return status;
}

bool os_url_get_ex(
        char* url,
        int timeout,
        os_url_get_fn *get_fn,
        void *get_ctx,
        struct os_url_stat *stat,
        long resumefrom,
        bool insecure)
{
    return os_url_get_ex_gzip(
            url,
            timeout,
            get_fn,
            get_ctx,
            stat,
            resumefrom,
            insecure,
            false);
}

/**
 * Upload data to the URL and use the @p put_fn callback to feed it data
 *
 * If @p info is not NULL, return various statistics regarding the transfer
 *
 */
bool os_url_put_ex(char* url, int timeout, os_url_put_fn *put_fn, void *put_ctx, size_t size, struct os_url_stat *stat)
{
    CURLcode        rc;

    bool                status = false;
    CURL               *curl = NULL;
    struct curl_slist  *headers = NULL;

    curl_err = CURLE_OK;

    curl = os_url_curl_gethandle(url);
    if (curl == NULL)
    {
        return false;
    }

    /* Notify cURL that we're doing an upload */
    rc = curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting URL: %d.\n", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_UPLOAD");
    }

    /* Set the the "Expect:" header, otherwise CURL waits 1 second for a response */

    headers = curl_slist_append(NULL, "Expect:");
    rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting HTTP headers: %d.\n", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_HTTPHEADER");
    }

    /* Setup timeout */
    rc = curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting timeout: %d.\n", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_TIMEOUT");
    }

    /* Setup data feeders */
    rc = curl_easy_setopt(curl, CURLOPT_READFUNCTION, put_fn);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting the writer function: %d.\n", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_READFUNCTION");
    }

    /* Set the context data, this will be passed to @p put_fn */
    rc = curl_easy_setopt(curl, CURLOPT_READDATA, put_ctx);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting write data: %d.\n", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_READDATA");
    }

    /* Set the receive(write) function to NULL otherwise the output will end up on STDOUT */
    rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, os_url_get_null_fn);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting the writer function: %d.\n", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_WRITEFUNCTION");
    }

    /* Set the upload file size */
    rc = curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)size);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting URL: %d.\n", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_INFILESIZE_LARGE");
    }

    /* Enforce cipher list */
    rc = curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, enforce_cipher_list);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error enforcing cipher list: %d.\n", rc);
        goto exit;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_SSL_CIPHER_LIST");
    }

    /* Do it! */
    LOG(DEBUG, "Putting URL: %s\n", url);

    curl_err = rc = curl_easy_perform(curl);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error uploading: %d.\n", rc);
        goto exit;
    }

    if (stat != NULL)
    {
        os_url_curl_getstats(curl, stat);
    }

    status = true;

exit:
    curl_err = rc;
    if (curl != NULL)
    {
        curl_easy_cleanup(curl);
    }

    if (headers != NULL)
    {
        curl_slist_free_all(headers);
    }

    return status;
}

/**
 * Initialize cURL exactly once
 */
CURL* os_url_curl_gethandle(char *url)
{
    CURLcode    rc;
    CURL*       curl = NULL;

    static task_once_t os_curl_once = TASK_ONCE_INIT;

    /* Initialize CURL only once */
    if (task_once(&os_curl_once))
    {
        LOG(DEBUG, "Initializing cURL.");
        curl_global_init(CURL_GLOBAL_ALL);
    }

    /* Create new cURL handle */
    curl = curl_easy_init();
    if (curl == NULL)
    {
        LOG(ERR, "cURL: Error initializing handle.\n");
        goto error;
    }

    /* Set the URL to the file being downloaded */
    rc = curl_easy_setopt(curl, CURLOPT_URL, url);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting URL: %d.\n", rc);
        goto error;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_URL");
    }

    /* Allow resolving hostnames to both IPv4 and IPv6. */
    rc = curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting IP protocol version: %d.\n", rc);
        goto error;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_IPRESOLVE");
    }

#ifdef OS_URL_FAKE_USER_AGENT
    /* Fake the user agent, some sites (speedtest.net) need this */
    rc = curl_easy_setopt(curl, CURLOPT_USERAGENT, OS_URL_FAKE_USER_AGENT);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Error setting user agent: %d.\n", rc);
        goto error;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_USERAGENT");
    }
#endif

    /*
     * XXX: This option is required in order for cURL to function properly with multiple threads.
     * However, this means that DNS lookup will not handle timeouts properly.
     *
     * A workaround for this (according to cURL documentation) is to build cURL with c-ares support.
     */
    rc = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 0);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Unable to set the NOSIGNAL option: %d.\n", rc);
        goto error;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_NOSIGNAL");
    }

    /*
     * Enforce TLSv1.2
     */
    rc = curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    if (rc != CURLE_OK)
    {
        LOG(ERR, "cURL: Unable to set the CURLOPT_SSLVERSION option to TLSv1_2: %d.\n", rc);
        goto error;
    }
    else
    {
        LOG(DEBUG, "cURL: setopt CURLOPT_SSLVERSION");
    }


    return curl;

error:
    if (curl != NULL) curl_easy_cleanup(curl);

    return NULL;
}

bool os_url_curl_getstats(CURL* curl, struct os_url_stat *stat)
{
    memset(stat, 0, sizeof(*stat));

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &stat->ous_rc);
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &stat->ous_time_total);
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &stat->ous_bytes_rx);
    curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &stat->ous_bytes_tx);

    /* Calculated stats */
    double d;

    curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &d);
    stat->ous_time_trans = stat->ous_time_total - d;

    return true;
}

/**
 * Simple wrapper around os_url_get(). In most cases this function should be used
 * instead of os_url_get_ex().
 */
bool os_url_get(char *url, os_url_get_fn *get_fn, void *get_ctx)
{
    return os_url_get_ex(url, 0, get_fn, get_ctx, NULL, 0L, false);
}

/**
 * Helper function for os_url_get_file()
 */
size_t os_url_get_file_fn(char *buf, size_t sz, size_t n, void *ctx)
{
    return fwrite(buf, sz, n, (FILE *)ctx);
}

/**
 * Null helper, usually used for benchmarking
 */
size_t os_url_get_null_fn(char *buf, size_t sz, size_t n, void *ctx)
{
    return sz*n;
}

/**
 * Download @p url to file @p f
 */
bool os_url_get_file(char* url, FILE* f)
{
    return os_url_get(url, os_url_get_file_fn, f);
}

/**
 * Download @p url to file @p f, do not verify certificates during a HTTPS session
 */
bool os_url_get_file_insecure(char* url, FILE* f)
{
    return os_url_get_ex(url, 0, os_url_get_file_fn, f, NULL, 0L, true);
}

bool os_url_get_file_insecure_gzip(char* url, FILE* f)
{
    return os_url_get_ex_gzip(url, 0, os_url_get_file_fn, f, NULL, 0L, true, true);
}


/**
 * Not all curl errors are the same. The nature of some errors
 * suggest that repeating the download, or trying to continue from
 * were download was stopped is reasonable. Some errors (like disk
 * full event for example), suggest that there is no sense continuing
 * download/upload
 *
 * This function abstract the type of the error, and suggest weather
 * continuing DL/UL make sense or not. In general, it works are curl_errors
 * curl_perform error black-list
 */
bool os_url_continue(void)
{
    bool do_continue = true;
    size_t i;

    for (i=0; i < sizeof(curl_stop_list)/sizeof(CURLcode); i++)
    {
        if (curl_stop_list[i] == curl_err)
        {
            do_continue = false;
            break; /* while loop    */
        }
    }

    return do_continue;
}
