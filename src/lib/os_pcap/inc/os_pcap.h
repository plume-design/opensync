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

#ifndef OS_PCAP_H_INCLUDED
#define OS_PCAP_H_INCLUDED

#include <pcap.h>

struct os_pcap;
typedef struct os_pcap os_pcap_t;

typedef void os_pcap_rx_fn_t(void *priv,
                             const void *pkt,
                             size_t pkt_len);

struct os_pcap_arg {
    struct ev_loop *loop;
    const char *if_name;
    const char *filter;
    pcap_direction_t direction;
    int snap_len;
    os_pcap_rx_fn_t *rx_fn;
    void *rx_fn_priv;
};

os_pcap_t *
os_pcap_new(const struct os_pcap_arg *arg);

void
os_pcap_drop(os_pcap_t *pcap);

void
os_pcap_drop_safe(os_pcap_t **pcap);

#endif /* OS_PCAP_H_INCLUDED */
