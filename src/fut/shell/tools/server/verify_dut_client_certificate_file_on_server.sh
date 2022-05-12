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
source "${FUT_TOPDIR}/shell/config/default_shell.sh"
# Ignore errors for fut_set_env.sh sourcing
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh &> /dev/null
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
source "${FUT_TOPDIR}/shell/lib/rpi_lib.sh"

usage()
{
cat << usage_string
tools/server/verify_dut_client_certificate_file_on_server.sh [-h]
Description:
    Validate if the "client certificate" on DUT is signed by authentic CA and also validate
    other certificate parameters like Issuer, Common Name.
    The script is intended for execution on RPI server or local PC within the framework
    directory structure, not on DUT!
Options:
    -h  show this help message
Arguments:
    client_cert=$1  -- client certificate that needs to be validated against CA - (string)(required)
    ca_cert=$2      -- CA certificate file that is used to validate client certificate - (string)(required)
Script usage example:
   ./tools/server/verify_dut_client_certificate_file_on_server.sh "client.pem" "ca.pem"
usage_string
exit 1
}

NARGS=2
if [ $# -ne ${NARGS} ]; then
    log -err "tools/server/verify_dut_client_certificate_file_on_server.sh: Requires exactly ${NARGS} input argument(s)."
    usage
fi

client_cert=${1}
ca_cert=${2}

plume_ca_file="${FUT_TOPDIR}/shell/tools/server/files/plume_ca_chain.pem"
cert_file="${FUT_TOPDIR}/${client_cert}"
ca_file="${FUT_TOPDIR}/${ca_cert}"

trap '
fut_info_dump_line
print_certificate_details $cert_file
print_certificate_details $plume_ca_file
print_certificate_details $ca_file
fut_info_dump_line
' EXIT SIGINT SIGTERM

# TEST EXECUTION:
log "tools/server/verify_dut_client_certificate_file_on_server.sh: Validating client certificate '$cert_file'..."

openssl x509 -in $cert_file -noout > /dev/null
[ $? -eq 0 ] &&
    log "tools/server/verify_dut_client_certificate_file_on_server.sh: Certificate ${client_cert} is in valid PEM format" ||
    raise "tools/server/verify_dut_client_certificate_file_on_server.sh: Certificate ${client_cert} format is not valid. Expected format of the certificate is PEM!" -l "tools/server/verify_dut_client_certificate_file_on_server.sh" -tc

openssl verify -verbose -CAfile $plume_ca_file $ca_file > /dev/null
[ $? -eq 0 ] &&
    log "tools/server/verify_dut_client_certificate_file_on_server.sh: CA certificate: ${ca_cert} approved by Plume CA: $plume_ca_file" ||
    raise "tools/server/verify_dut_client_certificate_file_on_server.sh: CA Certificate: ${ca_cert} not approved by Plume CA: $plume_ca_file" -l "tools/server/verify_dut_client_certificate_file_on_server.sh" -tc

end_date=$(openssl x509 -enddate -noout -in $cert_file | cut -d'=' -f2-)
openssl x509 -checkend 0 -noout -in $cert_file > /dev/null
[ $? -eq 0 ] &&
    log "tools/server/verify_dut_client_certificate_file_on_server.sh: Certificate ${client_cert} is not expired, valid until $end_date" ||
    raise "tools/server/verify_dut_client_certificate_file_on_server.sh: Certificate ${client_cert} has expired on $end_date" -l "tools/server/verify_dut_client_certificate_file_on_server.sh" -tc

pass
