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
source "${FUT_TOPDIR}/shell/lib/cm2_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

cm_setup_file="cm2/cm2_setup.sh"
usage()
{
cat << usage_string
cm2/cm2_ssl_check.sh [-h]
Description:
    - Script checks for required SSL verification files used by CM contained in SSL table
      Test fails if any of the files is not present in given path or it is empty
Options:
    -h  show this help message
Testcase procedure:
    - On DEVICE: Run: ./${cm_setup_file} (see ${cm_setup_file} -h)
    - On DEVICE: Run: ./cm2/cm2_ssl_check.sh
Script usage example:
    ./cm2/cm2_ssl_check.sh
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

trap '
fut_info_dump_line
print_tables SSL Manager Connection_Manager_Uplink
ifconfig eth0
check_restore_management_access || true
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "cm2/cm2_ssl_check.sh: CM2 test - SSL Check"

ca_cert_path=$(get_ovsdb_entry_value SSL ca_cert)
certificate_path=$(get_ovsdb_entry_value SSL certificate)
private_key_path=$(get_ovsdb_entry_value SSL private_key)

[ -e "$ca_cert_path" ] &&
    log "cm2/cm2_ssl_check.sh: ca_cert file is valid - $ca_cert_path - Success" ||
    raise "FAIL: ca_cert file is missing - $ca_cert_path" -l "cm2/cm2_ssl_check.sh" -tc
[ -e "$certificate_path" ] &&
    log "cm2/cm2_ssl_check.sh: certificate file is valid - $certificate_path - Success" ||
    raise "FAIL: certificate file is missing - $certificate_path" -l "cm2/cm2_ssl_check.sh" -tc
[ -e "$private_key_path" ] &&
    log "cm2/cm2_ssl_check.sh: private_key file is valid - $private_key_path - Success" ||
    raise "FAIL: private_key file is missing - $private_key_path" -l "cm2/cm2_ssl_check.sh" -tc

[ -s "$ca_cert_path" ] &&
    log "cm2/cm2_ssl_check.sh: ca_cert file is not empty - $ca_cert_path - Success" ||
    raise "FAIL: ca_cert file is empty - $ca_cert_path" -l "cm2/cm2_ssl_check.sh" -tc
[ -s "$certificate_path" ] &&
    log "cm2/cm2_ssl_check.sh: certificate file is not empty - $certificate_path - Success" ||
    raise "FAIL: certificate file is empty - $certificate_path" -l "cm2/cm2_ssl_check.sh" -tc
[ -s "$private_key_path" ] &&
    log "cm2/cm2_ssl_check.sh: private_key file is not empty - $private_key_path - Success" ||
    raise "FAIL: private_key file is empty - $private_key_path" -l "cm2/cm2_ssl_check.sh" -tc

pass
