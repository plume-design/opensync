/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "target_ble.h"

bool ble_init(
        void **context,
        void *caller_txt,
        struct ev_loop* loop,
        void (*ev_cb)(void *c, ble_event_t *e))
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_exit(void *context)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_enable_discovery_scan(
        void *context,
    ble_discovery_scan_params_t* params)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_disable_discovery_scan(
        void *context)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_connect_device(
    void *context,
    ble_mac_t mac,
	ble_connect_params_t *params)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_disconnect_device(
    void *context,
    ble_mac_t mac)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}


bool ble_discover_services(
        void *context,
        ble_mac_t mac,
        ble_service_discovery_params_t *params)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_discover_characteristics(
        void *context,
        ble_mac_t mac,
		ble_characteristic_discovery_params_t *params)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_discover_descriptors(
        void *context,
        ble_mac_t mac,
        ble_descriptor_discovery_params_t *params)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_enable_characteristic_notifications(
    void *context,
    ble_mac_t mac,
    ble_characteristic_notification_params *params)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_disable_characteristic_notifications(
    void *context,
    ble_mac_t  mac,
    ble_characteristic_notification_params *params)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_read_characteristic(
    void *context,
    ble_mac_t  mac,
    ble_read_characteristic_params_t *params)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_read_descriptor(
    void *context,
    ble_mac_t  mac,
    ble_read_descriptor_params_t *params)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_write_characteristic(
    void *context,
    ble_mac_t  mac,
	ble_write_characteristic_params_t *params)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}

bool ble_write_descriptor(
    void *context,
    ble_mac_t  mac,
	ble_write_descriptor_params_t *params)
{
    printf("TL Stub! in function [%s]\n", __func__);
    return true;
}
