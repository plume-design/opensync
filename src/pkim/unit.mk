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

##############################################################################
#
# PKI Manager
#
##############################################################################
UNIT_DISABLE := $(if $(CONFIG_MANAGER_PKIM),n,y)

UNIT_NAME := pkim

UNIT_TYPE := BIN

UNIT_SRC += src/pkim_cert.c
UNIT_SRC += src/pkim_main.c
UNIT_SRC += src/pkim_ovsdb.c

UNIT_CFLAGS += -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -I$(UNIT_PATH)/src
UNIT_CFLAGS += -I$(TOP_DIR)/src/lib/common/inc/
UNIT_CFLAGS += -I$(TOP_DIR)/src/lib/est/inc

UNIT_LDFLAGS += -lev
UNIT_LDFLAGS += -lcurl

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_DEPS    += src/lib/arena
UNIT_DEPS    += src/lib/common
UNIT_DEPS    += src/lib/est
UNIT_DEPS    += src/lib/osa
UNIT_DEPS    += src/lib/ovsdb
UNIT_DEPS    += src/lib/pjs
UNIT_DEPS    += src/lib/schema

$(eval $(call stam_generate,src/pkim_cert.dot))
