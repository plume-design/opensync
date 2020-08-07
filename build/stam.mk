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

define _stam_generate
	$(if $(filter "$(STAM_NAME)",""),$(error Please define STAM_NAME before using stam generator))
	$(if $(filter "$(STAM_DOT_FILE)",""),$(error Please define STAM_DOT_FILE before using stam generator))
	$(if $(filter "$(STAM_DEST_DIR)",""),$(error Please define STAM_DEST_DIR before using stam generator))

	$(NQ) " $(call color_generate,stam)[$(call COLOR_BOLD,$(STAM_NAME))] $@"
	$(Q) src/lib/stam/tools/libstam_gen.py $(STAM_DOT_FILE) $(1) --file --dest $(STAM_DEST_DIR) $(EXTRA_OPTS) \
		$(if $(filter "$(STAM_DISABLE_ACTIONS_CHECKS)","y"),--disable-actions-checks) \
		$(if $(filter "$(STAM_DISABLE_TRANSITIONS_CHECKS)","y"),--disable-transitions-checks)
endef

define stam_generate_source
	$(call _stam_generate,--source)
endef

define stam_generate_header
	$(call _stam_generate,--header)
endef

define stam_generate
$(call stam_generate2,$(1),$(basename $(notdir $(1))))
endef

define stam_generate2

UNIT_PRE += $(UNIT_BUILD)/$(2)_stam.h
UNIT_PRE += $(UNIT_BUILD)/$(2)_stam.c
UNIT_SRC_TOP += $(UNIT_BUILD)/$(2)_stam.c

UNIT_CFLAGS += -I$(UNIT_BUILD)

UNIT_CLEAN += $(UNIT_BUILD)/$(2)_stam.h
UNIT_CLEAN += $(UNIT_BUILD)/$(2)_stam.c

UNIT_DEPS += src/lib/stam

$(UNIT_BUILD)/$(2)_stam.h: $(UNIT_PATH)/$(1)
	$(NQ) " $(call color_generate,stam_h)  [$(call COLOR_BOLD,$(2))] $$@"
	$(Q) src/lib/stam/tools/libstam_gen.py $(UNIT_PATH)/$(1) --file --dest "$(UNIT_BUILD)" --header

$(UNIT_BUILD)/$(2)_stam.c: $(UNIT_PATH)/$(1)
	$(NQ) " $(call color_generate,stam_c)  [$(call COLOR_BOLD,$(2))] $$@"
	$(Q) src/lib/stam/tools/libstam_gen.py $(UNIT_PATH)/$(1) --file --dest "$(UNIT_BUILD)" --source

endef
