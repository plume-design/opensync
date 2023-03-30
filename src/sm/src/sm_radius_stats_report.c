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
sm_radius_stats_report_start(const sm_stats_request_t *request)
{
    sm_backend_report_start(STS_REPORT_RADIUS, request);
    LOGI("Started radius stats reporting");
}

void
sm_radius_stats_report_update(const sm_stats_request_t *request)
{
    sm_backend_report_update(STS_REPORT_RADIUS, request);
    LOGI("Updated radius stats reporting");
}

void
sm_radius_stats_report_stop(const sm_stats_request_t *request)
{
    sm_backend_report_stop(STS_REPORT_RADIUS, request);
    LOGI("Stopped radius stats reporting");
}

void sm_radius_stats_report(const sm_radius_stats_report_t* report)
{
    dpp_radius_stats_report_data_t dpp_report;

    dpp_report.records = (dpp_radius_stats_rec_t **)report->data;
    dpp_report.qty = report->count;
    dpp_report.timestamp = time(NULL) * 1000;

    dpp_put_radius_stats(&dpp_report);
}
