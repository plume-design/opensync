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

##############################################################################
#
# OpenSync low level API
#
##############################################################################

UNIT_NAME := osn

# Template type:
UNIT_TYPE := LIB

UNIT_CFLAGS += -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -I$(UNIT_PATH)/src

UNIT_SRC += src/osn_types.c

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_NETIF_NULL),src/osn_netif_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_NETIF_LINUX),src/osn_netif_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IPV4_NULL),src/osn_ip_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IPV4_LINUX),src/osn_ip_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_ROUTEV4_NULL),src/osn_route_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_ROUTEV4_LINUX),src/osn_route_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV4_CLIENT_NULL),src/osn_dhcp_client_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV4_CLIENT_UDHCP),src/osn_dhcp_client_udhcp.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV4_SERVER_NULL),src/osn_dhcp_server_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV4_SERVER_DNSMASQ),src/osn_dhcp_server_dnsmasq.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_UPNP_NULL),src/osn_upnp_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_UPNP_MINIUPNPD),src/osn_upnp_mupnp.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IPV6_NULL),src/osn_ip6_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IPV6_LINUX),src/osn_ip6_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV6_CLIENT_NULL),src/osn_dhcpv6_client_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV6_CLIENT_ODHCP6),src/osn_dhcpv6_client_odhcp6.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV6_SERVER_NULL),src/osn_dhcpv6_server_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV6_SERVER_DNSMASQ6),src/osn_dhcpv6_server_dnsmasq6.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IPV6_RADV_NULL),src/osn_ip6_radv_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IPV6_RADV_DNSMASQ6),src/osn_ip6_radv_dnsmasq6.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_PPPOE_NULL),src/osn_pppoe_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_PPPOE_LINUX),src/osn_pppoe_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_VLAN_NULL),src/osn_vlan_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_VLAN_LINUX),src/osn_vlan_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_QOS_NULL),src/osn_qos_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_QOS_LINUX),src/osn_qos_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_FW_NULL),src/osn_fw_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_FW_IPTABLES_FULL),src/osn_fw_iptables_full.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_FW_IPTABLES_THIN),src/osn_fw_iptables_thin.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_MAPT_NULL),src/osn_mapt_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_MAPT_CERNET),src/osn_mapt_cernet.c)

ifdef CONFIG_OSN_LINUX_ENABLED
UNIT_CFLAGS += -I$(UNIT_PATH)/src/linux

UNIT_SRC += $(if $(CONFIG_OSN_DNSMASQ6),src/linux/dnsmasq6_server.c)
UNIT_SRC += $(if $(CONFIG_OSN_DNSMASQ),src/linux/dnsmasq_server.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_IP),src/linux/lnx_ip.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_IPV6),src/linux/lnx_ip6.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_NETIF),src/linux/lnx_netif.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_NETLINK),src/linux/lnx_netlink.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_ROUTE),src/linux/lnx_route.c)
UNIT_SRC += $(if $(CONFIG_OSN_MINIUPNPD),src/linux/mupnp_server.c)
UNIT_SRC += $(if $(CONFIG_OSN_ODHCP6),src/linux/odhcp6_client.c)
UNIT_SRC += $(if $(CONFIG_OSN_UDHCPC),src/linux/udhcp_client.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_PPPOE),src/linux/lnx_pppoe.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_VLAN),src/linux/lnx_vlan.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_QOS),src/linux/lnx_qos.c)

UNIT_DEPS += src/lib/daemon
UNIT_DEPS += src/lib/evx
UNIT_DEPS += src/lib/ds
UNIT_DEPS += src/lib/execsh
endif

UNIT_EXPORT_CFLAGS := -I$(UNIT_PATH)/inc

UNIT_DEPS += src/lib/log
UNIT_DEPS += src/lib/kconfig
