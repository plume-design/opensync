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

# {# jinja-parse #}

LOG_MODULE="DNS"
RESOLV_CONF="/etc/resolv.conf"
LOOKUP_HOST={{CONTROLLER_ADDR.split(':')[1]}}
DNS_SERVERS="209.244.0.3 64.6.64.6 84.200.69.80"
PLOOKUP=$CONFIG_INSTALL_PREFIX/tools/plookup
OVSH=$CONFIG_INSTALL_PREFIX/tools/ovsh
timeout=$(timeout -t 0 true && echo timeout -t || echo timeout)

ret=0

is_internet_available()
{
    # Check if internet is available by pinging random two addresses from
    # list of global root DNS server (also used by CM).
    # Info: https://www.iana.org/domains/root/servers
    local addr0="198.41.0.4"
    local addr1="192.228.79.201"
    local addr2="192.33.4.12"
    local addr3="199.7.91.13"
    local addr4="192.5.5.241"
    local addr5="198.97.190.53"
    local addr6="192.36.148.17"
    local addr7="192.58.128.30"
    local addr8="193.0.14.129"
    local addr9="199.7.83.42"

    local r1=$(( 0x$(head -c 20 /dev/urandom | md5sum | head -c8) % 10 ))
    local r2=$(( (r1 + 4) % 10 ))
    eval addr_first="\$addr$r1"
    eval addr_second="\$addr$r2"

    ping $addr_first -c 2 -s 4 -w 2 > /dev/null || {
        ping $addr_second -c 2 -s 4 -w 2 > /dev/null || {
            # Internet not available after double check
            return 1
        }
    }

    return 0
}

check_dns_servers()
{
    local logMsg=$1
    local servers=$2

    pass=0
    testCnt=0

    for dns in $servers; do
        $timeout 10 $PLOOKUP $LOOKUP_HOST $dns > /dev/null 2>&1
        if [ $? -ne 0 ]; then
            log_err "$dns failed"
        else
            pass=$((pass + 1))
        fi
        testCnt=$((testCnt + 1))
    done

    if [ $pass -ne $testCnt ]; then
        log_debug "$logMsg $pass/$testCnt"
    fi

    if [ $pass -gt 0 ]; then
        Healthcheck_Pass
    fi
}


# Check only device in Gateway mode
WAN_TYPES="eth"

iftype=$($OVSH s Connection_Manager_Uplink -w is_used==true if_type -r)
if [ -z "$(echo $WAN_TYPES | grep $iftype)" ]; then
    Healthcheck_Pass
fi

bridge=$($OVSH s Connection_Manager_Uplink -w is_used==true bridge -r | grep -v "["set",[]]")
if [ -z "$bridge" ]; then
    link=$($OVSH s Connection_Manager_Uplink -w is_used==true if_name -r)
else
    link=$bridge
fi

gw_ip=`ifconfig $link | grep "inet addr"`
if [ -z "$gw_ip" ]; then
    log_info "Skip healthcheck, ip addr not ready"
    Healthcheck_Pass
fi

# In case that internet is not available we shouldn't execute DNS checks
is_internet_available
if [ $? -ne 0 ]; then
    log_info "Skip healthcheck, internet not available"
    Healthcheck_Pass
fi

# Check default DNS server configuration
$timeout 10 $PLOOKUP $LOOKUP_HOST > /dev/null 2>&1
if [ $? -ne 0 ]; then
    log_err "Default DNS failed"
else
    Healthcheck_Pass
fi

# Check default DNS servers
default_servers=`awk '$1 == "nameserver" {print $2}' $RESOLV_CONF`
check_dns_servers "Check default DNS servers" $default_servers

# Check reference DNS servers
check_dns_servers "Check reference DNS servers" $DNS_SERVERS

# Return fail, no DNS available
Healthcheck_Fail
