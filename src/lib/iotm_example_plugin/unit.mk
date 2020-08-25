# Copyright (c) 2020, Charter Communications Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Charter Communications Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
###############################################################################
#
# iotm example plugin library
#
###############################################################################
UNIT_NAME := iotm_example


# Template type.
# If compiled with clang, assume a native unit test target
# and build a static library
ifneq (,$(findstring clang,$(CC)))
	UNIT_TYPE := LIB
else
	UNIT_TYPE := SHLIB
	UNIT_DIR := lib
endif


UNIT_SRC := src/iotm_example_plugin.c

UNIT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -I$(TOP_DIR)/src/lib/common/inc/
UNIT_CFLAGS += -Isrc/iotm/inc


UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := -ljansson

UNIT_DEPS := src/qm/qm_conn
UNIT_DEPS += src/lib/ds
UNIT_DEPS += src/lib/ovsdb
UNIT_DEPS += src/lib/evsched
