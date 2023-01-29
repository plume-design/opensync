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
source "${FUT_TOPDIR}/shell/lib/wm2_lib.sh"
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="wm2/wm2_setup.sh"

usage()
{
cat << usage_string
wm2/wm2_validate_radio_mac_address.sh [-h] arguments
Description:
    - Script validates radio mac address in OVSDB with mac address from OS - LEVEL2
Arguments:
    -h  show this help message
    \$1  (if_name)        : interface name to validate address           : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./wm2/wm2_validate_radio_mac_address.sh <IF_NAME>
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

if [ $FUT_SKIP_L2 == 'true' ]; then
    raise "Flag to skip LEVEL2 testcases enabled, skipping execution." -l "wm2/wm2_validate_radio_mac_address.sh" -s
fi

NARGS=1
[ $# -ne ${NARGS} ] && usage && raise "Requires '${NARGS}' input argument(s)" -l "wm2/wm2_validate_radio_mac_address.sh" -arg
if_name=${1}

log_title "wm2/wm2_validate_radio_mac_address.sh: WM2 test - Verifying OVSDB hwaddr with ${if_name} MAC from OS"

# Step 1 get MAC from OS
mac_address_os=$(get_mac_from_os "${if_name}")
log -deb "wm2/wm2_validate_radio_mac_address.sh - OS MAC address: '$mac_address_os'"

# Validate MAC format
validate_mac "$mac_address_os" &&
    log -deb "wm2/wm2_validate_radio_mac_address.sh - OS MAC address: '$mac_address_os' is validated - Success" ||
    raise "FAIL: OS MAC address is not valid: '$mac_address_os'" -l "wm2/wm2_validate_radio_mac_address.sh" -oe

# Step 2 compare MAC from os and MAC from OVSDB
mac_address_ovsdb=$(${OVSH} s Wifi_Radio_State -w if_name=="$if_name" mac -r)
log -deb "wm2/wm2_validate_radio_mac_address.sh - OVSDB MAC address: '$mac_address_ovsdb'"

if [ "$mac_address_ovsdb" == "$mac_address_os" ]; then
    log -deb "wm2/wm2_validate_radio_mac_address.sh - MAC address: '$mac_address' at OS match MAC address: '$mac_address_ovsdb' from OVSDB - Success"
else
    raise "FAIL: MAC address: '$mac_address' from OS does not match MAC address: '$mac_address_ovsdb' from OVSDB" -l "wm2/wm2_validate_radio_mac_address.sh" -tc
fi

pass
