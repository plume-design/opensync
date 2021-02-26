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


# Execute this script with the following options:
# /tmp/app_policy.sh --cmd=insert --dpi_plugin=walleye_dpi

# To change the provider plugin from brightcloud to webpulse
# following changes needs are needed:
# 1. policy_name=dev_webpulse
# 2. provider_plugin=webpulse
# 3. Replace all ["set",[11] value to ["set",[3]

prog=$0
this_dir=$(dirname "$0")

# insert test policy
# Create the openflow rule for the egress traffic
insert_policy_1_cmd() {
    cat << EOF
ovsh i FSM_Policy \
     policy:=${policy_name} \
     idx:=${idx} \
     mac_op:=out \
     action:=gatekeeper \
     log:=all \
     name:="${policy_name}_rule_0"
EOF
}

insert_policy_2_cmd() {
    cat << EOF
ovsh i FSM_Policy \
     policy:=${policy_name} \
     idx:=${idx} \
     fqdncat_op:=out \
     action:=allow \
     log:=all \
     name:="${policy_name}_rule_1"
EOF
}


# get pod's location ID
get_location_id() {
    ovsh s AWLAN_Node mqtt_headers | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="locationId"){print $(i+2)}}}'
}

# get pod's node ID
get_node_id() {
    ovsh s AWLAN_Node mqtt_headers | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="nodeId"){print $(i+2)}}}'
}

get_policy_table() {
    ovsh s Flow_Service_Manager_Config -w handler==${w_handler} other_config | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="policy_table"){print $(i+2)}}}'
}

get_provider_plugin() {
    ovsh s Flow_Service_Manager_Config -w handler==${w_handler} other_config | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="provider_plugin"){print $(i+2)}}}'
}

#  get policy index to use.
get_policy_idx() {
    idx=$(ovsh s FSM_Policy idx -r | wc -l)
    echo ${idx}
}


# Create a FSM config entry. Resorting to json format due some
# unexpected map programming errors.
gen_fsmc_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "${cmd}",
        "table": "Flow_Service_Manager_Config",
        "row": {
               "handler": "${fsm_handler}",
               "type": "dpi_client",
               "plugin": "/usr/opensync/lib/libfsm_dpi_sni.so",
               "other_config": ["map",
                                [["provider_plugin","${provider_plugin}"],
                                ["policy_table","${policy_name}"],
                                ["flow_attributes","${attrs_tag}"],
                                ["dpi_plugin","${dpi_plugin}"],
                                ["dso_init","dpi_sni_plugin_init"],
                                ["mqtt_v","${mqtt_v}"]
                               ]]
         }
    }
]
EOF
}


# usage
usage() {
  cat <<EOF
          Usage: ${prog} <[options]>
          Options:
                -h this message
                --cmd=<insert | delete>
                --dpi=<dpi plugin>
                --attrs_tag=<flow attributes tag>
EOF
  exit 1
}


# h for help, long options otherwise
optspec="h-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -) LONG_OPTARG="${OPTARG#*=}"
           case "${OPTARG}" in
               attrs_tag=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   ATTRS_TAG=$val
                   ;;
               cmd=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   CMD=$val
                   ;;
               dpi_plugin=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   DPI_PLUGIN=$val
                   ;;
               provider_plugin=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   PROVIDER_PLUGIN=$val
                   ;;
               policy_table=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   POLICY_TABLE=$val
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

cmd=${CMD}
attrs_tag=${ATTRS_TAG:-'${dev_dpi_attrs}'}
dpi_plugin=${DPI_PLUGIN}
policy_name=${POLICY_TABLE:-dev_brightcloud}
provider_plugin=${PROVIDER_PLUGIN:-brightcloud}
fsm_handler=dev_fsm_dpi_sni

# Validate the command argument
if [ -z ${cmd} ]; then
    usage
fi

if [ ${cmd} != "insert" ] && [ ${cmd} != "delete" ]; then
    echo "Error: ${cmd} not a choice"
    usage
fi

# Validate the attr_tag argument
if [ -z ${attrs_tag} ]; then
    usage
fi

# Validate the attr_tag argument
if [ -z ${dpi_plugin} ]; then
    usage
fi

location_id=$(get_location_id)
node_id=$(get_node_id)
mqtt_v="dev-test/DNS/Queries/futs/${node_id}/${location_id}"

#insert attrs tag
ovsh i Openflow_Tag name:=dev_dpi_attrs \
     cloud_value:='["set",["http.host","tls.sni", "http.url"]]'

# Insert policy command
idx=$(get_policy_idx)
$(insert_policy_1_cmd)

sleep 1
idx=$((idx+1))
$(insert_policy_2_cmd)

sleep 1
ovsh u FSM_Policy -w name=="adultAndSensitive:d:,adultAndSen" fqdncats:del:'["set",[11]]'
# Add the dpi client plugin
eval ovsdb-client transact \'$(gen_fsmc_cmd)\'

sleep 1
ovsh U Flow_Service_Manager_Config -w handler==core_dpi_dispatch \
     other_config:ins:'["map",[["included_devices","$[all_clients]"]]]'
