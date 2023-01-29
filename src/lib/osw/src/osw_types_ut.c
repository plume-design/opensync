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

#include <osw_ut.h>

static void
osw_channel_ut_init(struct osw_channel *channel,
                    enum osw_channel_width width,
                    int control_freq_mhz)
{
    assert(channel != NULL);

    memset(channel, 0, sizeof(*channel));
    channel->width = width;
    channel->control_freq_mhz = control_freq_mhz;
}

OSW_UT(osw_hwaddr_ut_from_cstr)
{
    struct osw_hwaddr addr;

    const char *addr1_str = "ff:ff:ff:ff:ff:ff";
    const struct osw_hwaddr addr1 = { .octet = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, };

    OSW_UT_EVAL(osw_hwaddr_from_cstr(addr1_str, &addr) == true);
    OSW_UT_EVAL(osw_hwaddr_cmp(&addr1, &addr) == 0);

    const char *addr2_str = "e2:b4:f7:fc:4d:87";
    const struct osw_hwaddr addr2 = { .octet = { 0xe2, 0xb4, 0xf7, 0xfc, 0x4d, 0x87 }, };

    OSW_UT_EVAL(osw_hwaddr_from_cstr(addr2_str, &addr) == true);
    OSW_UT_EVAL(osw_hwaddr_cmp(&addr2, &addr) == 0);
}

OSW_UT(osw_channel_ut_from_op_class) {
    struct osw_channel ref_channel;
    struct osw_channel channel;
    bool result;

    /* op_class: 81, channel: 2 */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_20MHZ, 2417);
    result = osw_channel_from_op_class(81, 2, &channel);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(memcmp(&ref_channel, &channel, sizeof(ref_channel)) == 0);

    /* op_class: 82, channel: 5 (invalid)*/
    result = osw_channel_from_op_class(82, 5, &channel);
    OSW_UT_EVAL(result == false);

    /* op_class: 128, channel: 36 (best effort) FIXME is it "good enough" */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_80MHZ, 5180);
    result = osw_channel_from_op_class(128, 36, &channel);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(memcmp(&ref_channel, &channel, sizeof(ref_channel)) == 0);

    /* op_class: 131, channel: 9 */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_20MHZ, 5995);
    result = osw_channel_from_op_class(131, 9, &channel);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(memcmp(&ref_channel, &channel, sizeof(ref_channel)) == 0);

    /* op_class: 134, channel: 37 */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_160MHZ, 6135);
    result = osw_channel_from_op_class(134, 37, &channel);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(memcmp(&ref_channel, &channel, sizeof(ref_channel)) == 0);
}

