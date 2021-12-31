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

UNIT_DISABLE := $(if $(CONFIG_MANAGER_FCM),n,y)
UNIT_NAME := test_lan_stats

UNIT_TYPE := TEST_BIN

UNIT_SRC := test_lan_stats.c

UNIT_CFLAGS := -Isrc/fcm/inc

ifneq ($(CONFIG_FCM_OVS_CMD),y)
UNIT_LDFLAGS := -lopenvswitch
endif
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_DEPS := src/lib/lan_stats
UNIT_DEPS += src/lib/const
UNIT_DEPS += src/lib/log
UNIT_DEPS += src/lib/ovsdb
UNIT_DEPS += src/lib/network_metadata
UNIT_DEPS += src/lib/fcm_filter
UNIT_DEPS += src/lib/unity

# Ensure the required files are copied in its correct location
$(UNIT_BUILD)/.target: /tmp/stats.txt /tmp/stats_2.txt /tmp/stats_3.txt /tmp/stats_4.txt /tmp/stats_5.txt /tmp/stats_6.txt



/tmp/stats.txt: $(UNIT_PATH)/stats.txt FORCE_LANSTATS
	${NQ} " $(call color_copy,copy)    [$(call COLOR_BOLD,$<)] -> $@"
	${Q} cp $< $@
/tmp/stats_2.txt: $(UNIT_PATH)/stats_2.txt FORCE_LANSTATS
	${NQ} " $(call color_copy,copy)    [$(call COLOR_BOLD,$<)] -> $@"
	${Q} cp $< $@
/tmp/stats_3.txt: $(UNIT_PATH)/stats_3.txt FORCE_LANSTATS
	${NQ} " $(call color_copy,copy)    [$(call COLOR_BOLD,$<)] -> $@"
	${Q} cp $< $@
/tmp/stats_4.txt: $(UNIT_PATH)/stats_4.txt FORCE_LANSTATS
	${NQ} " $(call color_copy,copy)    [$(call COLOR_BOLD,$<)] -> $@"
	${Q} cp $< $@
/tmp/stats_5.txt: $(UNIT_PATH)/stats_5.txt FORCE_LANSTATS
	${NQ} " $(call color_copy,copy)    [$(call COLOR_BOLD,$<)] -> $@"
	${Q} cp $< $@
/tmp/stats_6.txt: $(UNIT_PATH)/stats_6.txt FORCE_LANSTATS
	${NQ} " $(call color_copy,copy)    [$(call COLOR_BOLD,$<)] -> $@"
	${Q} cp $< $@
FORCE_LANSTATS:
