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
# dm_schema - TR181 schema utilities
#
###############################################################################
UNIT_NAME := dm_schema
UNIT_TYPE := LIB
UNIT_DIR  := lib

UNIT_SRC += src/dm_schema.c
UNIT_SRC += src/dm_tr181_schema.c

UNIT_EXPORT_CFLAGS += -I$(UNIT_PATH)/inc
UNIT_CFLAGS        += -I$(UNIT_PATH)/inc

UNIT_DEPS += src/lib/common

# Regenerate inc/dm_tr181_schema.h and src/dm_tr181_schema.c from XML.
# Usage: make src/lib/dm_schema/generate
#
# Capture UNIT_PATH now (immediate assignment) so the recipe below sees the
# correct value at rule execution time — UNIT_PATH is overwritten by each
# subsequent unit.mk that the build system includes.
_DM_SCHEMA_DIR := $(UNIT_PATH)

# Space-separated list of top-level Device. objects to include in the schema.
# Omit DM_I (or leave empty) to include the full TR-181 tree.
# Excluded objects appear as stubs (type=DM_NODE_STUB) with no subtree.
DM_I += WiFi DeviceInfo QoS Firewall Routing Ethernet SoftwareModules DHCPv6
DM_I += DHCPv4 IP IPsec Bridging DNS GRE PPP NAT Hosts MAP PeriodicStatistics
DM_I += RouterAdvertisement DynamicDNS NeighborDiscovery

.PHONY: $(_DM_SCHEMA_DIR)/generate
$(_DM_SCHEMA_DIR)/generate:
	$(NQ) " generate  [dm_schema] inc/dm_tr181_schema.h src/dm_tr181_schema.c"
	$(Q)python3 $(_DM_SCHEMA_DIR)/gen_dm_tr181_schema.py \
		$(_DM_SCHEMA_DIR)/tr181-schema/tr-181-2-20-1-usp-full.xml \
		$(_DM_SCHEMA_DIR)/tr181-schema/vendor_opensync.xml \
		--out-h $(_DM_SCHEMA_DIR)/inc \
		--out-c $(_DM_SCHEMA_DIR)/src \
		$(if $(DM_I),-i "$(DM_I)")
