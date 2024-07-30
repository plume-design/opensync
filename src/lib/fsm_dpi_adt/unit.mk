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
# fsm dpi client plugin for ADT
#
###############################################################################
UNIT_NAME := fsm_dpi_adt

UNIT_DISABLE := $(if $(CONFIG_MANAGER_FSM), n, y)

ifeq ($(CONFIG_FSM_NO_DSO),y)
    UNIT_TYPE := LIB
else
    UNIT_TYPE := SHLIB
    UNIT_DIR := lib
endif

UNIT_SRC := src/fsm_dpi_adt.c
UNIT_SRC += src/dpi_adt.c
UNIT_SRC += src/fsm_dpi_adt_cache.c
UNIT_SRC += src/fsm_dpi_adt_cache_internal.c

UNIT_CFLAGS := -I$(UNIT_PATH)/inc

# This is REQUIRED so we can find libfsm_dpi_client.so
UNIT_LDFLAGS += -Wl,-rpath=$(INSTALL_PREFIX)/$(UNIT_DIR)

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)

UNIT_DEPS := src/lib/const
UNIT_DEPS += src/lib/dns_cache
UNIT_DEPS += src/lib/fsm_dpi_client
UNIT_DEPS += src/lib/fsm_policy
UNIT_DEPS += src/lib/fsm_utils
UNIT_DEPS += src/lib/log
UNIT_DEPS += src/lib/network_metadata
UNIT_DEPS += src/lib/policy_tags
UNIT_DEPS += src/lib/protobuf
