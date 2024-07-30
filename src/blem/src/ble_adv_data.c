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

#include "const.h"

#include "ble_adv_data.h"
#include "log.h"
#include "memutil.h"
#include "util.h"

ble_advertising_set_t g_advertising_sets[2] =
        {[BLE_ADVERTISING_SET_GENERAL] =
                 {.interval = CONFIG_BLEM_ADVERTISING_INTERVAL,
                  .adv.data.general =
                          {.service =
                                   {.len = 1 + ARRAY_SIZE(((ble_advertising_data_t *)0)->general.service.uuids) * 2,
                                    .type = 0x03,
                                    .uuids = {TO_LE16(CONFIG_BLEM_GATT_SERVICE_UUID)}},
                           .mfd =
                                   {.len = 1 + 2 + 20,
                                    .type = 0xFF,
                                    .cid = TO_LE16(CONFIG_BLEM_MANUFACTURER_DATA_COMPANY_ID)},
                           .version = 0x05},
                  .scan_rsp.data.cln = {.len = 1, .type = 0x09}},
         [BLE_ADVERTISING_SET_PROXIMITY] = {
             .interval = 100,
             .adv.data.proximity =
                     {.length = 0x1A, .type = 0xFF, .company_id = TO_LE16(0x004C), .beacon_type = TO_LE16(0x1502)}}};

/* Safety checks for proper alignments and offsets of packed structures */

C_STATIC_ASSERT(
        sizeof(ble_ad_structure_t) == 2 && offsetof(ble_ad_structure_t, len) == 0
                && offsetof(ble_ad_structure_t, type) == 1,
        "ble_ad_structure_t");

C_STATIC_ASSERT(
        sizeof(ble_advertising_data_t) == 28
                && sizeof(ble_advertising_data_t) == C_FIELD_SZ(ble_advertising_data_t, raw)
                /* general */
                && sizeof(ble_adv_data_general_t) == 28 && C_FIELD_SZ(ble_adv_data_general_t, service) == 4
                && C_FIELD_SZ(ble_adv_data_general_t, mfd) == 4
                && sizeof(ble_adv_data_general_t) - offsetof(ble_adv_data_general_t, version) == 20
                && C_FIELD_SZ(ble_adv_data_general_t, msg) == 6
                /* proximity */
                && sizeof(ble_adv_data_proximity_t) == 27,
        "ble_advertising_data_t size");
C_STATIC_ASSERT(
        offsetof(ble_advertising_data_t, general) == offsetof(ble_advertising_data_t, proximity)
                /* general */
                && offsetof(ble_adv_data_general_t, service) == 0 && offsetof(ble_adv_data_general_t, mfd) == 4
                && offsetof(ble_adv_data_general_t, version) == offsetof(ble_adv_data_general_t, mfd.data)
                && offsetof(ble_adv_data_general_t, msg.status) == offsetof(ble_adv_data_general_t, version) + 14
                /* proximity */
                && offsetof(ble_adv_data_proximity_t, major) == 22,
        "ble_advertising_data_t offset");

C_STATIC_ASSERT(
        sizeof(ble_scan_response_data_t) == 31
                && sizeof(ble_scan_response_data_t) == C_FIELD_SZ(ble_scan_response_data_t, raw),
        "ble_scan_response_data_t size");

uint8_t ble_adv_data_get_length(const ble_ad_structure_t *const first_ad, const size_t max_length)
{
    /* Jump over all advertising data elements to calculate the real length of the advertising payload.
     * The data can be malformed, so operate on a byte-by-byte basis instead of using the AD structures. */
    const uint8_t *const start = (const uint8_t *)first_ad;
    const uint8_t *const end = start + max_length;
    const uint8_t *p = start;

    while (p < end)
    {
        /* The first byte of the AD element is the length of the element, not counting the length byte itself */
        const uint8_t ade_len = *p;

        /* All valid AD elements must have length of at least 1 byte, for ADE Type value.
         * Moreover, AD elements can use all the available advertising payload bytes, but
         * no element can extend past the advertising data length limit. */
        if (ade_len < 1)
        {
            /* Reached the end of the null-padded advertising data */
            break;
        }
        p++; /*< `ade_len` of advertising data follows the length byte */
        if ((p + ade_len) > end)
        {
            /* The length of the AD element is invalid - as there is no reliable way to differentiate
             * between an uninitialized memory/padding and a valid AD element, the latter is assumed.
             * Use "null-termination" or provide the exact length of the advertising data. */
            const size_t max_str_len = 2 * max_length + 1;
            char *str = MALLOC(max_str_len);

            if (bin2hex(start, max_length, str, max_str_len) < 0)
            {
                str[0] = '\0';
            }
            LOGE("Advertising payload %s has invalid element [%d]... len %d > %d B)",
                 str,
                 p - start - 1,
                 ade_len,
                 max_length);

            FREE(str);
            return 0;
        }

        p += ade_len;

        /* In any case, the data length must fit into a single octet */
        if ((p - start) > UINT8_MAX)
        {
            LOGE("Advertising payload too big %d > %d B", p - start, UINT8_MAX);
            return 0;
        }
    }

    return (uint8_t)(p - start);
}
