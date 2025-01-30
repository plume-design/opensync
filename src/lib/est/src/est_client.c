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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <curl/curl.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "est_client.h"
#include "est_util.h"
#include "log.h"
#include "memutil.h"
#include "osp_pki.h"
#include "target.h"

#define MAX_EST_OPERATION_LEN      50
#define CURL_RETRY_AFTER_SUPPORTED (LIBCURL_VERSION_NUM >= 0x074200)

struct cert_data_buf
{
    char *cert;
    unsigned int size;
};

size_t cacert_write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
    int *recving = (int *)data;
    size_t realsize = CURLE_WRITE_ERROR;
    FILE *pFile;
    const char *cacert_file = target_tls_cacert_filename();

    LOGI("Receivd CA certificate %s\n", (char *)ptr);

    /* Store the cacert */
    if (*recving == 0)
    {
        pFile = fopen(cacert_file, "w+b");
    }
    else
    {
        pFile = fopen(cacert_file, "a+b");
    }

    if (pFile == NULL)
    {
        return CURLE_WRITE_ERROR;
    }

    realsize = fwrite(ptr, 1, size * nmemb, pFile);

    fclose(pFile);
    *recving = 1;

    return realsize;
}

/* Update the trusted CA store after every simple/re enroll */
void est_client_get_cacerts(struct est_client_cfg *est_cfg)
{
    CURL *curl = curl_easy_init();
    CURLcode res;
    char server_url[sizeof(est_cfg->server_url) + MAX_URL_LENGTH];
    size_t rc;

    /* Create Server URL adding cacerts */
    rc = snprintf(server_url, sizeof(server_url), "%s/.well-known/est/cacerts", est_cfg->server_url);
    if (rc >= (ssize_t)sizeof(server_url))
    {
        LOGE("Server URL is too long. Error updating cacerts.");
        return;
    }

    if (curl)
    {
        LOGI("Update CA certs from server");
        curl_easy_setopt(curl, CURLOPT_URL, server_url);

        int recving = 0;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cacert_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &recving);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            LOGI("Update cacerts failed");
        }

        curl_easy_cleanup(curl);
    }

    return;
}

size_t enroll_write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
    size_t realsize = size * nmemb;
    struct cert_data_buf *cert_data = data;

    LOGT("Received Certificate");

    cert_data->cert = REALLOC(cert_data->cert, cert_data->size + realsize + 1);
    memcpy(&cert_data->cert[cert_data->size], ptr, realsize);
    cert_data->size += realsize;
    cert_data->cert[cert_data->size] = 0;

    return realsize;
}

/* Get client certificate from EST server */
static int get_certificate(struct est_client_cfg *est_cfg, bool renew)
{
    CURLcode res;
    char server_url[sizeof(est_cfg->server_url) + MAX_URL_LENGTH];
    size_t rc;
    struct cert_data_buf cert_data = {0};

    CURL *curl = curl_easy_init();
    struct curl_slist *slist = NULL;
    char *subj = NULL;
    char *csr = NULL;
    char *certbuf = NULL;
    int retval = -1;

    /* Create Server URL to simpleenroll or reenroll */
    if (renew)
    {
        rc = snprintf(server_url, sizeof(server_url), "%s/.well-known/est/simplereenroll", est_cfg->server_url);
    }
    else
    {
        rc = snprintf(server_url, sizeof(server_url), "%s/.well-known/est/simpleenroll", est_cfg->server_url);
    }

    if (rc >= sizeof(server_url))
    {
        LOGE("Server URL too long. Error during simpleenroll/simplereenroll.");
        goto error;
    }

    LOGT("Generated server url = %s", server_url);

    if (est_cfg->subject == NULL)
    {
        subj = est_util_csr_subject();
        if (subj == NULL)
        {
            LOGE("Error generating subject line.");
            goto error;
        }
    }
    else
    {
        subj = strdup(est_cfg->subject);
    }

    csr = osp_pki_cert_request(subj);
    if (!csr)
    {
        LOGI("CSR get failed");
        goto error;
    }
    LOGT("CSR = %s", csr);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(csr));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, csr);
    curl_easy_setopt(curl, CURLOPT_URL, server_url);

    slist = curl_slist_append(slist, "Content-Type:application/pkcs10");
    slist = curl_slist_append(slist, "Content-Transfer-Encoding:base64");

    /* client_key and client_cert will be retrieved from osps */
    curl_easy_setopt(curl, CURLOPT_SSLKEY, target_tls_privkey_filename());
    curl_easy_setopt(curl, CURLOPT_SSLCERT, target_tls_mycert_filename());

    curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, enroll_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cert_data);

    /* TODO: Verify peer does not work currently because we dont have the EST proxy */
    // curl_easy_setopt(curl, CURLOPT_CAINFO, target_tls_cacert_filename);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    /*
     * return  0 - Success
     * return -1 - If curl fails to connect to the server
     * return -2 - Error code sent by the server
     */
    if ((res = curl_easy_perform(curl)) == CURLE_OK)
    {
        curl_off_t wait = 0;
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

#if CURL_RETRY_AFTER_SUPPORTED
        curl_easy_getinfo(curl, CURLINFO_RETRY_AFTER, &wait);
#endif

        if (wait || http_code != 200)
        {
            LOGI("Retry from server");
            if (est_cfg->update_response_cb) est_cfg->update_response_cb(wait, http_code);
            retval = -2;
            goto error;
        }
    }
    else
    {
        LOGN("curl_easy_perform() failed: %d\n", res);
        goto error;
    }

    cert_data.size = 0;
    LOGT("Cert received = %s", cert_data.cert);
    certbuf = est_util_pkcs7_to_pem(cert_data.cert);

    /* Save the client certificate to the device */
    if (!osp_pki_cert_update(certbuf)) LOGI("osp_pki_cert_update Failed");

    retval = 0;

error:
    if (slist != NULL) curl_slist_free_all(slist);
    if (curl != NULL) curl_easy_cleanup(curl);
    if (cert_data.cert != NULL) FREE(cert_data.cert);
    FREE(certbuf);
    FREE(csr);
    FREE(subj);

    return retval;
}

int est_client_get_ca_certs()
{
    /* TBD */
    return 0;
}

int est_client_cert_renew(struct est_client_cfg *est_cfg)
{
    LOGI("EST LIB Re enrolling");
    return get_certificate(est_cfg, true);
}

int est_client_get_cert(struct est_client_cfg *est_cfg)
{
    if (strlen(est_cfg->server_url) == 0) return -1;

    return get_certificate(est_cfg, false);
}
