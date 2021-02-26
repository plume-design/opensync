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
# usage example:
#  /tmp/create_gatekeeper_plugin.sh --server_url=https://ovs_dev.plume.com:443 --certs=/tmp/cacert.pem
prog=$0

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
  cat <<EOF
          Usage: ${prog} <[options]>
          Options:
                -h this mesage
                --provider_plugin=<the web cat provider>
                --server_url=<gatekeeper server url>
                --certs=<targeted application>
EOF
}


#  get policy index to use.
get_policy_idx() {
    idx=$(ovsh s FSM_Policy idx -r | wc -l)
    echo ${idx}
}

# Create a gatekeeper config entry. Resorting to json format due some
# unexpected map programming errors.
gen_gk_plugin_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "Flow_Service_Manager_Config",
        "row": {
               "handler": "${provider_plugin}",
               "type": "web_cat_provider",
               "plugin": "/usr/plume/lib/libfsm_gk.so",
               "other_config":
                        ["map",[
                        ["dso_init","gatekeeper_plugin_init"],
                        ["gk_url","${SERVER_URL}"],
                        ["cacert","${CERTS}"]
                        ]]
        }
    }
]
EOF
}


gen_policy_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FSM_Policy",
        "row": {
               "policy": "${policy_name}",
               "idx": ${idx},
               "mac_op": "out",
               "action": "gatekeeper",
               "log": "blocked",
               "name": "gatekeeper_rule_0"
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
                web_cat_provider=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    PROVIDER_PLUGIN=$val
                    ;;
                server_url=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   SERVER_URL=$val
                   ;;
                certs=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   CERTS=$val
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

provider_plugin=${PROVIDER_PLUGIN:-dev_gatekeeper}
policy_name=${POLICY_TABLE:-dev_gatekeeper}
# Insert policy command
# idx=$(get_policy_idx)
# eval ovsdb-client transact \'$(gen_policy_cmd)\'
cmd=

# Validate the command argument
if [ -z ${SERVER_URL} ]; then
    echo "Server URL not provided"
    usage
    exit 2
fi

# Validate the command argument
if [ -z ${CERTS} ]; then
    echo "Certificates path not provided"
    usage
    exit 2
fi

n="$(ovsh s Flow_Service_Manager_Config -w handler==${provider_plugin} -r | wc -l)"
if [ ${n} -eq 0 ]; then
    eval ovsdb-client transact \'$(gen_gk_plugin_cmd)\'
fi
