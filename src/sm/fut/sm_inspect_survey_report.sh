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
source "${FUT_TOPDIR}/shell/lib/sm_lib.sh"
source "${LIB_OVERRIDE_FILE}"

trap 'run_setup_if_crashed sm' EXIT SIGINT SIGTERM

usage="$(basename "$0") [-h] \$1 \$2 \$3 \$4 \$5 \$6

where arguments are:
    sm_report_radio=\$1 -- freq_band that is reported inside SM manager in logs (string)(required)
        - example:
            Caesar:
                50U freq_band is reported as 5GU
                50L freq_band is reported as 5GL
            Tyrion:
                50L freq_band is reported as 5G
    sm_channel=\$2 -- channel to inspect survey reporting on (int)(required)
    sm_survey_type=\$3 -- type of channel survey type in Wifi_Stats_Config (string)(required)
        - example
            - on-chan - inspect survey reporting on given channel
            - off-chan - inspect survey reporting off channel
    sm_reporting_interval=\$4 -- used as reporting_interval value for Wifi_Stats_Config (int)(required)
    sm_sampling_interval=\$5 -- used as sampling_interval value for Wifi_Stats_Config (int)(required)
    sm_report_type=\$6 -- used as report_type value for Wifi_Stats_Config (string)(required)

Script does following:
    - insert into Wifi_Stats_Config appropriate survey reporting configuration
    - tail logs (/tmp/logs/messages - logread -f) for matching patterns for initiating SM survey reporting
        - log messages are device/platform dependent

Dependent on:
    - running WM/NM managers - min_wm2_setup.sh - existance of active interfaces
    - sm_setup.sh
    - sm_reporting_env_setup.sh

Example of usage:
    $(basename "$0")
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if [ $# -lt 6 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

sm_radio_type=$1
sm_channel=$2
sm_survey_type=$3
sm_reporting_interval=$4
sm_sampling_interval=$5
sm_report_type=$6

tc_name="sm/$(basename "$0")"

log "$tc_name: Inspecting survey report on $sm_radio_type $sm_survey_type channel $sm_channel"

inspect_survey_report \
    "$sm_radio_type" \
    "$sm_channel" \
    "$sm_survey_type" \
    "$sm_reporting_interval" \
    "$sm_sampling_interval" \
    "$sm_report_type" || die "sm/$(basename "$0"): inspect_survey_report - Failed"

pass
