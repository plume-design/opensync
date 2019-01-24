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

#ifndef __FSM_DEMO_PLUGIN_H__
#define __FSM_DEMO_PLUGIN_H__
#include <pcap.h>
#include <stdint.h>
#include <time.h>

#include "os_types.h"
#include "fsm.h"

struct eth_header {
    os_macaddr_t dstmac;
    os_macaddr_t srcmac;
    uint16_t ethertype;
};

struct fsm_demo_plugin_parser {
    struct eth_header eth_header;
    int pcap_datalink;
    struct fsm_session *fsm_context;
};

struct fsm_demo_plugin_cache {
    int initialized;
    int pkt_count;
    struct fsm_session *session;
};

int fsm_demo_plugin_init(struct fsm_session *session);
void fsm_demo_plugin_handler(uint8_t * args,
                             const struct pcap_pkthdr *h,
                             const uint8_t *bytes);
void fsm_demo_plugin_periodic(struct fsm_session *session);
void fsm_demo_plugin_exit(struct fsm_session *session);

#endif // __FSM_DEMO_PLUGIN_H__
