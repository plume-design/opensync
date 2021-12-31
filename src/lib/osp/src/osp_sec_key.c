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
#include <stdint.h>
#include <stdbool.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#include "osp_sec.h"
#include "osp_unit.h"
#include "log.h"

/* Return encryption key to be used with @ref osp_sec_encrypt()
 * and @ref osp_sec_decrypt()
 *
 * @note: This is a rudimentary reference implementation.
 *
 * You are strongly encouraged to override this reference implementation with
 * your own private version of key generation. Otherwise encryption using a key
 * generated via this reference implementation is just a simple obfuscation.
 * */
bool osp_sec_get_key(void *key, int key_len)
{
    char pass[128];

#warning "Default osp_sec_get_key() implementation used." \
         "You should override it with your private version!"

    if (!osp_unit_id_get(pass, sizeof(pass)))
    {
        LOG(ERROR, "Error getting unit ID");
        return false;
    }

    if (PKCS5_PBKDF2_HMAC(pass, strlen(pass), NULL, 0, 5000, EVP_sha256(), key_len, key) != 1)
    {
        LOG(ERROR, "Error deriving encryption key from a password");
        return false;
    }

    return true;
}
