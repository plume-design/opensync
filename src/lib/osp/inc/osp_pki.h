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

#ifndef OSP_PKI_H_INCLUDED
#define OSP_PKI_H_INCLUDED

#include <stdbool.h>
#include <time.h>

/**
 * Check if the currently installed certificate and return the expire date
 * and subject line.
 *
 * @return It returns true if the certificate exists and is correct,
 *         otherwise it returns false.
 */
bool osp_pki_cert_info(time_t *expire_date, char *sub, size_t sub_sz);

/**
 * Generate and return a new CSR (certificate signing request). This function
 * may generate a new private key.
 *
 * @return This function returns an allocated string pointing to the
 *         certificate request. The caller is responsible for freeing
 *         the string.
 */
char *osp_pki_cert_request(const char *subject);

/**
 * Update and install new certificate. This function installs a new certificate
 * that was previously generated using `osp_pki_cert_request()`.
 *
 * @return True on success, false on error.
 *
 * @note This function may trigger an OpenSync restart.
 */
bool osp_pki_cert_update(const char *cert);

/**
 * Setup the PKI infrastructure. This function can be called in one of the
 * following scenarios:
 *  - Right after boot before managers start
 *  - Before a manager restart (between stop and start)
 *  - When the certificate was updated
 *
 * @return Returns false if setup has failed.
 */
bool osp_pki_setup(void);

#endif /* OSP_PKI_H_INCLUDED */
