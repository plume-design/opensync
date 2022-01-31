
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

# Check if a specific command is in the path. Bail if not found.
check_cmd() {
    local cmd=$1
    path_cmd=$(which ${cmd})
    if [ -z ${path_cmd} ]; then
        echo "Error: could not find ${cmd} command in path"
        exit 1
    fi
    echo "found ${cmd} as ${path_cmd}"
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
               "type": "dpi_dispatcher",
               "other_config": ["map",[["mqtt_v","${mqtt_v}"]]]
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

find_dpi_rules() {
    openflow_config=$1 # Openflow_Config table dump
    cloud_ofdpi=$2     # cloud controlled dpi interface ofport
    fut_ofdpi=$3       # FUT controlled dpi interface ofport
    switch=$4          # insert or delete
    bridge=$5

    awk -v cl_dpi=${cloud_ofdpi} \
        -v fut_dpi=${fut_ofdpi} \
        -v cmd=${switch} \
        -v br=${bridge} '
    # Whitespace trimming functions
    function ltrim(s) { sub(/^[ \t\r\n]+/, "", s); return s }
    function rtrim(s) { sub(/[ \t\r\n]+$/, "", s); return s }
    function trim(s) { return rtrim(ltrim(s)); }
    BEGIN
    {
        FS = "|"
        OFS = " "
    }
    {
       regex = "output:"cl_dpi
       # Jump the line if it does not redirect to br-home.dpi
       if (!($0 ~ regex)) { next }

       action = trim($4)
       priority = trim($6) + 1
       rule = trim($7)
       table = trim($8)
       token = trim($9)
       { gsub(cl_dpi, fut_dpi, action) }
       { gsub(/^/, "dev_", token) }

       if ( (cmd ~ /insert/) )
       {
           ovs_cmd = "ovsh i Openflow_Config"   \
                     " bridge:=" br             \
                     " token:=" token           \
                     " table:=" table           \
                     " rule:=\"" rule "\""      \
                     " priority:=" priority     \
                     " action:=\"" action "\""
       }

       if ( (cmd ~ /delete/) )
       {
           ovs_cmd = "ovsh d Openflow_Config -w "  \
                     " token==" token
       }
       print ovs_cmd
    }
    ' ${openflow_config}
}

get_cloud_dpi() {
    ovsh s -r Flow_Service_Manager_Config \
         handler if_name -w type==dpi_dispatcher > /tmp/dpis
    awk '
    # Whitespace trimming functions
    function ltrim(s) { sub(/^[ \t\r\n]+/, "", s); return s }
    function rtrim(s) { sub(/[ \t\r\n]+$/, "", s); return s }
    function trim(s) { return rtrim(ltrim(s)); }
    BEGIN
    {
        FS = " "
        OFS = " "
    }
    {
        handler = $1
        if_name = $2
        # skip dev dpi plugins
        if ( (handler ~ /^dev_/) ) { next }

        # Assume one non-dev dpi plugin
        print $2

    }
    ' /tmp/dpis
}

get_cloud_dpi_ofport() {
    local dpi_intf=$(get_cloud_dpi)
    if [ -z ${dpi_intf} ]; then
        return
    fi

    local port=$(ovs-vsctl get Interface ${dpi_intf} ofport)
    printf ${port}
}

get_lan_br() {
    local port=$(get_cloud_dpi)
    if [ -z ${port} ]; then
        return
    fi

    local br=$(ovs-vsctl port-to-br ${port})
    printf ${br}
}

check_platform_cmds() {
    check_cmd 'ovsh'
    check_cmd 'ovs-vsctl'
    check_cmd 'ip'
    check_cmd 'ovs-ofctl'
}

set_plugin() {
    $(gen_tap_cmd)
    $(tap_up_cmd)
    $(gen_no_flood_cmd)
    eval ovsdb-client transact \'$(gen_fsmc_cmd)\'
}

delete_plugin() {
    ovsh d Flow_Service_Manager_Config -w handler==${fsm_handler}
    ovs-vsctl del-port ${bridge} ${intf}
}
