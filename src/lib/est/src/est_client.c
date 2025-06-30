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

#include "arena.h"
#include "arena_util.h"
#include "est_client.h"
#include "est_request.h"
#include "est_util.h"
#include "log.h"
#include "osp_pki.h"
#include "target.h"

/*
 * Context for the cacerts callback
 */
struct est_client_cacerts_ctx
{
    arena_t *cc_arena;       /* Arena used for this request */
    const char *cc_url;      /* Request URL */
    est_request_fn_t *cc_fn; /* Request completion callback */
    void *cc_ctx;            /* Completion callback context */
};

/*
 * Context for the simpleenroll/simplereenroll callback
 */
struct est_client_simpleenroll_ctx
{
    arena_t *se_arena;       /* Arena used for this request */
    const char *se_url;      /* Request URL */
    est_request_fn_t *se_fn; /* Request completion callback */
    void *se_ctx;            /* Completion callback context */
};

static bool est_client_do_enroll(
        arena_t *arena,
        struct ev_loop *loop,
        const char *url,
        const char *csr,
        est_request_fn_t *enroll_fn,
        void *ctx);

static est_request_fn_t est_client_cacerts_fn;
static est_request_fn_t est_client_simple_enroll_fn;
static CURL *est_client_get_curl(arena_t *arena, const char *url);
static void defer_CURL_fn(void *defer);
static void defer_curl_slist_fn(void *data);
static bool defer_curl_slist(arena_t *arena, struct curl_slist *list);
static char *est_client_url_concat(arena_t *arena, const char *base, const char *path);

/*
 * =============================================================================
 * Public functions
 * =============================================================================
 */
bool est_client_cacerts(
        arena_t *arena,
        struct ev_loop *loop,
        const char *est_server,
        est_request_fn_t *cacerts_fn,
        void *ctx)
{
    arena_frame_auto_t af = arena_save(arena);

    struct est_client_cacerts_ctx *self = arena_malloc(arena, sizeof(*self));
    if (self == NULL)
    {
        LOG(ERR, "est: Error allocating context for cacerts: %s", est_server);
        return false;
    }

    self->cc_url = est_client_url_concat(arena, est_server, "/.well-known/est/cacerts");
    if (self->cc_url == NULL)
    {
        LOG(ERR, "est: Error creating cacerts URL from server URL: %s", est_server);
        return false;
    }
    self->cc_arena = arena;
    self->cc_fn = cacerts_fn;
    self->cc_ctx = ctx;

    LOG(INFO, "est: Requesting cacerts from: %s", self->cc_url);

    CURL *curl = est_client_get_curl(arena, self->cc_url);
    if (curl == NULL)
    {
        LOG(ERR, "est: ca_certs failed to create CURL object.");
        return false;
    }

    if (!est_request_curl_async(arena, loop, curl, est_client_cacerts_fn, self))
    {
        LOG(ERR, "est: Error creating async request during simpleenroll.");
        return false;
    }

    af = arena_save(arena);
    return true;
}

bool est_client_simple_enroll(
        arena_t *arena,
        struct ev_loop *loop,
        const char *est_server,
        const char *csr,
        est_request_fn_t *enroll_fn,
        void *ctx)
{
    ARENA_SCRATCH(scratch, arena);

    const char *url = est_client_url_concat(scratch, est_server, "/.well-known/est/simpleenroll");
    if (url == NULL)
    {
        LOG(ERR, "est: Error creating simpleenroll URL from server: %s", est_server);
        return false;
    }

    return est_client_do_enroll(arena, loop, url, csr, enroll_fn, ctx);
}

bool est_client_simple_reenroll(
        arena_t *arena,
        struct ev_loop *loop,
        const char *est_server,
        const char *csr,
        est_request_fn_t *enroll_fn,
        void *ctx)
{
    ARENA_SCRATCH(scratch, arena);

    const char *url = est_client_url_concat(scratch, est_server, "/.well-known/est/simplereenroll");
    if (url == NULL)
    {
        LOG(ERR, "est:  Error creating simplereenroll URL from server: %s", est_server);
        return false;
    }

    return est_client_do_enroll(arena, loop, url, csr, enroll_fn, ctx);
}

