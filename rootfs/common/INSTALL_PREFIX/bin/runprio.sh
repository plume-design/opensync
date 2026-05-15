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

# {# jinja-parse #}

# run a command with increased process priority
# and temporarily elevated system rt_runtime

# optional, per-platform or per-device mods
INSTALL_PREFIX={{ CONFIG_INSTALL_PREFIX }}
LOCAL_PERF_SH="$INSTALL_PREFIX/bin/runprio_local.sh"

if [ -e "$LOCAL_PERF_SH" ]; then
. $LOCAL_PERF_SH
fi

# chrt targets
NEW_RT_RUNTIME_PCT={{ CONFIG_SPEEDTEST_RT_RUNTIME }}
CHRT_PRIO="-r 1"
CHRT_APPS="iperf iperf3"

# nice targets (everything that is not chrt)
NICE_PRIO="-20"
SIGNALS="INT HUP TERM EXIT QUIT"

LOG_NAME="${0##*/}"

log()
{
    logger -s -t "$LOG_NAME" $@
}

use_chrt()
{
    local arg target
    for arg in "$@"; do
        for target in $CHRT_APPS; do
            if [ "$arg" = "$target" ]; then
                return 0
            fi
        done
    done

    return 1
}

perf_runprio_restore()
{
    RET=$?

    if [ -n "$ORIG_RT_RUNTIME" ]; then
        logger "[$PPID]: $0: restoring sched_rt_runtime from $NEW_RT_RUNTIME -> $ORIG_RT_RUNTIME"
        echo "$ORIG_RT_RUNTIME" > /proc/sys/kernel/sched_rt_runtime_us
    fi
    trap - $SIGNALS

    # call to possible external config
    if [ -e "$LOCAL_PERF_SH" ]; then
        perf_runprio_local_restore
    fi

    exit $RET
}

perf_runprio_apply()
{
    # temporarily elevate sched_rt_runtime
    ORIG_RT_RUNTIME=$(cat /proc/sys/kernel/sched_rt_runtime_us)
    ORIG_RT_PERIOD=$(cat /proc/sys/kernel/sched_rt_period_us)
    NEW_RT_RUNTIME=$((ORIG_RT_PERIOD * NEW_RT_RUNTIME_PCT / 100))
    if [ -n "$ORIG_RT_RUNTIME" -a "$NEW_RT_RUNTIME" -gt "$ORIG_RT_RUNTIME" ]; then
        logger "[$PPID]: $0: elevating sched_rt_runtime from $ORIG_RT_RUNTIME -> $NEW_RT_RUNTIME"
        trap perf_runprio_restore $SIGNALS
        echo "$NEW_RT_RUNTIME" > /proc/sys/kernel/sched_rt_runtime_us
    else
        logger "[$PPID]: $0: WARNING: not changing sched_rt_runtime from $ORIG_RT_RUNTIME -> $NEW_RT_RUNTIME"
    fi

    # if args start with - pass them to chrt
    if [ "${1:0:1}" = "-" ]; then
        CHRT_PRIO=
    fi

    # call to possible external config
    if [ -e "$LOCAL_PERF_SH" ]; then
        perf_runprio_local_apply
    fi
}

# apply any performance mods
perf_runprio_apply

if use_chrt "$@"; then
    logger "[$PPID]: $0: /usr/bin/chrt $CHRT_PRIO $*"
    /usr/bin/chrt $CHRT_PRIO "$@"
else
    logger "[$PPID]: $0: /bin/nice -$NICE_PRIO $*"
    /bin/nice -$NICE_PRIO "$@"
fi

# restore original system
perf_runprio_restore
