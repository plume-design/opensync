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


# Reset lib source guards
export FUT_BRV_LIB_SRC=false
export FUT_CM2_LIB_SRC=false
export FUT_DM_LIB_SRC=false
export FUT_NM2_LIB_SRC=false
export FUT_ONBRD_LIB_SRC=false
export FUT_PM_LED_LIB_SRC=false
export FUT_QM_LIB_SRC=false
export FUT_RPI_LIB_SRC=false
export FUT_SM_LIB_SRC=false
export FUT_UM_LIB_SRC=false
export FUT_UNIT_LIB_SRC=false
export FUT_UT_LIB_SRC=false
export FUT_WM2_LIB_SRC=false

# Export FUT required env vars if not already set
[ -z "$FUT_TOPDIR" ] && export FUT_TOPDIR="/tmp/fut-base"
[ -z "$OPENSYNC_ROOTDIR" ] && export OPENSYNC_ROOTDIR="/usr/opensync"
[ -z "$LOGREAD" ] && export LOGREAD="cat /var/log/messages"
[ -z "$DEFAULT_WAIT_TIME" ] && export DEFAULT_WAIT_TIME=30
[ -z "$OVSH" ] && export OVSH="${OPENSYNC_ROOTDIR}/tools/ovsh --quiet --timeout=180000"
[ -z "$CAC_TIMEOUT" ] && export CAC_TIMEOUT=60
[ -z "$LIB_OVERRIDE_FILE" ] && export LIB_OVERRIDE_FILE=
[ -z "$PATH" ] && export PATH="/bin:/sbin:/usr/bin:/usr/sbin:${OPENSYNC_ROOTDIR}/tools:${OPENSYNC_ROOTDIR}/bin"
[ -z "$MGMT_IFACE" ] && export MGMT_IFACE=eth0
[ -z "$MGMT_IFACE_UP_TIMEOUT" ] && export MGMT_IFACE_UP_TIMEOUT=10
[ -z "$MGMT_CONN_TIMEOUT" ] && export MGMT_CONN_TIMEOUT=30

echo "${FUT_TOPDIR}/shell/config/default_shell.sh sourced"
