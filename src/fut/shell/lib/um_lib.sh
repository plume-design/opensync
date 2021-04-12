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


# Include basic environment config
export FUT_UM_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/um_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Base library of common Upgrade Manager functions (Plume specific)
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function prepares device for UM tests.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  path to FW image (required)
#   $2  interface name (optional, default: eth0)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   um_setup_test_environment  <fw_path>
#   um_setup_test_environment  <fw_path> eth0
###############################################################################
um_setup_test_environment()
{
    fn_name="um_lib:um_setup_test_environment"
    NARGS_MIN=1
    NARGS_MAX=2
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    fw_path=$1
    if_name=${2:-eth0}

    log "$fn_name - Running UM setup"

    device_init &&
        log -deb "$fn_name - Device initialized - Success" ||
        raise "FAIL: Could not initialize device: device_init" -l "$fn_name" -ds

    start_openswitch &&
        log -deb "$fn_name - OpenvSwitch started - Success" ||
        raise "FAIL: Could not start OpenvSwitch: start_openswitch" -l "$fn_name" -ds

    start_udhcpc "$if_name" true &&
        log -deb "$fn_name - start_udhcpc on '$if_name' started - Success" ||
        raise "FAIL: Could not start DHCP client: start_udhcpc" -l "$fn_name" -ds

    log -deb "${fn_name} - Erasing $fw_path"
    rm -rf "$fw_path" ||
        true

    start_specific_manager um -d &&
        log -deb "$fn_name - start_specific_manager um - Success" ||
        raise "FAIL: Could not start manager: start_specific_manager um" -l "$fn_name" -ds

    log "$fn_name - UM setup - end"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function resets all triggers that would start upgrade process.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  path to FW image (required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   reset_um_triggers <fw_path>
###############################################################################
reset_um_triggers()
{
    fn_name="um_lib:reset_um_triggers"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    fw_path=$1

    log -deb "$fn_name - Erasing $fw_path"
    rm -rf "$fw_path" || true

    log -deb "$fn_name - Reseting AWLAN_Node UM fields"
    update_ovsdb_entry AWLAN_Node \
      -u firmware_pass '' \
      -u firmware_url '' \
      -u upgrade_dl_timer '0' \
      -u upgrade_status '0' \
      -u upgrade_timer '0' &&
          log -deb "$fn_name - AWLAN_Node UM fields reset" ||
          raise "FAIL: Could not reset AWLAN_Node fields" -l "$fn_name" -oe

    return 0
}

####################### SETUP SECTION - STOP ##################################

###############################################################################
# DESCRIPTION:
#   Function echoes upgrade manager's numerical code of identifier.
#   Raises exception if identifier not found.
# INPUT PARAMETER(S):
#   $1  upgrade_identifier (string) (required)
# RETURNS:
#   Echoes code.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   get_um_code UPG_ERR_DL_FW
#   get_um_code UPG_STS_FW_DL_END
###############################################################################
get_um_code()
{
    fn_name="um_lib:get_um_code"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    upgrade_identifier=$1

    case "$upgrade_identifier" in
        "UPG_ERR_ARGS")
            echo  "-1"
            ;;
        "UPG_ERR_URL")
            echo  "-3"
            ;;
        "UPG_ERR_DL_FW")
            echo  "-4"
            ;;
        "UPG_ERR_DL_MD5")
            echo  "-5"
            ;;
        "UPG_ERR_MD5_FAIL")
            echo  "-6"
            ;;
        "UPG_ERR_IMG_FAIL")
            echo  "-7"
            ;;
        "UPG_ERR_FL_ERASE")
            echo  "-8"
            ;;
        "UPG_ERR_FL_WRITE")
            echo  "-9"
            ;;
        "UPG_ERR_FL_CHECK")
            echo  "-10"
            ;;
        "UPG_ERR_BC_SET")
            echo  "-11"
            ;;
        "UPG_ERR_APPLY")
            echo  "-12"
            ;;
        "UPG_ERR_BC_ERASE")
            echo  "-14"
            ;;
        "UPG_ERR_SU_RUN ")
            echo  "-15"
            ;;
        "UPG_ERR_DL_NOFREE")
            echo  "-16"
            ;;
        "UPG_STS_FW_DL_START")
            echo  "10"
            ;;
        "UPG_STS_FW_DL_END")
            echo  "11"
            ;;
        "UPG_STS_FW_WR_START")
            echo  "20"
            ;;
        "UPG_STS_FW_WR_END")
            echo  "21"
            ;;
        "UPG_STS_FW_BC_START")
            echo  "30"
            ;;
        "UPG_STS_FW_BC_END")
            echo  "31"
            ;;
        *)
            raise "FAIL: Unknown upgrade_identifier {given:=$upgrade_identifier}" -l "$fn_name" -arg
            ;;
    esac
}

###############################################################################
# DESCRIPTION:
#   Function displays and interprets image dowload results.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  exit code
#   $2  download start time
#   $3  download timeout (not used)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   get_firmware_download_timer_result 0 2020.08.31-13:32:51
#   get_firmware_download_timer_result 1 2020.08.31-13:32:51
###############################################################################
get_firmware_download_timer_result()
{
    fn_name="um_lib:get_firmware_download_timer_result"
    exit_code=$1
    start_time=$2

    end_time=$(date -D "%H:%M:%S"  +"%Y.%m.%d-%H:%M:%S")
    t1=$(date -u -d "$start_time" +"%s")
    t2=$(date -u -d "$end_time" +"%s")

    download_time=$(( t2 - t1 ))

    if [ "$exit_code" -eq 0 ]; then
        log -deb "$fn_name - FW downloaded in given download time - downloaded in $download_time seconds"
    else
        ${OVSH} s AWLAN_Node -w upgrade_status=="$(get_um_code "UPG_ERR_DL_FW")"
        if [ "$?" -eq 0 ]; then
            log -deb "$fn_name - FW downloaded was aborted after upgrade_dl_timer"
        else
            ${OVSH} s AWLAN_Node
            raise "FAIL: FW download was not aborted after upgrade_dl_timer" -l "$fn_name" -tc
        fi
    fi

    return 0
}
