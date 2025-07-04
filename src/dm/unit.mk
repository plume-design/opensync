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
# Diagnostics Manager
#
###############################################################################
UNIT_DISABLE := $(if $(CONFIG_MANAGER_DM),n,y)

UNIT_NAME := dm

# Template type:
UNIT_TYPE := BIN

# List of source files
UNIT_SRC := src/dm.c
UNIT_SRC += src/dm_cli.c
UNIT_SRC += src/dm_ovsdb.c
UNIT_SRC += src/dm_hook.c
UNIT_SRC += src/dm_manager.c
UNIT_SRC += src/statem.c
UNIT_SRC += src/dm_reboot.c
UNIT_SRC += src/dm_chkmem.c
ifeq ($(CONFIG_DM_OSYNC_CRASH_REPORTS),y)
UNIT_SRC += src/dm_crash.c
endif
UNIT_SRC += src/dm_mod.c
UNIT_SRC += src/dm_sipalg.c
UNIT_SRC += $(if $(CONFIG_REVSSH_ENABLED), src/dm_revssh.c)
UNIT_SRC += src/dm_no_reboot.c

# Unit specific CFLAGS
UNIT_CFLAGS := -I$(UNIT_PATH)/inc

UNIT_LDFLAGS := -lev
UNIT_LDFLAGS += -ljansson

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

# Other units that this unit may depend on
UNIT_DEPS := src/lib/common
UNIT_DEPS += src/lib/ds
UNIT_DEPS += src/lib/osa
UNIT_DEPS += src/lib/ovsdb
UNIT_DEPS += src/lib/schema
UNIT_DEPS += src/lib/mosqev
UNIT_DEPS += src/lib/tailf
UNIT_DEPS += src/lib/target
UNIT_DEPS += src/lib/const
UNIT_DEPS += src/lib/module
UNIT_DEPS += src/lib/evx
UNIT_DEPS += src/lib/pasync
UNIT_DEPS += $(if $(CONFIG_REVSSH_ENABLED), src/lib/revssh)
UNIT_DEPS += src/lib/reboot_flags

UNIT_DEPS_CFLAGS += src/lib/version
