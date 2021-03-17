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

OVSH=$CONFIG_INSTALL_PREFIX/tools/ovsh
BSS_IS_UP_D=$CONFIG_INSTALL_PREFIX/scripts/healthcheck.bss.d

run_parts()
{
    dir=$1
    shift
    for i in $(ls $dir/*)
    do
        ( . "$i" "$@" ) && return 0
    done
    return 1
}

for ifname in $($OVSH -r s Wifi_VIF_Config if_name -w enabled==true)
do
    $OVSH -rU s Wifi_Radio_Config enabled vif_configs \
        | grep -F -- "$($OVSH -rU s Wifi_VIF_Config _uuid -w if_name==$ifname)" \
        | awk -vifname="$ifname" '$1 != "true" {print ifname " skip check, phy is down"}' \
        | grep . && continue
    run_parts "$BSS_IS_UP_D" "$ifname" && continue
    log_warn "$ifname: bss is not up"
    exit 1
done
