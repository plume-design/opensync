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
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source ${FUT_TOPDIR}/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/unit_lib.sh
source ${LIB_OVERRIDE_FILE}

############################################ INFORMATION SECTION - START ###############################################
#
#   Base library of common Upgrade Manager functions (Plume specific)
#
############################################ INFORMATION SECTION - STOP ################################################


############################################ SETUP SECTION - START #####################################################

um_setup_test_environment()
{
    fw_path=$1
    fn_name="um_lib:um_setup_test_environment"

    log -deb "UM SETUP"

    device_init ||
        raise "device_init" -l "$fn_name" -fc

    start_openswitch ||
        raise "start_openswitch" -l "$fn_name" -fc

    start_udhcpc eth0 true ||
        raise "start_udhcpc" -l "$fn_name" -fc

    log -deb "lib/um_lib: um_setup_environment - Erasing $fw_path"
    rm -rf "$fw_path" || true

    start_specific_manager um -d ||
        raise "start_specific_manager um" -l "$fn_name" -fc

    log -deb "$fn_name - UM setup - end"
}

reset_um_triggers()
{
    fw_path=$1
    fn_name="um_lib:reset_um_triggers"

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
          raise "{AWLAN_Node -> update}" -l "$fn_name" -oe
}

############################################ SETUP SECTION - STOP ######################################################

get_um_code()
{
    upgrade_identifier=$1
    fn_name="um_lib:get_um_code"

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
            raise "upgrade_identifier {given:=$upgrade_identifier}" -l "$fn_name" -arg
            ;;
    esac
}

fw_dl_timer_result()
{
    exit_code=$1
    start_time=$2
    fn_name="um_lib:fw_dl_timer_result"

    end_time=$(date -D "%H:%M:%S"  +"%Y.%m.%d-%H:%M:%S")
    t1=$(date -u -d "$start_time" +"%s")
    t2=$(date -u -d "$end_time" +"%s")

    download_time=$(( t2 - t1 ))

    if [ "$exit_code" -eq 0 ]; then
        log -deb "$fn_name - FW downloaded in given download_timer - downloaded in $download_time"
    else

        ${OVSH} s AWLAN_Node -w upgrade_status=="$(get_um_code "UPG_ERR_DL_FW")"

        if [ "$?" -eq 0 ]; then
            log -deb "$fn_name - FW downloaded was aborted after upgrade_dl_timer"
        else
            ${OVSH} s AWLAN_Node
            raise "FW download was not aborted after upgrade_dl_timer" -l "$fn_name" -tc
        fi
    fi

    return 0
}
