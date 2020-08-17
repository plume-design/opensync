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
capture=${this_dir}/conntrack_capture
new_vals=${this_dir}/new_vals

# usage
usage() {
  cat <<EOF
          Usage: ${prog} <[options]>
          Options:
                -h this message
                --source=<the source IP to filter on>
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

# parse the output of conntrack -L
# Check the mark of each flow and the number of packets
# - its n_packets field is not zero
# - its n_packets field is different from a previous record
# Parameters:
# $1: the output of ov-ofctl dump-flows <bridge>
# $2: source IP address to monitor
#     from the ovs-ofctl outpput parsing
# $3: The path to a debug file. Optional.
get_counters()
{
    prev=$1
    capture=$2
    source_ip=$3
    nw_proto=$4
    destination_port=$5
    dbg=$6

    # If no debug output provided, set the debug output to /dev/null
    if [ -z ${dbg} ]; then
        dbg=/dev/null
    fi

    now=$(date)
    # parse both previous records if any and the ovs-ofctl output
    awk -v dbgo=${dbg} \
        -v source="${source_ip}" \
        -v dest_port="${destination_port}" \
        -v proto="${nw_proto}" '
    BEGIN
    {
        OFS=","
        z0_tc = 0
        tc = 0
        ofc = 0
    }
    {
        if (NR==FNR) # Arrange previous counters in an associative array
        {
            # for future usage
            record_key = $1
            npackets_out[record_key] = $2
            npackets_in[record_key] = $3
            next
        }
        {
            # Jump the line if it does not contain the packets string
            if (!($0 ~ /packets/)) { print "No packet field";  next }

            # Process TCP flows
            if ($0 ~ /tcp/)
            {
                timeout = $3
                src = $5
                if (source != "src=None")
                {
                    if (!(src ~ source )) { next }
                }
                dst = $6
                sport = $7
                dport = $8
                out_packets = $9
                in_packets = $15
            }
            # Process UDP flows
            else if ($0 ~ /udp/)
            {
                timeout = $3
                src = $4
                dst = $5
                sport = $6
                dport = $7
                out_packets = $8
                in_packets = $15
            }
            # process ICMP flows
            else if ($0 ~ /icmp/)
            {
                timeout = $3
                src = $4
                dst = $5
                sport = $6
                dport = $7
                out_packets = $9
                in_packets = $16
            }
            else
            {
                next
            }

            protocol = $1
            if (proto != "None")
            {
                if (!(protocol ~ proto )) { next }
            }
            if (source != "src=None")
            {
                if (!(src ~ source )) { next }
            }

            if (dest_port != "dport=None")
            {
                if (!(dest_port ~ dport )) { next }
            }

            # Process the packet counters
            o_p = substr(out_packets, 9, length(out_packets) - 8)
            i_p = substr(in_packets, 9, length(in_packets) - 8)
            if ($0 ~ /zone/)
            {
                zs = $(NF - 1)
                zone = substr(zs, 6, length(zs) - 5)
                ms = $(NF - 2)
                mark = substr(ms, 6, length(ms) - 5)
            }
            else
            {
                zone = 0
                ms = $(NF - 1)
                mark = substr(ms, 6, length(ms) - 5)
            }

            if (zone == 0) { z0_tc = z0_tc + 1}
            tc = tc + 1
            offload_np = (o_p > 20) || (i_p > 20)
            offload_zn = (mark == 2)
            offload = offload_np && offload_zn
            if (offload) { ofc = ofc + 1 }
            print protocol,src,dst,sport,dport,"timeout="timeout,"zone="zone, \
                  " mark="mark " offload: "offload \
                  " outbound packets: "o_p, " inbound packets: "i_p

        }
    }
    END
    {
        OFS = " "
        print "----------------------------------------------------------------------"
        print "Zone 0: total connections: " z0_tc
        print "Zone 1: total connections: " tc, " offloaded connections: " ofc
    }
    ' ${prev} ${capture}
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
               source=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   SOURCE=$val
                   ;;
               dest=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   DEST=$val
                   ;;
               sport=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   SPORT=$val
                   ;;
               dport=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   DPORT=$val
                   ;;
               proto=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   PROTO=$val
                   ;;
               af=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   AF=$val
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

dbg_output=${DBGO:-}
seconds_to_wait=${INTERVAL:-5}
source="src="${SOURCE:-"None"}
dest="dst="${DEST:-"None"}
sport="sport"=${SPORT:-"None"}
dport="dport="${DPORT:-"None"}
proto=${PROTO:-"None"}
af=${AF:-"ipv4"}

# Check required commands
check_cmd 'conntrack'

# Make sure conntrack outputs the flow packet counters
echo 1 > /proc/sys/net/netfilter/nf_conntrack_acct

echo "$(date) Starting monitoring conntrack activity every ${seconds_to_wait} seconds"
echo ""
conntrack -L -f ${af} > ${capture}

# First capture, do not output the flows
get_counters ${capture} ${capture} ${source} ${proto} ${dbg_output}

# Now start the loop
while true; do
    sleep ${seconds_to_wait}
    # echo -e '\033[0;0H\033[2J'
    echo "$(date) conntrack activity over the past ${seconds_to_wait} seconds"
    echo "----------------------------------------------------------------------"
    conntrack -L -f ${af} > ${capture}
    get_counters ${capture} ${capture} ${source} ${proto} ${dport} ${dbg_output}
    echo ""
done
