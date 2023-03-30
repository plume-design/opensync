#!/bin/bash

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


current_dir=$(dirname "$(realpath "${BASH_SOURCE[0]}")")
export FUT_TOPDIR="$(realpath "$current_dir"/../../..)"

# FUT environment loading
source "${FUT_TOPDIR}/shell/config/default_shell.sh" &> /dev/null
# Ignore errors for fut_set_env.sh sourcing
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh &> /dev/null
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh" &> /dev/null

usage()
{
cat << usage_string
tools/server/get_common_name_from_certificate.sh [-h]
Description:
    This script echoes the common name in the client certificate.
Options:
    -h  show this help message
Arguments:
    client_cert=$1  -- client certificate used to extract common name - (string)(required)
Script usage example:
   ./tools/server/get_common_name_from_certificate.sh "client.pem"
usage_string
exit 1
}

NARGS=1
if [ $# -ne ${NARGS} ]; then
    raise "Requires exactly ${NARGS} input argument(s)." -arg
    usage
fi

client_cert=${1}
cert_file="${FUT_TOPDIR}/${client_cert}"

comm_name=$(openssl x509 -in $cert_file -noout -subject | sed 's/.*CN = //;s/,.*//' | tr '[:lower:]' '[:upper:]')
[ -z ${comm_name} ] && raise "Could not parse CN from certificate ${cert_file}" -l "./tools/server/get_common_name_from_certificate.sh" -fc
echo -n "${comm_name}"