/*
 * =============================================================================
 * Private functions
 * =============================================================================
 */

CURL *est_client_get_curl(arena_t *arena, const char *url)
{
    arena_frame_auto_t af = arena_save(arena);

    CURL *curl = curl_easy_init();
    if (curl == NULL || !arena_defer(arena, defer_CURL_fn, curl))
    {
        LOG(ERR, "est: Error acquiring cURL handle.");
        return NULL;
    }
    if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK)
    {
        LOG(ERR, "est: Error setting cURL option CURLOPT_URL.");
        return NULL;
    }
    if (curl_easy_setopt(curl, CURLOPT_SSLKEY, target_tls_privkey_filename()) != CURLE_OK)
    {
        LOG(ERR, "est: Error setting cURL option CURLOPT_SSLKEY.");
        return NULL;
    }
    if (curl_easy_setopt(curl, CURLOPT_SSLCERT, target_tls_mycert_filename()) != CURLE_OK)
    {
        LOG(ERR, "est: Error setting cURL option CURLOPT_SSLCERT.");
        return NULL;
    }
    if (curl_easy_setopt(curl, CURLOPT_CAINFO, target_tls_cacert_filename()) != CURLE_OK)
    {
        LOG(ERR, "est: Error setting cURL option CURLOPT_CAINFO.");
        return NULL;
    }

    /*
     * Enable debug mode -- mainly used by unit tests
     */
    if (access(CONFIG_TARGET_PATH_CERT "/.debug", R_OK) == 0)
    {
        LOG(NOTICE, "Running in debug mode.");
        /*
         * This is required for the EST mock server as it has a self-signed
         * certificate. Disable it in release builds.
         */
        if (curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0) != CURLE_OK)
        {
            LOG(ERR, "est_request: Error setting CURLOPT_SSL_VERIFYPEER");
            return false;
        }

        if (curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0) != CURLE_OK)
        {
            LOG(ERR, "est_request: Error setting CURLOPT_SSL_VERIFYHOST");
            return false;
        }
    }

    af = arena_save(arena);

    return curl;
}

/*
 * Function callback for a completed `cacerts` request.
 */
void est_client_cacerts_fn(const struct est_request_status ers, const char *data, void *ctx)
{
    struct est_client_cacerts_ctx *self = ctx;
    struct est_request_status status = ers;

    if (ers.status != ER_STATUS_OK)
    {
        LOG(ERR, "est: Cacerts request failed: %s", self->cc_url);
        goto error;
    }

    if (data == NULL)
    {
        LOG(ERR, "est: Cacerts didn't receive any data: %s", self->cc_url);
        status.status = ER_STATUS_ERROR;
        goto error;
    }

    /*
     * Convert the certificate from PKCS7 to PEM
     */
    char *cabuf = est_util_pkcs7_to_pem(self->cc_arena, data);
    if (cabuf == NULL)
    {
        LOG(ERR, "est: Failed to convert CACERT to PEM during cacert request: %s", self->cc_url);
        status.status = ER_STATUS_ERROR;
        goto error;
    }

    self->cc_fn(status, cabuf, self->cc_ctx);
    return;

error:
    self->cc_fn(status, NULL, self->cc_ctx);
    return;
}

/*
 * Issue a EST enroll request to server `est_server` using the certificate
 * signing request in `csr.
 *
 * Input parameters:
 *
 *  arena - memory arena for storing results, destroying the arena will cancel
 *      the request without calling `enroll_fn`
 *  loop - libev event loop
 *  est_server - EST server URL
 *  renew - whether this is a simpleenroll or simpleenroll request
 *  csr - certificate signing request
 *  enroll_fn - request completion callback
 *  enroll_data - callback context data
 *
 * On error this function will return `false` and `enroll_fn` will not be
 * called.
 */
