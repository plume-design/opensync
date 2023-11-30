
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

#/bin/sh

# Do not run on a node claimed to a VSB (Very Small Business) location, as it will clash
# with Captive_Portal pushed from the cloud
# When done, inspect status, i.e. mostly check if Captive_Portal table is in accordance with
# tinyproxy processes and config files
# Run cp_test_cleanup.sh to be ready for the next run

# Start cpm
ovsh u Node_Services enable:=true -w service==cpm
sleep 3

# Insert first entry, tinyproxy config should contain 'Listen 127.0.0.1' and 'port 8888'
ovsh i Captive_Portal \
name:=default \
proxy_method:=reverse \
uam_url:=https://www.mycaptiveportal1.com \
additional_headers:='["map",[["X-LocationID","123456789abcdef123456789"],["X-PodID","123456789A"],["X-PodMac","12:34:56:78:9a:bc"],["X-SSID","guest123"]]]' \
other_config:='["map",[["pkt_mark","0x2"]]]'
sleep 1

# Insert second entry, this tinyproxy config should contain 'Listen 127.0.0.1' and 'port 8889'
ovsh i Captive_Portal \
name:=employee \
proxy_method:=reverse \
uam_url:=https://www.mycaptiveportal2.com \
additional_headers:='["map",[["X-LocationID","123456789abcdef123456789"],["X-PodID","123456789A"],["X-PodMac","12:34:56:78:9a:bc"],["X-SSID","employee123"]]]' \
other_config:='["map",[["pkt_mark","0x2"],["listenip","127.0.0.1"],["listenport","8889"]]]'
sleep 1

# Update second entry, tinyproxy config should change to 'port 8890'
ovsh u Captive_Portal \
uam_url:=https://www.mycaptiveportal3.com \
other_config:='["map",[["pkt_mark","0x2"],["listenip","127.0.0.1"],["listenport","8890"]]]' \
-w name==employee
sleep 1

# Delete first entry, first tinyproxy config and pid file should be gone
ovsh d Captive_Portal -w name==default
sleep 1

# Delete the remaining
ovsh d Captive_Portal -w name==employee
sleep 1
