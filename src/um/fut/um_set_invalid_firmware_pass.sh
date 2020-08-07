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


if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/unit_lib.sh
source ${FUT_TOPDIR}/shell/lib/um_lib.sh
source ${LIB_OVERRIDE_FILE}

usage="
$(basename "$0") [-h] \$1 \$2 \$3 \$4

where options are:
    -h  show this help message

where arguments are:
    fw_path=\$1 -- download path of UM - used to clear the folder on UM setup - (string)(required)
    fw_url=\$2 -- used as firmware_url in AWLAN_Node table - (string)(required)
    fw_pass=\$2 -- used as firmware_pass in AWLAN_Node table - (string)(required)

this script is dependent on following:
    - running UM manager
    - udhcpc on interface

example of usage:
   /tmp/fut-base/shell/nm2/$(basename "$0").sh http://url_to_image invalid_fw_pass
"

while getopts hcs:fs: option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if [[ $# -lt 2 ]]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

fw_path=$1
fw_url=$2
fw_pass=$3
tc_name="um/$(basename "$0")"

trap '
  reset_um_triggers $fw_path || true
  run_setup_if_crashed um || true
' EXIT SIGINT SIGTERM

log "$tc_name: UM Corrupt FW image"
log "$tc_name: Setting firmware_url to $fw_url and firmware_pass to $fw_pass"

update_ovsdb_entry AWLAN_Node \
    -u firmware_pass "$fw_pass" \
    -u firmware_url "$fw_url" &&
        log "$tc_name: update_ovsdb_entry - Success to update" ||
        raise "$tc_name: update_ovsdb_entry - Failed to update" -l "$tc_name" -tc

log "$tc_name: Waiting for FW download start"
wait_ovsdb_entry AWLAN_Node -is upgrade_status $(get_um_code "UPG_STS_FW_DL_START") &&
    log "$tc_name: wait_ovsdb_entry - Success to wait" ||
    raise "$tc_name: wait_ovsdb_entry - Failed to wait" -l "$tc_name" -tc

log "$tc_name: Waiting for FW download finish"
wait_ovsdb_entry AWLAN_Node -is upgrade_status $(get_um_code "UPG_STS_FW_DL_END") &&
    log "$tc_name: wait_ovsdb_entry - Success to wait" ||
    raise "$tc_name: wait_ovsdb_entry - Failed to wait" -l "$tc_name" -tc

log "$tc_name: Setting AWLAN_Node upgrade_timer to 1 and "
update_ovsdb_entry AWLAN_Node -u upgrade_timer 1 &&
    log "$tc_name: update_ovsdb_entry - Success to update" ||
    raise "$tc_name: update_ovsdb_entry - Failed to update" -l "$tc_name" -tc

log "$tc_name: Waiting for FW corrupt image code UPG_ERR_IMG_FAIL - $(get_um_code "UPG_ERR_IMG_FAIL")"
wait_ovsdb_entry AWLAN_Node -is upgrade_status $(get_um_code "UPG_ERR_IMG_FAIL") &&
    log "$tc_name: wait_ovsdb_entry - Success to wait" ||
    raise "$tc_name: wait_ovsdb_entry - Failed to wait" -l "$tc_name" -tc

pass
