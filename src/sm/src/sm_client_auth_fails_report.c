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

#include "sm.h"

void
sm_client_auth_fails_report_start(const sm_stats_request_t *request)
{
    sm_backend_report_start(STS_REPORT_CLIENT_AUTH_FAILS, request);
    LOGI("Started %s client auth fails reporting",
         radio_get_name_from_type(request->radio_type));
}

void
sm_client_auth_fails_report_update(const sm_stats_request_t *request)
{
    sm_backend_report_update(STS_REPORT_CLIENT_AUTH_FAILS, request);
    LOGI("Modified %s client auth fails reporting",
         radio_get_name_from_type(request->radio_type));
}

void
sm_client_auth_fails_report_stop(const sm_stats_request_t *request)
{
    sm_backend_report_stop(STS_REPORT_CLIENT_AUTH_FAILS, request);
    LOGI("Stopped %s client auth fails reporting",
         radio_get_name_from_type(request->radio_type));
}

void
sm_client_auth_fails_report(const sm_client_auth_fails_report_t *report)
{
    dpp_client_auth_fails_report_data_t *dpp_report = NULL;
    uint64_t curr_tstamp;
    size_t i;
    size_t j;

    dpp_report = dpp_client_auth_fails_report_data_alloc();
    if (!dpp_report) {
        LOGW("Failed to alloc memory for client auth fails report");
        goto free_report;
    }

    dpp_report->radio_type = report->radio_type;

    ds_dlist_init(&dpp_report->bsses, dpp_client_auth_fails_bss_t, node);
    for (i = 0; i < report->bsses_len; i++) {
        const sm_client_auth_fails_bss_t *bss;
        dpp_client_auth_fails_bss_t *dpp_bss;

        bss = &report->bsses[i];
        dpp_bss = dpp_client_auth_fails_bss_alloc();
        if (!dpp_bss) {
            LOGW("Failed to alloc memory for client auth fails report");
            goto free_report;
        }

        STRSCPY_WARN(dpp_bss->if_name, bss->if_name);

        ds_dlist_init(&dpp_bss->clients, dpp_client_auth_fails_client_t, node);
        for (j = 0; j < bss->clients_len; j++) {
            const sm_client_auth_fails_client_t *client;
            dpp_client_auth_fails_client_t *dpp_client;

            client = &bss->clients[j];
            dpp_client = dpp_client_auth_fails_client_alloc();
            if (!dpp_client) {
                LOGW("Failed to alloc memory for client auth fails report");
                goto free_report;
            }

            STRSCPY_WARN(dpp_client->mac, client->mac);
            dpp_client->auth_fails = client->auth_fails;
            dpp_client->invalid_psk = client->invalid_psk;

            ds_dlist_insert_tail(&dpp_bss->clients, dpp_client);
        }

        ds_dlist_insert_tail(&dpp_report->bsses, dpp_bss);
    }

    dpp_put_client_auth_fails(dpp_report);

    curr_tstamp = time(NULL) * 1000; /* satisfy sm_timestamp_ms_to_date() input */
    LOGI("Sending %s client auth fails report at '%s'",
         radio_get_name_from_type(report->radio_type),
         sm_timestamp_ms_to_date(curr_tstamp));

free_report:
    dpp_client_auth_fails_report_data_free(dpp_report);
}
