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
# Opensync Wireless API
#
###############################################################################
UNIT_NAME := osw

# This unit relies heavily on MODULE(). LIB will generate
# broken binaries because .ctors section will be stripped
# away.
UNIT_TYPE := SHLIB
UNIT_DIR := lib

UNIT_SRC := src/osw_thread.c
UNIT_SRC += src/osw_ut.c
UNIT_SRC += src/osw_drv.c
UNIT_SRC += src/osw_drv_dummy.c
UNIT_SRC += src/osw_drv_target.c
UNIT_SRC += $(if $(wildcard $(PKG_CONFIG_SYSROOT_DIR)/usr/include/ccsp)$(wildcard $(UNIT_PATH)/inc/ccsp),src/osw_drv_wifihal_3_0.c,)
UNIT_SRC += src/osw_state.c
UNIT_SRC += src/osw_conf.c
UNIT_SRC += src/osw_confsync.c
UNIT_SRC += src/osw_types.c
UNIT_SRC += src/osw_bss_map.c
UNIT_SRC += src/osw_mux.c
UNIT_SRC += src/osw_tlv.c
UNIT_SRC += src/osw_tlv_merge.c
UNIT_SRC += src/osw_module.c
UNIT_SRC += src/osw_sta_chan_cap.c
UNIT_SRC += src/osw_stats_defs.c
UNIT_SRC += src/osw_stats.c
UNIT_SRC += src/osw_scan_sched.c
UNIT_SRC += src/osw_sta_cache.c
UNIT_SRC += src/osw_btm.c
UNIT_SRC += src/osw_rrm_meas.c
UNIT_SRC += src/osw_throttle.c
UNIT_SRC += src/osw_time.c
UNIT_SRC += src/osw_timer.c
UNIT_SRC += src/osw_util.c
UNIT_SRC += src/osw_ev.c
UNIT_SRC += src/osw_cqm.c
UNIT_SRC += src/osw_hostap_conf.c
UNIT_SRC += src/osw_hostap_common.c
UNIT_SRC += src/osw_wpas_conf.c
UNIT_SRC += src/osw_hostap.c
UNIT_SRC += src/osw_rrm.c
UNIT_SRC += src/osw_rrm_bcn_meas_rpt_cache.c
UNIT_SRC += src/osw_token.c
UNIT_SRC += src/osw_l2uf.c
UNIT_SRC += src/osw_defer_vif_down.c
UNIT_SRC += src/osw_diag.c

UNIT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -D_GNU_SOURCE

# FIXME: This isn't optimal, but the Wifi HAL doesn't really
# making this any easier by ifdefing functions and parts of
# structures. The drv_wifihal_3_0.c which is relying on this
# is supposed to support Wifi HAL 3.0 anyway. If this is
# tried to be run against older Wifi HAL it'll be missing a
# lot of symbols so dlsym() will fail anyway.
UNIT_CFLAGS += -DWIFI_HAL_VERSION_3

UNIT_LDFLAGS := -lpthread
UNIT_LDFLAGS += -lev
UNIT_LDFLAGS += -lm

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_DEPS := src/lib/module
UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/osa
UNIT_DEPS += src/lib/osn
UNIT_DEPS += src/lib/rq
UNIT_DEPS += src/lib/hostap2

ifneq (,$(wildcard $(TARGET_DIR)/usr/include/libnl3)$(LIBNL3_HEADERS))
UNIT_SRC += src/osw_drv_nl80211.c
UNIT_DEPS += src/lib/nl
UNIT_CFLAGS += $(LIBNL3_HEADERS)
endif

ifeq ($(BUILD_SHARED_LIB),y)
UNIT_DEPS += src/lib/opensync
endif
