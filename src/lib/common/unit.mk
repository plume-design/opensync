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
# Backtrace, utils, ...
#
###############################################################################
UNIT_NAME := common

# Template type:
UNIT_TYPE := LIB

UNIT_SRC := src/os_time.c
UNIT_SRC += src/util.c
UNIT_SRC += src/monitor.c
UNIT_SRC += src/os.c
UNIT_SRC += src/os_util.c
UNIT_SRC += src/os_exec.c
UNIT_SRC += src/memutil.c
UNIT_SRC += src/netutil.c
UNIT_SRC += src/sockaddr_storage.c
UNIT_SRC += src/iso3166.c
UNIT_SRC += src/os_send_raw.c
UNIT_SRC += src/os_llc_snap.c
UNIT_SRC += $(if $(CONFIG_OS_EV_TRACE), src/os_ev_trace.c)
UNIT_SRC += src/kv_parser.c
UNIT_SRC += $(if $(CONFIG_MEM_MONITOR), src/mem_monitor.c)

UNIT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -fasynchronous-unwind-tables
UNIT_CFLAGS += -Isrc/lib/osa/inc
UNIT_LDFLAGS := -rdynamic -ldl -ljansson -lrt

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

# Other units that this unit may depend on
UNIT_DEPS_CFLAGS := src/lib/log
UNIT_DEPS := src/lib/ds
UNIT_DEPS += src/lib/osa

