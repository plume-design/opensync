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
# libopensync: global shared library composed of all static libraries
#
###############################################################################

UNIT_NAME := opensync

OPENSYNC_LIB_NAME := libopensync.so

ifneq ($(BUILD_SHARED_LIB),y)

UNIT_DISABLE := y

else

UNIT_TYPE := SHLIB

UNIT_DIR := lib

# UNIT_DEPS := $(UNIT_ALL_LIB_UNITS)
# UNIT_ALL_LIB_UNITS isn't optimal because it includes also libs that
# no bin is dependent upon, so we need to build the list of
# dependent lib units from all bin units

# get all bin deps
BIN_DEPS := $(foreach BIN,$(UNIT_ALL_BIN_UNITS) $(UNIT_ALL_TEST_BIN_UNITS),$(sort $(DEPS_$(BIN))))
# $(sort) also removes duplicates
BIN_DEPS := $(sort $(BIN_DEPS))
# only interested in deps of lib type
UNIT_DEPS := $(foreach DEP,$(BIN_DEPS),$(if $(filter LIB,$(UNIT_TYPE_$(DEP))),$(DEP)))
UNIT_DEPS += src/lib/version
UNIT_DEPS := $(sort $(UNIT_DEPS))


# library file names
LIB_FILES := $(foreach LIB,$(UNIT_DEPS),$(UNIT_FILES_$(LIB)))

UNIT_LDFLAGS := -Wl,--whole-archive $(LIB_FILES) -Wl,--no-whole-archive

# By default rpath libraries cannot be overridden through
# LD_LIBRARY_PATH. To ease development and testing change
# that so LD_LIBRARY_PATH can override the absolute rpath
# paths. All modern standard C libraries support this.
UNIT_BIN_LDFLAGS += -Wl,--enable-new-dtags

UNIT_BIN_LDFLAGS += -Wl,-rpath=$(INSTALL_PREFIX)/$(UNIT_DIR) -lopensync

UNIT_BIN_LDFLAGS += $(foreach DEP,$(sort $(UNIT_DEPS)),$(LDFLAGS_$(DEP)))

$(UNIT_ALL_BIN_FILES): $(call UNIT_MARK_FILE,$(UNIT_PATH))
$(UNIT_ALL_TEST_BIN_FILES): $(call UNIT_MARK_FILE,$(UNIT_PATH))

endif
