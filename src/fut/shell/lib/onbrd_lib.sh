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
    source "${FUT_TOPDIR}/shell/config/default_shell.sh"
fi
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${LIB_OVERRIDE_FILE}"


############################################ INFORMATION SECTION - START ###############################################
#
#   Base library of common On-boarding functions
#
############################################ INFORMATION SECTION - STOP ################################################

############################################ SETUP SECTION - START #####################################################

onbrd_setup_test_environment()
{
    fn_name="onbrd_lib:onbrd_setup_test_environment"
    log -deb "$fn_name - Running ONBRD setup"

    device_init ||
        raise "device_init" -l "$fn_name" -fc

    cm_disable_fatal_state ||
        raise "cm_disable_fatal_state" -l "$fn_name" -fc

    start_openswitch ||
        raise "start_openswitch" -l "$fn_name" -fc

    restart_managers
    log "${fn_name}: Executed restart_managers, exit code: $?"

    # Check if all radio interfaces are created
    for if_name in "$@"
    do
        wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is if_name "$if_name" ||
            raise "Wifi_Radio_State" -l "$fn_name" -ow
    done

    return 0
}

############################################ SETUP SECTION - STOP ######################################################

############################################ TEST CASE SECTION - START #################################################

# Returns number of radios in Wifi_Radio_State
# if_name must be unique for each radio interface
get_number_of_radios()
{
    num=$(${OVSH} s Wifi_Radio_State if_name -r | wc -l)
    echo "$num"
}


# Checks if number of radios is the same as in provided parameter.
#     Returns 0 if equal
#     Returns 1 if not equal
# Usage example(s):
#     check_number_of_radios 2
#     check_number_of_radios 3
check_number_of_radios()
{
    fn_name="onbrd_lib:check_number_of_radios"
    num_of_radios_1=$1
    num_of_radios_2=$(get_number_of_radios)

    log -deb "$fn_name - number of radios is $num_of_radios_2"

    if [ "$num_of_radios_1" = "$num_of_radios_2" ]; then
        return 0
    else
        return 1
    fi
}

# Function checks if LEVEL2 inet_addr is the same as in test case config.
# Returns 0 if equal.
verify_wan_ip_l2()
{
    fn_name="onbrd_lib:verify_wan_ip_l2"
    br_wan=$1

    # LEVEL2
    inet_addr=$(ifconfig "$br_wan" | grep 'inet addr' | awk '/t addr:/{gsub(/.*:/,"",$2); print $2}')

    if [ -z "$inet_addr" ]; then
        log -deb "$fn_name - inet_addr is empty string"
        return 1
    fi

    if [ "$2" = "$inet_addr" ]; then
        log -deb "$fn_name - SUCCESS: OVSDB inet_addr '$2' equals LEVEL2 inet_addr '$inet_addr'"
        return 0
    else
        log -deb "$fn_name - FAIL: OVSDB inet_addr '$2' not equal to LEVEL2 inet_addr '$inet_addr'"
    fi
}


create_patch_interface()
{
    fn_name="onbrd_lib:create_patch_interface"

    num1=$(ovs-vsctl show | grep "$2" | grep Interface | awk '{print $2}' | wc -l)
    if [ "$num1" -gt 0 ]; then
        # Add WAN-to-HOME patch port
        log -deb "$fn_name - '$2' patch exists"
    else
        # Add WAN-to-HOME patch port
        log -deb "$fn_name - adding '$2' to patch port"
        add_bridge_port "$1" "$2"
        set_interface_patch "$1" "$2" "$3"
    fi

    num2=$(ovs-vsctl show | grep "$3" | grep Interface | awk '{print $2}' | wc -l)
    if [ "$num2" -gt 0 ]; then
        # Add WAN-to-HOME patch port
        log -deb "$fn_name - '$3' patch exists"
    else
        # Add WAN-to-HOME patch port
        log -deb "$fn_name - adding $3 to patch port"
        add_bridge_port "$1" "$3"
        set_interface_patch "$1" "$3" "$2"
    fi

    ovs-vsctl show
}


check_if_patch_exists()
{
    fn_name="onbrd_lib:check_if_patch_exists"

    num=$(ovs-vsctl show | grep "$1" | grep Interface | awk '{print $2}' | wc -l)
    if [ "$num" -gt 0 ]; then
        log -deb "$fn_name - '$1' interface exists"
        return 0
    else
        log -deb "$fn_name - '$1' interface does NOT exist"
        return 1
    fi
}

# Checks if target equals any resolution
# takes variable number of arguments, provide at least 1
# Return 0 if resolution equals target
# Return 1 if no resolution equals target
onbrd_check_hostname_resolved()
{
    fn_name="onbrd_lib:onbrd_check_hostname_resolved"

    target=$(get_ovsdb_entry_value Manager target -r)

    for resolution in $@
    do
        if [ "$target" = "$resolution" ]; then
            log -deb "$fn_name - target '$target' equals resolution '$resolution'"
            return 0
        else
            log -deb "fn_name - target '$target' NOT equal to resolution '$resolution'"
        fi
    done

    return 1
}

onbrd_check_fw_pattern_match()
{
    fn_name="onbrd_lib:onbrd_check_fw_pattern_match"

    fw=$(get_ovsdb_entry_value AWLAN_Node firmware_version -r)

    echo "$fw" | grep -q ^[0-9][.][0-9]

    if [ "$?" = "0" ]; then
        log -deb "$fn_name - Pattern match"
        return 0
    else
        log -deb "$fn_name - Pattern didn't match"
        return 1
    fi
}

############################################ TEST CASE SECTION - STOP ##################################################
