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
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="nm2/$(basename "$0")"
manager_setup_file="nm2/nm2_setup.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script configures interfaces dns through Wifi_inet_Config 'dns' field and checks if it is propagated
      into Wifi_Inet_State table and to the system, fails otherwise
Arguments:
    -h  show this help message
    \$1 (if_name)       : field if_name in Wifi_Inet_Config table                 : (string)(required)
    \$2 (if_type)       : field if_type in Wifi_Inet_Config table                 : (string)(required)
    \$3 (primary_dns)   : primary entry for field dns in Wifi_Inet_Config table   : (string)(required)
    \$4 (secondary_dns) : secondary entry for field dns in Wifi_Inet_Config table : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <IF-NAME> <IF-TYPE> <PRIMARY-DNS> <SECONDARY-DNS>
Script usage example:
   ./${tc_name} wifi0 vif 1.2.3.4 4.5.6.7
usage_string
}
while getopts h option; do
    case "$option" in
        h)
            usage && exit 1
            ;;
        *)
            echo "Unknown argument" && exit 1
            ;;
    esac
done
NARGS=4
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

trap '
    reset_inet_entry $if_name || true
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

if_name=$1
if_type=$2
primary_dns=$3
secondary_dns=$4

log_title "${tc_name}: NM2 test - Testing table Wifi_Inet_Config field dns"

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
