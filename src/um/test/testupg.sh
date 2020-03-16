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


SERVER=http://192.168.4.15/
FAKESERVER=http://192.168.7.15/
FILE=openwrt-os-PIRANHA-ap-dogfood-squashfs-0.8-61a60c8-6.img
FAKEFILE=openwrt-os-PIRANHA-ap-dogfood-squashfs-0.8-blabla-7.img

#
# Check that FW image file exists, verify checksum
# exit on error
#
FW_check()
{
    if [ -f /tmp/pfirmware/${FILE} ]
    then
        cd /tmp/pfirmware
        md5sum -c ${FILE}.md5
        if [ $? != 0 ]
        then
            echo "MD5SUM check fail"
            exit 1
        else
            # remove the file, re-trigger upgrade
            rm -rf ${FILE}
        fi
        cd -
    else
        echo "Error, SW image file is missing"
        exit 1
    fi
}


#
# Show all ovsdb upgrade related fields in AWLAN_Node table
#
ovsdb_status()
{
    ovsh select AWLAN_Node upgrade_status upgrade_timer firmware_url
}


#############################
# Main
#############################

# kill upgrade process
#echo "============================================================"
echo "Killing upgrade"
killall upgrade

#make sure AWLAN_Node table is clean
echo "============================================================"
echo "Cleaning up AWLAN"
ovsh update AWLAN_Node upgrade_status:=0 upgrade_timer:=0 firmware_url:="" > /dev/null

echo "============================================================"
echo "AWLAN_Node status"
ovsdb_status

#echo "============================================================"
#restart upgrade process
echo "Restarting upgrade process"
/usr/plume/bin/upgrade > /tmp/upgrade.log 2>&1 &

#echo "============================================================"
#read -n 1 -p "Unplug network cable now !!! <press enter>" test

#trigger download
echo "============================================================"
echo "Triggering download FAKE server"
ovsh update AWLAN_Node firmware_url:=${FAKESERVER}${FILE} > /dev/null

sleep 2
#show status
ovsdb_status

#show status once again
sleep 10
ovsdb_status
echo "============================================================"

#read -n 1 -p "Plug network cable now !!! <press enter>" test


#Try to restart upgrade
echo "============================================================"
echo "re-trigger download FAKE URL"
ovsh update AWLAN_Node firmware_url:="" > /dev/null
ovsh update AWLAN_Node firmware_url:=${SERVER}${FAKEFILE}  > /dev/null
sleep 10

#show status
echo "============================================================"
ovsdb_status

echo "============================================================"
echo "re-trigger download correct SERVER & URL"
ovsh update AWLAN_Node firmware_url:="" > /dev/null
ovsh update AWLAN_Node firmware_url:=${SERVER}${FILE}  > /dev/null
sleep 10

#show status
echo "============================================================"
ovsdb_status

#trigger actual upgrade
echo "============================================================"
ovsh update AWLAN_Node firmware_url:="" > /dev/null
ovsh update AWLAN_Node upgrade_timer:=30  > /dev/null
sleep 5

#show status
echo "============================================================"
ovsdb_status

