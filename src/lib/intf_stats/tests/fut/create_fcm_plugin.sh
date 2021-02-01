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

# Create a FCM Collector Config entry. Resorting to json format due some
# unexpected map programming errors.
gen_fcm_collector_config_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Collector_Config",
        "row": {
               "filter_name": "${filter_name}",
               "interval": ${collect_interval},
               "name": "${config_name}",
               "report_name": "${report_name}",
               "other_config": ["map",[["dso_init","${dso_init}"]]]
         }
    }
]
EOF
}

gen_fcm_report_config_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FCM_Report_Config",
        "row": {
               "name": "${report_name}",
               "format": "${report_format}",
               "interval": ${report_interval},
               "hist_interval": ${hist_interval},
               "mqtt_topic": "${mqtt_topic}"
         }
    }
]
EOF
}

gen_Wifi_Inet_Config_cmd() {
    for i in $intf_list
    do
        ovsh u Wifi_Inet_Config -w if_name=="$i" collect_stats:=true
    done
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

# FCM Collector Config parameters
filter_name=intf_filter
collect_interval=10
config_name=intfstats
report_name=intf_stat_report
dso_init=intf_stats_plugin_init

# Wifi_Inet_Config parameters
intf_list="eth0 home-ap-24 home-ap-50 bhaul-ap-24 bhaul-ap-50"

# FCM Report Config parameters
report_format=delta
report_interval=60
hist_interval=0
location_id=$(get_location_id)
node_id=$(get_node_id)
mqtt_topic="dev-test/interfaceStats/${config_name}/${node_id}/${location_id}"

# Check required commands
check_cmd 'ovsh'

# Mark the interfaces that need to be monitored
gen_Wifi_Inet_Config_cmd

eval ovsdb-client transact \'$(gen_fcm_collector_config_cmd)\'
eval ovsdb-client transact \'$(gen_fcm_report_config_cmd)\'
