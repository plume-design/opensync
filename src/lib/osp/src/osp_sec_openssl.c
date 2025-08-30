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
 *  OSP Security API implemented using libcrypto (openssl)
 * ===========================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#include "osp_sec.h"
#include "osp_unit.h"
#include "util.h"
#include "log.h"

#define AES256_KEY_SIZE     32
#define AES256_IV_SIZE      16

/**
 * Get a random initialization vector to be used for encryption.
 *
 */
static bool osp_sec_get_iv(void *iv, int iv_len)
{
    if (RAND_bytes(iv, iv_len) != 1)
    {
        LOG(ERROR, "Error getting random bytes for IV");
        return false;
    }
    return true;
}

/**
 * Encrypt or decrypt input buffer 'in' of length 'in_len' into buffer
 * 'out' of maximum length 'out_len'
 *
 * On success, buffer 'out' is written with encrypted or decrypted data and
 * the function returns the number of bytes written.
 *
 * If 'out_len' is less then the required amount to write all data then no data
 * is written and only the required amount is returned. Hence you can first call
 * this function with out_len==0 to check for the required amount.
 *
 * Note: with encryption: the actual number of bytes returned will be the same
 * as calculated required amount. With decryption, the actual number of bytes
 * returned may be a bit lower then the previously calculated required amount
 * for the buffer (exact amount cannot be calculated in advance without
 * attempting to decrypt)
 *
 * On error, a negative number is returned.
 *
 */
static ssize_t osp_sec_cipher(void *out, size_t out_len,
                              const void *in, size_t in_len,
                              bool encrypt)
{
    const EVP_CIPHER *cipher = EVP_aes_256_cbc();
    EVP_CIPHER_CTX *ctx = NULL;
    uint8_t key[AES256_KEY_SIZE];
    uint8_t iv[AES256_IV_SIZE];
    size_t out_needed_len;
    int final_len = 0;
    int tmp_len;
    int ret;

    if (encrypt)
    {
        out_needed_len = (in_len / EVP_CIPHER_block_size(cipher)) * EVP_CIPHER_block_size(cipher)
                           + EVP_CIPHER_block_size(cipher);

        out_needed_len += EVP_CIPHER_iv_length(cipher); // room for storing IV
    }
    else
    {
        if (in_len > (size_t)EVP_CIPHER_iv_length(cipher))
        {
            out_needed_len = in_len - EVP_CIPHER_iv_length(cipher);
        }
        else
        {
            out_needed_len = in_len;
        }
    }

    if (out_len < out_needed_len)
    {
        return out_needed_len;
    }

    /* Get encryption key (assumed to be unique per device): */
    if (!osp_sec_get_key(key, sizeof(key)))
    {
        LOG(ERROR, "Failed to get encryption key");
        return -1;
    }

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL)
    {
        return -1;
    }

    if (encrypt) // encrypt
    {
        /* Generate random IV. This will ensure that if the same key is used on
         * the same plaintext blocks a different ciphertext block is produced
         * making cryptoanalysis more difficult.
         * */
        if (!osp_sec_get_iv(iv, sizeof(iv)))
        {
            LOG(ERROR, "Failed to get encryption IV");
            final_len = -1;
            goto error;
        }
        memcpy(out, iv, sizeof(iv)); // store IV at buffer beginning
        out += sizeof(iv);
        final_len += sizeof(iv);
    }
    else // decrypt
    {
        // Read stored IV
        memcpy(iv, in, sizeof(iv));
        in += sizeof(iv);
        in_len -= sizeof(iv);
    }

    ret = EVP_CipherInit_ex(ctx, cipher, NULL, key, iv, encrypt);
    if (ret != 1)
    {
        LOG(ERROR, "Failed to init cipher context");
        final_len = -1;
        goto error;
    }

    ret = EVP_CipherUpdate(ctx, out, &tmp_len, in, in_len);
    if (ret != 1)
    {
        LOG(ERROR, "Failed to %s", encrypt ? "encrypt" : "decrypt");
        final_len = -1;
        goto error;
    }
    final_len += tmp_len;

    ret = EVP_CipherFinal_ex(ctx, out + tmp_len, &tmp_len);
    if (ret != 1)
    {
        LOG(ERROR, "Failed to finalize %s", encrypt ? "encrypt" : "decrypt");
        final_len = -1;
        goto error;
    }
    final_len += tmp_len;

error:
    if (ctx)
    {
        EVP_CIPHER_CTX_free(ctx);
    }

    return final_len;
}

ssize_t osp_sec_encrypt(void *out, size_t out_len, const void *in, size_t in_len)
{
    return osp_sec_cipher(out, out_len, in, in_len, true);
}

ssize_t osp_sec_decrypt(void *out, size_t out_len, const void *in, size_t in_len)
{
    return osp_sec_cipher(out, out_len, in, in_len, false);
}
