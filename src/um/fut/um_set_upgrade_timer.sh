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
source "${FUT_TOPDIR}/shell/lib/um_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

tc_name="um/$(basename "$0")"
manager_setup_file="um/um_setup.sh"
um_resource_path="resource/um/"
um_image_name_default="um_set_upg_timer_fw"
um_create_md5_file_path="tools/server/um/um_create_md5_file.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script validates UM upgrade_timer being respected and UM starts with upgrade after timer is done
      Script fails if UM starts upgrade before end of upgrade_timer or does not start to upgrade after the timer
Arguments:
    -h  show this help message
    \$1 (fw_path)      : download path of UM - used to clear the folder on UM setup                      : (string)(required)
    \$2 (fw_url)       : used as firmware_url in AWLAN_Node table                                        : (string)(required)
    \$3 (fw_up_timer)  : used as upgrade_timer in AWLAN_Node table                                       : (string)(required)
    \$4 (fw_name)      : used as to delete the file on device from \$1 (fw_path) to skip upgrade process : (string)(required)
Testcase procedure:
    - On RPI SERVER: Prepare clean FW (.img) in ${um_resource_path}
                     Duplicate image with different name (example. ${um_image_name_default}.img) (cp <CLEAN-IMG> <NEW-IMG>)
                     Create MD5 sum for image (example. ${um_image_name_default}.img.md5) (see ${um_create_md5_file_path} -h)
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <FW-PATH> <FW-URL>
Script usage example:
   ./${tc_name} /tmp/pfirmware http://192.168.4.1:8000/fut-base/resource/um/${um_image_name_default}.img
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
NARGS=4
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
fw_path=${1}
fw_url=${2}
fw_up_timer=${3}
fw_name=${4}

trap '
    fut_info_dump_line
    print_tables AWLAN_Node
    fut_info_dump_line
    reset_um_triggers $fw_path || true
    run_setup_if_crashed um || true
' EXIT SIGINT SIGTERM

log_title "$tc_name: UM test - Download FW - upgrade_timer - 2 seconds +/-"

log "$tc_name: Setting firmware_url to $fw_url"
update_ovsdb_entry AWLAN_Node -u firmware_url "$fw_url" &&
    log "$tc_name: update_ovsdb_entry - AWLAN_Node::firmware_url is $fw_url - Success" ||
    raise "FAIL: update_ovsdb_entry - AWLAN_Node::firmware_url is not $fw_url" -l "$tc_name" -oe

fw_start_code=$(get_um_code "UPG_STS_FW_DL_START")
log "$tc_name: Waiting for FW download to start, AWLAN_Node::upgrade_status to become UPG_STS_FW_DL_START ('$dl_start_code')"
wait_ovsdb_entry AWLAN_Node -is upgrade_status "$fw_start_code" &&
    log "$tc_name: wait_ovsdb_entry - AWLAN_Node::upgrade_status is $fw_start_code - Success" ||
    raise "FAIL: wait_ovsdb_entry - AWLAN_Node::upgrade_status is not $fw_start_code" -l "$tc_name" -tc

fw_stop_code=$(get_um_code "UPG_STS_FW_DL_END")
log "$tc_name: Waiting for FW download to finish"
wait_ovsdb_entry AWLAN_Node -is upgrade_status "$fw_stop_code" &&
    log "$tc_name: wait_ovsdb_entry - AWLAN_Node::upgrade_status is $fw_stop_code - Success" ||
    raise "FAIL: wait_ovsdb_entry - AWLAN_Node::upgrade_status is not $fw_stop_code" -l "$tc_name" -tc

log "$tc_name: Setting AWLAN_Node::upgrade_timer to $fw_up_timer"
update_ovsdb_entry AWLAN_Node -u upgrade_timer "$fw_up_timer" &&
    log "$tc_name: update_ovsdb_entry - AWLAN_Node::upgrade_timer is $fw_up_timer - Success" ||
    raise "FAIL: update_ovsdb_entry - AWLAN_Node::upgrade_timer is not $fw_up_timer" -l "$tc_name" -oe

# Delete image file on device to skip upgrade process
if [ -n "$fw_path" ] && [ -n "$fw_name" ]; then
    rm -rf "$fw_path/$fw_name"
fi

start_time=$(date -D "%H:%M:%S"  +"%Y.%m.%d-%H:%M:%S")

upg_start_code=$(get_um_code "UPG_STS_FW_WR_START")
log "$tc_name: Waiting for UM upgrade start"
wait_ovsdb_entry AWLAN_Node -is upgrade_status "$upg_start_code" &&
    log "$tc_name: wait_ovsdb_entry - AWLAN_Node::upgrade_status is $upg_start_code - Success" ||
    raise "FAIL: wait_ovsdb_entry - AWLAN_Node::upgrade_status is not $upg_start_code" -l "$tc_name" -tc

end_time=$(date -D "%H:%M:%S"  +"%Y.%m.%d-%H:%M:%S")

t1=$(date -u -d "$start_time" +"%s")
t2=$(date -u -d "$end_time" +"%s")

upgrade_time=$(( t2 - t1 ))
upgrade_time_lower=$(( $upgrade_time - 2 ))
upgrade_time_upper=$(( $upgrade_time + 2 ))

if [ "$upgrade_time_lower" -le "$fw_up_timer" ] && [ "$upgrade_time_upper" -ge "$fw_up_timer" ]; then
    log "$tc_name: Upgrade started in given upgrade_timer - given $fw_up_timer - resulted in $upgrade_time"
else
    raise "FAIL Upgrade DID NOT start in given upgrade_timer - given $fw_up_timer - resulted in $upgrade_time" -l "$tc_name" -tc
fi

pass
