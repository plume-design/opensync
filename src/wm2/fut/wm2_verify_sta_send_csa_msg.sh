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
source "${FUT_TOPDIR}/shell/lib/wm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="wm2/$(basename "$0")"
manager_setup_file="wm2/wm2_setup.sh"
# Wait for channel to change, not necessarily become usable (CAC for DFS)
channel_change_timeout=60

usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script is executed on LEAF device to verify GW sent csa msg on GW channel change
Arguments:
    -h  show this help message
    \$1  (gw_vif_mac)     : GW VIF mac address that LEAF is connected to : (string)(required)
    \$2  (gw_csa_channel) : Channel that triggered CSA on GW             : (string)(required)
Script usage example:
    ./${tc_name} d2:b4:f7:f0:23:26 6
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
[ $# -ne ${NARGS} ] && usage && raise "Requires '${NARGS}' input argument(s)" -l "${tc_name}" -arg

gw_vif_mac=${1}
gw_csa_channel=${2}

log_title "$tc_name: WM2 test - Verifying sta_send_csa message for MAC ${gw_vif_mac} to channel ${gw_csa_channel}"

# Mar 18 10:29:06 WM[19842]: <INFO>    TARGET: wifi0: csa rx to bssid d2:b4:f7:f0:23:26 chan 6 width 0MHz sec 0 cfreq2 0 valid 1 supported 1
wm_csa_log_grep="$LOGREAD | tail -500 | grep -i 'csa' | grep -i '${gw_vif_mac} chan ${gw_csa_channel}'"

wait_for_function_response 0 "${wm_csa_log_grep}" 10 &&
    log -deb "$tc_name - sta_send_csa message found in logs" ||
    raise "FAIL: Failed to find sta_send_csa message in logs" -l "$tc_name" -tc

pass
