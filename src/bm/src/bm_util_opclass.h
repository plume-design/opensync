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

#ifndef BM_UTIL_OPCLASS_H_INCLUDED
#define BM_UTIL_OPCLASS_H_INCLUDED

#include <os_types.h>

uint8_t ieee80211_global_op_class_get(const uint8_t band, const uint8_t channel, const uint16_t width);
bool ieee80211_global_op_class_is_contained_in(const uint8_t superset_op_class, const uint8_t subset_op_class);
bool ieee80211_global_op_class_is_channel_supported(const uint8_t op_class, const uint8_t chan);
uint8_t ieee80211_global_op_class_to_20mhz_op_class(const uint8_t op_class, const uint8_t chan);
bool ieee80211_global_op_class_is_2ghz(const uint8_t op_class);
bool ieee80211_global_op_class_is_5ghz(const uint8_t op_class);
bool ieee80211_global_op_class_is_6ghz(const uint8_t op_class);
bool ieee80211_global_op_class_is_dfs(const uint8_t op_class);
bool ieee80211_global_op_class_is_20mhz(const uint8_t op_class);
bool ieee80211_global_op_class_is_40mhz(const uint8_t op_class);
bool ieee80211_global_op_class_is_80mhz(const uint8_t op_class);
bool ieee80211_global_op_class_is_80plus80mhz(const uint8_t op_class);
bool ieee80211_global_op_class_is_160mhz(const uint8_t op_class);
bool ieee80211_global_op_class_is_320mhz(const uint8_t op_class);

#endif /* BM_UTIL_OPCLASS_H_INCLUDED */
