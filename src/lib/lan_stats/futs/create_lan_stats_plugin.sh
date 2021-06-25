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

set -x

OVSH='/usr/opensync/tools/ovsh'

# get filter index to start from.
get_filter_idx() {
    bump=$1
    idx=$(${OVSH} s FCM_Filter index -r | wc -l)
    # bump up the index by one if required
    idx=$((idx+bump))
    echo ${idx}
}


# get pod's location ID
get_location_id() {
    ${OVSH} s AWLAN_Node mqtt_headers | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="locationId"){print $(i+2)}}}'
}

# get pod's node ID
get_node_id() {
    ${OVSH} s AWLAN_Node mqtt_headers | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="nodeId"){print $(i+2)}}}'
}

install_all_clients_collector_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_lan_filter",
               "dmac": "$[@all_clients]",
               "dmac_op": "in",
               "index": ${collector_index},
               "action": "include"
        }
    }
]

EOF
}

install_node_gateway_collector_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_lan_filter",
               "dmac": "\${gateways}",
               "dmac_op": "in",
               "smac_op": "out",
               "smac": "\${node_eth}",
               "index": ${collector_index},
               "action": "include"
        }
    }
]

EOF
}

install_node_clients_collector_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_lan_filter",
               "dmac": ["set",["\$[#all_clients]", "\${node_eth}"]],
               "dmac_op": "out",
               "smac_op": "out",
               "smac": "\${node_eth}",
               "index": ${collector_index},
               "action": "include"
        }
    }
]

EOF
}

# install report filter
install_dev_lan_stats_report_config() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Report_Config",
        "row": {
               "name": "dev_lan_flow_report",
               "format": "delta",
               "interval": 30,
               "mqtt_topic": "${mqtt_v}/07"
        }
    }
]

EOF
}

# install collector
install_dev_lan_stats_collector() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Collector_Config",
        "row": {
               "name": "dev_lanstats",
               "interval": 10,
               "filter_name": "dev_lan_filter",
               "report_name": "dev_lan_flow_report",
               "other_config":
                        ["map",[
                        ["dso_init","lan_stats_plugin_init"],
                        ["dso","/usr/opensync/lib/libfcm_lanstats.so"],
                        ["active","dev_lanstats"]
                        ]]
        }
    }
]

EOF
}

location_id=$(get_location_id)
node_id=$(get_node_id)
mqtt_v="lan/dog1/${location_id}"

# Add the lan2lan exclude collector filter
collector_index=$(get_filter_idx 1)
eval ovsdb-client transact \'$(install_all_clients_collector_filter)\'
sleep 1

collector_index=$((collector_index+1))
eval ovsdb-client transact \'$(install_node_gateway_collector_filter)\'
sleep 1

collector_index=$((collector_index+1))
eval ovsdb-client transact \'$(install_node_clients_collector_filter)\'
sleep 1

# Add the dev report config
eval ovsdb-client transact \'$(install_dev_lan_stats_report_config)\'
sleep 1

# Add the dev collector
eval ovsdb-client transact \'$(install_dev_lan_stats_collector)\'
