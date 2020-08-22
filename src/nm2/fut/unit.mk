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

UNIT_NAME := fut_nm

UNIT_DISABLE := n

# Template type:
UNIT_TYPE := FUT
# Output directory
UNIT_DIR := shell/tests/nm

UNIT_FILE := nm2_setup.sh
UNIT_FILE += nm2_configure_nonexistent_iface.sh
UNIT_FILE += nm2_enable_disable_iface_network.sh
UNIT_FILE += nm2_ovsdb_configure_interface_dhcpd.sh
UNIT_FILE += nm2_ovsdb_ip_port_forward.sh
UNIT_FILE += nm2_ovsdb_remove_reinsert_iface.sh
UNIT_FILE += nm2_rapid_multiple_insert_delete_iface.sh
UNIT_FILE += nm2_rapid_multiple_insert_delete_ovsdb_row.sh
UNIT_FILE += nm2_set_broadcast.sh
UNIT_FILE += nm2_set_dns.sh
UNIT_FILE += nm2_set_gateway.sh
UNIT_FILE += nm2_set_inet_addr.sh
UNIT_FILE += nm2_set_ip_assign_scheme.sh
UNIT_FILE += nm2_set_mtu.sh
UNIT_FILE += nm2_set_nat.sh
UNIT_FILE += nm2_set_netmask.sh
UNIT_FILE += nm2_set_parent_ifname.sh
UNIT_FILE += nm2_set_upnp_mode.sh
UNIT_FILE += nm2_set_vlan_id.sh
