#!/bin/sh -axe

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


## DESCRIPTION ##
# This is time-event logger test.
## /DESCRIPTION ##

## PARAMETERS ##
# shell ssh access command
test -n "$dut"
# number of logs to be used for testing
logno=${logno:-256}
# command to tail syslog output
syslog_tail=${syslog_tail:-"logread -f"}
# temporary directory since /tmp/ might not be available
tmpdir=${tmpdir:-"/tmp"}
## /PARAMETERS ##

self=$0

step() {
    name=${self}_$(echo "$*" | tr ' ' '_' | tr -dc a-z0-9_)
    if "$@"
    then
        echo "$name PASS" | tee -a "logs/$self/ret"
    else
        echo "$name FAIL" | tee -a "logs/$self/ret"
    fi
}

rm -f "logs/$self/ret"

validate_telog() {
    $dut <<. || return $?
        logfile="${tmpdir}/telogs.txt"
        cat_tag="TETEST"
        tesrv_name="qm"
        logno_ok=0
        logno_fail=0

        raise() {
            echo \$1
            kill \$(jobs -p) > /dev/null 2>&1
            false
        }

        # generates TELOGs to the syslog
        # \$1 : number of logs
        gen_telogs() {
            logno_ok=0
            logno_fail=0
            for n in \$(seq 1 \$1); do
                if timeout 5 telog -c \$cat_tag -s "STEP_\$n" "Time-event log testing message \$n" > /dev/null 2>&1; then
                    logno_ok=\$((logno_ok+1))
                elif [ \$? -ge 124 ]; then # timeout detected
                    echo "telog call no. \$n failed with timeout"
                    return 1
                else # regular telog error, expected when log server is down
                    logno_fail=\$((logno_fail+1))
                fi
            done
            return 0
        }

        echo "Starting time-event logger test"

        if [ ! -x "\$(which telog)" ]; then
            raise "telog tool not installed, test skipped"
        fi

        # pid of time-event logging server process
        tesrv_pid=

        tesrv_pid=\$(pidof \$tesrv_name)
        if [ ! "\$tesrv_pid" ]; then
            echo "Starting time-event logging server \$tesrv_name"
            start_specific_manager \$tesrv_name ||
                raise "FAIL: Could not start time-event logging server \$tesrv_name"
            sleep 5
            tesrv_pid=\$(pidof \$tesrv_name)
        fi

        # start logread to capture all TELOGs with my unique category when QM is running
        rm -f \$logfile
        if [ "\$tesrv_pid" ]; then
            echo "Capturing TELOGs into \$logfile file"
            $syslog_tail | grep -e "TELOG.*\$cat_tag" >\$logfile &
        fi

        # generate 256 telogs to check the resistance of the system to heavy load
        echo "Generating \$logno time-event logs for verification"
        if ! gen_telogs \$logno; then
            raise "FAIL: cannot generate time-event logs"
        else
            # wait a while to flush all logs to the file
            sleep 5
        fi

        # kill all background jobs
        kill \$(jobs -p) > /dev/null 2>&1

        # Until now no problems, check logs if any
        if [ -r \$logfile ]; then
            echo "Verifying captured time-event logs"
            if [ \$logno_ok -gt 0 ]; then
                # we expect to capture 95% of generated logs
                logno_min=\$((logno_ok*95/100))
                lc=\$(wc -l \$logfile)
                lc=\${lc%% *}
                if [ "\$lc" -a "\$lc" -ge "\$logno_min" ]; then
                    echo "\$logno logs transmitted. \$logno_ok of \$logno logs received."
                else
                    raise "Too few TELOGs generated to \$logfile Expected <\$logno_min..\$logno> logs, captured \$lc logs."
                fi
            else
                raise "Log server is broken, \$logno_fail logs were lost (not captured)"
            fi
        else
            log "Log server is down, verification skipped"
        fi
.
}

step validate_telog

cat "logs/$self/ret"
