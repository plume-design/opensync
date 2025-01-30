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

#include <ev.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kconfig.h"
#include "log.h"
#include "memutil.h"
#include "module.h"
#include "os.h"
#include "osp.h"
#include "osp_dl.h"
#include "target.h"
#include "util.h"
#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/md5.h>

#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"

#define OS_CA_PATH            CONFIG_TARGET_PATH_OPENSYNC_CERTS "/" CONFIG_TARGET_OPENSYNC_CAFILE
#define OS_DOWNLOAD_PATH      "/tmp/downloaded_file.pem"
#define FILE_SIZE_LIMIT_MB    20
#define FILE_SIZE_LIMIT_BYTES (FILE_SIZE_LIMIT_MB * 1024 * 1024)

static ovsdb_table_t table_AWLAN_Node;
static bool download_in_progress = false;

struct download_progress
{
    FILE *file;
    const char *file_path;
    size_t total_size;
};

MODULE(pm_cert_cert_update, pm_cert_cert_update_init, pm_cert_cert_update_fini);

bool pm_calculate_md5(const char *file_path, unsigned char *digest)
{
    FILE *file = fopen(file_path, "rb");
    if (file == NULL)
    {
        LOGE("Error opening cert '%s'", file_path);
        return false;
    }

#if OPENSSL_VERSION_NUMBER >= 0x030000000  // 3.0.0
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_create();
    const EVP_MD *md = EVP_md5();
    EVP_DigestInit_ex(md_ctx, md, NULL);
#else
    MD5_CTX context;
    MD5_Init(&context);
#endif

    size_t buffer_size = 2048;
    unsigned char buffer[buffer_size];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, buffer_size, file)) != 0)
    {
#if OPENSSL_VERSION_NUMBER >= 0x030000000  // 3.0.0
        EVP_DigestUpdate(md_ctx, buffer, bytes_read);
#else
        MD5_Update(&context, buffer, bytes_read);
#endif
    }

    fclose(file);

#if OPENSSL_VERSION_NUMBER >= 0x030000000  // 3.0.0
    EVP_DigestFinal_ex(md_ctx, digest, 0);
    EVP_MD_CTX_destroy(md_ctx);
#else
    MD5_Final(digest, &context);
#endif
    return true;
}

/*
 * Compares the existing certificate with the new one.
 */
bool pm_are_files_equal(const char *existing_ca, const char *new_ca)
{
    unsigned char md5_digest1[MD5_DIGEST_LENGTH];
    unsigned char md5_digest2[MD5_DIGEST_LENGTH];

    if (!pm_calculate_md5(existing_ca, md5_digest1) || !pm_calculate_md5(new_ca, md5_digest2))
    {
        LOGE("Can't calculate MD5.");
        return false;
    }

    if (memcmp(md5_digest1, md5_digest2, MD5_DIGEST_LENGTH) != 0)
    {
        LOGI("CA files are different.");
        return false;
    }

    return true;
}

void overwrite_file(const char *new_ca, const char *existing_ca)
{
    if (remove(existing_ca) != 0)
    {
        LOGE("Error deleting old file");
        return;
    }

    if (rename(new_ca, existing_ca) != 0)
    {
        LOGE("Error renaming file");
        return;
    }

    LOGI("File overwritten successfully.");
}

/*
 * Callback function for writing data received by CURL
 */
size_t write_callback(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    return fwrite(ptr, size, nmemb, stream);
}

/*
 * Progress callback function to monitor download size.
 */
static int progress_callback(
        void *progress_data,
        curl_off_t dltotal,
        curl_off_t dlnow,
        curl_off_t ultotal,
        curl_off_t ulnow)
{
    struct download_progress *progress = (struct download_progress *)progress_data;
    progress->total_size = (size_t)dlnow;

    // If the download exceeds the specified file size limit, abort the download
    if (progress->total_size > FILE_SIZE_LIMIT_BYTES)
    {
        LOGE("Download exceeds %dMB. Aborting...", FILE_SIZE_LIMIT_MB);
        fclose(progress->file);
        remove(progress->file_path);
        return 1;
    }

    return 0;
}

/*
 * Download the cert with cURL.
 */
bool download_with_curl(const char *url)
{
    CURL *curl;
    CURLcode res;
    struct download_progress progress = {NULL, OS_DOWNLOAD_PATH, 0};

    curl = curl_easy_init();
    if (curl)
    {
        progress.file = fopen(OS_DOWNLOAD_PATH, "wb");
        if (progress.file == NULL)
        {
            LOGE("Error opening file for download: '%s'", OS_DOWNLOAD_PATH);
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, progress.file);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);  // Enable progress meter
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

        res = curl_easy_perform(curl);
        fclose(progress.file);
        curl_easy_cleanup(curl);

        // If the download was aborted, return false
        if (res == CURLE_ABORTED_BY_CALLBACK)
        {
            LOGE("Download aborted due to file size limit.");
            return false;
        }

        return (res == CURLE_OK);
    }

    return false;
}

static void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old_rec,
        struct schema_AWLAN_Node *awlan_node)
{
    LOG(INFO, "AWLAN_Node update: %s", __func__);

    // Ensure the 'pm_update_cert' field has a value
    if (strlen(awlan_node->pm_update_cert) > 0)
    {
        if (download_in_progress)
        {
            LOGW("Download is already in progress.");
            return;
        }

        const char *url = awlan_node->pm_update_cert;

        download_in_progress = true;

        // Use download_with_curl function to download the certificate
        if (download_with_curl(url))
        {
            // Check if the downloaded file has a ".pem" extension
            LOGI("Download completed successfully");

            if (!pm_are_files_equal(OS_CA_PATH, OS_DOWNLOAD_PATH))
            {
                overwrite_file(OS_DOWNLOAD_PATH, OS_CA_PATH);
                LOGI("Certificate updated.");
            }
            else
            {
                LOGI("No changes required. Certificates are identical.");
            }
        }
        else
        {
            LOGE("Download failed using CURL");
        }

        download_in_progress = false;
    }

    // Update the AWLAN_Node table regardless of download status
    ovsdb_table_update_f(
            &table_AWLAN_Node,
            awlan_node,
            ((char *[]){"+", SCHEMA_COLUMN(AWLAN_Node, pm_update_cert), NULL}));
}

void pm_cert_cert_update_init(void *data)
{
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);

    OVSDB_TABLE_MONITOR_F(AWLAN_Node, ((char *[]){SCHEMA_COLUMN(AWLAN_Node, pm_update_cert), NULL}));
    LOG(INFO, "Certification update: %s()", __func__);
}

void pm_cert_cert_update_fini(void *data)
{
    LOGI("Deinitializing cert update.");
}
