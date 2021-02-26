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

manager_setup_file="um/um_setup.sh"
um_resource_path="resource/um/"
tc_name="um/$(basename "$0")"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script verifies download of FW image if during the download process another
      download is triggered. Initial download should not be interrupted.

      Test requires some download rate limiter enabled so the image would not be downloaded
      in less than 10s. This enables the test to trigger an interruption during the download.

      Script populates AWLAN_Node table, upgrade_dl_timer and firmware_url fields and
      in effect triggers download.
      After update it checks if download is in progress by monitoring upgrade_status.
      If field contains code UPG_STS_FW_DL_START it is assumed download has started.

      Immediately after correct status code it starts another download by setting
      AWLAN_Node table again, but with different firmware_url (fw_url_2).

      Test assumes first download will not be interrupted.
      Script waits for upgrade_status field value to become UPG_STS_FW_DL_END.
      If so test passes.

Arguments:
    -h: show this help message
    \$1 (fw_path)     : download path for FW download at DUT - (string)(required)
    \$2 (fw_url)      : used as 1st firmware_url on RPI server in AWLAN_Node table - (string)(required)
    \$3 (fw_url_2)    : used as 2nd firmware_url on RPI server in AWLAN_Node table - (string)(required)
    \$4 (fw_dl_timer) : used as upgrade_dl_timer in AWLAN_Node table to initiate download procedure - (int)(required)

Testcase procedure:
    This script is executed on DUT.
    It is dependent on the following managers:
        - running UM manager
        - WAN connectivity to access FW image
    - On RPI SERVER: Prepare clean FW (.img) in ${um_resource_path}
                     Duplicate image with different name, use prefix in front of 1st name
                     Create md5 file for 2nd image
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <FW-PATH> <FW-NAME-1> <FW-NAME-1> <fw_dl_timer>

Script usage example:
   ${tc_name} "http://url_to_image image_name_1.img image_name_2.img 10"
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

fw_path=$1
fw_url=$2
fw_url_2=$3
fw_dl_timer=$4

trap '
  reset_um_triggers $fw_path || true
  run_setup_if_crashed um || true
' EXIT SIGINT SIGTERM

log_title "$tc_name: UM test - Download FW - upgrade_dl_timer - $fw_dl_timer seconds"

log "$tc_name: Setting upggrade_dl_timer to $fw_dl_timer firmware_url to $fw_url"
update_ovsdb_entry AWLAN_Node \
    -u upgrade_dl_timer "$fw_dl_timer" \
    -u firmware_url "$fw_url" &&
        log "$tc_name: update_ovsdb_entry - Success to update" ||
        raise "update_ovsdb_entry - Failed to update" -l "$tc_name" -tc

start_time=$(date -D "%H:%M:%S"  +"%Y.%m.%d-%H:%M:%S")

log "$tc_name: Waiting for FW download start"
dl_start_code=$(get_um_code "UPG_STS_FW_DL_START")
wait_ovsdb_entry AWLAN_Node -is upgrade_status "$dl_start_code" &&
    log "$tc_name: wait_ovsdb_entry - AWLAN_Node upgrade_status is $dl_start_code" ||
    raise "wait_ovsdb_entry - Failed to set upgrade_status in AWLAN_Node to $dl_start_code" -l "$tc_name" -tc
log "$tc_name: Download of 1st FW image started and in progress!"

# Download of first image started...
# ... now start with downloading new image (illegal or unexpected action).
log "$tc_name: Starting another fw image download '$fw_url_2' while first one is still in progress"
update_ovsdb_entry AWLAN_Node \
    -u upgrade_dl_timer "$fw_dl_timer" \
    -u firmware_url "$fw_url_2" &&
        log "$tc_name: update_ovsdb_entry - Success to update" ||
        raise "update_ovsdb_entry - Failed to update" -l "$tc_name" -tc

log "$tc_name: Waiting for FW download to finish"
dl_end_code=$(get_um_code "UPG_STS_FW_DL_END")
wait_ovsdb_entry AWLAN_Node -is upgrade_status "$dl_end_code" &&
    fw_download_result 0 "$start_time" ||
    fw_download_result 1 "$start_time"

pass
