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

#include <string.h>
#include <memutil.h>
#include <os_pcap.h>
#include <os_fdbuf.h>
#include <os_fdbuf_pcap.h>
#include <ff_lib.h>

#define OS_FDBUF_PCAP_FILTER "llc u"

struct os_fdbuf_pcap {
    char *if_name;
    os_pcap_t *pcap;
};

static void
os_fdbuf_pcap_rx_cb(void *priv,
                    const void *pkt,
                    const size_t len)
{
    struct os_fdbuf_pcap *pcap = priv;
    const char *if_name = pcap->if_name;

    os_fdbuf_ingest(if_name, pkt, len);
}

os_fdbuf_pcap_t *
os_fdbuf_pcap_new(const char *if_name)
{
    if (ff_is_flag_enabled("fdb_update_frame_handling") == false) return NULL;
    struct os_fdbuf_pcap *pcap = CALLOC(1, sizeof(*pcap));
    const struct os_pcap_arg arg = {
        .filter = OS_FDBUF_PCAP_FILTER,
        .if_name = if_name,
        .direction = PCAP_D_IN,
        .snap_len = 64,
        .rx_fn = os_fdbuf_pcap_rx_cb,
        .rx_fn_priv = pcap,
    };
    pcap->pcap = os_pcap_new(&arg);
    pcap->if_name = STRDUP(if_name);
    return pcap;
}

void
os_fdbuf_pcap_drop(os_fdbuf_pcap_t *pcap)
{
    if (pcap == NULL) return;

    os_pcap_drop_safe(&pcap->pcap);
    FREE(pcap->if_name);
    FREE(pcap);
}

void
os_fdbuf_pcap_drop_safe(os_fdbuf_pcap_t **pcap)
{
    if (pcap == NULL) return;
    os_fdbuf_pcap_drop(*pcap);
    *pcap = NULL;
}
