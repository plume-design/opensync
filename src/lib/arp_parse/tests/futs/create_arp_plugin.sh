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


# Series of generic routines updating ovsdb tables.
# TBD: It would make sense to commonize them all.

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
    echo "Usage: $0 [bridge_name] [interface name] [ofport]"
}

# Create tap interface
gen_tap_cmd() {
    cat << EOF
ovs-vsctl add-port ${bridge} ${intf}  \
          -- set interface ${intf}  type=internal \
          -- set interface ${intf}  ofport_request=${ofport}
EOF
}

# Bring tap interface up
tap_up_cmd() {
    cat << EOF
ip link set ${intf} up
EOF
}

# Mark the interface no-flood, only the traffic matching the flow filter
# will hit the plugin
gen_no_flood_cmd() {
    cat << EOF
ovs-ofctl mod-port ${bridge} ${intf} no-flood
EOF
}

# Create the openflow rule for the egress traffic
gen_oflow_cmd() {
    cat << EOF
ovsh i Openflow_Config \
     token:=${of_token} \
     bridge:=${bridge} \
     table:=0 \
     priority:=${priority} \
     rule:=${of_rule} \
     action:="normal,output:${ofport}"
EOF
}


# Create a FSM config entry. Resorting to json format due some
# unexpected map programming errors.
gen_fsmc_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "Flow_Service_Manager_Config",
        "row": {
               "handler": "${fsm_handler}",
               "if_name": "${intf}",
               "pkt_capt_filter": "${filter}",
               "plugin": "${plugin}",
               "other_config": ["map",[["dso_init","${dso_init}"]]]
         }
    }
]
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

# Let's start
bridge=${2:-br-home}
intf=${3:-br-home.tarp}
ofport=${4:-50005} # must be unique to the bridge

# of_token: openflow_config rule name. Must start with 'dev' so the
# controller leaves it alone
of_token=dev_flow_arp
of_rule="dl_type=0x0806"

# Flow_Service_Manager_Config parameters
plugin=/usr/opensync/lib/libfsm_arp.so
dso_init=arp_plugin_init
fsm_handler=dev_arp # must start with 'dev' so the controller leaves it alone

priority=200 # must be higher than controller pushed rules

# Check required commands
check_cmd 'ovsh'
check_cmd 'ovs-vsctl'
check_cmd 'ip'
check_cmd 'ovs-ofctl'

location_id=$(get_location_id)
node_id=$(get_node_id)
mqtt_v="dev-test/${fsm_handler}/${node_id}/${location_id}"

$(gen_tap_cmd)
$(tap_up_cmd)
$(gen_no_flood_cmd)
$(gen_oflow_cmd)
eval ovsdb-client transact \'$(gen_fsmc_cmd)\'
