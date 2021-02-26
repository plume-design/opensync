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
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="tools/device/check_wan_connectivity.sh"
def_n_ping=2
def_ip="1.1.1.1"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script checks device L3 upstream connectivity with ping tool
Dependency:
    - "ping" tool with "-c" option to specify number of packets sent
Arguments:
    -h                        : Show this help message
    - \$1 (n_ping)            : How many packets are sent                    : (int)(optional)(default=${def_n_ping})
    - \$2 (internet_check_ip) : IP address to validate internet connectivity : (string)(optional)(default=${def_ip})
Script usage example:
   ./${tc_name}
usage_string
}
while getopts h option > /dev/null 2>&1; do
    case "$option" in
        h)
            usage && exit 1
            ;;
        *)
            ;;
    esac
done

n_ping=${1:-$def_n_ping}
internet_check_ip=${2:-$def_ip}

wait_for_function_response 0  "ping -c${n_ping} ${internet_check_ip}"
if [ $? -eq 0 ]; then
    log "${tc_name}: Can ping internet"
    exit 0
else
    log -err "${tc_name}: Can not ping internet"
    exit 1
fi
