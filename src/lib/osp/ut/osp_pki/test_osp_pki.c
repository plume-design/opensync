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

#include <fcntl.h>
#include <unistd.h>

#include "unity.h"
#include "unit_test_utils.h"

#include "arena.h"
#include "arena_util.h"
#include "execssl.h"
#include "log.h"
#include "osp_pki.h"
#include "osp_ps.h"

#define N_CERTS 10

/*
 * Test private key generated with:
 *      openssl ecparam -name prime256v1 -genkey -out privatekey.pem
 */
const char test_pkey[] = "-----BEGIN EC PARAMETERS-----\n"
                         "BggqhkjOPQMBBw==\n"
                         "-----END EC PARAMETERS-----\n"
                         "-----BEGIN EC PRIVATE KEY-----\n"
                         "MHcCAQEEIFDBzZHNRYvca4ywjWyazmLqmr9dVRT6wJe+Gt6QOaqYoAoGCCqGSM49\n"
                         "AwEHoUQDQgAERB+qWpBzAiXwuYo4DZKMCQrwzEXTFNv2nEJ289wGztQmenbmJy6F\n"
                         "9nDA0FKbFxZfVwpROQMiUy5AHyjAbdKQCw==\n"
                         "-----END EC PRIVATE KEY-----\n";

void defer_unlink_fn(void *data)
{
    (void)unlink((char *)data);
}

#define TMP_CSR "/tmp/test_osp_pki.csr"
#define TMP_CA  "/tmp/test_osp_pki.ca"

void test_osp_pki_erase(void)
{
    TEST_ASSERT_TRUE(osp_ps_erase_store_name("certs", OSP_PS_PRESERVE));
}

void test_osp_pki_cert(void)
{
    ARENA_SCRATCH(scratch);
    char *csr;

    csr = osp_pki_cert_request(NULL, "/commonName=default");
    TEST_ASSERT_FALSE(csr == NULL);
    TEST_ASSERT_TRUE(arena_defer_free(scratch, csr));

    /*
     * Test if CSR is valid: osp_pki_cert_request() returns a base64-encoded
     * binary DER certificate. Decode it first, then check if its valid.
     */
    TEST_ASSERT_TRUE(execssl_arena(scratch, csr, "base64", "-A", "-d", "-out", TMP_CSR) != NULL);
    TEST_ASSERT_TRUE(arena_defer(scratch, defer_unlink_fn, TMP_CSR));
    TEST_ASSERT_TRUE(execssl_arena(scratch, NULL, "req", "-in", TMP_CSR, "-inform", "DER", "-noout", "-text") != NULL);

    /*
     * Generate the self-signed CA
     */
    TEST_ASSERT_TRUE(execssl_arena(
            scratch,
            test_pkey,
            "req",
            "-x509",
            "-new",
            "-key",
            "/dev/stdin",
            "-out",
            TMP_CA,
            "-days",
            "365",
            "-subj",
            "/commonName=test_osp_ki"));
    TEST_ASSERT_TRUE(arena_defer(scratch, defer_unlink_fn, TMP_CA));

    /*
     * Sign the CSR, get the certificate and update the PKI cert
     */
    char *crt = execssl_arena(
            scratch,
            test_pkey,
            "x509",
            "-req",
            "-in",
            TMP_CSR,
            "-CA",
            TMP_CA,
            "-CAkey",
            "/dev/stdin",
            "-CAcreateserial",
            "-days",
            "1");
    TEST_ASSERT_TRUE(crt != NULL);
    TEST_ASSERT_TRUE(osp_pki_cert_update(NULL, crt));
}

