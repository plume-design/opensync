
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

# ~/bin/sh

# Cleanup config
config_cleanup()
{
    ovsh d Lte_Config -w if_name==wwan0
    ovsh d Wifi_Inet_Config -w if_name==wwan0
    ovsh u AWLAN_Node mqtt_topics:del:'["map",[["LteStats", "dev-test/LteStats/dog1/HC83C0005B/602e11e768b6592af397e9f2"]]]'
}

# Configure LTE
# Kore + no MTU
configure_kore()
{
    ovsh i Lte_Config -w if_name==wwan0 if_name:=wwan0 manager_enable:=true lte_failover_enable:=true ipv4_enable:=true modem_enable:=true force_use_lte:=false apn:=data.icore.name active_simcard_slot:=1 os_persist:=false report_interval:=60
    ovsh i Wifi_Inet_Config -w if_name==wwan0 if_name:=wwan0 if_type:=lte ip_assign_scheme:=dhcp enabled:=true network:=true NAT:=true os_persist:=false
    ovsh u AWLAN_Node mqtt_topics:ins:'["map",[["LteStats", "dev-test/LteStats/dog1/HC83C0005B/602e11e768b6592af397e9f2"]]]'
}

# Pod (mvno) + no MTU
configure_pod()
{
    ovsh i Lte_Config -w if_name==wwan0 if_name:=wwan0 manager_enable:=true lte_failover_enable:=true ipv4_enable:=true modem_enable:=true force_use_lte:=false apn:=data641003 active_simcard_slot:=0 os_persist:=false report_interval:=900
    ovsh i Wifi_Inet_Config -w if_name==wwan0 if_name:=wwan0 if_type:=lte ip_assign_scheme:=dhcp enabled:=true network:=true NAT:=true os_persist:=false
    ovsh u AWLAN_Node mqtt_topics:ins:'["map",[["LteStats", "dev-test/LteStats/dog1/HC83C0005B/602e11e768b6592af397e9f2"]]]'
}

# Add persist
configure_persist()
{
    ovsh u Lte_Config -w if_name==wwan0 os_persist:=true
    ovsh u Wifi_Inet_Config -w if_name==wwan0 os_persist:=true
}

# Force Lte
force_lte_on()
{
    ovsh s Lte_Config -w wwan0 force_use_lte:=true
    sleep 30
}

force_lte_off()
{
    ovsh s Lte_Config -w wwan0 force_use_lte:=false
    sleep 30
}

# Dump network info
dump_network_info()
{
    echo -e "AT+CIMI\r" | microcom -t 100 /dev/ttyUSB2
    echo -e "AT+QCCID\r" | microcom -t 100 /dev/ttyUSB2
    echo -e "AT+COPS?\r" | microcom -t 100 /dev/ttyUSB2
    echo -e "AT+CREG?\r" | microcom -t 100 /dev/ttyUSB2
    echo -e "AT+CSQ\r" | microcom -t 100 /dev/ttyUSB2
    echo -e "AT+QNWINFO\r" | microcom -t 100 /dev/ttyUSB2
    echo -e "AT+QSPN\r" | microcom -t 100 /dev/ttyUSB2
    echo -e "AT+QENG=\"servingcell\"\r" | microcom -t 100 /dev/ttyUSB2
    echo -e "AT+QGDCNT?\r" | microcom -t 100 /dev/ttyUSB2
    echo -e "at+cgcontrdp\r" | microcom -t 100 /dev/ttyUSB2
}

# Dump the config/state
dump_config()
{
    ovsh s Lte_Config
    ovsh s Lte_State
    ovsh s Wifi_Inet_Config -w if_name==wwan0
    ovsh s Wifi_Route_State
    ifconfig wwan0
    route -n
}

while getopts "s:p:f:c:" opt;
do
   case $opt in
      s)
         sim=$OPTARG
         ;;
      p)
         persist=$OPTARG
         ;;
      f)
         force=$OPTARG
         ;;
      c)
         cleanup=$OPTARG
         ;;
   esac
done

if [ $cleanup == "clean" ]; then
    config_cleanup
fi

if [ $sim == "kore" ]; then
   echo "Using Kore SIM config"
   configure_kore
fi

if [ $sim == "pod" ]; then
   echo "Using POD SIM config"
   configure_pod
fi

if [ $persist == "persist" ]; then
   echo "Setting persist"
   configure_persist
fi

sleep 30
dump_network_info
dump_config
ping -c 1 -I wwan0 8.8.8.8

if [ $force == "force" ]; then
   echo "Force LTE"
   force_lte_on
   ping -c 1 8.8.8.8
   dump_network_info
   dump_config
   force_lte_off
   ping -c 1 8.8.8.8
   dump_network_info
   dump_config
fi

if [ $cleanup == "clean" ]; then
    config_cleanup
fi

