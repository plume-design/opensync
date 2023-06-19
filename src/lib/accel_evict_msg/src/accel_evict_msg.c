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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <netinet/if_ether.h>
#include <linux/if_packet.h>

#include "os.h"
#include "os_nif.h"
#include "log.h"
#include "util.h"
#include "accel_evict_msg.h"

#define FF_MAX_PAYLOAD    48

// LLC+XID packet defaults for layer 2 update frames
#define LLC_CR            0x01    // Response Bit
#define LLC_TEST_RSP      0xF3    // LLC frame TEST response


/**
 * @note OpenSync processes all LLC frames -> searches for L2UF messages. To avoid accidentally
 *       flushing by mac, we add flush frame ETH_P_PLUME_FM for the pcap filter. If you change
 * 	     this value, please apply appropriate corrections to all l2uf pcap filters.
 */
#define ETH_P_PLUME_FM    0x33	// LLC frame size == llc packet length


struct llc_hdr {
    uint8_t     dsap;
    uint8_t     ssap;
    uint8_t     control;
} __attribute__ ((packed)) ;

struct accel_evict_msg {
    struct      ethhdr eth_hdr;
    struct      llc_hdr	llc_hdr;
    uint8_t     data[FF_MAX_PAYLOAD];
} __attribute__ ((packed)) ;

static int accel_evict_msg_send_packet(const char *ifname, struct accel_evict_msg *ff, size_t ff_len)
{
    struct sockaddr_ll  saddr;
    int                 ifidx;
    int                 fd = -1;

    fd = socket(AF_PACKET, SOCK_RAW, 0);
    if (fd < 0)
    {
        LOGE("accel_evict_msg: Failed to created raw socket");
        return -1;
    }

    ifidx = if_nametoindex(ifname);
    if (ifidx == 0)
    {
        LOGE("accel_evict_msg: Failed to get sys_index for '%s'", ifname);
        goto error_exit;
    }

    memset(&saddr, 0, sizeof(saddr));
    saddr.sll_ifindex = ifidx;
    saddr.sll_halen   = ETHER_ADDR_LEN;
    memcpy(&saddr.sll_addr, &ff->eth_hdr.h_source, ETHER_ADDR_LEN);

    if (sendto(fd, ff, ff_len, 0, (const struct sockaddr *)&saddr, sizeof(saddr)) < 0)
    {
        LOGE("accel_evict_msg: sendto() failed, errno: %d", errno);
        goto error_exit;
    }
    close(fd);

    LOGD("accel_evict_msg: sent %u bytes to %s",(uint)ff_len, ifname);
    return 0;

error_exit:
    if (fd >= 0) { close(fd); }
    return -1;

}

int accel_evict_msg(const char *ifname, const os_macaddr_t *target_mac, const uint8_t *data, size_t data_len)
{
    struct accel_evict_msg evict_msg;
    os_macaddr_t local_mac;

    if(!os_nif_macaddr_get((char*)ifname, &local_mac))
    {
        LOGW("accel_evict_msg: unable to get %s mac address", ifname);
        return -1;
    }

    if(target_mac)
        memcpy(evict_msg.eth_hdr.h_dest, target_mac, ETHER_ADDR_LEN);
    else
        memset(evict_msg.eth_hdr.h_dest, 0xff, ETHER_ADDR_LEN);
    memcpy(evict_msg.eth_hdr.h_source, &local_mac, ETHER_ADDR_LEN);
    evict_msg.eth_hdr.h_proto = htons(ETH_P_PLUME_FM);

    evict_msg.llc_hdr.dsap = 0;
    evict_msg.llc_hdr.ssap = 0 | LLC_CR;
    evict_msg.llc_hdr.control = LLC_TEST_RSP;

    WARN_ON(data_len > FF_MAX_PAYLOAD);
    memcpy(evict_msg.data, data, (data_len < FF_MAX_PAYLOAD) ? data_len : FF_MAX_PAYLOAD);

    LOGD("accel_evict_msg: (%u) -> "PRI(os_macaddr_t), (uint)sizeof(struct accel_evict_msg), FMT(os_macaddr_t, *target_mac));

    return accel_evict_msg_send_packet(ifname, &evict_msg, sizeof(struct accel_evict_msg));
}
