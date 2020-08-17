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

UNIT_NAME := fut_cm

UNIT_DISABLE := $(if $(CONFIG_MANAGER_CM),n,y)

# Template type:
UNIT_TYPE := FUT
# Output directory
UNIT_DIR := shell/tests/cm

UNIT_FILE := cm2_ble_status_interface_down.sh
UNIT_FILE += cm2_ble_status_internet_block.sh
UNIT_FILE += cm2_cloud_down.sh
UNIT_FILE += cm2_dns_failure.sh
UNIT_FILE += cm2_internet_lost.sh
UNIT_FILE += cm2_link_lost.sh
UNIT_FILE += cm2_setup.sh
UNIT_FILE += cm2_ssl_check.sh
UNIT_FILE += cm2_verify_gre_tunnel_dut_gw.sh
UNIT_FILE += cm2_verify_gre_tunnel_ref_extender.sh
