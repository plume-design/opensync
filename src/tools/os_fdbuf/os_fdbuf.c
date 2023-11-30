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

#include <os_fdbuf.h>
#include <os_fdbuf_pcap.h>
#include <log.h>
#include <ev.h>

static int
usage(const char *self)
{
    fprintf(stderr, "usage: %s: <IFNAME> <\"announce\"|\"ingest\">\n", self);
    return -1;
}

static int
announce(const char *if_name)
{
    const bool announce_failed = (os_fdbuf_announce(if_name) != 0);
    WARN_ON(announce_failed);
    return 0;
}

static int
ingest(const char *if_name)
{
    os_fdbuf_pcap_t *pcap = os_fdbuf_pcap_new(if_name);
    ev_run(EV_DEFAULT_ 0);
    os_fdbuf_pcap_drop_safe(&pcap);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) return usage(argv[0]);
    log_open("os_fdbuf", LOG_OPEN_DEFAULT);
    log_severity_set(LOG_SEVERITY_DEBUG);
    const char *if_name = argv[1];
    const char *action = argv[2];
    if (strcmp(action, "announce") == 0) return announce(if_name);
    if (strcmp(action, "ingest") == 0) return ingest(if_name);
    return usage(argv[0]);
}
