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
source ${this_dir}/dpi_utils.sh

capture=${this_dir}/openflow_config_capture


# usage
usage() {
  cat <<EOF
          Usage: ${prog} --cmd=[insert | delete] <[options]>
          Options:
                -h this mesage
                --bridge=<LAN bridge>
EOF
}

# Get command line arguments
# h for help, long options otherwise
optspec="h-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -) LONG_OPTARG="${OPTARG#*=}"
           case "${OPTARG}" in
                cmd=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    # echo "Parsing option: '--${opt}', value: '${val}'"
                    CMD=$val
                    ;;
                intf=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    # echo "Parsing option: '--${opt}', value: '${val}'"
                    INTF=$val
                    ;;
                ofport=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    # echo "Parsing option: '--${opt}', value: '${val}'"
                    OF_PORT=$val
                    ;;
                plugin=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    # echo "Parsing option: '--${opt}', value: '${val}'"
                    PLUGIN=$val
                    ;;
                dso_init=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    # echo "Parsing option: '--${opt}', value: '${val}'"
                    DSO_INIT=$val
                    ;;
                topic_prefix=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    # echo "Parsing option: '--${opt}', value: '${val}'"
                    TOPIC_PREFIX=$val
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


# Let's start
intf=${INTF:-br-home.devdpi}
ofport=${OF_PORT:-20001} # must be unique to the bridge
gcmd=${CMD}
plugin=${PLUGIN}
dso_init=${DSO_INIT}
# Chek that insert/delete was passed
if [ -z ${gcmd} ]; then
    usage
    exit 1
fi

if [ ${gcmd} != "insert" ] && [ ${gcmd} != "delete" ]; then
    echo "Unknown command ${cmd}. Exiting"
    exit 1
fi

# Check required commands
check_platform_cmds

# set Flow_Service_Manager_Config plugin parameters
filter=ip
fsm_handler=dev_dpi # must start with 'dev' so the controller leaves it alone

# Compute the plugin mqtt topic
location_id=$(get_location_id)
node_id=$(get_node_id)
topic_prefix=${TOPIC_PREFIX:-dev-test/${fsm_handler}}
mqtt_v="${topic_prefix}/${node_id}/${location_id}"

# Retrieve the LAN bridge
bridge=$(get_lan_br)
echo "LAN bridge: ${bridge}"

# Retrieve the ofport of the cloud controlled dpi interface
cloud_ofport=$(get_cloud_dpi_ofport)
if [ -z ${cloud_ofport} ]; then
    echo "Could not retrieve the cloud controlled dpi ofport"
    exit 1
fi

echo "cloud controlled dpi ofport: ${cloud_ofport}"

# Create or delete openflow rules
ovsh -T s Openflow_Config > ${capture}
find_dpi_rules ${capture} ${cloud_ofport} ${ofport} ${gcmd} ${bridge} > /tmp/cmds
while read -r l; do
    $l;
done < /tmp/cmds

# Create the dpi interface if instructed to
if [ ${gcmd} == "insert" ]; then
    set_plugin
else
    delete_plugin
fi
