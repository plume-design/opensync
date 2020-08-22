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

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "osp_led.h"


static const char* led_state_str[OSP_LED_ST_LAST] =
{
    [OSP_LED_ST_IDLE]           = "idle",
    [OSP_LED_ST_ERROR]          = "error",
    [OSP_LED_ST_CONNECTED]      = "connected",
    [OSP_LED_ST_CONNECTING]     = "connecting",
    [OSP_LED_ST_CONNECTFAIL]    = "connectfail",
    [OSP_LED_ST_WPS]            = "wps",
    [OSP_LED_ST_OPTIMIZE]       = "optimize",
    [OSP_LED_ST_LOCATE]         = "locate",
    [OSP_LED_ST_HWERROR]        = "hwerror",
    [OSP_LED_ST_THERMAL]        = "thermal",
    [OSP_LED_ST_BTCONNECTABLE]  = "btconnectable",
    [OSP_LED_ST_BTCONNECTING]   = "btconnecting",
    [OSP_LED_ST_BTCONNECTED]    = "btconnected",
    [OSP_LED_ST_BTCONNECTFAIL]  = "btconnectfail",
    [OSP_LED_ST_UPGRADING]      = "upgrading",
    [OSP_LED_ST_UPGRADED]       = "upgraded",
    [OSP_LED_ST_UPGRADEFAIL]    = "upgradefail",
    [OSP_LED_ST_HWTEST]         = "hwtest",
};


const char* osp_led_state_to_str(enum osp_led_state state)
{
    if ((state < 0) || (state >= OSP_LED_ST_LAST))
        return "";

    return led_state_str[state];
}

enum osp_led_state osp_led_str_to_state(const char *str)
{
    int i;

    for (i = 0; i < OSP_LED_ST_LAST; i++)
    {
        if (!strcmp(str, led_state_str[i]))
        {
            return (enum osp_led_state)i;
        }
    }

    return OSP_LED_ST_LAST;
}
