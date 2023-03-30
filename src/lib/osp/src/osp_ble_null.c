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

#include "osp_ble.h"

bool osp_ble_init(struct ev_loop *loop,
                  osp_ble_on_device_connected_cb_t on_device_connected_cb,
                  osp_ble_on_device_disconnected_cb_t on_device_disconnected_cb,
                  osp_ble_on_pairing_status_cb_t on_pairing_status_cb,
                  osp_ble_on_gatt_json_cb_t on_gatt_json_cb)
{
    (void)loop;
    (void)on_device_connected_cb;
    (void)on_device_disconnected_cb;
    (void)on_pairing_status_cb;
    (void)on_gatt_json_cb;

    return true;
}

void osp_ble_close(void)
{
}

bool osp_ble_get_service_uuid(uint16_t *uuid)
{
    (void)uuid;

    return true;
}

bool osp_ble_set_device_name(const char *device_name)
{
    (void)device_name;

    return true;
}

bool osp_ble_set_advertising_data(const uint8_t payload[31], uint8_t len)
{
    (void)payload;
    (void)len;

    return true;
}

bool osp_ble_set_scan_response_data(const uint8_t payload[31], uint8_t len)
{
    (void)payload;
    (void)len;

    return true;
}

bool osp_ble_set_advertising_params(bool enabled, bool sr_enabled, uint16_t interval_ms)
{
    (void)enabled;
    (void)sr_enabled;
    (void)interval_ms;

    return true;
}

bool osp_ble_set_connectable(bool enabled)
{
    (void)enabled;

    return true;
}

bool osp_ble_calculate_pairing_passkey(const uint8_t token[4], uint32_t *passkey)
{
    (void)token;
    (void)passkey;

    return true;
}

bool osp_ble_set_pairing_passkey(uint32_t passkey)
{
    (void)passkey;

    return true;
}

bool osp_ble_set_gatt_json(const char *value, uint16_t len)
{
    (void)value;
    (void)len;

    return true;
}
