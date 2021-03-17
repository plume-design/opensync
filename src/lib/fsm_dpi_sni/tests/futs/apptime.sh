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
insert_policy_cmd() {
    cat << EOF
ovsh i FSM_Policy \
     policy:=${policy_name} \
     idx:=${idx} \
     fqdn_op:=in \
     action:=drop \
     name:="${policy_name}_rule_0"
EOF
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
               "type": "dpi_client",
               "plugin": "/usr/opensync/lib/libfsm_dpi_sni.so",
               "other_config": ["map",
                                [
                                    ["flow_attributes","${attrs_tag}"],
                                    ["dpi_plugin","${dpi_plugin}"],
                                    ["dso_init","dpi_sni_plugin_init"],
                                    ["mqtt_v","${mqtt_v}"],
                                    ["excluded_devices","${x_devs}"]
                               ]]
         }
    }
]
EOF
}


# h for help, long options otherwise
optspec="h-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -) LONG_OPTARG="${OPTARG#*=}"
           case "${OPTARG}" in
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
attrs_tag=${ATTRS_TAG:-'${dev_app_attrs}'}
dpi_plugin=${DPI_PLUGIN:-walleye_dpi}
policy_name=dev_app
fsm_handler=dev_fsm_dpi_app
x_devs='${dev_x}'

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

#insert attrs tag
ovsh i Openflow_Tag name:=dev_app_attrs \
     cloud_value:='["set",["tag","toldata","service.protocol","service.network","service.platform","service.application","service.feature","server.name"]]'

ovsh i Openflow_Tag name:=dev_x \
     cloud_value:='["set",["58:d9:c3:f7:9d:f3"]]'

eval ovsdb-client transact \'$(gen_fsmc_cmd)\'
