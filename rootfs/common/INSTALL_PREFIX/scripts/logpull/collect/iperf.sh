#!/bin/sh
#
# Collect iperf debug log
#
. "$LOGPULL_LIB"

collect_iperf_debug_log()
{
    if [ -e /tmp/debug_iperf_out.log ]; then
        collect_file /tmp/debug_iperf_out.log
    fi
}

collect_iperf_debug_log
