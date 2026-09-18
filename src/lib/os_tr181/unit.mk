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
# OS TR191 abstraction
#
###############################################################################
UNIT_DISABLE := $(if $(CONFIG_OS_TR181_LIB),n,y)

UNIT_NAME := os_tr181

UNIT_TYPE := LIB

UNIT_SRC += src/os_tr181_common.c
UNIT_SRC += src/os_tr181_val.c
UNIT_SRC += src/os_tr181_val_json.c
UNIT_SRC += src/os_tr181_internal.c
UNIT_SRC += src/os_tr181_libev.c
UNIT_SRC += $(if $(CONFIG_OS_TR181_LIB_NULL), src/os_tr181_null.c)
UNIT_SRC += $(if $(CONFIG_OS_TR181_LIB_CCSP), src/os_tr181_ccsp.c)
UNIT_SRC += $(if $(CONFIG_OS_TR181_LIB_CCSP), src/os_tr181_val_rbus.c)
UNIT_SRC += $(if $(CONFIG_OS_TR181_LIB_AMX),  src/os_tr181_ambiorix.c)
UNIT_SRC += $(if $(CONFIG_OS_TR181_LIB_AMX),  src/os_tr181_val_amxc.c)

UNIT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_LDFLAGS += -ljansson
UNIT_LDFLAGS += -lev

#### CCSP
ifeq ($(CONFIG_OS_TR181_LIB_CCSP),y)
UNIT_CFLAGS += -I/usr/local/include/dbus-1.0 -I/usr/local/lib/dbus-1.0/include
UNIT_CFLAGS += -I/usr/local/include/ccsp/linux
UNIT_LDFLAGS += -Wl,-rpath=/usr/local/lib -lccsp_common -ldbus-1 -lsafec-3.5 -lrbus -lrtMessage -lcjson
endif
#### Ambiorix
ifeq ($(CONFIG_OS_TR181_LIB_AMX),y)
UNIT_LDFLAGS += -lamxc -lamxb -lamxp -lamxd -lamxo

# auto-detect if write_once is supported by the amxd library
FLAG_WRITE_ONCE_C = src/os_tr181_amx_test_write_once.c
FLAG_WRITE_ONCE_MK = $(UNIT_BUILD)/flag_write_once.mk
ifeq ($(wildcard $(FLAG_WRITE_ONCE_MK)),)
$(info $(UNIT_NAME) write_once check $(shell \
	mkdir -p $$(dirname $(UNIT_BUILD)/$(FLAG_WRITE_ONCE_C)) && \
	$(CC) \
		$(UNIT_CFLAGS) \
		-c -o $(UNIT_BUILD)/$(FLAG_WRITE_ONCE_C).o \
		$(UNIT_PATH)/$(FLAG_WRITE_ONCE_C) \
	&& printf "UNIT_CFLAGS += %s" "-DAMXD_HAS_WRITE_ONCE=1" > $(FLAG_WRITE_ONCE_MK) \
	|| printf "# write_once param not available" > $(FLAG_WRITE_ONCE_MK); \
	cat $(FLAG_WRITE_ONCE_MK); \
))
endif
include $(FLAG_WRITE_ONCE_MK)

endif

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/log

