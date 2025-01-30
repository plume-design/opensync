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
# Opensync Wireless / One Wifi Core
#
###############################################################################
UNIT_DISABLE := $(if $(CONFIG_OW),n,y)

UNIT_NAME := ow
UNIT_TYPE := SHLIB
UNIT_DIR := lib

UNIT_SRC += src/ow_core.c
UNIT_SRC += src/ow_core_thread.c
UNIT_SRC += src/ow_conf.c
UNIT_SRC += src/ow_conf_barrier.c
UNIT_SRC += src/ow_frm_inject_file.c
UNIT_SRC += $(if $(wildcard $(PKG_CONFIG_SYSROOT_DIR)/usr/include/ccsp)$(wildcard $(UNIT_PATH)/inc/ccsp),src/ow_webconfig.c,)
UNIT_SRC += src/ow_ovsdb.c
UNIT_SRC += src/ow_ovsdb_ms.c
UNIT_SRC += src/ow_ovsdb_cconf.c
UNIT_SRC += src/ow_ovsdb_stats.c
UNIT_SRC += src/ow_ovsdb_dfs_backup.c
UNIT_SRC += src/ow_ovsdb_csa.c
UNIT_SRC += src/ow_ovsdb_wps.c
UNIT_SRC += src/ow_sigalrm.c
UNIT_SRC += src/ow_ovsdb_steer.c
UNIT_SRC += src/ow_ovsdb_mld_onboard.c
UNIT_SRC += src/ow_steer.c
UNIT_SRC += src/ow_steer_sta.c
UNIT_SRC += src/ow_steer_candidate_list.c
UNIT_SRC += src/ow_steer_policy.c
UNIT_SRC += src/ow_steer_policy_stack.c
UNIT_SRC += src/ow_steer_policy_bss_filter.c
UNIT_SRC += src/ow_steer_policy_chan_cap.c
UNIT_SRC += src/ow_steer_policy_force_kick.c
UNIT_SRC += src/ow_steer_policy_pre_assoc.c
UNIT_SRC += src/ow_acl_kick.c
UNIT_SRC += src/ow_demo_stats.c
UNIT_SRC += src/ow_stats_conf.c
UNIT_SRC += src/ow_stats_conf_file.c
UNIT_SRC += src/ow_ev_timer.c
UNIT_SRC += src/ow_steer_policy_snr_level.c
UNIT_SRC += src/ow_steer_policy_snr_xing.c
UNIT_SRC += src/ow_steer_executor_action.c
UNIT_SRC += src/ow_steer_executor.c
UNIT_SRC += src/ow_steer_executor_action_acl.c
UNIT_SRC += src/ow_steer_executor_action_deauth.c
UNIT_SRC += src/ow_steer_executor_action_btm.c
UNIT_SRC += src/ow_sta_channel_override.c
UNIT_SRC += src/ow_sta_log_snr.c
UNIT_SRC += src/ow_state_watchdog.c
UNIT_SRC += src/ow_dfs_backup.c
UNIT_SRC += src/ow_dfs_chan_clip.c
UNIT_SRC += src/ow_wps.c
UNIT_SRC += src/ow_xphy_csa_conf.c
UNIT_SRC += src/ow_xphy_csa_ovsdb.c
UNIT_SRC += src/ow_steer_bm.c
UNIT_SRC += src/ow_steer_bm_policy_hwm_2g.c
UNIT_SRC += src/ow_steer_candidate_assessor.c
UNIT_SRC += src/ow_steer_policy_btm_response.c
UNIT_SRC += src/ow_l2uf_kick.c
UNIT_SRC += src/ow_steer_local_neigh.c
UNIT_SRC += src/ow_mbss_prefer_nonhidden.c
UNIT_SRC += src/ow_telog.c
UNIT_SRC += src/ow_radar_next_channel.c

UNIT_EXPORT_CFLAGS += -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -D_GNU_SOURCE
UNIT_DEPS += src/lib/osw

# ow_drv_target (to be removed eventually):
UNIT_DEPS += src/lib/target
UNIT_DEPS += src/lib/schema
UNIT_DEPS += src/lib/ovsdb
UNIT_DEPS += src/lib/datapipeline
UNIT_DEPS += src/lib/timevt
