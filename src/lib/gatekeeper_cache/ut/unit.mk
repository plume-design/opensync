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

UNIT_DISABLE:= $(if $(CONFIG_LIBGATEKEEPER_CACHE),n,y)

UNIT_NAME := test_gatekeeper_cache

UNIT_TYPE := TEST_BIN

UNIT_SRC := test_gatekeeper_cache_main.c
UNIT_SRC += test_gatekeeper_cache.c
UNIT_SRC += test_gatekeeper_cache_flush.c
UNIT_SRC += test_gatekeeper_cache_cmp.c
UNIT_SRC += test_gatekeeper_cache_ovsdb_app.c

# Add include path to the internal headers
UNIT_CFLAGS := -I$(UNIT_PATH)/../src
UNIT_CFLAGS += -Isrc/fsm/inc
UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)

UNIT_LDFLAGS := -lev -ljansson
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)


UNIT_DEPS := src/lib/log
UNIT_DEPS += src/lib/osa
UNIT_DEPS += src/lib/const
UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/unity
UNIT_DEPS += src/lib/gatekeeper_cache
UNIT_DEPS += src/lib/gatekeeper_msg
UNIT_DEPS += src/lib/fsm_policy
UNIT_DEPS += src/lib/network_metadata
UNIT_DEPS += src/lib/unit_test_utils
UNIT_DEPS += src/lib/gatekeeper_msg

# data/genmac.txt is copied automatically
