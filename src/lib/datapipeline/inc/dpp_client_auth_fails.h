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

#ifndef DPP_CLIENT_AUTH_FAILS_H
#define DPP_CLIENT_AUTH_FAILS_H

typedef struct {
    mac_address_str_t mac;
    uint32_t auth_fails;
    uint32_t invalid_psk;

    ds_dlist_node_t node;
} dpp_client_auth_fails_client_t;

typedef struct {
    ifname_t if_name;
    ds_dlist_t clients;

    ds_dlist_node_t node;
} dpp_client_auth_fails_bss_t;

typedef struct {
    radio_type_t radio_type;
    ds_dlist_t bsses;
} dpp_client_auth_fails_report_data_t;

static inline dpp_client_auth_fails_client_t *
dpp_client_auth_fails_client_alloc(void)
{
    return calloc(1, sizeof(dpp_client_auth_fails_client_t));
}

static inline void
dpp_client_auth_fails_client_free(dpp_client_auth_fails_client_t *client)
{
    free(client);
}

static inline dpp_client_auth_fails_bss_t *
dpp_client_auth_fails_bss_alloc(void)
{
    return calloc(1, sizeof(dpp_client_auth_fails_bss_t));
}

static inline void
dpp_client_auth_fails_bss_free(dpp_client_auth_fails_bss_t *bss)
{
    if (!bss)
        return;

    while (!ds_dlist_is_empty(&bss->clients)) {
        dpp_client_auth_fails_client_t *client = ds_dlist_head(&bss->clients);
        ds_dlist_remove(&bss->clients, client);
        dpp_client_auth_fails_client_free(client);
    }

    free(bss);
}

static inline dpp_client_auth_fails_report_data_t *
dpp_client_auth_fails_report_data_alloc(void)
{
    return calloc(1, sizeof(dpp_client_auth_fails_report_data_t));
}

static inline void
dpp_client_auth_fails_report_data_free(dpp_client_auth_fails_report_data_t *report)
{
    if (!report)
        return;

    while (!ds_dlist_is_empty(&report->bsses)) {
        dpp_client_auth_fails_bss_t *bss = ds_dlist_head(&report->bsses);
        ds_dlist_remove(&report->bsses, bss);
        dpp_client_auth_fails_bss_free(bss);
    }

    free(report);
}

#endif /* DPP_CLIENT_AUTH_FAILS_H */
