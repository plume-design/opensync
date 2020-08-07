#!/bin/sh

# Copyright (c) 2015, Plume Design Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Plume Design Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


# Include basic environment config
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source ${FUT_TOPDIR}/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/unit_lib.sh
source ${LIB_OVERRIDE_FILE}


############################################ INFORMATION SECTION - START ###############################################
#
#   Base library of common LED Manager functions
#
############################################ INFORMATION SECTION - STOP ################################################

test_ledm_led_config()
{
    fn_name="ledm_lib:test_ledm_led_config"
    log -deb "$fn_name - Turning on WHITE LED"
    led_config_ovsdb on ||
        raise "led_config_ovsdb on" -l "$fn_name" -fc
    check_led_gpio_state on ||
        raise "check_led_gpio_state on" -l "$fn_name" -fc
    sleep 1

    log -deb "$fn_name - Turning LED off"
    led_config_ovsdb off ||
        raise "led_config_ovsdb off" -l "$fn_name" -fc
    check_led_gpio_state off ||
        raise "check_led_gpio_state off" -l "$fn_name" -fc
    sleep 1

    log -deb "$fn_name - LED mode blink"
    led_config_ovsdb blink ||
        raise "led_config_ovsdb blink" -l "$fn_name" -fc
    sleep 5

    log -deb "$fn_name - LED mode breathe"
    led_config_ovsdb breathe ||
        raise "led_config_ovsdb breathe" -l "$fn_name" -fc
    sleep 5

    log -deb "$fn_name - LED mode pattern"
    led_config_ovsdb pattern ||
        raise "led_config_ovsdb pattern" -l "$fn_name" -fc
    sleep 5

    led_config_ovsdb off ||
        raise "led_config_ovsdb off" -l "$fn_name" -fc
    check_led_gpio_state off ||
        raise "check_led_gpio_state off" -l "$fn_name" -fc
}

led_config_ovsdb()
{
    log -deb "This device is not supported! Passing!"
    return 0
}

check_led_gpio_state()
{
    log -deb "This device is not supported! Passing!"
    return 0
}
