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


# FUT environment loading
# shellcheck disable=SC1091
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="onbrd/onbrd_setup.sh"
usage()
{
cat << usage_string
onbrd/onbrd_set_and_verify_bridge_mode.sh [-h] arguments
Description:
    - Validate device bridge mode settings
Arguments:
    -h : show this help message
    \$1 (wan_interface)  : WAN interface name  : (string)(required)
    \$2 (wan_ip)         : WAN IP              : (string)(required)
    \$3 (home_interface) : LAN interface name  : (string)(required)
    \$4 (patch_w2h)      : WAN to LAN patch    : (string)(required)
    \$5 (patch_h2w)      : LAN to WAN patch    : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./onbrd/onbrd_set_and_verify_bridge_mode.sh <WAN-INTERFACE> <WAN-IP> <HOME-INTERFACE> <PATCH-W2H> <PATCH-H2W>
Script usage example:
   ./onbrd/onbrd_set_and_verify_bridge_mode.sh eth0 192.168.200.10 br-home patch-w2h patch-h2w
usage_string
}

if [ -n "${1}" ]; then
    case "${1}" in
        help | \
        --help | \
        -h)
            usage && exit 1
            ;;
        *)
            ;;
    esac
fi

check_kconfig_option "TARGET_CAP_EXTENDER" "y" ||
    raise "TARGET_CAP_EXTENDER != y - Testcase applicable only for EXTENDER-s" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -s
log "onbrd/onbrd_set_and_verify_bridge_mode.sh: Checking if WANO is enabled, if yes, skip..."
check_kconfig_option "CONFIG_MANAGER_WANO" "y" &&
    raise "Test of bridge mode is not compatible if WANO is present on system" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -s

NARGS=5
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -arg
# Fill variables with provided arguments or defaults.
wan_interface=${1}
wan_ip=${2}
home_interface=${3}
patch_w2h=${4}
patch_h2w=${5}

trap '
fut_info_dump_line
print_tables Wifi_Inet_Config Wifi_Inet_State
ovs-vsctl show
fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "onbrd/onbrd_set_and_verify_bridge_mode.sh: ONBRD test - Verify Bridge Mode Settings"

# WAN bridge section
# Check if DHCP client is running on WAN bridge
wait_for_function_response 0 "check_pid_udhcp $wan_interface" &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: check_pid_udhcp - PID found, DHCP client running - Success" ||
    raise "FAIL: check_pid_udhcp - PID not found, DHCP client NOT running" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$wan_interface" -u NAT true &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::NAT is 'true' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::NAT is not 'true'" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_interface" -is NAT true &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::NAT is 'true' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::NAT is not 'true'" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$wan_interface" -u ip_assign_scheme dhcp &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::ip_assign_scheme is 'dhcp' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::ip_assign_scheme is not 'dhcp'" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_interface" -is ip_assign_scheme dhcp &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::ip_assign_scheme is 'dhcp' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::ip_assign_scheme is not 'dhcp'" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_interface" -is inet_addr "$wan_ip" &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::inet_addr is private - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::inet_addr is not private" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

# LAN bridge section
update_ovsdb_entry Wifi_Inet_Config -w if_name "$home_interface" -u NAT false &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::NAT is 'false' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::NAT is not 'false'" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$home_interface" -is NAT false &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::NAT is 'false' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::NAT is not 'false'" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$home_interface" -u ip_assign_scheme none &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::ip_assign_scheme is 'none' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::ip_assign_scheme is not 'none'" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$home_interface" -is ip_assign_scheme none &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::ip_assign_scheme is 'none' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::ip_assign_scheme is not 'none'" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$home_interface" -u "dhcpd" "[\"map\",[]]" &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::dhcpd is [\"map\",[]] - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::dhcpd is not [\"map\",[]]" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$home_interface" -is "dhcpd" "[\"map\",[]]" &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::dhcpd is [\"map\",[]] - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::dhcpd [\"map\",[]]" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$home_interface" -is "netmask" "0.0.0.0" &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::netmask is '0.0.0.0' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::netmask is not '0.0.0.0'" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$home_interface" -is inet_addr "0.0.0.0" &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::inet_addr is '0.0.0.0' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::inet_addr is not '0.0.0.0'" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

# Creating patch interface
log "onbrd/onbrd_set_and_verify_bridge_mode.sh: create_patch_interface - creating patch $patch_w2h $patch_h2w"
create_patch_interface "$wan_interface" "$patch_w2h" "$patch_h2w" &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: check_patch_if_exists - patch interface $patch_w2h created - Success" ||
    raise "FAIL: check_patch_if_exists - Failed to create patch interface $patch_w2h" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

wait_for_function_response 0 "check_if_patch_exists $patch_w2h" &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: check_patch_if_exists - patch interface $patch_w2h exists - Success" ||
    raise "FAIL: check_patch_if_exists - patch interface $patch_w2h does not exists" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

wait_for_function_response 0 "check_if_patch_exists $patch_h2w" &&
    log "onbrd/onbrd_set_and_verify_bridge_mode.sh: check_patch_if_exists - patch interface $patch_h2w exists - Success" ||
    raise "FAIL: check_patch_if_exists - patch interface $patch_h2w does not exists" -l "onbrd/onbrd_set_and_verify_bridge_mode.sh" -tc

# Restart managers to tidy up config
restart_managers

pass
