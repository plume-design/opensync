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
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="onbrd/$(basename "$0")"
manager_setup_file="onbrd/onbrd_setup.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    Validate firmware_version field in table AWLAN_Node.
    The test script acquires the FW version string automatically from the
    device and matches to one of two matching rules.
Arguments:
    -h              : show this help message
    \$1 match_rule  : how do we verify that the FW version string is valid: (string)(required)
                    : Options:
                    :   - non_empty(default): only verify that the version string is present and not empty
                    :   - pattern_match     : match the version string with the requirements set by the cloud
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} match_rule
Script usage example:
   ./${tc_name} non_empty
   ./${tc_name} pattern_match
usage_string
exit 1
}
while getopts h option; do
    case "$option" in
        h)
            usage
            ;;
        *)
            raise "Unknown argument" -l "${tc_name}" -arg
            ;;
    esac
done

trap '
fut_info_dump_line
print_tables AWLAN_Node
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=1
[ $# -ne ${NARGS} ] && raise "Requires exactly '${NARGS}' input argument(s)" -l "${tc_name}" -arg
match_rule=${1:-"non_empty"}

log_title "$tc_name: ONBRD test - Verify FW version string in AWLAN_Node '${match_rule}'"

# TESTCASE:
fw_version_string=$(get_ovsdb_entry_value AWLAN_Node firmware_version -r)
log "$tc_name: Verifying FW version string '${fw_version_string}'"

if [ "${match_rule}" = "non_empty" ]; then
    log -deb "$tc_name: FW version string must not be empty"
    [ "${fw_version_string}" = "non_empty" ] &&
        log -deb "$tc_name: FW version string is not empty" ||
        raise "FW version string is empty" -l "$tc_name" -tc
elif [ "${match_rule}" = "pattern_match" ]; then
    log -deb "$tc_name: FW version string must match parsing rules and regular expression"
    verify_fw_pattern "${fw_version_string}" &&
        log -deb "$tc_name: FW version string is valid" ||
        raise "FW version string is not valid" -l "$tc_name" -tc
else
    raise "Invalid match_rule '${match_rule}', must be 'non_empty' or 'pattern_match'" -l "$tc_name" -arg
fi

pass
