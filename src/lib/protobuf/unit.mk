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
# protocol buffer library
#
###############################################################################
UNIT_NAME := protobuf

# Template type:
UNIT_TYPE := LIB

PROTO_SRC_DIR := $(WORKDIR)/pb-src
PROTO_INC_DIR := $(WORKDIR)/pb-inc

ifeq ($(wildcard $(PROTO_SRC_DIR)),)
$(shell mkdir -p $(PROTO_SRC_DIR))
endif

ifeq ($(wildcard $(PROTO_INC_DIR)),)
$(shell mkdir -p $(PROTO_INC_DIR))
endif

define protobuf_generate
UNIT_PRE += $(PROTO_INC_DIR)/$(2).pb-c.h
UNIT_PRE += $(PROTO_SRC_DIR)/$(2).pb-c.c
UNIT_SRC_TOP += $(PROTO_SRC_DIR)/$(2).pb-c.c
UNIT_EXPORT_LDFLAGS += -lprotobuf-c

UNIT_CFLAGS += -I$(PROTO_INC_DIR)

UNIT_CLEAN += $(PROTO_INC_DIR)/$(2).pb-c.h
UNIT_CLEAN += $(PROTO_SRC_DIR)/$(2).pb-c.c

$(PROTO_SRC_DIR)/$(2).pb-c.c: $(UNIT_PATH)/$(1)
	$(Q)protoc-c --c_out=. --proto_path=./src/lib/protobuf/ $(1)
	$(Q)mv $(2).pb-c.c $(PROTO_SRC_DIR)
	$(Q)mv $(2).pb-c.h $(PROTO_INC_DIR)
endef

$(eval $(call protobuf_generate,dpi_stats.proto,dpi_stats))
$(eval $(call protobuf_generate,gatekeeper_hero_stats.proto,gatekeeper_hero_stats))
$(eval $(call protobuf_generate,gatekeeper.proto,gatekeeper))
$(eval $(call protobuf_generate,interface_stats.proto,interface_stats))
$(eval $(call protobuf_generate,ip_dns_telemetry.proto,ip_dns_telemetry))
$(eval $(call protobuf_generate,lte_info.proto,lte_info))
$(eval $(call protobuf_generate,mdns_records_telemetry.proto,mdns_records_telemetry))
$(eval $(call protobuf_generate,network_metadata.proto,network_metadata))
$(eval $(call protobuf_generate,object_manager.proto,object_manager))
$(eval $(call protobuf_generate,opensync_nflog.proto,opensync_nflog))
$(eval $(call protobuf_generate,opensync_stats.proto,opensync_stats))
$(eval $(call protobuf_generate,time_event.proto,time_event))
$(eval $(call protobuf_generate,adv_data_typing.proto,adv_data_typing))
$(eval $(call protobuf_generate,upnp_portmap.proto,upnp_portmap))
$(eval $(call protobuf_generate,cell_info.proto,cell_info))
$(eval $(call protobuf_generate,thread_network_info.proto,thread_network_info))

UNIT_EXPORT_CFLAGS += -I$(PROTO_INC_DIR)
