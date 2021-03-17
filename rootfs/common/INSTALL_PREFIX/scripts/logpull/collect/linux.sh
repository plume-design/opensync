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

#
# Collect common Linux info
#
. "$LOGPULL_LIB"

collect_linux()
{
    collect_cmd uname -a
    collect_cmd uptime
    collect_cmd date
    collect_cmd ps w
    collect_cmd free
    collect_cmd df -k
    collect_cmd dmesg
    collect_cmd lspci
    collect_cmd ifconfig -a
    collect_cmd ip a
    collect_cmd ip -d link show
    collect_cmd ip neigh show
    collect_cmd ip address
    collect_cmd route -n
    collect_cmd route -A inet6 -n
    collect_cmd iptables -L -v -n
    collect_cmd iptables -t nat -L -v -n
    collect_cmd iptables -t mangle -L -v -n
    collect_cmd ip6tables -L -v -n
    collect_cmd ip6tables -t nat -L -v -n
    collect_cmd ip6tables -t mangle -L -v -n
    collect_cmd iwconfig
    collect_cmd lsmod
    collect_cmd mpstat -A -I ALL
    collect_cmd pidstat -Ir -T ALL
    collect_cmd mount

    collect_file /proc/stat
    collect_file /proc/meminfo
    collect_file /proc/loadavg
    collect_file /proc/net/dev
    collect_file /proc/mtd

    collect_cmd top -n 1 -b

    collect_cmd netstat -nep
    collect_cmd netstat -nlp
    collect_cmd netstat -atp
    collect_cmd conntrack -L
}

collect_ethernet()
{
    if [ $CONFIG_TARGET_ETH0_LIST ]; then
        collect_cmd ethtool $CONFIG_TARGET_ETH0_NAME
        collect_cmd ethtool -S $CONFIG_TARGET_ETH0_NAME
    fi
    if [ $CONFIG_TARGET_ETH1_LIST ]; then
        collect_cmd ethtool $CONFIG_TARGET_ETH1_NAME
        collect_cmd ethtool -S $CONFIG_TARGET_ETH1_NAME
    fi
    if [ $CONFIG_TARGET_ETH2_LIST ]; then
        collect_cmd ethtool $CONFIG_TARGET_ETH2_NAME
        collect_cmd ethtool -S $CONFIG_TARGET_ETH2_NAME
    fi
    if [ $CONFIG_TARGET_ETH3_LIST ]; then
        collect_cmd ethtool $CONFIG_TARGET_ETH3_NAME
        collect_cmd ethtool -S $CONFIG_TARGET_ETH3_NAME
    fi
    if [ $CONFIG_TARGET_ETH4_LIST ]; then
        collect_cmd ethtool $CONFIG_TARGET_ETH4_NAME
        collect_cmd ethtool -S $CONFIG_TARGET_ETH4_NAME
    fi
    if [ $CONFIG_TARGET_ETH5_LIST ]; then
        collect_cmd ethtool $CONFIG_TARGET_ETH5_NAME
        collect_cmd ethtool -S $CONFIG_TARGET_ETH5_NAME
    fi
}

collect_linux
collect_ethernet
