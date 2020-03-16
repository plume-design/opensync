#!/bin/sh

intf=${1:-br-home.thttp}
bridge=${2:-br-home}
fsm_handler=dev_http # must start with 'dev' so the controller leaves it alone
of_out_token=dev_flow_http_out
tag_name=dev_tag_http

check_cmd() {
    cmd=$1
    path_cmd=$(which ${cmd})
    if [ -z ${path_cmd} ]; then
        echo "Error: could not find ${cmd} command in path"
        exit 1
    fi
    echo "found ${cmd} as ${path_cmd}"
}

# Check required commands
check_cmd 'ovsh'
check_cmd 'ovs-vsctl'
check_cmd 'ip'
check_cmd 'ovs-ofctl'

ovsh d Flow_Service_Manager_Config -w handler==${fsm_handler}
ovsh d Openflow_Tag -w name==${tag_name}
ovsh d Openflow_Config -w token==${of_out_token}
ovs-vsctl del-port ${bridge} ${intf}
