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
# TARGET specific layer library
#
###############################################################################
UNIT_NAME := target

# Template type:
UNIT_TYPE := LIB

TARGET_COMMON_SRC := src/target_stub.c
TARGET_COMMON_SRC += src/target_map.c
TARGET_COMMON_SRC += src/target_linux.c
TARGET_COMMON_SRC += src/target_mac_learn.c
TARGET_COMMON_SRC += $(if $(CONFIG_TARGET_HWSIM),src/target_hwsim.c,)

UNIT_SRC += $(TARGET_COMMON_SRC)
ifeq ($(filter-out native,$(TARGET)),)
UNIT_SRC += src/target_native.c
endif

UNIT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -I$(UNIT_BUILD)
UNIT_CFLAGS += -DTARGET_H='"target_$(TARGET).h"'

UNIT_LDFLAGS := -ldl -lpthread

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_DEPS := src/lib/common
UNIT_DEPS += src/lib/ds
UNIT_DEPS += src/lib/const
UNIT_DEPS += src/lib/schema
UNIT_DEPS += src/lib/log
UNIT_DEPS += src/lib/ovs_mac_learn
UNIT_DEPS += src/lib/osp
UNIT_DEPS += src/lib/version
UNIT_DEPS += $(if $(CONFIG_TARGET_HWSIM),src/lib/hostap,)

UNIT_DEPS_CFLAGS += src/lib/datapipeline

ifneq ($(CONFIG_TARGET_HWSIM),)
ifneq ($(HOSTAP_SOURCE),)
# Note: This requires hostapd to be built without
# debugs/traces. Otherwise the two .o files will not be
# sufficient, and once additional .o files are pulled in,
# symbol conflicts will emerge. Be wary.

# Note: This is most intended for native builds for
# UML/local testing. This is not intended to be used or
# relied on for real target implementations.

UNIT_CFLAGS += -I/usr/include/libnl3
UNIT_LDFLAGS += -lnl-genl-3 -lnl-3
UNIT_EXPORT_CFLAGS += -I$(HOSTAP_SOURCE)/src/common
UNIT_OBJ += $(UNIT_BUILD)/os_unix.o
UNIT_OBJ += $(UNIT_BUILD)/wpa_ctrl.o

$(UNIT_BUILD)/os_unix.o: $(HOSTAP_SOURCE)/build/hostapd/src/utils/os_unix.o
	cp $< $@

$(UNIT_BUILD)/wpa_ctrl.o: $(HOSTAP_SOURCE)/build/hostapd/src/common/wpa_ctrl.o
	cp $< $@
endif
endif

#
# Kconfig based configuration
#
TARGET_COMMON_SRC += src/target_kconfig.c
TARGET_COMMON_SRC += src/target_kconfig_managers.c
UNIT_DEPS += src/lib/kconfig


#
# Stubs
#

TARGET_IMPL_H := $(UNIT_BUILD)/target_impl.h
UNIT_CLEAN := $(TARGET_IMPL_H)

# auto stubs: generate a list of implemented apis
define UNIT_POST_MACRO
TARGET_OBJ_IMPL := $$(filter-out %target_stub.o,$$(UNIT_OBJ))
TARGET_OBJ_STUB := $$(filter %target_stub.o,$$(UNIT_OBJ))
$$(TARGET_OBJ_STUB): $$(TARGET_IMPL_H)
$$(TARGET_IMPL_H): $$(TARGET_OBJ_IMPL)
	$$(Q) for API in `nm --defined-only $$(TARGET_OBJ_IMPL) | grep ' [A-Z] target_' | cut -d' ' -f3 `; \
		do echo "#define IMPL_$$$$API"; done > $$(TARGET_IMPL_H).tmp
	$$(Q) if ! cmp -s $$(TARGET_IMPL_H).tmp $$(TARGET_IMPL_H); then \
		echo " $$(call color_generate,generate)[$$(call color_target,target)] $$(TARGET_IMPL_H)"; \
		mv $$(TARGET_IMPL_H).tmp $$(TARGET_IMPL_H); \
		fi
endef
