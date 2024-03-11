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

#include "nf_utils.h"

int
nf_process_ct_cb(const struct nlmsghdr *nlh, void *data)
{
    return 0;
}


int
nf_ct_init(struct ev_loop *loop)
{
    return 0;
}


int
nf_ct_exit(void)
{
    return 0;
}


int
nf_ct_set_mark(nf_flow_t *flow)
{
    return 0;
}


int
nf_ct_set_mark_timeout(nf_flow_t *flow, uint32_t timeout)
{
    return 0;
}


int
nf_ct_set_flow_mark(struct net_header_parser *net_pkt,
                    uint32_t mark, uint16_t zone)
{
    return 0;
}


bool
nf_ct_get_flow_entries(int af_family, ds_dlist_t *g_nf_ct_list, uint16_t zone_id)
{
    return 0;
}


void
nf_ct_print_entries(ds_dlist_t *g_nf_ct_list) {}

bool nf_ct_filter_ip(int af, void *ip)
{
    return true;
}


void
nf_free_ct_flow_list(ds_dlist_t *ct_list) {}

