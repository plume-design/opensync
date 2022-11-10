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

#include <stdbool.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "unity.h"
#include "memutil.h"

#include "log.h"
#include "net_header_parse.h"

#include "unit_test_utils.h"
#include "fsm_csum_utils.h"

struct net_header_parser    net_parser;

/* DHCP v6 Request Frame (164 bytes)*/
static unsigned char pkt51[164] = {
0x33, 0x33, 0x00, 0x01, 0x00, 0x02, 0xdc, 0xa6, /* 33...... */
0x32, 0x4f, 0x80, 0xa6, 0x86, 0xdd, 0x60, 0x05, /* 2O....`. */
0x61, 0x97, 0x00, 0x6e, 0x11, 0x01, 0xfe, 0x80, /* a..n.... */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xde, 0xa6, /* ........ */
0x32, 0xff, 0xfe, 0x4f, 0x80, 0xa6, 0xff, 0x02, /* 2..O.... */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x02, 0x22, /* ......." */
0x02, 0x23, 0x00, 0x6e, 0x1b, 0xa1, 0x03, 0xef, /* .#.n.... */
0x46, 0x06, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x01, /* F....... */
0x00, 0x01, 0x27, 0xb2, 0x03, 0xf6, 0xdc, 0xa6, /* ..'..... */
0x32, 0x4f, 0x80, 0xa6, 0x00, 0x02, 0x00, 0x0e, /* 2O...... */
0x00, 0x01, 0x00, 0x01, 0x28, 0x5f, 0xfb, 0x3a, /* ....(_.: */
0xfa, 0x75, 0xbc, 0x4e, 0x20, 0x96, 0x00, 0x06, /* .u.N ... */
0x00, 0x08, 0x00, 0x17, 0x00, 0x18, 0x00, 0x27, /* .......' */
0x00, 0x1f, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00, /* ........ */
0x00, 0x03, 0x00, 0x28, 0x32, 0x4f, 0x80, 0xa6, /* ...(2O.. */
0x00, 0x00, 0x0e, 0x10, 0x00, 0x00, 0x15, 0x18, /* ........ */
0x00, 0x05, 0x00, 0x18, 0x26, 0x01, 0x06, 0x47, /* ....&..G */
0x45, 0x00, 0x02, 0xc2, 0x00, 0x00, 0x00, 0x00, /* E....... */
0xb4, 0x10, 0x18, 0x3d, 0x00, 0x00, 0x1c, 0x20, /* ...=...  */
0x00, 0x00, 0x1d, 0x4c                          /* ...L */
};

void
test_udp_csum_calculation(void)
{
    size_t      len = 0;
    uint16_t    csum = 0;
    uint16_t    expected_csum = ntohs(0x1ba1);

    memset(&net_parser, 0, sizeof(struct net_header_parser));

    UT_CREATE_PCAP_PAYLOAD(pkt51, &net_parser);
    len = sizeof(pkt51);
    TEST_ASSERT_TRUE(len != 0);

    net_parser.packet_len = len;
    net_parser.data = (uint8_t *)pkt51;

    len = net_header_parse(&net_parser);
    TEST_ASSERT_TRUE(len != 0);

    csum = fsm_compute_udp_checksum(pkt51, &net_parser);
    TEST_ASSERT_EQUAL_INT16(expected_csum, csum);
}

void
run_test_fsm_csum_utils(void)
{
    RUN_TEST(test_udp_csum_calculation);
}
