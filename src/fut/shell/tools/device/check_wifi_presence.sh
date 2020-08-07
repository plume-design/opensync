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


# Include basic environment config from file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi

source ${FUT_TOPDIR}/shell/lib/unit_lib.sh
source $LIB_DIR/wm2_lib.sh
source ${LIB_OVERRIDE_FILE}

trap 'run_setup_if_crashed wm || true' EXIT SIGINT SIGTERM

# check_type options:
#  - "Wifi_VIF_State" : default
#  - "Wifi_Radio_State"
#  - "client_connect"
#  - "leaf_connect"

check_type=${1:-Wifi_VIF_State}
enabled=${2:-true}
key=$3
value=$4

log "tools/device/$(basename "$0"): Checking WiFi presence - check type: $check_type"

case $check_type in
    Wifi_VIF_State)
        ${OVSH} s Wifi_VIF_State --where $key==$value enabled:=$enabled
        [[ $? == 0 ]] &&
            log "tools/device/$(basename "$0"): WiFi is present"; exit $? ||
            log "tools/device/$(basename "$0"): WiFi is NOT present"; exit $?
    ;;
    Wifi_Radio_State)
        ${OVSH} s Wifi_Radio_State --where $key==$value enabled:=$enabled
        [[ $? == 0 ]] &&
            log "tools/device/$(basename "$0"): WiFi is present"; exit $? ||
            log "tools/device/$(basename "$0"): WiFi is NOT present"; exit $?
    ;;
    leaf_connect)
        log "tools/device/$(basename "$0"): Not implemented. Pending implementation!"
        exit 0
    ;;
    client_connect)
        log "tools/device/$(basename "$0"): Not implemented. Pending implementation!"
        exit 0
    ;;
    *)
        log "tools/device/$(basename "$0"): Usage: $0 {Wifi_VIF_State|Wifi_Radio_State|client_connect}"
        exit 1
esac
