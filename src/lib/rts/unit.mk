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
# Library to enable runtime scanner
#
###############################################################################
UNIT_NAME := rts

UNIT_TYPE := STATIC_LIB

UNIT_SRC := src/rts.c
UNIT_SRC += src/rts_buffer.c
UNIT_SRC += src/rts_config.c
UNIT_SRC += src/rts_mpmc.c
UNIT_SRC += src/rts_slob.c
UNIT_SRC += src/rts_vm.c

UNIT_CFLAGS += -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -fPIC

# RTS stuff
RTS_MAJOR = $(shell awk '/MAJOR/ {print $$3}' $(UNIT_PATH)/.version)
RTS_MINOR = $(shell awk '/MINOR/ {print $$3}' $(UNIT_PATH)/.version)
RTS_PATCH = $(shell awk '/PATCH/ {print $$3}' $(UNIT_PATH)/.version)

UNIT_CFLAGS += -DRTS_MAJOR=$(RTS_MAJOR)
UNIT_CFLAGS += -DRTS_MINOR=$(RTS_MINOR)
UNIT_CFLAGS += -DRTS_PATCH=$(RTS_PATCH)

ifeq ($(CONFIG_WALLEYE_RTS_RELEASE_BUILD),y)
UNIT_CFLAGS += -DNDEBUG # Disable asserts
UNIT_CFLAGS += -O2 -nostdlib -nodefaultlibs -fno-stack-protector # optimization
endif

UNIT_DEPS := src/lib/log

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)

UNIT_DEPS := src/lib/common
UNIT_DEPS := src/lib/log
