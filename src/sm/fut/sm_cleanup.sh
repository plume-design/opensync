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


# Clean up after tests for SM.

# FUT environment loading
# shellcheck disable=SC1091
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

usage()
{
cat << usage_string
sm/sm_cleanup.sh [-h] arguments
Description:
    - Script removes interface from Wifi_VIF_Config.
Arguments:
    -h : show this help message
    \$1 (if_name)    : used for interface name  : (string)(required)
Script usage example:
    ./sm/sm_cleanup.sh home-ap-l50
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

NARGS=1
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "sm/sm_cleanup.sh" -arg
if_name=${1}

log "sm/sm_cleanup.sh: Clean up interface ${if_name} from Wifi_VIF_Config"
remove_ovsdb_entry Wifi_VIF_Config -w if_name "${if_name}" &&
    log "sm/sm_cleanup.sh: OVSDB entry from Wifi_VIF_Config removed for $if_name - Success" ||
    log -err "sm/sm_cleanup.sh: Failed to remove OVSDB entry from Wifi_VIF_Config for $if_name"

wait_ovsdb_entry_remove Wifi_VIF_State -w if_name "${if_name}" &&
    log "sm/sm_cleanup.sh: OVSDB entry from Wifi_VIF_State removed for $if_name - Success" ||
    log -err "sm/sm_cleanup.sh: Failed to remove OVSDB entry from Wifi_VIF_State for $if_name"

print_tables Wifi_Inet_Config
print_tables Wifi_Inet_State
print_tables Wifi_VIF_Config
print_tables Wifi_VIF_State

pass
