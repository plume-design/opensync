#!/bin/sh
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
