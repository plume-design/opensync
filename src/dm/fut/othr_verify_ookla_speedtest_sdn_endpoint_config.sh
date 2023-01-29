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
source "${FUT_TOPDIR}/shell/lib/othr_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="dm/othr_setup.sh"
usage()
{
cat << usage_string
othr/othr_verify_ookla_speedtest_sdn_endpoint_config.sh [-h] arguments
Description:
    - Script verifies correct configuration of ookla speedtest feature on DUT.
Arguments:
    -h   show this help message
    \$1  (config_path) :  full path to the config file of speedtest server :   (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./othr/othr_verify_ookla_speedtest_sdn_endpoint_config.sh <SPEEDTEST_CONFIG_PATH>
Script usage example:
    ./othr/othr_verify_ookla_speedtest_sdn_endpoint_config.sh http://config.speedtest.net/v1/embed/x340jwcv4nz2ou3r/config
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
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "othr/othr_verify_ookla_speedtest_sdn_endpoint_config.sh" -arg
config_path=$1

log_title "othr/othr_verify_ookla_speedtest_sdn_endpoint_config.sh: OTHR test - Verify configuration of ookla speedtest feature"

check_kconfig_option "CONFIG_3RDPARTY_OOKLA" "y" ||
    check_kconfig_option "CONFIG_SPEEDTEST_OOKLA" "y" ||
        raise "OOKLA not present on device" -l "othr/othr_verify_ookla_speedtest_sdn_endpoint_config.sh" -s

ookla_bin="${OPENSYNC_ROOTDIR}/bin/ookla"
[ -e "$ookla_bin" ] &&
    log "othr/othr_verify_ookla_speedtest_sdn_endpoint_config.sh: ookla speedtest binary is present on system - Success" ||
    raise "FAIL: Ookla speedtest binary is not present on system" -l "othr/othr_verify_ookla_speedtest_sdn_endpoint_config.sh" -s

speed_test_result=$(${ookla_bin} --upload-conn-range=16 -fjson -c ${config_path} -f human-readable 2>/dev/null)
if [ $? -eq 0 ]; then
    log "othr/othr_verify_ookla_speedtest_sdn_endpoint_config.sh: Speedtest process started with below details:"
    echo "$speed_test_result"
else
    raise "FAIL: Speedtest process not started" -l "othr/othr_verify_ookla_speedtest_sdn_endpoint_config.sh" -tc
fi

pass
