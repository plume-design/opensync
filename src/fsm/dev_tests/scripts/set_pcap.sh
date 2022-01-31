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

# Insert or delete snaplen for the given plugin
update_other_config_cmd() {
    key=$1
    value=$2
    cat << EOF
["Open_vSwitch",
    {
        "op": "mutate",
        "table": "Flow_Service_Manager_Config",
        "where": [["handler", "==", "${handler}"]],
        "mutations": [["other_config", "${cmd}",
                      ["map", [["${key}","${value}"]]]]]
    }
]
EOF
}


# get snaplen
get_snaplen() {
    ovsh s Flow_Service_Manager_Config -w handler==${handler} other_config | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="pcap_snaplen"){print $(i+2)}}}'
}

# set snaplen
set_snaplen() {
    snaplen=$(get_snaplen)
    if ! [ -z ${snaplen} ]; then
        cmd_save=${cmd}
        snaplen_val_save=${snaplen_val}
        cmd="delete"
        snaplen_val=${snaplen}
        eval ovsdb-client transact \
             \'$(update_other_config_cmd "pcap_snaplen" ${snaplen_val})\'
        snaplen_val=${snaplen_val_save}
        cmd=${cmd_save}
    fi
    eval ovsdb-client transact \
         \'$(update_other_config_cmd "pcap_snaplen" ${snaplen_val})\'
}


# get mode
get_mode() {
    ovsh s Flow_Service_Manager_Config -w handler==${handler} other_config | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="pcap_mode"){print $(i+2)}}}'
}

# set mode
set_mode() {
    mode=$(get_mode)
    if ! [ -z ${mode} ]; then
        cmd_save=${cmd}
        mode_val_save=${mode_val}
        cmd="delete"
        mode_val=${mode}
        eval ovsdb-client transact \
             \'$(update_other_config_cmd "pcap_mode" ${mode_val})\'
        mode_val=${mode_val_save}
        cmd=${cmd_save}
    fi
    eval ovsdb-client transact \
         \'$(update_other_config_cmd "pcap_mode" ${mode_val})\'
}

# get buffer size
get_bsize() {
    ovsh s Flow_Service_Manager_Config -w handler==${handler} other_config | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="pcap_bsize"){print $(i+2)}}}'
}

# set buffer size
set_bsize() {
    bsize=$(get_bsize)
    if ! [ -z ${bsize} ]; then
        cmd_save=${cmd}
        bufsz_val_save=${bufsz_val}
        cmd="delete"
        bufsz_val=${bsize}
        eval ovsdb-client transact \
             \'$(update_other_config_cmd "pcap_bsize" ${bufsz_val})\'
        bufsz_val=${bufsz_val_save}
        cmd=${cmd_save}
    fi
    eval ovsdb-client transact \
         \'$(update_other_config_cmd "pcap_bsize" ${bufsz_val})\'
}

# get count
get_count() {
    ovsh s Flow_Service_Manager_Config -w handler==${handler} other_config | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="pcap_cnt"){print $(i+2)}}}'
}

# set count
set_count() {
    count=$(get_count)
    if ! [ -z ${count} ]; then
        cmd_save=${cmd}
        count_val_save=${count_val}
        cmd="delete"
        count_val=${count}
        eval ovsdb-client transact \
             \'$(update_other_config_cmd "pcap_count" ${count_val})\'
        count_val=${count_val_save}
        cmd=${cmd_save}
    fi
    eval ovsdb-client transact \
         \'$(update_other_config_cmd "pcap_count" ${count_val})\'
}

# usage
usage() {
  cat <<EOF
          Usage: ${prog} <[options]>
          Options:
                -h this message
                --cmd=<insert | delete>
                --handler=<handler>
                --pcap_snaplen=<pcap snap length>
                --pcap_mode=[  1 | 0 ]
                --pcap_bsize=<pcap buffer size>
                --pcap_cnt=<pcap max packets to dispatch>
EOF
  exit 1
}


# h for help, long options otherwise
optspec="h-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -) LONG_OPTARG="${OPTARG#*=}"
           case "${OPTARG}" in
               handler=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   HANDLER=$val
                   ;;
               cmd=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   CMD=$val
                   ;;
               pcap_snaplen=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   SNAPLEN=$val
                   ;;
               pcap_mode=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   MODE=$val
                   ;;
               pcap_bsize=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   BSIZE=$val
                   ;;
               pcap_cnt=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   CNT=$val
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
handler=${HANDLER}
snaplen_val=${SNAPLEN}
mode_val=${MODE}
bufsz_val=${BSIZE}
count_val=${CNT}

# Validate the command argument
if [ -z ${cmd} ]; then
    usage
fi

if [ ${cmd} != "insert" ] && [ ${cmd} != "delete" ]; then
    echo "Error: ${cmd} not a choice"
    usage
fi

# Validate the plugin argument
if [ -z ${handler} ]; then
    usage
fi

# Apply the snaplen update
if ! [ -z ${snaplen_val} ]; then
    set_snaplen
fi

# Apply the mode update
if ! [ -z ${mode_val} ]; then
    set_mode
fi

# Apply the pcap buffer size update
if ! [ -z ${bufsz_val} ]; then
    set_bsize
fi

# Apply the pcap count update
if ! [ -z ${count_val} ]; then
    set_count
fi
