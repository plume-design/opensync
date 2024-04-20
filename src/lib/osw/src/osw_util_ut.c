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

#include <log.h>
#include <osw_ut.h>

/*
 * Frame 1 (this hexdump can be imported in WireShark)
 * - no RRM Neighbor Report
 * - no BSS Transition
 * 0000   00 00 3a 01 02 00 00 00 03 00 02 00 00 00 00 00
 * 0010   02 00 00 00 03 00 30 00 31 04 05 00 00 08 74 65
 * 0020   73 74 2d 73 61 65 01 08 02 04 0b 16 0c 12 18 24
 * 0030   32 04 30 48 60 6c 30 14 01 00 00 0f ac 04 01 00
 * 0040   00 0f ac 04 01 00 00 0f ac 08 00 00 2d 1a 3c 10
 * 0050   1b ff ff 00 00 00 00 00 00 00 00 00 00 01 00 00
 * 0060   00 00 00 00 00 00 00 00 7f 0a 04 00 02 02 01 40
 * 0070   00 40 00 01 3b 15 51 51 52 53 54 73 74 75 76 77
 * 0080   78 79 7a 7b 7c 7d 7e 7f 80 81 82 dd 07 00 50 f2
 * 0090   02 00 01 00
 */
const uint8_t osw_util_ut_non_11kv_assoc_ies[] = {
    0x00, 0x08, 0x74, 0x65, 0x73, 0x74, 0x2d, 0x73, 0x61, 0x65, 0x01, 0x08,
    0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18, 0x24, 0x32, 0x04, 0x30, 0x48, 0x60, 0x6c, 0x30, 0x14,
    0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f,
    0xac, 0x08, 0x00, 0x00, 0x2d, 0x1a, 0x3c, 0x10, 0x1b, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7f, 0x0a, 0x04, 0x00, 0x02, 0x02, 0x01, 0x40, 0x00, 0x40, 0x00, 0x01, 0x3b, 0x15, 0x51, 0x51,
    0x52, 0x53, 0x54, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x80, 0x81, 0x82, 0xdd, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00
};
const size_t osw_util_ut_non_11kv_assoc_ies_len = sizeof(osw_util_ut_non_11kv_assoc_ies);

/*
 * Frame 2 (this hexdump can be imported in WireShark)
 * - support RRM Neighbor Report
 * - support BTM
 * 0000   00 00 3c 00 fe 9f 07 00 dd 92 b6 2a 26 94 6a cf
 * 0010   fe 9f 07 00 dd 92 70 86 11 11 14 00 00 05 43 78
 * 0020   54 48 32 01 08 8c 12 98 24 b0 48 60 6c 21 02 f9
 * 0030   15 24 0a 24 04 34 04 64 0c 95 04 a5 01 30 26 01
 * 0040   00 00 0f ac 04 01 00 00 0f ac 04 01 00 00 0f ac
 * 0050   08 cc 00 01 00 12 3c 42 86 4d 1d 68 a9 21 fe d9
 * 0060   ef e7 59 91 7e 46 05 33 08 01 00 00 2d 1a 6f 00
 * 0070   1b ff ff 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 0080   00 00 00 00 00 00 00 00 7f 08 00 00 08 00 00 00
 * 0090   00 40 bf 0c 32 70 81 0f fa ff 00 00 fa ff 00 00
 * 00a0   ff 1c 23 01 08 08 00 00 80 44 30 02 00 1d 00 9f
 * 00b0   08 00 0c 00 fa ff fa ff 39 1c c7 71 1c 07 dd 0b
 * 00c0   00 17 f2 0a 00 01 04 00 00 00 00 dd 05 00 90 4c
 * 00d0   04 07 dd 0a 00 10 18 02 00 00 10 00 00 02 dd 07
 * 00e0   00 50 f2 02 00 01 00
 */
