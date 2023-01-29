#!/bin/bash -ax

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

export USER_MODE_LINUX=y

mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t tmpfs tmp /tmp
mount -t tmpfs tmp /var
mount -o rw,remount /
mkdir -p /var/run
ip link set lo up

PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin
dir=$(grep -o 'env_dir=[^ ]*' /proc/cmdline | cut -d= -f2)
env=$(grep -o 'env=[^ ]*' /proc/cmdline | cut -d= -f2)
owm=$(grep -o 'env_owm=[^ ]*' /proc/cmdline | cut -d= -f2)
db=$(grep -o 'env_db=[^ ]*' /proc/cmdline | cut -d= -f2)
cmd=$(grep -o 'env_cmd=.*' /proc/cmdline | cut -d= -f2-)
PATH=$PATH:$(dirname "$owm")
LD_LIBRARY_PATH=$(dirname "$owm")/../lib

eval "$(echo "$env" | base64 -d)"

iw dev wlan0 del || true
iw dev wlan1 del || true
iw phy phy0 interface add wlan0 type station
iw phy phy0 interface add wlan0_1 type station
iw phy phy0 interface add wlan0_2 type station
iw phy phy1 interface add wlan1 type station
iw phy phy1 interface add wlan1_1 type station
iw phy phy1 interface add wlan1_2 type station

mkdir -p /var/run/hostapd
mkdir -p /var/run/wpa_supplicant
rm -rf /var/run/hostapd
rm -rf /var/run/wpa_supplicant

hostapd -g /var/run/hostapd/global -B -f /tmp/hapd-glob.log -dd -t
wpa_supplicant -g /var/run/wpa_supplicantglobal -B -f /tmp/wpas_glob.log -dd -t

db1=/var/run/db1.sock
db2=/var/run/db2.sock
conf1=/var/lib/openvswitch/conf1.db
conf2=/var/lib/openvswitch/conf2.db

mkdir -p /var/lib/openvswitch
mkdir -p /var/run/openvswitch
cp -v "$db" "$conf1"
cp -v "$db" "$conf2"

dut="env PLUME_OVSDB_SOCK_PATH=$db1 sh -axe"
dut_phy=phy0
dut_vif_sta=wlan0
dut_vif_ap0=wlan0_1
dut_vif_ap1=wlan0_2
dut_vif_sta_idx=0
dut_vif_ap0_idx=1
dut_vif_ap1_idx=2
dut_mac_sta=$(cat /sys/class/net/$dut_vif_sta/address)
dut_mac_ap0=$(cat /sys/class/net/$dut_vif_ap0/address)
dut_mac_ap1=$(cat /sys/class/net/$dut_vif_ap1/address)

ulimit -c unlimited
echo /tmp/core > /proc/sys/kernel/core_pattern
cd "$dir"

# This runs 2 WM instances, one nested in another, but
# otherwise operating on 2 databases. The $dut and $ref take
# care of wrapping things so that, eg. ovsh works with the
# correct WM2 instance. Running them like that allows the
# test flow to finish when the inner most test script
# finishes, or if something crashes at any layer.
time ovsdb-server \
	--remote=punix:$db1 \
	--run "env PLUME_OVSDB_SOCK_PATH=$db1 $owm $cmd" \
	"$conf1"

cp /tmp/*.log "logs/$cmd/"
cp /tmp/core "logs/$cmd/"

halt -f -p
