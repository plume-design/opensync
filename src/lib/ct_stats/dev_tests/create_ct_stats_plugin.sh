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


# install collector
install_dev_collector() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Collector_Config",
        "row": {
               "name": "dev_ct_stats",
               "interval": 10,
               "filter_name": "dev_ct_stats_collector_filter",
               "report_name": "dev_ip_flow_report",
               "other_config":
                        ["map",[
                        ["ct_zone","1"],
                        ["dso","/usr/opensync/lib/libfcm_ct_stats.so"],
                        ["dso_init","ct_stats_plugin_init"],
                        ["active","true"]
                        ]]
        }
    }
]

EOF
}


# install lan2lan exclude collector filter
install_dev_lan_exclude_collector_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_ct_stats_collector_filter",
               "smac": "$[all_connected_devices]",
               "smac_op": "in",
               "dmac": "$[all_connected_devices]",
               "dmac_op": "in",
               "index": ${collector_index},
               "action": "exclude"
        }
    }
]

EOF
}


# install outbound frozen and redirect collector filter
install_dev_outbound_ips_collector_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_ct_stats_collector_filter",
               "dst_ip": "$[fsm-redirect-addrs]",
               "dst_ip_op": "in",
               "index": ${collector_index},
               "action": "exclude"

        }
    }
]

EOF
}


# install inbound frozen and redirect collector filter
install_dev_inbound_ips_collector_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_ct_stats_collector_filter",
               "src_ip": "$[fsm-redirect-addrs]",
               "src_ip_op": "in",
               "index": ${collector_index},
               "action": "exclude"

        }
    }
]

EOF
}


# install outbound dns collector filter
install_dev_outbound_dns_collector_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_ct_stats_collector_filter",
               "dst_port": ["set",["53"]],
               "dst_port_op": "in",
               "index": ${collector_index},
               "action": "exclude"

        }
    }
]

EOF
}


# install inbound dns collector filter
install_dev_inbound_dns_collector_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_ct_stats_collector_filter",
               "src_port": ["set",["53"]],
               "src_port_op": "in",
               "index": ${collector_index},
               "action": "exclude"

        }
    }
]

EOF
}


# install outbound pass through collector filter
install_dev_outbound_include_collector_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_ct_stats_collector_filter",
               "smac": "$[ipflow-devices]",
               "smac_op": "in",
               "index": ${collector_index},
               "action": "include"

        }
    }
]

EOF
}


# install pass through collector filter
install_dev_inbound_include_collector_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_ct_stats_collector_filter",
               "dmac": "$[ipflow-devices]",
               "dmac_op": "in",
               "index": ${collector_index},
               "action": "include"

        }
    }
]

EOF
}


# install inbound report filter
install_dev_inbound_report_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_ct_stats_report_filter",
               "dmac": "$[ipflow-devices]",
               "dmac_op": "in",
               "index": ${report_index},
               "pktcnt": 1,
               "pktcnt_op": "geq",
               "action": "include"
        }
    }
]

EOF
}


# install outbound report filter
install_dev_outbound_report_filter() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Filter",
        "row": {
               "name": "dev_ct_stats_report_filter",
               "smac": "$[ipflow-devices]",
               "smac_op": "in",
               "index": ${report_index},
               "pktcnt": 1,
               "pktcnt_op": "geq",
               "action": "include"
        }
    }
]

EOF
}


# install report filter
install_dev_report_config() {
    cat <<EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Report_Config",
        "row": {
               "name": "dev_ip_flow_report",
               "format": "delta",
               "interval": 60,
               "mqtt_topic": "${mqtt_v}",
               "report_filter": "dev_ct_stats_report_filter"
        }
    }
]

EOF
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


location_id=$(get_location_id)
node_id=$(get_node_id)
mqtt_v="IP/Flows/dog1/${node_id}/${location_id}"

# Add the lan2lan exclude collector filter
collector_index=$(get_filter_idx 1)
eval ovsdb-client transact \'$(install_dev_lan_exclude_collector_filter)\'
sleep 1

# Add the dev outbound redirect-addrs ips exclude collector filter
collector_index=$((collector_index+1))
eval ovsdb-client transact \'$(install_dev_outbound_ips_collector_filter)\'
sleep 1

# Add the dev inbound redirect-addrs ips exclude collector filter
collector_index=$((collector_index+1))
eval ovsdb-client transact \'$(install_dev_inbound_ips_collector_filter)\'
sleep 1

# Add the dev outbound dns collector filter
collector_index=$((collector_index+1))
eval ovsdb-client transact \'$(install_dev_outbound_dns_collector_filter)\'
sleep 1

# Add the dev inbound dns collector filter
collector_index=$((collector_index+1))
eval ovsdb-client transact \'$(install_dev_inbound_dns_collector_filter)\'
sleep 1

# Add the dev outbound pass through collector filter
collector_index=$((collector_index+1))
eval ovsdb-client transact \'$(install_dev_outbound_include_collector_filter)\'
sleep 1

# Add the dev inbound pass through collector filter
collector_index=$((collector_index+1))
eval ovsdb-client transact \'$(install_dev_inbound_include_collector_filter)\'
sleep 1

# Add the dev outbound report filter
report_index=$((collector_index+1))
eval ovsdb-client transact \'$(install_dev_outbound_report_filter)\'
sleep 1

# Add the dev inbound report filter
report_index=$((report_index+1))
eval ovsdb-client transact \'$(install_dev_inbound_report_filter)\'
sleep 1

# Add the dev report config
eval ovsdb-client transact \'$(install_dev_report_config)\'
sleep 1

# Add the dev collector
eval ovsdb-client transact \'$(install_dev_collector)\'
