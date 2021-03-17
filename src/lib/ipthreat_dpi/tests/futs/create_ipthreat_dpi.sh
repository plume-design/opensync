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

# insert test policy
# Create the openflow rule for the egress traffic
#
gen_policy_1_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "${cmd}",
        "table": "FSM_Policy",
        "row": {
               "policy": "outbound_${policy_name}",
               "idx": ${idx},
               "ipaddr_op": "in",
               "ipaddrs": ["set",["198.55.101.102"]],
               "action": "drop",
               "log": "blocked",
               "name": "outbound_${policy_name}_rule_0"
         }
    }
]
EOF
}

gen_policy_2_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "${cmd}",
        "table": "FSM_Policy",
        "row": {
               "policy": "outbound_${policy_name}",
               "idx": ${idx},
               "ipaddr_op": "out",
               "fqdncat_op": "out",
               "action": "allow",
               "log": "all",
               "name": "outbound_${policy_name}_rule_1"
         }
    }
]
EOF
}


gen_policy_3_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "${cmd}",
        "table": "FSM_Policy",
        "row": {
               "policy": "inbound_${policy_name}",
               "idx": ${idx},
               "ipaddr_op": "in",
               "ipaddrs": ["set",["198.55.101.102"]],
               "action": "drop",
               "log": "blocked",
               "name": "inbound_${policy_name}_rule_0"
         }
    }
]
EOF
}

gen_policy_4_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "${cmd}",
        "table": "FSM_Policy",
        "row": {
               "policy": "inbound_${policy_name}",
               "idx": ${idx},
               "ipaddr_op": "out",
               "fqdncat_op": "out",
               "action": "allow",
               "log": "all",
               "name": "inbound_${policy_name}_rule_1"
         }
    }
]
EOF
}

gen_gatekeeper_policy_5_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "${cmd}",
        "table": "FSM_Policy",
        "row": {
               "policy": "inbound_${policy_name}",
               "idx": ${idx},
               "mac_op": "out",
               "action": "gatekeeper",
               "log": "blocked",
               "name": "inbound_gk_${policy_name}_rule_0"
         }
    }
]
EOF
}

gen_gatekeeper_policy_6_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "${cmd}",
        "table": "FSM_Policy",
        "row": {
               "policy": "outbound_${policy_name}",
               "idx": ${idx},
               "mac_op": "out",
               "action": "gatekeeper",
               "log": "blocked",
               "name": "outbound_gk_${policy_name}_rule_0"
         }
    }
]
EOF
}

get_location_id() {
    ovsh s AWLAN_Node mqtt_headers | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="locationId"){print $(i+2)}}}'
}

# get pod's node ID
get_node_id() {
    ovsh s AWLAN_Node mqtt_headers | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="nodeId"){print $(i+2)}}}'
}


update_included_devices_cmd() {
    ovsh u Flow_Service_Manager_Config -w handler==core_dpi_dispatch \
         other_config:ins:'["map",[["included_devices","$[all_clients]"]]]'
}

insert_target_tag_cmd() {
    cat << EOF
ovsh i Openflow_Tag \
     cloud_value:=["set",["${iptd_dev_mac}"]]
     name:="dev-iptd-devices"
EOF
}

insert_excluded_tag_cmd() {
    cat << EOF
ovsh i Openflow_Tag \
     cloud_value:=["set",["${no_iptd_dev_mac}"]]
     name:="dev-no-iptd-devices"
EOF
}

# get pod's location ID
get_policy_table() {
    ovsh s Flow_Service_Manager_Config -w handler==${w_handler} other_config | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="policy_table"){print $(i+2)}}}'
}

# get pod's location ID
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
               "type": "dpi_plugin",
               "other_config": ["map",
                                [["provider_plugin","${provider_plugin}"],
                                ["inbound_policy_table","inbound_${policy_name}"],
                                ["outbound_policy_table","outbound_${policy_name}"],
                                ["dpi_dispatcher","core_dpi_dispatch"],
                                ["targeted_devices","\${dev-iptd-devices}"],
                                ["dso_init","ipthreat_dpi_plugin_init"],
                                ["excluded_devices","\${dev-no-iptd-devices}"],
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
                --dev_mac=<device mac>
EOF
  exit 1
}


# h for help, long options otherwise
optspec="h-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -) LONG_OPTARG="${OPTARG#*=}"
           case "${OPTARG}" in
               dev_mac=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTAGR%=$val}
                   DEV_MAC=$val
                   ;;
               service=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTAGR%=$val}
                   SERVICE=$val
                   ;;
               cmd=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   CMD=$val
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
policy_name=dev_iptd
provider_plugin=${SERVICE:-dev_wc_null}
fsm_handler=ipthreat_dpi
dpi_dispatch_fn=dpi_dispatcher
iptd_dev_mac=${DEV_MAC:-"00:e0:4c:20:9f:85"}
# Validate the command argument
if [ -z ${cmd} ]; then
    usage
fi

if [ ${cmd} != "insert" ] && [ ${cmd} != "delete" ]; then
    echo "Error: ${cmd} not a choice"
    usage
fi

location_id=$(get_location_id)
node_id=$(get_node_id)
mqtt_v="dev-test/DNS/Queries/futs/${node_id}/${location_id}"

# Insert policy command
idx=$(get_policy_idx)
eval ovsdb-client transact \'$(gen_gatekeeper_policy_5_cmd)\'

sleep 1
idx=$((idx+1))
eval ovsdb-client transact \'$(gen_gatekeeper_policy_6_cmd)\'

$(insert_target_tag_cmd)

# Add the dpi client plugin
eval ovsdb-client transact \'$(gen_fsmc_cmd)\'
update_included_devices_cmd
