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

# usage
usage() {
  cat <<EOF
          Usage: ${prog} --client1=<the first client> --client2=<the second client> <[options]>
          Options:
                -h this message
                --bridge=<the ovs bridge>
EOF
}

# Create tap interface
gen_tap_cmd() {
    cat << EOF
ovs-vsctl add-port ${bridge} ${intf}  \
          -- set interface ${intf}  type=internal \
          -- set interface ${intf}  ofport_request=${ofport}
EOF
}

# Mark the interface no-flood, only the traffic matching the flow filter
# will hit the plugin
gen_no_flood_cmd() {
    cat << EOF
ovs-ofctl mod-port ${bridge} ${intf} no-flood
EOF
}



# Run the requested test
run_test() {
    eval $(${do_test})
}

START=0
STOP=0

# h for help, long options otherwise
optspec="h-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -) LONG_OPTARG="${OPTARG#*=}"
           case "${OPTARG}" in
                client1=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    CLIENT1=$val
                    ;;
                client2=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    CLIENT2=$val
                    ;;
                bridge=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    BRIDGE=$val
                    ;;
                tap_intf=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    INTF=$val
                    ;;
                tap_ofport=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    OFPORT=$val
                    ;;
                test=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    TEST=$val
                    ;;
                start )
                    START=1
                    ;;
                stop )
                    STOP=1
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

client1_mac=${CLIENT1}
client2_mac=${CLIENT2}
client3_mac=${CLIENT3}
test=${TEST}
start=${START}
stop=${STOP}
bridge=${BRIDGE:-brsdn}
tap_intf=${INTF:-tap0}
tap_ofport=${OFPORT:-5000}

case ${test} in
    "test1" )
        ;;
    "test2" )
        ;;
    "test3a" )
        ;;
    "test3b" )
        ;;
    "test4" )
        ;;
    *)
        echo "Error: unrecognized test ${test}"
        usage
        exit 1
        ;;
esac

# Check required commands
check_cmd 'ovs-vsctl'
check_cmd 'ip'
check_cmd 'ovs-ofctl'

# Get the bridge mac address
br_mac=$(ip link show dev ${bridge} | \
             awk '{ if ( ! ($0 ~ /link\/ether/) ) { next } print $2 }')

# Load the available tests
basedir=$(dirname "$0")
source ${basedir}/openflow_test1.sh
source ${basedir}/openflow_test2.sh
source ${basedir}/openflow_test3a.sh
source ${basedir}/openflow_test3b.sh
source ${basedir}/openflow_test4.sh

# Basic check on the start/stop parameters
valid=$((start^stop))
if [ ${valid} -eq 0 ]; then
    echo "start/stop arguments mismatch"
    exit 1
fi

# build the test routine name
if [ ${start} -eq 1 ]; then
    action="start_"
fi
if [ ${stop} -eq 1 ]; then
    action="stop_"
fi
do_test=${action}${test}

# Run the requested test
run_test
