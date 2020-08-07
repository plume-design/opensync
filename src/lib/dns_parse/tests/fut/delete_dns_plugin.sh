#!/bin/sh

prog=$0

# usage
usage() {
  cat <<EOF
          Usage: ${prog} --mac=<the client device> <[options]>
          Options:
                -h this mesage
                --bridge=<the ovs bridge>
                --intf=<the tap interface>
EOF
}

optspec="h-:"


while getopts "$optspec" optchar; do
    case "${optchar}" in
        -) LONG_OPTARG="${OPTARG#*=}"
           case "${OPTARG}" in
                mac=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    # echo "Parsing option: '--${opt}', value: '${val}'"
                    MAC=$val
                    ;;
                bridge=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    # echo "Parsing option: '--${opt}', value: '${val}'"
                    BRIDGE=$val
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

intf=${INTF:-br-home.tdns}
bridge=${BRIDGE:-br-home}
fsm_handler=dev_dns
of_out_token=dev_flow_dns_out
of_in_token=dev_flow_dns_in
of_tx_token=dev_dns_fwd_flow
tag_name=dev_tag_dns

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
ovsh d Openflow_Config -w token==${of_in_token}
ovsh d Openflow_Config -w token==${of_out_token}
ovsh d Openflow_Config -w token==${of_tx_token}
ovs-vsctl del-port ${bridge} ${intf}
ovsh d FSM_Policy -w policy==dev_webpulse
ovsh d FSM_Policy -w policy==dev_brightcloud
ovsh d FSM_Policy -w policy==dev_test
ovsh d Flow_Service_Manager_Config -w handler==dev_webpulse
ovsh d Flow_Service_Manager_Config -w handler==dev_brightcloud