OSW_UT(osw_channel_ut_to_op_class) {
    struct osw_channel channel;
    bool result;
    uint8_t op_class;

    /* op_class: 81, channel: 2 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_20MHZ, 2417);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 81);

    /* op_class: 128, channel: 36 (best effort) FIXME is it "good enough" */
    osw_channel_ut_init(&channel, OSW_CHANNEL_80MHZ, 5180);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 128);

    /* op_class: 131, channel: 9 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_20MHZ, 5995);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 131);

    /* op_class: 131, channel: 37 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_20MHZ, 6135);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 131);

    /* op_class: 134, channel: 37 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_40MHZ, 6135);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 132);

    /* op_class: 134, channel: 37 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_80MHZ, 6135);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 133);

    /* op_class: 134, channel: 37 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_160MHZ, 6135);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 134);
}

OSW_UT(osw_op_class_to_20mhz)
{
    uint8_t op_class_20_mhz;

    OSW_UT_EVAL(osw_op_class_to_20mhz(81, 2, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 81);
    OSW_UT_EVAL(osw_op_class_to_20mhz(82, 14, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 82);
    OSW_UT_EVAL(osw_op_class_to_20mhz(115, 36, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 115);
    OSW_UT_EVAL(osw_op_class_to_20mhz(116, 44, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 115);
    OSW_UT_EVAL(osw_op_class_to_20mhz(117, 48, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 115);
    OSW_UT_EVAL(osw_op_class_to_20mhz(118, 56, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 118);
    OSW_UT_EVAL(osw_op_class_to_20mhz(119, 52, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 118);
    OSW_UT_EVAL(osw_op_class_to_20mhz(120, 56, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 118);
    OSW_UT_EVAL(osw_op_class_to_20mhz(121, 100, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 121);
    OSW_UT_EVAL(osw_op_class_to_20mhz(122, 100, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 121);
    OSW_UT_EVAL(osw_op_class_to_20mhz(123, 112, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 121);
    OSW_UT_EVAL(osw_op_class_to_20mhz(124, 157, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 124);
    OSW_UT_EVAL(osw_op_class_to_20mhz(125, 153, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 125);
    OSW_UT_EVAL(osw_op_class_to_20mhz(126, 157, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 125);
    OSW_UT_EVAL(osw_op_class_to_20mhz(127, 153, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 125);
    OSW_UT_EVAL(osw_op_class_to_20mhz(128, 40, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 115);
    OSW_UT_EVAL(osw_op_class_to_20mhz(129, 56, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 118);
    OSW_UT_EVAL(osw_op_class_to_20mhz(130, 157, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 125);
    OSW_UT_EVAL(osw_op_class_to_20mhz(131, 193, &op_class_20_mhz) == true);
    OSW_UT_EVAL(op_class_20_mhz == 131);
    /* TODO Add UTs for remaining op_classes*/

    /* Invalid input */
    OSW_UT_EVAL(osw_op_class_to_20mhz(82, 5, &op_class_20_mhz) == false);
    OSW_UT_EVAL(osw_op_class_to_20mhz(127, 154, &op_class_20_mhz) == false);
}

OSW_UT(osw_types_ut_2g_chan_list)
{
    const int *list;

    list = osw_2g_chan_list(1, 20, 11);
    assert(list != NULL);
    assert(list[0] == 1);
    assert(list[1] == 0);

    list = osw_2g_chan_list(11, 20, 11);
    assert(list != NULL);
    assert(list[0] == 11);
    assert(list[1] == 0);

    list = osw_2g_chan_list(13, 20, 13);
    assert(list != NULL);
    assert(list[0] == 13);
    assert(list[1] == 0);

    list = osw_2g_chan_list(13, 20, 11);
    assert(list == NULL);

    list = osw_2g_chan_list(1, 40, 11);
    assert(list != NULL);
    assert(list[0] == 1);
    assert(list[1] == 5);
    assert(list[2] == 0);

    /* Current expectation is that HT40+ is always preferred
     * until it is not possible to do, at which point HT40-
     * should be returned.
     */
    list = osw_2g_chan_list(6, 40, 11);
    assert(list != NULL);
    assert(list[0] == 6);
    assert(list[1] == 10);
    assert(list[2] == 0);

    list = osw_2g_chan_list(11, 40, 11);
    assert(list != NULL);
    assert(list[0] == 7);
    assert(list[1] == 11);
    assert(list[2] == 0);

    list = osw_2g_chan_list(13, 40, 11);
    assert(list == NULL);

    list = osw_2g_chan_list(9, 40, 11);
    assert(list != NULL);
    assert(list[0] == 5);
    assert(list[1] == 9);
    assert(list[2] == 0);

    list = osw_2g_chan_list(9, 40, 13);
    assert(list != NULL);
    assert(list[0] == 9);
    assert(list[1] == 13);
    assert(list[2] == 0);
}

OSW_UT(osw_types_dfs_overlap)
{
    struct osw_channel non_dfs = {
        .control_freq_mhz = 5180, /* ch36 */
    };
    struct osw_channel pri_dfs = {
        .control_freq_mhz = 5300, /* ch60 */
    };
    struct osw_channel sb_dfs = {
        .control_freq_mhz = 5180, /* ch36 @ 160MHz */
        .width = OSW_CHANNEL_160MHZ,
        .center_freq0_mhz = 5250, /* ch50 */
    };

    assert(osw_channel_overlaps_dfs(&non_dfs) == false);
    assert(osw_channel_overlaps_dfs(&pri_dfs) == true);
    assert(osw_channel_overlaps_dfs(&sb_dfs) == true);
}
