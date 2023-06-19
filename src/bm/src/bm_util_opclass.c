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

#include <os_types.h>
#include "bm_util_opclass.h"

#define BM_UTIL_OPCLASS_MAP_MAX_CHAN_BUF 16

struct ieee80211_global_op_class_record {
    uint8_t  band;
    uint8_t  channels[BM_UTIL_OPCLASS_MAP_MAX_CHAN_BUF];
    uint16_t width;
    uint8_t  op_class;
};

static const struct ieee80211_global_op_class_record op_class_map[] = {
    {24,    {1,2,3,4,5,6,7,8,9,10,11,12,13,0},                     20,   81},
    {24,    {14,0},                                                20,   82},
    {24,    {1,2,3,4,5,6,7,8,9,0},                                 40,   83},
    {24,    {5,6,7,8,9,10,11,12,13,0},                             40,   84},
    {50,    {36,40,44,48,0},                                       20,   115},
    {50,    {52,56,60,64,0},                                       20,   118},
    {50,    {100,104,108,112,116,120,124,128,132,136,140,144,0},   20,   121},
    {50,    {149,153,157,161,0},                                   20,   124},
    {50,    {36,44,0},                                             40,   116},
    {50,    {40,48,0},                                             40,   117},
    {50,    {52,60,0},                                             40,   119},
    {50,    {56,64,0},                                             40,   120},
    {50,    {100,108,116,124,132,140,0},                           40,   122},
    {50,    {104,112,120,128,136,144,0},                           40,   123},
    {50,    {149,157,0},                                           40,   126},
    {50,    {153,161,0},                                           40,   127},
    {50,    {0},                                                   80,   128},
    {50,    {0},                                                   160,  129},
    {60,    {0},                                                   20,   131},
    {60,    {0},                                                   40,   132},
    {60,    {0},                                                   80,   133},
    {60,    {0},                                                   160,  134},
    {60,    {0},                                                   320,  137},
    {0,     {0},                                                   0,    0}
};

static bool
ieee80211_global_op_class_is_channel_in_list(const uint8_t channel,
                                             const uint8_t *channel_list)
{
    const uint8_t *chan_it = channel_list;

    if (channel_list == NULL) return false;

    while (*chan_it != 0) {
        if (channel == *chan_it) return true;
        chan_it++;
    }

    return false;
}

uint8_t
ieee80211_global_op_class_get(const uint8_t band,
                              const uint8_t channel,
                              const uint16_t width)
{
    uint8_t ret_val = 0;
    const struct ieee80211_global_op_class_record *record;

    record = op_class_map;
    while (record->band != 0) {
        if (band == record->band) {
            if (width == record->width) {
                const bool is_list_empty = (record->channels[0] == 0);
                const bool list_contains_all_channels = is_list_empty;
                const bool is_channel_in_list = (ieee80211_global_op_class_is_channel_in_list(channel, record->channels));
                if (is_channel_in_list == true ||
                    list_contains_all_channels == true) {
                    ret_val = record->op_class;
                    break;
                }
            }
        }
        record++;
    }

    return ret_val;
}

bool
ieee80211_global_op_class_is_contained_in(const uint8_t superset_op_class, const uint8_t subset_op_class)
{
    static const int op_class_list[] = {
         /* <op_class>, <sub op_class1>, <sub op_class2>..., 0, */
          81, 0,
          82, 81, 0,
          83, 81, 84, 0,
          84, 81, 83, 0,
         115, 0,
         116, 115, 117, 0,
         117, 115, 116, 0,
         118, 0,
         119, 118, 120, 0,
         120, 118, 119, 0,
         121, 0,
         122, 121, 123, 0,
         123, 121, 122, 0,
         124, 0,
         125, 124, 0,
         126, 124, 125, 127, 0,
         127, 124, 125, 126, 0,
         128, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 0,
         129, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 0,
         130, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 0,
         131, 0,
         132, 131, 0,
         133, 131, 132, 0,
         134, 131, 132, 133, 0,
         135, 131, 132, 133, 0,
         136, 0,
         -1,  /* keep last */
    };

    const int *p;

    for (p = op_class_list; *p != -1; p++) {
        if (*p == superset_op_class) {
            while (*p != 0) {
                if (*p == subset_op_class)
                    return true;
                p++;
            }
        } else {
            while (*p != 0) p++;
        }
    }
    return false;
}

