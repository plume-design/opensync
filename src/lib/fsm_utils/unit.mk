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
# FSM utils library
#
###############################################################################
UNIT_NAME := fsm_utils
UNIT_TYPE := LIB

UNIT_DISABLE := $(if $(CONFIG_LIB_FSM_UTILS),n,y)

UNIT_SRC := src/fsm_dpi_utils.c
UNIT_SRC += src/fsm_csum_utils.c
UNIT_SRC += src/fsm_packet_reinject_utils.c
UNIT_SRC += src/fsm_dns_tag.c
UNIT_SRC += src/fsm_dns_cache_utils.c
UNIT_SRC += src/fsm_ipc.c
UNIT_SRC += $(if $(CONFIG_OS_EV_TRACE), src/fsm_fn_trace.c)

UNIT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -Isrc/fsm/inc
UNIT_CFLAGS += -Isrc/lib/dns_parse/inc
UNIT_CFLAGS += -Isrc/lib/imc/inc
UNIT_LDFLAGS :=

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_DEPS := src/lib/const
UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/ds
UNIT_DEPS += src/lib/log
UNIT_DEPS += src/lib/ustack
UNIT_DEPS += src/lib/nf_utils
UNIT_DEPS += src/lib/fsm_policy
UNIT_DEPS += src/lib/network_metadata
UNIT_DEPS += src/lib/gatekeeper_cache
UNIT_DEPS += src/lib/neigh_table
UNIT_DEPS += src/lib/dpi_stats
UNIT_DEPS += src/lib/osn
UNIT_DEPS += $(if $(CONFIG_FSM_IPC_USE_OSBUS), src/lib/osbus)
