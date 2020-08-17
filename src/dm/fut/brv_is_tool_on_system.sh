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


# Include basic environment config
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source ${FUT_TOPDIR}/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/brv_lib.sh

tc_name="brv/$(basename $0)"
usage()
{
cat << EOF
${tc_name} [-h] tool_path
where options are:
    -h  show this help message
input arguments:
    tool_path=$1 -- name or path of the required tool - (string)(required)
                    If only the tool name is provided, PATH is searched.
                    If the absolute path is provided, that is used to confirm the presence.
this script is dependent on following:
    - running brv_setup.sh
example of usage:
   ${tc_name} "ls"
   ${tc_name} "bc"
EOF
}

while getopts h option; do
    case "$option" in
        h)
            usage
            exit 1
            ;;
    esac
done

NARGS=1
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
tool_path=$1
log_title "${tc_name}: Verify tool '${tool_path}' is present on device"

is_tool_on_system ${tool_path}
rc=$?
if [ $rc == 0 ]; then
    log -deb "${tc_name}: tool '${tool_path}' found on device"
elif [ $rc == 126 ]; then
    raise "Tool '${tool_path}' found on device but could not be invoked" -l "${tc_name}" -ec ${rc} -tc
else
    raise "Tool '${tool_path}' could not be found on device" -l "${tc_name}" -ec ${rc} -tc
fi

pass