bool ieee80211_global_op_class_is_channel_supported(const uint8_t op_class, const uint8_t chan)
{
    /* Table E-4 in IEEE Std 802.11-2012 - Global operating classes */
    static const int op_class_channels_list[] = {
         /* <op_class>, <sub op_class1>, <sub op_class2>..., 0, */
          81, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, /* 20 MHz */
          82, 14, 0, /* 20 MHz */
          83, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, /* 40 MHz */
          84, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, /* 40 MHz */
         115, 36, 40, 44, 48, 0, /* indoor only */
         116, 36, 44, 0,  /* indoor only 40 MHz */
         117, 40, 48, 0, /* indoor only 40 MHz */
         118, 52, 56, 60, 64, 0,  /* dfs */
         119, 52, 60, 0, /* dfs 40 MHz */
         120, 56, 64, 0, /* dfs 40 MHz */
         121, 100, 104, 108, 112, 116, 120, 124, 128, 132, 140, 144, 0, /* 20 MHz */
         122, 100, 108, 116, 124, 132, 142, 0, /* 40 MHz */
         123, 104, 112, 120, 128, 136, 0, /* 40 MHz */
         124, 149, 153, 157, 161, 0, /* 20 MHz */
         125, 149, 153, 157, 161, 165, 169, 173, 177, 0, /* 20 MHz */
         126, 149, 157, 165, 173, 0, /* 40 MHz */
         127, 153, 161, 169, 177, 0, /* 40 MHz */
         128, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177, 0, /* center freqs 42, 58, 106, 122, 138, 155, 171 80 MHz */
         129, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 149, 153, 157, 161, 165, 169, 173, 177, 0, /* center freqs 50, 114, 163; 160 MHz */
         130, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177, 0, /* center freqs 42, 58, 106, 122, 138, 155, 171 80 MHz */
         131, 1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 93, 97, 101,
              105, 109, 113, 117, 121, 125, 129, 133, 137, 141, 145, 149, 153, 157, 161, 165, 169, 173, 177, 181,
              185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 233, 0, /* UHB channels, 20 MHz */
         132, 1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 93, 97, 101,
              105, 109, 113, 117, 121, 125, 129, 133, 137, 141, 145, 149, 153, 157, 161, 165, 169, 173, 177, 181,
              185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 233, 0, /* UHB channels, 40 MHz */
         133, 1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 93, 97, 101,
              105, 109, 113, 117, 121, 125, 129, 133, 137, 141, 145, 149, 153, 157, 161, 165, 169, 173, 177, 181,
              185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 233, 0, /* UHB channels, 80 MHz */
         134, 1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 93, 97, 101,
              105, 109, 113, 117, 121, 125, 129, 133, 137, 141, 145, 149, 153, 157, 161, 165, 169, 173, 177, 181,
              185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 233, 0, /* UHB channels, 160 MHz */
         135, 1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 93, 97, 101,
              105, 109, 113, 117, 121, 125, 129, 133, 137, 141, 145, 149, 153, 157, 161, 165, 169, 173, 177, 181,
              185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 233, 0, /* UHB channels, 80+80 MHz */
         136, 2, 0, /* UHB channels, 20 MHz */
         137, 1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 93, 97, 101,
              105, 109, 113, 117, 121, 125, 129, 133, 137, 141, 145, 149, 153, 157, 161, 165, 169, 173, 177, 181,
              185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 233, 0, /* UHB channels, 320 MHz: 31, 63, 95, 127, 159, 191 */
         -1,  /* keep last */
    };

    const int *p;

    for (p = op_class_channels_list; *p != -1; p++) {
        if (*p == op_class) {
            p++;
            while (*p != 0) {
                if (*p == chan)
                    return true;
                p++;
            }
        } else {
            while (*p != 0) p++;
        }
    }
    return false;
}

uint8_t ieee80211_global_op_class_to_20mhz_op_class(const uint8_t op_class, const uint8_t chan)
{
    switch (op_class) {
        case 81:
        case 83:
        case 84:
            return 81;
        case 82:
            return 82;
        case 115:
        case 116:
        case 117:
            return 115;
        case 118:
        case 119:
        case 120:
           return 118;
        case 121:
        case 122:
        case 123:
            return 121;
        case 124:
            return 124;
        case 125:
        case 126:
        case 127:
            return 125;
        case 128:
        case 129:
        case 130:
            if (ieee80211_global_op_class_is_channel_supported(115, chan))
                return 115;
            if (ieee80211_global_op_class_is_channel_supported(118, chan))
                return 118;
            if (ieee80211_global_op_class_is_channel_supported(121, chan))
                return 121;
            if (ieee80211_global_op_class_is_channel_supported(125, chan))
                return 125;
            break;
        case 131:
        case 132:
        case 133:
        case 134:
        case 135:
        case 137:
            return 131;
        case 136:
            return 136;
    }
    return 0;
}

bool ieee80211_global_op_class_is_2ghz(const uint8_t op_class)
{
    if (op_class >= 81 && op_class <= 84)
        return true;

    return false;
}

bool ieee80211_global_op_class_is_5ghz(const uint8_t op_class)
{
    if (op_class >= 115 && op_class <= 130)
        return true;

    return false;
}

bool ieee80211_global_op_class_is_6ghz(const uint8_t op_class)
{
    if ((op_class >= 131 && op_class <= 137))
        return true;

    return false;
}

bool ieee80211_global_op_class_is_dfs(const uint8_t op_class)
{
    if (op_class >= 118 && op_class <= 120)
        return true;

    return false;
}

bool ieee80211_global_op_class_is_20mhz(const uint8_t op_class)
{
    if (op_class == 81 || op_class == 82 || op_class == 115 ||
        op_class == 118 || op_class == 121 || op_class == 125 ||
        op_class == 131 || op_class == 136)
        return true;

    return false;
}

bool ieee80211_global_op_class_is_40mhz(const uint8_t op_class)
{
    if (op_class == 83 || op_class == 84 || op_class == 116 ||
        op_class == 117 || op_class == 119 || op_class == 120 ||
        op_class == 122 || op_class == 123 || op_class == 126 ||
        op_class == 127 || op_class == 132)
        return true;

    return false;
}

bool ieee80211_global_op_class_is_80mhz(const uint8_t op_class)
{
    if (op_class == 128 || op_class == 130 || op_class == 133)
        return true;

    return false;
}

bool ieee80211_global_op_class_is_80plus80mhz(const uint8_t op_class)
{
    if (op_class == 135)
        return true;

    return false;
}

bool ieee80211_global_op_class_is_160mhz(const uint8_t op_class)
{
    if (op_class == 129 || op_class == 135)
        return true;

    return false;
}

bool ieee80211_global_op_class_is_320mhz(const uint8_t op_class)
{
    if (op_class == 137)
        return true;

    return false;
}