const uint8_t osw_util_ut_11kv_assoc_ies[] = {
    0x00, 0x05, 0x43, 0x78, 0x54, 0x48, 0x32, 0x01, 0x08, 0x8c, 0x12, 0x98,
    0x24, 0xb0, 0x48, 0x60, 0x6c, 0x21, 0x02, 0xf9, 0x15, 0x24, 0x0a, 0x24, 0x04, 0x34, 0x04, 0x64,
    0x0c, 0x95, 0x04, 0xa5, 0x01, 0x30, 0x26, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00,
    0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x08, 0xcc, 0x00, 0x01, 0x00, 0x12, 0x3c, 0x42,
    0x86, 0x4d, 0x1d, 0x68, 0xa9, 0x21, 0xfe, 0xd9, 0xef, 0xe7, 0x59, 0x91, 0x7e, 0x46, 0x05, 0x33,
    0x08, 0x01, 0x00, 0x00, 0x2d, 0x1a, 0x6f, 0x00, 0x1b, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7f, 0x08, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x40, 0xbf, 0x0c, 0x32, 0x70, 0x81, 0x0f,
    0xfa, 0xff, 0x00, 0x00, 0xfa, 0xff, 0x00, 0x00, 0xff, 0x1c, 0x23, 0x01, 0x08, 0x08, 0x00, 0x00,
    0x80, 0x44, 0x30, 0x02, 0x00, 0x1d, 0x00, 0x9f, 0x08, 0x00, 0x0c, 0x00, 0xfa, 0xff, 0xfa, 0xff,
    0x39, 0x1c, 0xc7, 0x71, 0x1c, 0x07, 0xdd, 0x0b, 0x00, 0x17, 0xf2, 0x0a, 0x00, 0x01, 0x04, 0x00,
    0x00, 0x00, 0x00, 0xdd, 0x05, 0x00, 0x90, 0x4c, 0x04, 0x07, 0xdd, 0x0a, 0x00, 0x10, 0x18, 0x02,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x02, 0xdd, 0x07,  0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00
};
const size_t osw_util_ut_11kv_assoc_ies_len = sizeof(osw_util_ut_11kv_assoc_ies);

OSW_UT(osw_util_ut_parse_assoc_req_ies) {
    struct osw_assoc_req_info info;

    /* Frame 1 */
    OSW_UT_EVAL(osw_parse_assoc_req_ies(&osw_util_ut_non_11kv_assoc_ies, sizeof(osw_util_ut_non_11kv_assoc_ies), &info) == true);
    OSW_UT_EVAL(info.wnm_bss_trans == false);
    OSW_UT_EVAL(info.rrm_neighbor_bcn_act_meas == false);

    /* Frame 2 */
    OSW_UT_EVAL(osw_parse_assoc_req_ies(&osw_util_ut_11kv_assoc_ies, sizeof(osw_util_ut_11kv_assoc_ies), &info) == true);
    OSW_UT_EVAL(info.wnm_bss_trans == true);
    OSW_UT_EVAL(info.rrm_neighbor_bcn_act_meas == true);
}

