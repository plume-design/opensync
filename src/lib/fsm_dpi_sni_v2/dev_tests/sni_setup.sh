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
# /tmp/sni_setup.sh --cmd=insert

prog=$0
this_dir=$(dirname "$0")
. ${this_dir}/ovsdb_common.sh

sni_specific_tags='"http.host","http.url","tls.sni"'

insert_policy_1_cmd() {
    cat << EOF
ovsh -q i FSM_Policy \
     policy:=${policy_name} \
     idx:=${idx} \
     mac_op:=out \
     action:=gatekeeper \
     log:=all \
     name:="${policy_name}_rule_0"
EOF
}


# Create a FSM config entry. Resorting to json format due some
# unexpected map programming errors.
# Let's use a simple parameter to this "insert" or "delete"
# as it makes it more explicit.
gen_fsmc_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "${cmd}",
        "table": "Flow_Service_Manager_Config",
        "row": {
               "handler": "${fsm_handler}",
               "type": "dpi_client",
               "plugin": "/usr/opensync/lib/libfsm_dpi_sni_v2.so",
               "other_config": ["map",
                                [["provider_plugin","${provider_plugin}"],
                                 ["policy_table","${policy_name}"],
                                 ["flow_attributes","${attrs_tag}"],
                                 ["dpi_plugin","${dpi_plugin}"],
                                 ["dso_init","fsm_dpi_sni_init"],
                                 ["mqtt_v","${mqtt_v}"]
                               ]]
         }
    }
]
EOF
}


cmd_get_br_ip()
{
    ipaddr=$(ip -f inet addr show dev br-home | sed -n 's/^ *inet *\([.0-9]*\).*/\1/p')
    echo ${ipaddr}
}


cmd_insert()
{
    # Check if the attributes are already present
    ovsh s Openflow_Tag -w name==dev_dpi_sni >/dev/null
    if [ $? -eq 0 ]; then
        echo "ADT monitoring already enabled"
        exit 1
    fi

    # Now we can monitor all the tags for SNI
    echo "Adding tags for SNI: ${sni_specific_tags}"
    ovsh i Openflow_Tag name:=dev_dpi_sni "cloud_value:=[\"set\",[${sni_specific_tags}]]" > /dev/null
    sleep 1

    # Insert policy command
    echo "Adding policy"
    idx=$(get_next_policy_idx)
    $(insert_policy_1_cmd)
    sleep 1

    # Add the dpi client plugin
    echo "Set up the plugin for ${fsm_handler}"
    eval ovsdb-client transact \'$(gen_fsmc_cmd)\' > /dev/null
    sleep 1

    # Pretty output to make life easier
    cat << EOF

Below command to start the python script in docker:
  ./dock-run ./mqtt_get_msgs.py --pod-sn ${node_id} --custom-topics ${mqtt_v} --log-level DEBUG

EOF
}

cmd_delete()
{
    ovsh -q d Openflow_Tag -w name==dev_dpi_sni
    ovsh -q d FSM_Policy -w policy==${policy_name}
    ovsh -q d Flow_Service_Manager_Config -w handler==${fsm_handler}

    # Making sure we'll resume from a clean slate
    killall fsm

    echo Restarting FSM now
}

# usage
usage()
{
  cat <<EOF
    Usage: ${prog} <[options]>
        Options:
            -h this message
            --cmd=<insert | delete>            REQUIRED
            --dpi=<dpi plugin>                 defaults to walleye_dpi
            --attrs_tag=<flow attributes tag>  defaults to \${dev_dpi_sni}
    Attributes to be monitored will be read from ${config_file} (one attribute per line)
EOF
  exit 1
}


# h for help, long options otherwise
optspec="h-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -)
            LONG_OPTARG="${OPTARG#*=}"
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
attrs_tag=${ATTRS_TAG:-'${dev_dpi_sni}'}
dpi_plugin=${DPI_PLUGIN:-'walleye_dpi'}
policy_name=${POLICY_TABLE:-dev_dpi_sni}
provider_plugin=${PROVIDER_PLUGIN:-gatekeeper}
fsm_handler=dev_dpi_sni
location_id=$(get_location_id)
node_id=$(get_node_id)
mqtt_v="dev-test/SNI/Requests/dog1/${node_id}/${location_id}"

case ${cmd} in
    "insert")
        cmd_insert ;;
    "delete")
        cmd_delete ;;
    *)
        usage
        exit 0 ;;
esac

sleep 1
ovsh -q U Flow_Service_Manager_Config -w handler==core_dpi_dispatch \
     other_config:ins:'["map",[["included_devices","$[all_clients]"]]]' > /dev/null