bool est_client_do_enroll(
        arena_t *arena,
        struct ev_loop *loop,
        const char *url,
        const char *csr,
        est_request_fn_t *enroll_fn,
        void *ctx)
{
    arena_frame_auto_t af = arena_save(arena);

    struct est_client_simpleenroll_ctx *self = arena_malloc(arena, sizeof(*self));
    if (self == NULL)
    {
        LOG(ERR, "est: Error allocating context during enroll: %s", url);
        return false;
    }
    self->se_url = arena_strdup(arena, url);
    if (self->se_url == NULL)
    {
        LOG(ERR, "est: Error creating simpleenroll URL from server URL: %s", url);
        return false;
    }
    self->se_arena = arena;
    self->se_fn = enroll_fn;
    self->se_ctx = ctx;

    CURL *curl = est_client_get_curl(arena, self->se_url);
    if (curl == NULL)
    {
        LOG(ERR, "est: enroll failed to create CURL object.");
        return false;
    }
    if (curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(csr)) != CURLE_OK)
    {
        LOG(ERR, "est: Error setting cURL option CURLOPT_POSTFIELDSIZE.");
        return false;
    }
    if (curl_easy_setopt(curl, CURLOPT_POSTFIELDS, csr) != CURLE_OK)
    {
        LOG(ERR, "est: Error setting cURL option CURLOPT_POSTFIELDS.");
        return false;
    }

    struct curl_slist *slist = NULL;
    slist = curl_slist_append(slist, "Content-Type:application/pkcs10");
    slist = curl_slist_append(slist, "Content-Transfer-Encoding:base64");
    if (!defer_curl_slist(arena, slist))
    {
        LOG(ERR, "est: Error deferring cURL slist.");
        return false;
    }
    if (curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist) != CURLE_OK)
    {
        LOG(ERR, "est: Error setting cURL option CURLOPT_HTTPHEADER.");
        return false;
    }

    if (!est_request_curl_async(arena, loop, curl, est_client_simple_enroll_fn, self))
    {
        LOG(ERR, "est: Error creating async request during simpleenroll.");
        return false;
    }

    af = arena_save(arena);
    return true;
}

void est_client_simple_enroll_fn(const struct est_request_status ers, const char *data, void *ctx)
{
    struct est_client_simpleenroll_ctx *self = ctx;
    struct est_request_status status = ers;

    if (ers.status != ER_STATUS_OK)
    {
        LOG(ERR, "est: Simpleenroll request failed: %s", self->se_url);
        goto error;
    }

    if (data == NULL)
    {
        LOG(ERR, "est: Simpleenroll didn't receive any data: %s", self->se_url);
        goto error;
    }

    /*
     * Convert the certificate from PKCS7 to PEM
     */
    char *certbuf = est_util_pkcs7_to_pem(self->se_arena, data);
    if (certbuf == NULL)
    {
        LOG(ERR, "est: Failed to convert certificate to PEM during simpleenroll: %s", self->se_url);
        goto error;
    }

    self->se_fn(status, certbuf, self->se_ctx);
    return;

error:
    status.status = ER_STATUS_ERROR;
    self->se_fn(status, NULL, self->se_ctx);
    return;
}

struct est_request_fn_ctx
{
    struct est_request_status rf_status;
    const char *rf_data;
};

void est_request_fn(struct est_request_status errcode, const char *data, void *ctx)
{
    struct est_request_fn_ctx *rf = ctx;

    rf->rf_status = errcode;
    rf->rf_data = data;
}

/*
 * Concatenate `base` and `path` to create a new URL. Trailing slashes from
 * `base` are stripped before `path` is appended.
 *
 * The new URL is allocated on `arena`.
 */
char *est_client_url_concat(arena_t *arena, const char *base, const char *path)
{
    ARENA_SCRATCH(scratch, arena);

    char *url = arena_strdup(scratch, base);
    if (url == NULL) return NULL;

    /* Strip trailing slashes */
    strchomp(url, "/");

    return arena_sprintf(arena, "%s%s", url, path);
}

/*
 * =============================================================================
 * ARENA support functions
 * =============================================================================
 */
void defer_CURL_fn(void *defer)
{
    LOG(DEBUG, "est_request: Delete cURL easy handle.");
    curl_easy_cleanup(defer);
}

void defer_curl_slist_fn(void *data)
{
    struct curl_slist *slist = data;

    if (slist != NULL)
    {
        curl_slist_free_all(slist);
    }
}

bool defer_curl_slist(arena_t *arena, struct curl_slist *list)
{
    return arena_defer(arena, defer_curl_slist_fn, list);
}
