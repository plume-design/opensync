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

#include "target_zigbee.h"

bool zigbee_init(
        void **context,
        void *caller_ctx,
        struct ev_loop* loop,
        void (*ev_cb)(void *c, zigbee_event_t *e))
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_exit(void *contex)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_permit_join(
        void *context,
        zigbee_permit_join_params_t params)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_network_joining_permitted(
        void *context)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_send_network_leave(
        void *context,
        zigbee_mac_t mac)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_discover_endpoints(
        void *context,
        zigbee_mac_t mac,
        zigbee_discover_endpoints_params_t params)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_read_attributes(
        void *context,
        zigbee_mac_t mac,
        zigbee_read_attributes_params_t params)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_write_attributes(
        void *context,
        zigbee_mac_t mac,
        zigbee_write_attributes_params_t params)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_configure_reporting(
        void *context,
        zigbee_mac_t mac,
        zigbee_configure_reporting_params_t params)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_read_reporting_configuration(
        void *context,
        zigbee_mac_t mac,
        zigbee_read_reporting_configuration_params_t params)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_discover_attributes(
        void *context,
        zigbee_mac_t mac,
        zigbee_discover_attributes_params_t params)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_discover_commands_received(
        void *context,
        zigbee_mac_t mac,
        zigbee_discover_commands_received_params_t params)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_discover_commands_generated(
        void *context,
        zigbee_mac_t mac,
        zigbee_discover_commands_generated_params_t params)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}

bool zigbee_send_cluster_specific_command(
        void *context,
        zigbee_mac_t mac,
        zigbee_send_cluster_specific_command_params_t params)
{
    printf("%s: In stub Zigbee Target Layer\n", __func__);
    return true;
}
