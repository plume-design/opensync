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

UNIT_NAME := fut_device_tools

UNIT_DISABLE := n

# Template type:
UNIT_TYPE := FUT
# Output directory
UNIT_DIR := shell/tools/device

UNIT_FILE := change_channel.sh
UNIT_FILE += check_time.sh
UNIT_FILE += check_wifi_presence.sh
UNIT_FILE += connect_to_fut_cloud_min.sh
UNIT_FILE += connect_to_fut_cloud.sh
UNIT_FILE += create_inet_interface.sh
UNIT_FILE += create_radio_vif_interface.sh
UNIT_FILE += default_setup.sh
UNIT_FILE += genCA.sh
UNIT_FILE += get_radio_mac_from_ovsdb.sh
UNIT_FILE += man_traffic_address.sh
UNIT_FILE += man_traffic_protocol.sh
UNIT_FILE += start_udhcpc.sh
UNIT_FILE += vif_clean.sh
UNIT_FILE += wm2_setup.sh
