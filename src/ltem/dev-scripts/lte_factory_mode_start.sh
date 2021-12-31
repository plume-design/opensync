
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

# !/bin/sh
# Uncomment the following lines if your physical sim is in slot 1
echo -e "at+qdsim=1\r" | microcom -t 100 /dev/ttyUSB2   # switch to slot 1
echo -e "at+cfun=1,1\r" | microcom -t 100 /dev/ttyUSB2  # reset LTE module
sleep 30
/usr/opensync/tools/quectel-CM &
sleep 30
route add default dev wwan0
cp /tmp/wwan0.resolv /etc/resolv.conf
brctl delif br0 wl1
brctl delif br0 eth1
wl -i wl1 down
wl -i wl1 bw_cap 2g 0xff
wl -i wl1 chanspec 6u
wl -i wl1 up
wl -i wl1 ssid 2G
wl -i wl1 status
ifconfig wl1 10.1.1.1 netmask 255.255.255.0
iptables -t nat -A POSTROUTING -o wwan0 -j MASQUERADE
