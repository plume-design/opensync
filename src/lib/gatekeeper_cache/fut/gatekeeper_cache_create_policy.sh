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


# usage example:
#  /tmp/gatekeeper_cache_validation.sh --action=flush_all
#
#  /tmp/gatekeeper_cache_validation.sh --action=flush --ipaddr_op=out
#  --ipaddrs=15.168.40.1,100.21.12.15 --mac_op=in
#  --macs=aa:bb:cc:dd:ee:ff,xx:yy:gg:hh:dd:pp --fqdn_op=out --fqdns=google.com,plume.com
#
#  /tmp/gatekeeper_cache_validation.sh --action=flush --mac_op=in
#  --macs=aa:bb:cc:dd:ee:ff,xx:yy:gg:hh:dd:pp
#
#  /tmp/gatekeeper_cache_validation.sh --action=flush --ipaddr_op=out
#  --ipaddrs=15.168.40.1,100.21.12.15
#
#  /tmp/gatekeeper_cache_validation.sh_v3 --action=flush --fqdn_op=sfl_in --fqdns=google.com,plume.com

prog=$0

. /usr/opensync/etc/kconfig # TODO: This should point to {INSTALL_PREFIX}/etc/kconfig


# usage
usage() {
  cat <<EOF
          Usage: ${prog} <[options]>
          Options:
                -h this mesage
                --action=<policy table action>
                --ipaddr_op=<ip address op>
                --ipaddrs=<set of ip addresses>
                --mac_op=<mac address op>
                --macs=<mac address>
                --fqdn_op=<fqdn op>
                --fqdns=<fqdn>
EOF
}

#  get policy index to use.
get_policy_idx() {
    idx=$(ovsh s FSM_Policy idx -r | wc -l)
    echo ${idx}
}

flush_all_policy_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FSM_Policy",
        "row": {
               "policy": "dev_gatekeeper",
               "idx": ${idx},
               "action": "flush_all",
               "name": "clear_gatekeeper_cache"
         }
    }
]
EOF
}

flush_mixed_policy_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FSM_Policy",
        "row": {
               "policy": "dev_gatekeeper",
               "idx": ${idx},
               "action": "flush",
               "name": "clear_gatekeeper_cache",
               "ipaddr_op": "${IPADDR_OP}",
               "ipaddrs": ["set", ["${IPADDRS}"]],
               "mac_op": "${MAC_OP}",
               "macs": ["set", ["${MACS}"]],
               "fqdn_op" : "${FQDN_OP}",
               "fqdns" : ["set", ["${FQDNS}"]]
         }
    }
]
EOF
}

flush_fqdn_policy_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FSM_Policy",
        "row": {
               "policy": "dev_gatekeeper",
               "idx": ${idx},
               "action": "flush",
               "name": "clear_gatekeeper_cache",
               "fqdn_op": "${FQDN_OP}",
               "fqdns": ["set", ["${FQDNS}"]]
         }
    }
]
EOF
}

flush_mac_ips_policy_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FSM_Policy",
        "row": {
               "policy": "dev_gatekeeper",
               "idx": ${idx},
               "action": "flush",
               "name": "clear_gatekeeper_cache",
               "mac_op": "${MAC_OP}",
               "macs": ["set", ["${MACS}"]]
         }
    }
]
EOF
}

flush_ips_policy_cmd() {
    cat << EOF
["Open_vSwitch",
    {
        "op": "insert",
        "table": "FSM_Policy",
        "row": {
               "policy": "dev_gatekeeper",
               "idx": ${idx},
               "action": "flush",
               "name": "clear_gatekeeper_cache",
               "ipaddr_op": "${IPADDR_OP}",
               "ipaddrs": ["set", ["${IPADDRS}"]]
         }
    }
]
EOF
}

# flush all
flush_all() {
    echo "Executing flush all fsm policy cmd:"
    echo  \'$(flush_all_policy_cmd)\'
    eval ovsdb-client transact \'$(flush_all_policy_cmd)\'
}

# Selective flush
selective_flush() {
    if [[ ! -z "${IPADDR_OP}" && ! -z "${IPADDRS}" && ! -z "${MAC_OP}" && ! -z "${MACS}" && ! -z "${FQDN_OP}" && ! -z "${FQDNS}" ]]; then
            echo "Executing flush Mixed fsm policy cmd:"
            echo \'$(flush_mixed_policy_cmd)\'
            eval ovsdb-client transact \'$(flush_mixed_policy_cmd)\'

    elif [[ ! -z "${MAC_OP}" && ! -z "${MACS}" ]]; then
        echo "Executing flush all ips for MAC fsm policy cmd:"
        echo  \'$(flush_mac_ips_policy_cmd)\'
        eval ovsdb-client transact \'$(flush_mac_ips_policy_cmd)\'

    elif [[ ! -z "${IPADDR_OP}" && ! -z "${IPADDRS}" ]]; then
        echo "Executing flush ips fsm policy cmd:"
        echo  \'$(flush_ips_policy_cmd)\'
        eval ovsdb-client transact \'$(flush_ips_policy_cmd)\'

    elif [[ ! -z "${FQDN_OP}" && ! -z "${FQDNS}" ]]; then
        echo "Executing flush FQDN fsm policy cmd:"
        echo  \'$(flush_fqdn_policy_cmd)\'
        eval ovsdb-client transact \'$(flush_fqdn_policy_cmd)\'

    else
        usage
        exit 2
    fi
}

# h for help, long options otherwise
optspec="h-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -) LONG_OPTARG="${OPTARG#*=}"
           case "${OPTARG}" in
                action=?* )
                    val=${LONG_OPTARG}
                    opt=${OPTARG%=$val}
                    ACTION=$val
                    ;;
                ipaddr_op=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   IPADDR_OP=$val
                   ;;
                ipaddrs=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   IPADDRS=$val
                   ;;
                mac_op=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   MAC_OP=$val
                   ;;
                macs=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   MACS=$val
                   ;;
                fqdn_op=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   FQDN_OP=$val
                   ;;
                fqdns=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   FQDNS=$val
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


# Validate the command argument
if [ -z ${ACTION} ]; then
    echo "policy action not provided"
    usage
    exit 2
fi

# Insert policy command
idx=$(get_policy_idx)

if [ "${ACTION}" = "flush_all" ]; then
    echo "flushing all"
    flush_all
fi

if [ "${ACTION}" = "flush" ]; then
    echo "Selective flush"
    selective_flush
fi
