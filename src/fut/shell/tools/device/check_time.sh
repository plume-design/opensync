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


# It is important for this particular testcase, to capture current time ASAP
time_now=$(date -u +"%s")

# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/wm2_lib.sh
source ${LIB_OVERRIDE_FILE}


usage="$(basename "$0") [-h]

Script is used to check device time synchonization.

where options are:
    -h  show this help message
arguments:
    time_ref=\$1 -- format: seconds since epoch. Used to compare system time. (int)(required)
    accuracy=\$2 -- format: seconds. Allowed time deviation from reference time. (int)(required)
It is important to compare timestamps to the same time zone: UTC is used internally!

example of usage:
    reference_time=\$(date --utc +\"%s\")
    /tmp/fut-base/shell/tools/$(basename "$0") \$reference_time
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

# Input parameters
if [[ $# -lt 1 ]]; then
    echo 1>&2 "$0: incorrect number of input arguments"
    echo "$usage"
    exit 2
fi

time_ref=$1
accuracy=5
tc_name="tools/device/$(basename "$0")"

# Timestamps in human readable format
time_ref_str=$(date -d @"${time_ref}")
time_now_str=$(date -d @"${time_now}")

# Calculate time difference and ensure absolute value
time_diff=$(( time_ref - time_now ))
if [ $time_diff -lt 0 ]; then
    time_diff=$(( -time_diff ))
fi

log "$tc_name: Checking time ${time_now_str} against reference ${time_ref_str}"
if [ $time_diff -le "$accuracy" ]; then
    log "$tc_name: Time difference ${time_diff}s is within ${accuracy}s"
else
    log "$tc_name: Time difference ${time_diff}s is NOT within ${accuracy}s. Test might fail."
fi

exit 0
