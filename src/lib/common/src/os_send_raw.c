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

#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <linux/if_packet.h>

#include <log.h>
#include <const.h>
#include <util.h>
#include <memutil.h>

#define LOG_PREFIX(fmt, ...) "os_send_raw: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_IDX(if_index, fmt, ...) LOG_PREFIX("%u: " fmt, if_index, ##__VA_ARGS__)

static void
os_send_raw_eth_packet_hexdump(const unsigned int if_index,
                               const void *buf,
                               const size_t len)
{
    const size_t hex_size = (len * 2) + 1;
    char *hex = MALLOC(hex_size);
    const int rv = bin2hex(buf, len, hex, hex_size);
    const bool ok = (rv == 0);
    const bool bin2hex_failed = !ok;
    WARN_ON(bin2hex_failed);
    if (ok) {
        LOGD(LOG_PREFIX_IDX(if_index, "hex: %s", hex));
    }
    FREE(hex);
}

int
os_send_raw_eth_packet(unsigned int if_index,
                       const void *buf,
                       const size_t len)
{
    os_send_raw_eth_packet_hexdump(if_index, buf, len);

    const bool bad_if_index = (if_index == 0);
    if (bad_if_index) {
        LOGD(LOG_PREFIX_IDX(if_index, "invalid if_index"));
        return -EINVAL;
    }

    const int fd = socket(AF_PACKET, SOCK_RAW, 0);
    if (fd < 0) {
        LOGD(LOG_PREFIX_IDX(if_index, "failed to create raw socket: %d (%s)",
                            errno, strerror(errno)));
        return -errno;
    }

    struct sockaddr_ll saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sll_ifindex = if_index;
    saddr.sll_halen = ETH_ALEN;
    memcpy(&saddr.sll_addr, buf, ETH_ALEN);

    const int send_err = sendto(fd, buf, len, 0, (struct sockaddr *)&saddr, sizeof(saddr));
    close(fd);

    if (send_err < 0) {
        LOGD(LOG_PREFIX_IDX(if_index, "sendto() failed: %d (%s)",
                            errno, strerror(errno)));
        return -errno;
    }

    LOGD(LOG_PREFIX_IDX(if_index, "sent %zu bytes", len));
    return 0;
}

int
os_send_raw_eth_packet_ifname(const char *if_name,
                              const void *buf,
                              const size_t len)
{
    const unsigned int if_index = if_nametoindex(if_name);
    return os_send_raw_eth_packet(if_index, buf, len);
}
