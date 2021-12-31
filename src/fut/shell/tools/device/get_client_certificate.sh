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
# Script echoes single line so we are redirecting source output to /dev/null
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh &> /dev/null
source /tmp/fut-base/shell/config/default_shell.sh &> /dev/null
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh" &> /dev/null
[ -n "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" &> /dev/null
[ -n "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" &> /dev/null

usage()
{
cat << usage_string
./tools/device/get_client_certificate.sh [-h] arguments
Description:
    - This script echoes either absolute path to client certificate file/CA file
      or just file names from the SSL table respectively.
Arguments:
    -h  show this help message
    \$1 (file_type) : 'cert_file' to get certificate file and 'ca_file' to get CA file: (string)(required)
    \$2 (query) : 'full_path' to echo full path to the files and 'file_name' to echo just filenames : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./tools/device/get_client_certificate.sh <FILE-TYPE> <QUERY-OPTION>
Script usage example:
    ./tools/device/get_client_certificate.sh "cert_file" "full_path"
    ./tools/device/get_client_certificate.sh "ca_file" "file_name"
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
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly ${NARGS} input argument(s)" -l "tools/device/get_client_certificate.sh" -arg

file_type=${1}
query=${2}

if [ ${file_type} == "cert_file" ]; then
    file=$(get_ovsdb_entry_value SSL certificate -r)
elif [ ${file_type} == "ca_file" ]; then
    file=$(get_ovsdb_entry_value SSL ca_cert -r)
else
    raise "FAIL: ARG1 - Wrong option provided" -l "tools/device/get_client_certificate.sh" -arg
fi

[ -e "$file" ] ||
    raise "FAIL: '$file_type' is NOT present on DUT" -l "tools/device/get_client_certificate.sh" -tc

[ ${query} == "full_path" ] &&
    echo -n "${file}" && exit 0
[ ${query} == "file_name" ] &&
    echo -n "${file##*/}" && exit 0

raise "FAIL: ARG2 - Wrong option provided" -l "tools/device/get_client_certificate.sh" -arg
