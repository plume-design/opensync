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

# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"

trap '
    reset_inet_entry $if_name || true
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

tc_name="nm2/$(basename "$0")"
usage()
{
cat << EOF
${tc_name} [-h] if_name if_type primary_dns secondary_dns
Options:
    -h  show this help message
Arguments:
    if_name=$1 -- field if_name in Wifi_Inet_Config table - (string)(required)
    if_type=$2 -- field if_type in Wifi_Inet_Config table - (string)(required)
    primary_dns=$2 -- primary entry for field dns in Wifi_Inet_Config table - (string)(required)
    secondary_dns=$3 -- secondary entry for field dns in Wifi_Inet_Config table - (string)(required)
Dependencies:
    NM manager, WM manager
Example:
    ${tc_name} wifi0 vif 8.8.4.4 1.1.1.1
EOF
exit 1
}

while getopts h option; do
    case "$option" in
        h)
            usage
            ;;
    esac
done

NARGS=4
[ $# -ne ${NARGS} ] && raise "Requires exactly '${NARGS}' input argument(s)" -l "${tc_name}" -arg
if_name=$1
if_type=$2
primary_dns=$3
secondary_dns=$4

log_title "${tc_name}: Testing table Wifi_Inet_Config field dns"

log "${tc_name}: Creating Wifi_Inet_Config entries for $if_name"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -inet_addr 10.10.10.30 \
    -netmask "255.255.255.0" \
    -if_type "$if_type" &&
        log "$tc_name: Interface successfully created" ||
        raise "Failed to create interface" -l "$tc_name" -tc

log "$tc_name: Setting DNS for $if_name to $primary_dns, $secondary_dns"
enable_disable_custom_dns "$if_name" "$primary_dns" "$secondary_dns" ||
    raise "Failed to set custom DNS - interface $if_name" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if Primary DNS was properly applied to $if_name"
wait_for_function_response 0 "check_resolv_conf $primary_dns" &&
    log "$tc_name: LEVEL2: Primary dns set on /etc/resolv.conf - interface $if_name" ||
    raise "Primary DNS configuration NOT VALID - interface $if_name" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if Secondary DNS was properly applied to $if_name"
wait_for_function_response 0 "check_resolv_conf $secondary_dns" &&
    log "$tc_name: LEVEL2: Secondary dns set on /etc/resolv.conf - interface $if_name" ||
    raise "Secondary DNS configuration NOT VALID - interface $if_name" -l "$tc_name" -tc

pass
