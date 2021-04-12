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
source "${FUT_TOPDIR}/shell/lib/um_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="um/$(basename "$0")"
manager_setup_file="um/um_setup.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script validates AWLAN_Node 'upgrade_status' field proper code change if invalid fw url is provided
Arguments:
    -h  show this help message
    \$1 (fw_path) : download path of UM - used to clear the folder on UM setup  : (string)(required)
    \$2 (fw_url)  : used as firmware_url in AWLAN_Node table                    : (string)(required)
    \$3 (fw_pass) : used as firmware_pass in AWLAN_Node table                   : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <FW-PATH> <FW-URL> <FW-PASS>
Script usage example:
   ./${tc_name} /tmp/pfirmware http://some_random_url_without_fw_image/fw.img
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
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
fw_path=$1
fw_url=$2

trap '
    fut_info_dump_line
    print_tables AWLAN_Node
    fut_info_dump_line
    reset_um_triggers $fw_path || true
    run_setup_if_crashed um || true
' EXIT SIGINT SIGTERM

log_title "$tc_name: UM test - Download FW - invalid firmware_url"

log "$tc_name: Setting firmware_url to $fw_url"
update_ovsdb_entry AWLAN_Node -u firmware_url "$fw_url" &&
    log "$tc_name: update_ovsdb_entry - Success to update" ||
    raise "update_ovsdb_entry - Failed to update" -l "$tc_name" -tc

log "$tc_name: Waiting for UM DL of FW image failed"
wait_ovsdb_entry AWLAN_Node -is upgrade_status "$(get_um_code "UPG_ERR_DL_FW")" &&
    log "$tc_name: wait_ovsdb_entry - Success to wait" ||
    raise "wait_ovsdb_entry - Failed to wait" -l "$tc_name" -tc

pass
