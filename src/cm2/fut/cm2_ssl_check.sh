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


# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi

source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/cm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"

trap '
    check_restore_management_access || true
    run_setup_if_crashed cm || true
' EXIT SIGINT SIGTERM

usage="
$(basename "$0") [-h] \$1

where options are:
    -h  show this help message

this script is dependent on following:
    - running CM manager
    - running NM manager

example of usage:
    /tmp/fut-base/shell/cm2/$(basename "$0")
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

tc_name="cm2/$(basename "$0")"
log "$tc_name: CM2 test - SSL check"

ca_cert_path=$(get_ovsdb_entry_value SSL ca_cert)
certificate_path=$(get_ovsdb_entry_value SSL certificate)
private_key_path=$(get_ovsdb_entry_value SSL private_key)

[ -e "$ca_cert_path" ] &&
    log "$tc_name: ca_cert file is valid - $ca_cert_path" ||
    raise "ca_cert file is missing - $ca_cert_path" -l "$tc_name" -tc

[ -e "$certificate_path" ] &&
    log "$tc_name: certificate file is valid - $certificate_path" ||
    raise "certificate file is missing - $certificate_path" -l "$tc_name" -tc

[ -e "$private_key_path" ] &&
    log "$tc_name: private_key file is valid - $private_key_path" ||
    raise "private_key file is missing - $private_key_path" -l "$tc_name" -tc

[ -s "$ca_cert_path" ] &&
    log "$tc_name: ca_cert file is not empty - $ca_cert_path" ||
    raise "ca_cert file is empty - $ca_cert_path" -l "$tc_name" -tc

[ -s "$certificate_path" ] &&
    log "$tc_name: certificate file is not empty - $certificate_path" ||
    raise "certificate file is empty - $certificate_path" -l "$tc_name" -tc

[ -s "$private_key_path" ] &&
    log "$tc_name: private_key file is not empty - $private_key_path" ||
    raise "private_key file is empty - $private_key_path" -l "$tc_name" -tc

pass
