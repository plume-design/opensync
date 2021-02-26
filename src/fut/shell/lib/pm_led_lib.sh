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
export FUT_PM_LED_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/pm_led_lib.sh sourced"
####################### INFORMATION SECTION - START ###########################
#
#   Base library of common LED Manager functions
#
####################### INFORMATION SECTION - STOP ############################

###############################################################################
# DESCRIPTION:
#   Function cycles through modes of LED operation:
#       - on,
#       - off,
#       - blink,
#       - breathe and
#       - pattern blink.
#   Checks return codes of commands, raises exception if not a success.
#   After issuing all command it checks actual GPIO states for off mode.
#   Raises exception if LED gpio state is not as expected.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   test_pm_led_config
###############################################################################
test_pm_led_config()
{
    fn_name="pm_led_lib:test_pm_led_config"

    log -deb "$fn_name - Turning on WHITE LED"
    led_config_ovsdb on &&
        log -deb "$fn_name - led_config_ovsdb on - Success" ||
        raise "FAIL: Could not set WHITE LED on: led_config_ovsdb on" -l "$fn_name" -tc
    check_led_gpio_state on &&
        log -deb "$fn_name - check_led_gpio_state on - Success" ||
        raise "FAIL: WHITE LED not on: check_led_gpio_state on" -l "$fn_name" -tc
    sleep 1

    log -deb "$fn_name - Turning LED off"
    led_config_ovsdb off &&
        log -deb "$fn_name - led_config_ovsdb off - Success" ||
        raise "FAIL: Could not set LED off: led_config_ovsdb off" -l "$fn_name" -tc
    check_led_gpio_state off &&
        log -deb "$fn_name - check_led_gpio_state off - Success" ||
        raise "FAIL: LED not off: check_led_gpio_state off" -l "$fn_name" -tc
    sleep 1

    log -deb "$fn_name - LED mode blink"
    led_config_ovsdb blink &&
        log -deb "$fn_name - led_config_ovsdb blink - Success" ||
        raise "FAIL: Could not set LED blink: led_config_ovsdb blink" -l "$fn_name" -tc
    sleep 5

    log -deb "$fn_name - LED mode breathe"
    led_config_ovsdb breathe &&
        log -deb "$fn_name - led_config_ovsdb breathe - Success" ||
        raise "FAIL: Could not set LED breathe: led_config_ovsdb breathe" -l "$fn_name" -tc
    sleep 5

    log -deb "$fn_name - LED mode pattern"
    led_config_ovsdb pattern &&
        log -deb "$fn_name - led_config_ovsdb pattern - Success" ||
        raise "FAIL: Could not set LED pattern: led_config_ovsdb pattern" -l "$fn_name" -tc
    sleep 5

    led_config_ovsdb off &&
        log -deb "$fn_name - led_config_ovsdb off - Success" ||
        raise "FAIL: Could not set LED off: led_config_ovsdb off" -l "$fn_name" -tc
    check_led_gpio_state off &&
        log -deb "$fn_name - check_led_gpio_state off - Success" ||
        raise "FAIL: LED not off: check_led_gpio_state off" -l "$fn_name" -tc
}

###############################################################################
# DESCRIPTION:
#   Function not implemented.
#   Should be implemented in override for each supported device.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   Always.
# USAGE EXAMPLE(S):
#   led_config_ovsdb
###############################################################################
led_config_ovsdb()
{
    fn_name="pm_led_lib:test_pm_led_config"
    log -deb "$fn_name - This device is not supported! Passing!"
    return 0
}

###############################################################################
# DESCRIPTION:
#   Function not implemented.
#   Should be implemented in override for each supported device.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   Always.
# USAGE EXAMPLE(S):
#   check_led_gpio_state
###############################################################################
check_led_gpio_state()
{
    fn_name="pm_led_lib:test_pm_led_config"
    log -deb "$fn_name - This device is not supported! Passing!"
    return 0
}
