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
source "${FUT_TOPDIR}/shell/lib/dm_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="dm/$(basename "$0")"
manager_setup_file="dm/dm_setup.sh"
status=""
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Check if manager/service is running if 'enable' field in the 'Node_Services'
      table is set true.
    - Check if manager/service is killed if 'enable' field in the 'Node_Services'
      table is set false.
Arguments:
    -h  show this help message
    \$1 (manager)     : Node_Services::service to verify                 : (string)(required)
    \$2 (kconfig_val) : Kconfig option to check if 'manager' is compiled : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <MANAGER> <KCONFIG_VAL>
Script usage example:
    ./${tc_name} wm CONFIG_MANAGER_WM
    ./${tc_name} blem CONFIG_MANAGER_BLEM
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

NARGS=2
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input arguments" -l "${tc_name}" -arg

log_title "$tc_name: DM test - Verify status of each manager against 'enable' field value in the 'Node_Services' table."

print_tables Node_Services

manager=${1}
kconfig_val=${2}

check_kconfig_option "$kconfig_val" "y" &&
    log "$tc_name: $kconfig_val = y - KCONFIG exists on the device" ||
    raise "$kconfig_val != y - KCONFIG does not exist on the device" -l "$tc_name" -s

check_ovsdb_entry Node_Services -w service "$manager" &&
    log "$tc_name: Node_Services table contains $manager" ||
    raise "FAIL: Node_Services table does not contain $manager" -l "$tc_name" -tc

service_enabled=$(get_ovsdb_entry_value Node_Services enable -w service $manager -r)
pid_of_manager=$(get_pid "${OPENSYNC_ROOTDIR}/bin/$manager")

if [ $service_enabled == "false" ]; then
    status="not "
    [ -z $pid_of_manager ] ||
        raise "FAIL: Service ${manager} is running despite 'enable' field is set 'false' in 'Node_Services' table." -l "$tc_name" -tc
elif [ $service_enabled == "true" ]; then
    [ -z $pid_of_manager ] &&
        raise "FAIL: Service ${manager} is not running despite 'enable' field is set 'true' in 'Node_Services' table." -l "$tc_name" -tc
else
    raise "FAIL: 'enable' field for ${manager} is set invalid value in 'Node_Services' table." -l "$tc_name" -tc
fi

log "$tc_name: Success: Service '${manager}' is "$status"running as 'enable' field is set '${service_enabled}' in the 'Node_Services' table."

pass

