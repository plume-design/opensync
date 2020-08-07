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

if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
source "${LIB_OVERRIDE_FILE}"

usage="
$(basename "$0") [-h] \$1

where options are:
    -h  show this help message
arguments:
    time_ref=\$1 -- format: seconds since epoch. Used to compare system time. (int)(required)
    time_accuracy=\$2 -- format: seconds. Allowed time deviation from reference time. (int)(required)
It is important to compare timestamps to the same time zone: UTC is used internally!

example of usage:
    accuracy=2
    reference_time=\$(date --utc +\"%s\")
   /tmp/fut-base/shell/onbrd/$(basename "$0") \$reference_time \$time_accuracy
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
if [ $# -ne 2 ]; then
    echo 1>&2 "$0: incorrect number of input arguments"
    echo "$usage"
    exit 2
fi

time_ref=$1
time_accuracy=$2
tc_name="onbrd/$(basename "$0")"

# Timestamps in human readable format
time_ref_str=$(date -d @"${time_ref}")
time_now_str=$(date -d @"${time_now}")

# Calculate time difference and ensure absolute value
time_diff=$(( time_ref - time_now ))
if [ $time_diff -lt 0 ]; then
    time_diff=$(( -time_diff ))
fi

log "$tc_name: Checking time ${time_now_str} against reference ${time_ref_str}"
if [ $time_diff -le "$time_accuracy" ]; then
    log "$tc_name: Time difference ${time_diff}s is within ${time_accuracy}s"
else
    raise "Time difference ${time_diff}s is NOT within ${time_accuracy}s" -l "$tc_name" -tc
fi

pass