void test_osp_pki_cert_label(void)
{
    ARENA_SCRATCH(scratch);

    for (int ii = 0; ii < N_CERTS; ii++)
    {
        arena_frame_auto_t af = arena_save(scratch);
        char *label = arena_sprintf(scratch, "label%d", ii);
        char *subj = arena_sprintf(scratch, "/commonName=%s/", label);
        char *csr = osp_pki_cert_request(label, subj);
        TEST_ASSERT_FALSE(csr == NULL);
        TEST_ASSERT_TRUE(arena_defer_free(scratch, csr));

        /*
         * Test if CSR is valid: osp_pki_cert_request() returns a base64-encoded
         * binary DER certificate. Decode it first, then check if its valid.
         */
        TEST_ASSERT_TRUE(execssl_arena(scratch, csr, "base64", "-A", "-d", "-out", TMP_CSR) != NULL);
        TEST_ASSERT_TRUE(arena_defer(scratch, defer_unlink_fn, TMP_CSR));
        TEST_ASSERT_TRUE(
                execssl_arena(scratch, NULL, "req", "-in", TMP_CSR, "-inform", "DER", "-noout", "-text") != NULL);

        /*
         * Generate the self-signed CA
         */
        TEST_ASSERT_TRUE(execssl_arena(
                scratch,
                test_pkey,
                "req",
                "-x509",
                "-new",
                "-key",
                "/dev/stdin",
                "-out",
                TMP_CA,
                "-days",
                "365",
                "-subj",
                "/commonName=test_osp_ki"));
        TEST_ASSERT_TRUE(arena_defer(scratch, defer_unlink_fn, TMP_CA));

        /*
         * Sign the CSR, get the certificate and update the PKI cert
         */
        char *crt = execssl_arena(
                scratch,
                test_pkey,
                "x509",
                "-req",
                "-in",
                TMP_CSR,
                "-CA",
                TMP_CA,
                "-CAkey",
                "/dev/stdin",
                "-CAcreateserial",
                "-days",
                "1");
        TEST_ASSERT_TRUE(crt != NULL);
        TEST_ASSERT_TRUE(osp_pki_cert_update(label, crt));
    }
}

void test_osp_pki_verify(const char *label)
{
    ARENA_SCRATCH(scratch);

    char *cert_path;
    char *test_label;

    if (label == NULL)
    {
        cert_path = CONFIG_TARGET_PATH_CERT "/" CONFIG_TARGET_PATH_PRIV_CERT;
        test_label = "CN=default";
    }
    else
    {
        cert_path = arena_sprintf(scratch, "%s/%s/%s", CONFIG_TARGET_PATH_CERT, label, CONFIG_TARGET_PATH_PRIV_CERT);
        TEST_ASSERT_TRUE(cert_path != NULL);
        test_label = arena_sprintf(scratch, "CN=%s", label);
        TEST_ASSERT_TRUE(test_label != NULL);
    }

    char *csr = execssl_arena(scratch, NULL, "x509", "-in", cert_path, "-noout", "-subject");
    TEST_ASSERT_TRUE(csr != NULL);
    TEST_ASSERT_TRUE(strstr(csr, test_label) != NULL);
}

void test_osp_pki_setup(void)
{
    ARENA_SCRATCH(scratch);

    TEST_ASSERT_TRUE(osp_pki_setup());

    test_osp_pki_verify(NULL);

    for (int ii = 0; ii < N_CERTS; ii++)
    {
        char *label = arena_sprintf(scratch, "label%d", ii);
        test_osp_pki_verify(label);
    }
}

void test_osp_pki_remove(void)
{
    ARENA_SCRATCH(scratch);

    for (int ii = 0; ii < N_CERTS; ii++)
    {
        char *label = arena_sprintf(scratch, "label%d", ii);
        TEST_ASSERT_TRUE(osp_pki_cert_remove(label));
    }
}

int main(void)
{
    log_open("test_osp_pki", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_ALERT);

    ut_init("test_osp_pki", NULL, NULL);

    RUN_TEST(test_osp_pki_erase);
    RUN_TEST(test_osp_pki_cert);
    RUN_TEST(test_osp_pki_cert_label);
    RUN_TEST(test_osp_pki_setup);
    RUN_TEST(test_osp_pki_remove);

    ev_default_destroy();

    ut_fini();
}
