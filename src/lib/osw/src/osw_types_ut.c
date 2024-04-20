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
                    int control_freq_mhz,
                    int center_freq0_mhz)
{
    assert(channel != NULL);

    memset(channel, 0, sizeof(*channel));
    channel->width = width;
    channel->control_freq_mhz = control_freq_mhz;
    channel->center_freq0_mhz = center_freq0_mhz;
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

OSW_UT(osw_channel_ut_from_op_class)
{
    struct osw_channel ref_channel;
    struct osw_channel channel;
    bool result;

    /* op_class: 81, channel: 2 */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_20MHZ, 2417, 2417);
    result = osw_channel_from_op_class(81, 2, &channel);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(memcmp(&ref_channel, &channel, sizeof(ref_channel)) == 0);

    /* op_class: 82, channel: 5 (invalid)*/
    result = osw_channel_from_op_class(82, 5, &channel);
    OSW_UT_EVAL(result == false);

    /* op_class: 128, channel: 36 */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_80MHZ, 5180, 5210);
    result = osw_channel_from_op_class(128, 36, &channel);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(memcmp(&ref_channel, &channel, sizeof(ref_channel)) == 0);

    /* op_class: 128, channel: 30, invalid channel */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_80MHZ, 5180, 5210);
    result = osw_channel_from_op_class(128, 30, &channel);
    OSW_UT_EVAL(result == false);

    /* op_class: 136, channel: 2 */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_20MHZ, 5935, 5935);
    result = osw_channel_from_op_class(136, 2, &channel);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(memcmp(&ref_channel, &channel, sizeof(ref_channel)) == 0);

    /* op_class: 131, channel: 9 */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_20MHZ, 5995, 5995);
    result = osw_channel_from_op_class(131, 9, &channel);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(memcmp(&ref_channel, &channel, sizeof(ref_channel)) == 0);

    /* op_class: 134, channel: 37 */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_160MHZ, 6135, 6185);
    result = osw_channel_from_op_class(134, 37, &channel);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(memcmp(&ref_channel, &channel, sizeof(ref_channel)) == 0);

    /* op_class: 83, channel: 6, ht40+ */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_40MHZ, 2437, 2447);
    result = osw_channel_from_op_class(83, 6, &channel);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(memcmp(&ref_channel, &channel, sizeof(ref_channel)) == 0);

    /* op_class: 84, channel: 6, ht40- */
    osw_channel_ut_init(&ref_channel, OSW_CHANNEL_40MHZ, 2437, 2427);
    result = osw_channel_from_op_class(84, 6, &channel);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(memcmp(&ref_channel, &channel, sizeof(ref_channel)) == 0);

    /* op_class: 83, channel: 11, ht40+, impossible */
    result = osw_channel_from_op_class(83, 11, &channel);
    OSW_UT_EVAL(result == false);

    /* op_class: 84, channel: 1, ht40-, impossible */
    result = osw_channel_from_op_class(84, 1, &channel);
    OSW_UT_EVAL(result == false);
}

