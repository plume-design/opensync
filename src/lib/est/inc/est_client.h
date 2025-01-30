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

#ifndef EST_CLIENT_H_INCLUDED
#define EST_CLIENT_H_INCLUDED

#include "const.h"

#define MAX_URL_LENGTH 500

typedef void est_client_retry_cb_t(long retry_after, long http_rsp_code);

typedef enum
{
    AUTH_BASIC = 0,
    AUTH_DIGEST
} est_client_auth_method_t;

struct est_client_cfg
{
    char server_url[MAX_URL_LENGTH];
    est_client_auth_method_t auth_method;
    char uname[C_USERNAME_LEN];
    char pwd[C_PASSWORD_LEN];
    char *subject;
    est_client_retry_cb_t *update_response_cb;
};

int est_client_cert_renew(struct est_client_cfg *est_cfg);
int est_client_get_cert(struct est_client_cfg *est_cfg);
void est_client_get_cacerts(struct est_client_cfg *est_cfg);

#endif /* EST_CLIENT_H_INCLUDED */
