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

#include "sm_lat_sys.h"

sm_lat_sys_t *sm_lat_sys_alloc(void)
{
    return NULL;
}
void sm_lat_sys_drop(sm_lat_sys_t *s)
{
}

const char *sm_lat_sys_sample_get_ifname(const sm_lat_sys_sample_t *s)
{
    return NULL;
}
const uint8_t *sm_lat_sys_sample_get_mac_address(const sm_lat_sys_sample_t *s)
{
    return NULL;
}
const uint8_t *sm_lat_sys_sample_get_dscp(const sm_lat_sys_sample_t *s)
{
    return NULL;
}
const uint32_t *sm_lat_sys_sample_get_min(const sm_lat_sys_sample_t *s)
{
    return NULL;
}
const uint32_t *sm_lat_sys_sample_get_max(const sm_lat_sys_sample_t *s)
{
    return NULL;
}
const uint32_t *sm_lat_sys_sample_get_avg(const sm_lat_sys_sample_t *s)
{
    return NULL;
}
const uint32_t *sm_lat_sys_sample_get_last(const sm_lat_sys_sample_t *s)
{
    return NULL;
}
const uint32_t *sm_lat_sys_sample_get_num_pkts(const sm_lat_sys_sample_t *s)
{
    return NULL;
}

void sm_lat_sys_ifname_set(sm_lat_sys_t *s, const char *if_name, bool enable)
{
}
void sm_lat_sys_ifname_flush(sm_lat_sys_t *s)
{
}

void sm_lat_sys_dscp_set(sm_lat_sys_t *s, bool enable)
{
}

void sm_lat_sys_kind_set_min(sm_lat_sys_t *s, bool enable)
{
}
void sm_lat_sys_kind_set_max(sm_lat_sys_t *s, bool enable)
{
}
void sm_lat_sys_kind_set_avg(sm_lat_sys_t *s, bool enable)
{
}
void sm_lat_sys_kind_set_last(sm_lat_sys_t *s, bool enable)
{
}
void sm_lat_sys_kind_set_num_pkts(sm_lat_sys_t *s, bool enable)
{
}

void sm_lat_sys_set_report_fn_t(sm_lat_sys_t *s, sm_lat_sys_report_fn_t *fn, void *priv)
{
}

sm_lat_sys_poll_t *sm_lat_sys_poll(sm_lat_sys_t *s, sm_lat_sys_done_fn_t *fn, void *priv)
{
    return NULL;
}
void sm_lat_sys_poll_drop(sm_lat_sys_poll_t *p)
{
}
