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

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_ROUTEV4_NULL),src/osn_route_null.c,src/osn_route_linux.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_ROUTEV6_NULL),src/osn_route6_null.c,src/osn_route6_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV4_CLIENT_NULL),src/osn_dhcp_client_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV4_CLIENT_UDHCP),src/osn_dhcp_client_udhcp.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV4_SERVER_NULL),src/osn_dhcp_server_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_DHCPV4_SERVER_DNSMASQ),src/osn_dhcp_server_dnsmasq.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_UPNP_NULL),src/osn_upnp_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_UPNP_MINIUPNPD),src/osn_upnp.c)
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

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_TAP_NULL),src/osn_tap_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_TAP_INTERFACE),src/osn_tap_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_QOS_NULL),src/osn_qos_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_QOS_LINUX),src/osn_qos_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_QDISC_NULL),src/osn_qdisc_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_QDISC_LINUX),src/osn_qdisc.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_ADAPTIVE_QOS_NULL),src/osn_adaptive_qos_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_ADAPTIVE_QOS_CAKE_AUTORATE),src/osn_cake_autorate.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_FW_NULL),src/osn_fw_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_FW_IPTABLES_FULL),src/osn_fw_iptables_full.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_FW_IPTABLES_THIN),src/osn_fw_iptables_thin.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_FW_EBTABLES_FULL),src/osn_fw_ebtables_full.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_FW_EBTABLES_THIN),src/osn_fw_ebtables_thin.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IPSET_NULL),src/osn_ipset_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IPSET_LINUX),src/osn_ipset_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_CELL_NULL),src/osn_lte_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_CELL_LINUX),src/osn_lte_linux.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_CELL_LINUX),src/osn_cell_modem.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_CELL_LINUX),src/osn_cell_esim.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_NFLOG_NULL),src/osn_nflog_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_NFLOG_LINUX),src/osn_nflog_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IGMP_NULL),src/osn_igmp_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IGMP_LINUX),src/osn_igmp_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_MLD_NULL),src/osn_mld_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_MLD_LINUX),src/osn_mld_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_VPN_LINUX),src/osn_vpn_linux.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_VPN_NULL),src/osn_vpn_null.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IPSEC_LINUX_STRONGSWAN),src/osn_ipsec_strongswan.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_IPSEC_NULL),src/osn_ipsec_null.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_TUNNEL_IFACE_LINUX),src/osn_tunnel_iface_linux.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_TUNNEL_IFACE_NULL),src/osn_tunnel_iface_null.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_TC_LINUX),src/osn_tc_linux.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_BRIDGING_NULL),src/osn_bridge_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_LINUX_BRIDGING),src/osn_bridge_linux.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_ROUTE_RULE_LINUX),src/osn_route_rule_linux.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_ROUTE_RULE_NULL),src/osn_route_rule_null.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_MAP_LINUX),src/osn_map_linux.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_MAP_LINUX),src/osn_map_v6plus.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_MAP_NULL),src/osn_map_null.c)

ifdef CONFIG_OSN_LINUX_ENABLED
UNIT_CFLAGS += -I$(UNIT_PATH)/src/linux

UNIT_SRC += $(if $(CONFIG_OSN_DNSMASQ6),src/linux/dnsmasq6_server.c)
UNIT_SRC += $(if $(CONFIG_OSN_DNSMASQ),src/linux/dnsmasq_server.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_IP),src/linux/lnx_ip.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_IPV6),src/linux/lnx_ip6.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_NETIF),src/linux/lnx_netif.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_NETLINK),src/linux/lnx_netlink.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_ROUTE),src/linux/lnx_route.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_ROUTE_LIBNL3),src/linux/lnx_routes.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_ROUTE_IP),src/linux/lnx_route_config.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_ROUTE_STATE_LIBNL3),src/linux/lnx_route_state_nl.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_ROUTE_STATE_PROC),src/linux/lnx_route_state_proc.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_ROUTE6),src/linux/lnx_route6.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_ROUTE6_LIBNL3),src/linux/lnx_route6_nl.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_ROUTE6_STATE_LIBNL3),src/linux/lnx_route6_state_nl.c)
UNIT_SRC += $(if $(CONFIG_OSN_MINIUPNPD),src/linux/mupnp_server.c)
UNIT_SRC += $(if $(CONFIG_OSN_MINIUPNPD),src/linux/mupnp_cfg_iptv.c)
UNIT_SRC += $(if $(CONFIG_OSN_MINIUPNPD),src/linux/mupnp_cfg_wan.c)
UNIT_SRC += $(if $(CONFIG_OSN_ODHCP6),src/linux/odhcp6_client.c)
UNIT_SRC += $(if $(CONFIG_OSN_UDHCPC),src/linux/udhcp_client.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_IPSET),src/linux/lnx_ipset.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_PPPOE),src/linux/lnx_pppoe.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_VLAN),src/linux/lnx_vlan.c)


UNIT_SRC += $(if $(CONFIG_OSN_LINUX_BRIDGING),src/linux/lnx_bridge.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_TAPIF),src/linux/lnx_tap.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_QOS),src/linux/lnx_qos.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_QDISC),src/linux/lnx_qdisc.c)
UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_ADAPTIVE_QOS_CAKE_AUTORATE),src/linux/cake_autorate.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_LTE),src/linux/lnx_lte.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_IGMP),src/linux/lnx_igmp.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_MLD),src/linux/lnx_mld.c)
UNIT_SRC += $(if $(or $(CONFIG_OSN_LINUX_IGMP),$(CONFIG_OSN_LINUX_MLD)),src/linux/lnx_mcast_bridge.c)
UNIT_SRC += $(if $(CONFIG_OSN_VPN_IPSEC),src/linux/strongswan.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_TUNNEL_IFACE),src/linux/lnx_tunnel_iface.c)
UNIT_SRC += src/linux/udhcp_const.c
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_TC),src/linux/lnx_tc.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_ROUTE_RULE_IP),src/linux/lnx_route_rule_iproute.c)

UNIT_SRC += $(if $(CONFIG_OSN_BACKEND_MAP_LINUX),src/linux/lnx_map.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_MAPT_NAT46),src/linux/lnx_map_mapt_nat46.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_MAPT_NULL),src/linux/lnx_map_mapt_null.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_MAPE_IP6TNL),src/linux/lnx_map_mape_ip6tnl.c)
UNIT_SRC += $(if $(CONFIG_OSN_LINUX_MAPE_NULL),src/linux/lnx_map_mape_null.c)

ifeq ($(or $(CONFIG_OSN_LINUX_ROUTE_LIBNL3),$(CONFIG_OSN_LINUX_MAPE_IP6TNL),$(CONFIG_OSN_LINUX_ROUTE6_LIBNL3),$(CONFIG_OSN_LINUX_ROUTE_STATE_LIBNL3)),y)
UNIT_CFLAGS += $(LIBNL3_HEADERS)
UNIT_LDFLAGS += -lnl-3 -lnl-route-3
endif

UNIT_DEPS += src/lib/daemon
UNIT_DEPS += src/lib/evx
UNIT_DEPS += src/lib/ds
UNIT_DEPS += src/lib/ds_util
UNIT_DEPS += src/lib/execsh
endif

UNIT_EXPORT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_DEPS += src/lib/log
UNIT_DEPS += src/lib/kconfig
UNIT_DEPS += src/lib/schema
UNIT_DEPS += src/lib/neigh_table