OSW_UT(osw_util_ut_circ_buf_basic_usage) {
    const size_t buf_size = 4;
    int buf[buf_size];
    size_t i;
    bool result;
    struct osw_circ_buf circ_buf;

    /* Empty buffer */
    osw_circ_buf_init(&circ_buf, buf_size);
    OSW_UT_EVAL(osw_circ_buf_is_empty(&circ_buf) == true);
    OSW_UT_EVAL(osw_circ_buf_is_full(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_head(&circ_buf) == osw_circ_buf_tail(&circ_buf));

    /* Push first element */
    result = osw_circ_buf_push(&circ_buf, &i);
    OSW_UT_EVAL(result == true);
    buf[i] = 10;

    OSW_UT_EVAL(osw_circ_buf_is_empty(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_is_full(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_head(&circ_buf) != osw_circ_buf_tail(&circ_buf));

    i = osw_circ_buf_head(&circ_buf);
    OSW_UT_EVAL(buf[i] == 10);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(i == osw_circ_buf_tail(&circ_buf));

    /* Push second element */
    result = osw_circ_buf_push(&circ_buf, &i);
    OSW_UT_EVAL(result == true);
    buf[i] = 11;

    OSW_UT_EVAL(osw_circ_buf_is_empty(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_is_full(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_head(&circ_buf) != osw_circ_buf_tail(&circ_buf));

    i = osw_circ_buf_head(&circ_buf);
    OSW_UT_EVAL(buf[i] == 10);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(buf[i] == 11);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(i == osw_circ_buf_tail(&circ_buf));

    /* Push third element filling the buffer */
    result = osw_circ_buf_push(&circ_buf, &i);
    OSW_UT_EVAL(result == true);
    buf[i] = 12;

    OSW_UT_EVAL(osw_circ_buf_is_empty(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_is_full(&circ_buf) == true);
    OSW_UT_EVAL(osw_circ_buf_head(&circ_buf) != osw_circ_buf_tail(&circ_buf));

    i = osw_circ_buf_head(&circ_buf);
    OSW_UT_EVAL(buf[i] == 10);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(buf[i] == 11);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(buf[i] == 12);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(i == osw_circ_buf_tail(&circ_buf));

    /* Push next value */
    result = osw_circ_buf_push(&circ_buf, &i);
    OSW_UT_EVAL(result == false);

    i = osw_circ_buf_push_rotate(&circ_buf);
    buf[i] = 13;

    OSW_UT_EVAL(osw_circ_buf_is_empty(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_is_full(&circ_buf) == true);
    OSW_UT_EVAL(osw_circ_buf_head(&circ_buf) != osw_circ_buf_tail(&circ_buf));

    i = osw_circ_buf_head(&circ_buf);
    OSW_UT_EVAL(buf[i] == 11);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(buf[i] == 12);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(buf[i] == 13);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(i == osw_circ_buf_tail(&circ_buf));

    /* Push one more value */
    result = osw_circ_buf_push(&circ_buf, &i);
    OSW_UT_EVAL(result == false);

    i = osw_circ_buf_push_rotate(&circ_buf);
    buf[i] = 14;

    OSW_UT_EVAL(osw_circ_buf_is_empty(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_is_full(&circ_buf) == true);
    OSW_UT_EVAL(osw_circ_buf_head(&circ_buf) != osw_circ_buf_tail(&circ_buf));

    i = osw_circ_buf_head(&circ_buf);
    OSW_UT_EVAL(buf[i] == 12);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(buf[i] == 13);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(buf[i] == 14);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(i == osw_circ_buf_tail(&circ_buf));

    /* Pop first value */
    result = osw_circ_buf_pop(&circ_buf, &i);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(buf[i] == 12);

    OSW_UT_EVAL(osw_circ_buf_is_empty(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_is_full(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_head(&circ_buf) != osw_circ_buf_tail(&circ_buf));

    i = osw_circ_buf_head(&circ_buf);
    OSW_UT_EVAL(buf[i] == 13);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(buf[i] == 14);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(i == osw_circ_buf_tail(&circ_buf));

    /* Pop second value */
    result = osw_circ_buf_pop(&circ_buf, &i);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(buf[i] == 13);

    OSW_UT_EVAL(osw_circ_buf_is_empty(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_is_full(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_head(&circ_buf) != osw_circ_buf_tail(&circ_buf));

    i = osw_circ_buf_head(&circ_buf);
    OSW_UT_EVAL(buf[i] == 14);
    i = osw_circ_buf_next(&circ_buf, i);
    OSW_UT_EVAL(i == osw_circ_buf_tail(&circ_buf));

    /* Pop last values */
    result = osw_circ_buf_pop(&circ_buf, &i);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(buf[i] == 14);

    OSW_UT_EVAL(osw_circ_buf_is_empty(&circ_buf) == true);
    OSW_UT_EVAL(osw_circ_buf_is_full(&circ_buf) == false);
    OSW_UT_EVAL(osw_circ_buf_head(&circ_buf) == osw_circ_buf_tail(&circ_buf));
}

OSW_UT(osw_util_ut_circ_buf_foreach_macro) {
    const size_t buf_size = 4;
    const int ref_buf[] = { 12, 13, 14, };
    bool cmp_buf[] = { false, false, false, };
    int buf[buf_size];
    size_t ref_i;
    size_t i;
    struct osw_circ_buf circ_buf;

    /* Init & fill buffer */
    osw_circ_buf_init(&circ_buf, buf_size);

    i = osw_circ_buf_push_rotate(&circ_buf);
    buf[i] = 12;
    i = osw_circ_buf_push_rotate(&circ_buf);
    buf[i] = 13;
    i = osw_circ_buf_push_rotate(&circ_buf);
    buf[i] = 14;

    /* Check macro */
    ref_i = 0;
    OSW_CIRC_BUF_FOREACH(&circ_buf, i) {
        cmp_buf[ref_i] = buf[i] == ref_buf[ref_i];
        ref_i++;
    }

    OSW_UT_EVAL(ref_i == 3);
    OSW_UT_EVAL(cmp_buf[0] == true);
    OSW_UT_EVAL(cmp_buf[1] == true);
    OSW_UT_EVAL(cmp_buf[2] == true);
}

OSW_UT(osw_ies_supported_channels_2ghz)
{
    unsigned char ies[] = {
        DOT11_SUPPORTED_CHANNELS,
        0,
        1, 11,
    };
    ies[1] = sizeof(ies) - 2;
    const struct element *e = (const struct element *)ies;

    struct osw_assoc_req_info info;
    MEMZERO(info);
    osw_parse_supported_channels(e, &info);

    const int expected[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    };

    ASSERT(ARRAY_SIZE(expected) == info.channel_cnt,
            strfmta("wrong channel count, expected %zu found %u",
                ARRAY_SIZE(expected),
                info.channel_cnt));

    size_t i;
    for (i = 0; i < info.channel_cnt; i++) {
        ASSERT(info.channel_list[i] == expected[i],
                strfmta("wrong channel at %zu, expected %d found %d",
                    i, expected[i], info.channel_list[i]));
    }
}

OSW_UT(osw_ies_supported_channels_5ghz)
{
    unsigned char ies[] = {
        DOT11_SUPPORTED_CHANNELS,
        0,
        36, 4,
        52, 4,
        100, 11,
        149, 4,
        165, 1,
    };
    ies[1] = sizeof(ies) - 2;
    const struct element *e = (const struct element *)ies;

    struct osw_assoc_req_info info;
    MEMZERO(info);
    osw_parse_supported_channels(e, &info);

    const int expected[] = {
        36, 40, 44, 48,
        52, 56, 60, 64,
        100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140,
        149, 153, 157, 161,
        165,
    };

    ASSERT(ARRAY_SIZE(expected) == info.channel_cnt,
            strfmta("wrong channel count, expected %zu found %u",
                ARRAY_SIZE(expected),
                info.channel_cnt));

    size_t i;
    for (i = 0; i < info.channel_cnt; i++) {
        ASSERT(info.channel_list[i] == expected[i],
                strfmta("wrong channel at %zu, expected %d found %d",
                    i, expected[i], info.channel_list[i]));
    }
}

OSW_UT(osw_ies_supported_channels_bounds)
{
    unsigned char ies[] = {
        DOT11_SUPPORTED_CHANNELS,
        0,
        1, 200,
        1, 200,
        1, 200,
    };
    ies[1] = sizeof(ies) - 2;
    const struct element *e = (const struct element *)ies;


    struct osw_assoc_req_info info;
    MEMZERO(info);
    osw_parse_supported_channels(e, &info);

    ASSERT(info.channel_cnt == ARRAY_SIZE(info.channel_list),
            "out of bounds");
}
