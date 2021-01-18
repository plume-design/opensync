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

###############################################################################
#
# WAN Orchestrator
#
###############################################################################
UNIT_DISABLE := $(if $(CONFIG_MANAGER_WANO),n,y)

UNIT_NAME := wano
UNIT_TYPE := BIN

UNIT_SRC += src/wano_connection_manager_uplink.c
UNIT_SRC += src/wano_inet_config.c
UNIT_SRC += src/wano_inet_state.c
UNIT_SRC += src/wano_localconfig.c
UNIT_SRC += src/wano_main.c
UNIT_SRC += src/wano_ovs_port.c
UNIT_SRC += src/wano_plugin.c
UNIT_SRC += src/wano_ppline.c

UNIT_CFLAGS += -I$(UNIT_PATH)/src
UNIT_CFLAGS += -I$(UNIT_PATH)/inc

UNIT_EXTERN_CFLAGS += -I$(UNIT_PATH)/inc

UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/json_util
UNIT_DEPS += src/lib/log
UNIT_DEPS += src/lib/module
UNIT_DEPS += src/lib/osa
UNIT_DEPS += src/lib/osn
UNIT_DEPS += src/lib/osp
UNIT_DEPS += src/lib/ovsdb
UNIT_DEPS += src/lib/pjs
UNIT_DEPS += src/lib/reflink
UNIT_DEPS += src/lib/target

# WANO pipeline state machine
$(eval $(call stam_generate,src/wano_ppline.dot))

###############################################################################
# WAN Plug-ins
###############################################################################
ifeq ($(CONFIG_MANAGER_WANO_PLUGIN_DHCPV4),y)
UNIT_SRC += src/wanp_dhcpv4.c
$(eval $(call stam_generate,src/wanp_dhcpv4.dot))
endif

ifeq ($(CONFIG_MANAGER_WANO_PLUGIN_DHCPV6),y)
UNIT_SRC += src/wanp_dhcpv6.c
$(eval $(call stam_generate,src/wanp_dhcpv6.dot))
endif

ifeq ($(CONFIG_MANAGER_WANO_PLUGIN_STATIC_IPV4),y)
UNIT_SRC += src/wanp_static_ipv4.c
$(eval $(call stam_generate,src/wanp_static_ipv4.dot))
endif

ifeq ($(CONFIG_MANAGER_WANO_PLUGIN_PPPOE),y)
UNIT_SRC += src/wanp_pppoe.c
$(eval $(call stam_generate,src/wanp_pppoe.dot))
endif

ifeq ($(CONFIG_MANAGER_WANO_PLUGIN_VLAN),y)
UNIT_SRC += src/wanp_vlan.c
$(eval $(call stam_generate,src/wanp_vlan.dot))
endif

ifeq ($(CONFIG_MANAGER_WANO_PLUGIN_ETHCLIENT),y)
UNIT_SRC += src/wanp_ethclient.c
endif
