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


prog=$0
this_dir=$(dirname "$0")
prev_vals=${this_dir}/prev_vals
capture=${this_dir}/ovs-ofctl_capture
new_vals=${this_dir}/new_vals

# usage
usage() {
  cat <<EOF
          Usage: ${prog} <[options]>
          Options:
                -h this message
                --bridge=<the ovs bridge> (default brsdn)
                --interval=<the polling interval in seconds> (default 5 seconds)
                --dbg_output=<path to a file> (default: n dbg output)
EOF
}

# Check if a specific command is in the path. Bail if not found.
check_cmd() {
    cmd=$1
    path_cmd=$(which ${cmd})
    if [ -z ${path_cmd} ]; then
        echo "Error: could not find ${cmd} command in path"
        exit 1
    fi
    echo "found ${cmd} as ${path_cmd}"
}

# Delete recorded values
delete_vals()
{
    vals=$1
    rm -f ${vals}
    # Set an empty line in the file to avoid awk bailing
    echo "" > ${vals}
}


# parse the output of ovs-ofctl dump-flow ${bridge}
# Check if each flow has seen activity:
# - its n_packets field is not zero
# - its n_packets field is different from a previous record
# Parameters:
# $1: the output of ov-ofctl dump-flows <bridge>
# $2: A file containing the tuples (flow id, packets counter)
#     from a previous ovs-ofctl outpput parsing
# 3:  A file updated to contain the tuples (flow id, packets counter)
#     from the ovs-ofctl outpput parsing
# $4: The path to a debug file. Optional.
get_counters()
{
    capture=$1
    previous_values=$2
    new_values=$3
    dbg=$4

    # If no debug output provided, set the debug output to /dev/null
    if [ -z ${dbg} ]; then
        dbg=/dev/null
    fi

    now=$(date)
    # parse both previous records if any and the ovs-ofctl output
    awk -v output=${new_values} -v dbgo=${dbg} -v anow="${now}" '
    {
        if (NR==FNR) # Arrange previous counters in an associative array
        {
            record_key = $1
            npackets0[record_key] = $2
            next
        }
        {
            # Jump the line if it does not contain the n_packets string
            if (!($0 ~ /n_packets/)) { next }

            # Retrieve the number of packets advertized in the current flow
            # Jump the line if the packets counter is 0
            n_p = substr($4, 11, length($4)-11)
            if (n_p == 0) { next }

            # Retrieve the flow key
            fkey = $(NF - 1)

            # Retrieve the flow action
            faction = $(NF)

            # Retrieve the flow table
            table = $3

            # Construct the lookup key for previous records
            lookup_key = table fkey "," faction

            # Record the (lookup_key, counter) tuple
            print lookup_key " " n_p " packets" >> output

            # Process the flow not previously recorded
            if (!(lookup_key in npackets0))
            {
                print anow ": key " lookup_key " not found " >> dbgo
                print lookup_key " " n_p " packets (new)"
                next
            }

            # Retrieve previous counters value
            prev_val = npackets0[lookup_key]

            # Log value for debug purposes
            print anow ": key " lookup_key " previous value " prev_val >> dbgo

            # Check if the packets counters is updated
            count = n_p - npackets0[lookup_key]
            print anow ": key " lookup_key " new value: " n_p ", diff " count >> dbgo

            # If the packets counter was updated, print the flow to stdout
            if (count != 0)
            {
                print lookup_key " " n_p " packets (+ " count ")"
            }
        }
    }
    ' ${previous_values} ${capture}
}


# h for help, long options otherwise
optspec="h-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -) LONG_OPTARG="${OPTARG#*=}"
           case "${OPTARG}" in
               interval=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   INTERVAL=$val
                   ;;
               dbg_output=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   DBGO=$val
                   ;;
               bridge=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   BRIDGE=$val
                   ;;
               *)
                   if [ "$OPTERR" = 1 ] && [ "${optspec:0:1}" != ":" ]; then
                       echo "Unknown option --${OPTARG}" >&2
                   fi
                   ;;
           esac;;
        h)
            usage
            exit 2
            ;;
        *)
            if [ "$OPTERR" != 1 ] || [ "${optspec:0:1}" = ":" ]; then
                echo "Non-option argument: '-${OPTARG}'"
            fi
            ;;
    esac
done

bridge=${BRIDGE:-brsdn}
dbg_output=${DBGO:-}
seconds_to_wait=${INTERVAL:-5}

# Check required commands
check_cmd 'ovs-ofctl'

# On start, remove any previous record
delete_vals ${prev_vals}
delete_vals ${new_vals}

echo "$(date) Starting monitoring openflow activity every ${seconds_to_wait} seconds"
echo ""
ovs-ofctl dump-flows ${bridge} > ${capture}

# First capture, do not output the flows
get_counters ${capture} ${prev_vals} ${prev_vals} ${dbg_output} > /dev/null


# Now start the loop
while true; do
    sleep ${seconds_to_wait}
    # echo -e '\033[0;0H\033[2J'
    echo "$(date) openflow activity over the past ${seconds_to_wait} seconds"
    echo "----------------------------------------------------------------------"
    ovs-ofctl dump-flows ${bridge} > ${capture}
    get_counters ${capture} ${prev_vals} ${new_vals} ${dbg_output}
    cp ${new_vals} ${prev_vals}
    delete_vals ${new_vals}
    echo ""
done
