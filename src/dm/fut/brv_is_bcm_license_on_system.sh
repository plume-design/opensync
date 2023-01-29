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
source "${FUT_TOPDIR}/shell/lib/brv_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

brv_setup_file="brv/brv_setup.sh"
usage()
{
cat << usage_string
brv/brv_is_bcm_license_on_system.sh [-h] arguments
Description:
    - Script checks if:
        1. BCM license is present on the system.
        2. License has support for services - OpenvSwitch HW acceleration
            and service queue needed for WorkPass rate limiting.
Arguments:
    -h : show this help message
    \$1 (license_file) : BCM license with full path : (string)(required)
    \$2 (service) : Service support to be validated: (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${brv_setup_file} (see ${brv_setup_file} -h)
                 Run: ./brv/brv_is_bcm_license_on_system.sh <LICENSE-FILE> <SERVICE>
Script usage example:
   ./brv/brv_is_bcm_license_on_system.sh "/proc/driver/license" "FULL OVS"
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
NARGS=2
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "brv/brv_is_bcm_license_on_system.sh" -arg

license_file=$1
service=$2

log_title "brv/brv_is_bcm_license_on_system.sh: BRV test - Check if license '${license_file}' is present on BCM device"

test -e "${license_file}"
if [ $? = 0 ]; then
    log "brv/brv_is_bcm_license_on_system.sh: License '${license_file}' found on device - Success"
else
    raise "FAIL: License '${license_file}' could not be found on device" -l "brv/brv_is_bcm_license_on_system.sh" -tc
fi

cat "${license_file}" | grep -w "${service}" &&
    log "brv/brv_is_bcm_license_on_system.sh: License '${license_file}' has support for '${service}' - Success" ||
    raise "FAIL: License '${license_file}' does not have support for '${service}'" -l "brv/brv_is_bcm_license_on_system.sh" -tc

pass