OSW_UT(osw_channel_ut_to_op_class) {
    struct osw_channel channel;
    bool result;
    uint8_t op_class;

    /* op_class: 81, channel: 2 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_20MHZ, 2417, 2417);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 81);

    /* op_class: 128, channel: 36 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_80MHZ, 5180, 5210);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 128);

    /* op_class: 131, channel: 9 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_20MHZ, 5995, 5995);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 131);

    /* op_class: 131, channel: 37 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_20MHZ, 6135, 6135);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 131);

    /* op_class: 134, channel: 37 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_40MHZ, 6135, 6125);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 132);

    /* op_class: 134, channel: 37 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_80MHZ, 6135, 6065);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 133);

    /* op_class: 134, channel: 37 */
    osw_channel_ut_init(&channel, OSW_CHANNEL_160MHZ, 6135, 6185);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 134);

    /* op_class: 83, channel: 1, ht40+ */
    osw_channel_ut_init(&channel, OSW_CHANNEL_40MHZ, 2412, 2422);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 83);

    /* op_class: 83, channel: 6, ht40+ */
    osw_channel_ut_init(&channel, OSW_CHANNEL_40MHZ, 2437, 2447);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 83);

    /* op_class: 83, channel: 6, ht40- */
    osw_channel_ut_init(&channel, OSW_CHANNEL_40MHZ, 2437, 2427);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(op_class == 84);

    /* op_class: n/a, channel: 1, ht40- */
    osw_channel_ut_init(&channel, OSW_CHANNEL_40MHZ, 2412, 2402);
    result = osw_channel_to_op_class(&channel, &op_class);
    OSW_UT_EVAL(result == false);
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
        .center_freq0_mhz = 5180, /* ch36 */
    };
    struct osw_channel pri_dfs = {
        .control_freq_mhz = 5300, /* ch60 */
        .center_freq0_mhz = 5300, /* ch60 */
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

static void
osw_types_ut_recode_akm(const char *src,
                        uint32_t expected_suite,
                        enum osw_akm expected_akm,
                        const char *expected_cstr)
{
    const uint32_t suite = osw_suite_from_dash_str(src);
    const enum osw_akm akm = osw_suite_into_akm(suite);
    const char *akm_cstr = osw_akm_into_cstr(akm);
    char suite_buf[16] = {0};
    osw_suite_into_dash_str(suite_buf, sizeof(suite_buf), suite);

    LOGT("suite = %08"PRIx32 "(=> %s)", suite, suite_buf);
    LOGT("akm = %d (%s)", akm, akm_cstr);
    assert(suite == expected_suite);
    assert(akm == expected_akm);
    assert(strcmp(akm_cstr, expected_cstr) == 0);
    assert(strcmp(suite_buf, src) == 0);
}

OSW_UT(osw_types_akm)
{
    osw_types_ut_recode_akm("00-0f-ac-2", OSW_SUITE_AKM_RSN_PSK, OSW_AKM_RSN_PSK, "rsn-psk");
    osw_types_ut_recode_akm("00-50-f2-1", OSW_SUITE_AKM_WPA_8021X, OSW_AKM_WPA_8021X, "wpa-8021x");
    osw_types_ut_recode_akm("50-6f-9a-2", OSW_SUITE_AKM_WFA_DPP, OSW_AKM_WFA_DPP, "wfa-dpp");
}

static void
osw_types_ut_recode_cipher(const char *src,
                           uint32_t expected_suite,
                           enum osw_cipher expected_cipher,
                           const char *expected_cstr)
{
    const uint32_t suite = osw_suite_from_dash_str(src);
    const enum osw_cipher cipher = osw_suite_into_cipher(suite);
    const char *cipher_cstr = osw_cipher_into_cstr(cipher);
    char suite_buf[16] = {0};
    osw_suite_into_dash_str(suite_buf, sizeof(suite_buf), suite);

    LOGT("suite = %08"PRIx32 "(=> %s)", suite, suite_buf);
    LOGT("cipher = %d (%s)", cipher, cipher_cstr);
    assert(suite == expected_suite);
    assert(cipher == expected_cipher);
    assert(strcmp(cipher_cstr, expected_cstr) == 0);
    assert(strcmp(suite_buf, src) == 0);
}

OSW_UT(osw_types_cipher)
{
    osw_types_ut_recode_cipher("00-0f-ac-12", OSW_SUITE_CIPHER_RSN_BIP_GMAC_256, OSW_CIPHER_RSN_BIP_GMAC_256, "rsn-bip-gmac-256");
    osw_types_ut_recode_cipher("00-50-f2-5", OSW_SUITE_CIPHER_WPA_WEP_104, OSW_CIPHER_WPA_WEP_104, "wpa-wep-104");
}

OSW_UT(osw_types_from_channel_num_width)
{
    struct osw_channel c;

    OSW_UT_EVAL(osw_channel_control_fits_center(1, 1, OSW_CHANNEL_20MHZ) == true);
    OSW_UT_EVAL(osw_channel_control_fits_center(1, 3, OSW_CHANNEL_40MHZ) == true);
    OSW_UT_EVAL(osw_channel_control_fits_center(5, 3, OSW_CHANNEL_40MHZ) == true);
    OSW_UT_EVAL(osw_channel_control_fits_center(149, 155, OSW_CHANNEL_80MHZ) == true);
    OSW_UT_EVAL(osw_channel_control_fits_center(153, 155, OSW_CHANNEL_80MHZ) == true);
    OSW_UT_EVAL(osw_channel_control_fits_center(157, 155, OSW_CHANNEL_80MHZ) == true);
    OSW_UT_EVAL(osw_channel_control_fits_center(161, 155, OSW_CHANNEL_80MHZ) == true);
    OSW_UT_EVAL(osw_channel_control_fits_center(148, 155, OSW_CHANNEL_80MHZ) == false);
    OSW_UT_EVAL(osw_channel_control_fits_center(150, 155, OSW_CHANNEL_80MHZ) == false);
    OSW_UT_EVAL(osw_channel_control_fits_center(162, 155, OSW_CHANNEL_80MHZ) == false);

    /* These are ambiguous because of 6GHz */
    OSW_UT_EVAL(osw_channel_from_channel_num_width(1, OSW_CHANNEL_20MHZ, &c) == false);
    OSW_UT_EVAL(osw_channel_from_channel_num_width(2, OSW_CHANNEL_20MHZ, &c) == false);
    OSW_UT_EVAL(osw_channel_from_channel_num_width(5, OSW_CHANNEL_20MHZ, &c) == false);
    OSW_UT_EVAL(osw_channel_from_channel_num_width(157, OSW_CHANNEL_20MHZ, &c) == false);
    OSW_UT_EVAL(osw_channel_from_channel_num_width(157, OSW_CHANNEL_80MHZ, &c) == false);

    OSW_UT_EVAL(osw_channel_from_channel_num_width(4, OSW_CHANNEL_20MHZ, &c) == true);
    OSW_UT_EVAL(c.control_freq_mhz == 2427);
    OSW_UT_EVAL(c.center_freq0_mhz == 2427);
    OSW_UT_EVAL(c.width == OSW_CHANNEL_20MHZ);

    OSW_UT_EVAL(osw_channel_from_channel_num_width(36, OSW_CHANNEL_20MHZ, &c) == true);
    OSW_UT_EVAL(c.control_freq_mhz == 5180);
    OSW_UT_EVAL(c.center_freq0_mhz == 5180);
    OSW_UT_EVAL(c.width == OSW_CHANNEL_20MHZ);

    OSW_UT_EVAL(osw_channel_from_channel_num_width(40, OSW_CHANNEL_80MHZ, &c) == true);
    OSW_UT_EVAL(c.control_freq_mhz == 5200);
    OSW_UT_EVAL(c.center_freq0_mhz == 5210);
    OSW_UT_EVAL(c.width == OSW_CHANNEL_80MHZ);

    OSW_UT_EVAL(osw_channel_from_channel_num_width(69, OSW_CHANNEL_80MHZ, &c) == true);
    OSW_UT_EVAL(c.control_freq_mhz == 6295);
    OSW_UT_EVAL(c.center_freq0_mhz == 6305);
    OSW_UT_EVAL(c.width == OSW_CHANNEL_80MHZ);
}

OSW_UT(osw_channel_downgrade)
{
    struct osw_channel c1 = {
        .control_freq_mhz = 5180, /* 36 */
        .center_freq0_mhz = 5250, /* 50 */
        .width = OSW_CHANNEL_160MHZ,
    };
    const struct osw_channel c1_80 = {
        .control_freq_mhz = 5180, /* 36 */
        .center_freq0_mhz = 5210, /* 42 */
        .width = OSW_CHANNEL_80MHZ,
    };
    const struct osw_channel c1_40 = {
        .control_freq_mhz = 5180, /* 36 */
        .center_freq0_mhz = 5190, /* 38 */
        .width = OSW_CHANNEL_40MHZ,
    };
    const struct osw_channel c1_20 = {
        .control_freq_mhz = 5180, /* 36 */
        .center_freq0_mhz = 5180, /* 36 */
        .width = OSW_CHANNEL_20MHZ,
    };
    struct osw_channel c2 = {
        .control_freq_mhz = 5220, /* 44 */
        .center_freq0_mhz = 5250, /* 50 */
        .width = OSW_CHANNEL_160MHZ,
    };
    const struct osw_channel c2_80 = {
        .control_freq_mhz = 5220, /* 44 */
        .center_freq0_mhz = 5210, /* 42 */
        .width = OSW_CHANNEL_80MHZ,
    };
    const struct osw_channel c2_40 = {
        .control_freq_mhz = 5220, /* 44 */
        .center_freq0_mhz = 5230, /* 46 */
        .width = OSW_CHANNEL_40MHZ,
    };
    const struct osw_channel c2_20 = {
        .control_freq_mhz = 5220, /* 44 */
        .center_freq0_mhz = 5220, /* 42 */
        .width = OSW_CHANNEL_20MHZ,
    };
    struct osw_channel c3_80no36 = {
        .control_freq_mhz = 5220, /* 44 */
        .center_freq0_mhz = 5210, /* 42 */
        .width = OSW_CHANNEL_80MHZ,
        .puncture_bitmap = 0x0001,
    };
    struct osw_channel c3_80no40 = {
        .control_freq_mhz = 5220, /* 44 */
        .center_freq0_mhz = 5210, /* 42 */
        .width = OSW_CHANNEL_80MHZ,
        .puncture_bitmap = 0x0002,
    };
    struct osw_channel c3_80no48 = {
        .control_freq_mhz = 5220, /* 44 */
        .center_freq0_mhz = 5210, /* 42 */
        .width = OSW_CHANNEL_80MHZ,
        .puncture_bitmap = 0x0008,
    };
    struct osw_channel c4_80no36 = {
        .control_freq_mhz = 5200, /* 40 */
        .center_freq0_mhz = 5210, /* 42 */
        .width = OSW_CHANNEL_80MHZ,
        .puncture_bitmap = 0x0001,
    };
    struct osw_channel c4_80no44 = {
        .control_freq_mhz = 5200, /* 40 */
        .center_freq0_mhz = 5210, /* 42 */
        .width = OSW_CHANNEL_80MHZ,
        .puncture_bitmap = 0x0004,
    };
    struct osw_channel c4_80no48 = {
        .control_freq_mhz = 5200, /* 40 */
        .center_freq0_mhz = 5210, /* 42 */
        .width = OSW_CHANNEL_80MHZ,
        .puncture_bitmap = 0x0008,
    };

    assert(osw_channel_downgrade(&c1) == true);
    assert(c1.center_freq0_mhz == c1_80.center_freq0_mhz);

    assert(osw_channel_downgrade(&c1) == true);
    assert(c1.center_freq0_mhz == c1_40.center_freq0_mhz);

    assert(osw_channel_downgrade(&c1) == true);
    assert(c1.center_freq0_mhz == c1_20.center_freq0_mhz);

    assert(osw_channel_downgrade(&c1) == false);

    assert(osw_channel_downgrade(&c2) == true);
    LOGD("%d %d", c2.center_freq0_mhz, c2_80.center_freq0_mhz);
    assert(c2.center_freq0_mhz == c2_80.center_freq0_mhz);

    assert(osw_channel_downgrade(&c2) == true);
    assert(c2.center_freq0_mhz == c2_40.center_freq0_mhz);

    assert(osw_channel_downgrade(&c2) == true);
    assert(c2.center_freq0_mhz == c2_20.center_freq0_mhz);

    assert(osw_channel_downgrade(&c2) == false);

    assert(osw_channel_downgrade(&c3_80no36) == true);
    assert(c3_80no36.puncture_bitmap == 0x0000);

    assert(osw_channel_downgrade(&c3_80no40) == true);
    assert(c3_80no40.puncture_bitmap == 0x0000);

    assert(osw_channel_downgrade(&c3_80no48) == true);
    assert(c3_80no48.puncture_bitmap == 0x0002);

    assert(osw_channel_downgrade(&c4_80no36) == true);
    assert(c4_80no36.puncture_bitmap == 0x0001);

    assert(osw_channel_downgrade(&c4_80no44) == true);
    assert(c4_80no44.puncture_bitmap == 0x0000);

    assert(osw_channel_downgrade(&c4_80no48) == true);
    assert(c4_80no48.puncture_bitmap == 0x0000);
}

OSW_UT(osw_channel_ht40_offset)
{
    const struct osw_channel c36_20 = {
        .control_freq_mhz = 5180,
        .center_freq0_mhz = 5180,
        .width = OSW_CHANNEL_20MHZ,
    };
    const struct osw_channel c36_40 = {
        .control_freq_mhz = 5180,
        .center_freq0_mhz = 5190,
        .width = OSW_CHANNEL_40MHZ,
    };
    const struct osw_channel c40_40 = {
        .control_freq_mhz = 5200,
        .center_freq0_mhz = 5190,
        .width = OSW_CHANNEL_40MHZ,
    };
    const struct osw_channel c36_80 = {
        .control_freq_mhz = 5180,
        .center_freq0_mhz = 5210,
        .width = OSW_CHANNEL_80MHZ,
    };
    const struct osw_channel c40_80 = {
        .control_freq_mhz = 5200,
        .center_freq0_mhz = 5210,
        .width = OSW_CHANNEL_80MHZ,
    };
    const struct osw_channel c44_80 = {
        .control_freq_mhz = 5220,
        .center_freq0_mhz = 5210,
        .width = OSW_CHANNEL_80MHZ,
    };
    const struct osw_channel c48_80 = {
        .control_freq_mhz = 5240,
        .center_freq0_mhz = 5210,
        .width = OSW_CHANNEL_80MHZ,
    };

    OSW_UT_EVAL(osw_channel_ht40_offset(&c36_20) == 0);
    OSW_UT_EVAL(osw_channel_ht40_offset(&c36_40) == 1);
    OSW_UT_EVAL(osw_channel_ht40_offset(&c40_40) == -1);
    OSW_UT_EVAL(osw_channel_ht40_offset(&c36_80) == 1);
    OSW_UT_EVAL(osw_channel_ht40_offset(&c40_80) == -1);
    OSW_UT_EVAL(osw_channel_ht40_offset(&c44_80) == 1);
    OSW_UT_EVAL(osw_channel_ht40_offset(&c48_80) == -1);
}

OSW_UT(osw_op_class_to_band)
{
    OSW_UT_EVAL(osw_op_class_to_band(0) == OSW_BAND_UNDEFINED);
    OSW_UT_EVAL(osw_op_class_to_band(55) == OSW_BAND_UNDEFINED);
    OSW_UT_EVAL(osw_op_class_to_band(255) == OSW_BAND_UNDEFINED);

    OSW_UT_EVAL(osw_op_class_to_band(81) == OSW_BAND_2GHZ);
    OSW_UT_EVAL(osw_op_class_to_band(123) == OSW_BAND_5GHZ);
    OSW_UT_EVAL(osw_op_class_to_band(134) == OSW_BAND_6GHZ);
}
