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

#ifndef OSP_SEC_H_INCLUDED
#define OSP_SEC_H_INCLUDED

/**
 * Encrypt input buffer @p in
 *
 * @param[out]       out         Encrypted data will be written here
 * @param[out]       out_len     Maximum size of the @p out buffer
 * @param[in]        in          Input buffer with plaintext data to encrypt
 * @param[in]        in_len      Size of the input plaintext data
 *
 * @return
 * If @out_len is large enough to store all the encrypted data and encryption
 * was successful, the amount of encrypted bytes written is returned
 *
 * If @out_len is 0 (or in general if @out_len < required amount to write all
 * encrypted data) then no encryption is attempted and only the calculated
 * required amount for @p out_len is returned.
 *
 * Hence you can call this function initially with out=NULL and out_len==0 to
 * check for the required minimum amount of buffer @p out.
 *
 * On error, a negative number is returned.
 *
 */
ssize_t osp_sec_encrypt(void *out, size_t out_len, const void *in, size_t in_len);

/**
 * Decrypt input buffer @p in (previously encrypted with @ref osp_sec_encrypt())
 *
 * @param[out]       out         Decrypted data will be written here
 * @param[out]       out_len     Maximum size of the @p out buffer
 * @param[in]        in          Input buffer with ciphertext data to decrypt
 * @param[in]        in_len      Size of the input ciphertext data
 *
 * @return
 * If @out_len is large enough to store all the decrypted data and decryption
 * was successful, the amount of decrypted bytes written is returned
 *
 * If @out_len is 0 (or in general if @out_len < required amount to write all
 * decrypted data) then no decryption is attempted and only the calculated
 * required amount for @p out_len is returned.
 *
 * Hence you can use this function initially with out=NULL and out_len==0 to
 * check for the required amount.
 *
 * On error, a negative number is returned.
 */
ssize_t osp_sec_decrypt(void *out, size_t out_len, const void *in, size_t in_len);

/**
 * Return encryption key to be used with @ref osp_sec_encrypt()
 * and @ref osp_sec_decrypt()
 *
 * @param[out]       key          Encryption key
 * @param[in]        key_len      The length of encryption key requested.
 *
 * @return
 * True on success
 */
bool osp_sec_get_key(void *key, int key_len);

#endif /* OSP_SEC_H_INCLUDED */
