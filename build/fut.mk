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

FUT_PACK_NAME := $(TARGET)-$(shell $(call version-gen,make))
FUT_PACK_FILENAME := fut-$(FUT_PACK_NAME).tar.bz2
FUT_PACK_PATHNAME ?= $(IMAGEDIR)/$(FUT_PACK_FILENAME)

.PHONY: fut-store

fut-clean:
	$(NQ) "$(call color_install,clean) $(call color_profile,$(FUTDIR))"
	$(Q)rm -rf $(FUTDIR)

fut-store:
	$(NQ) "$(call color_install, create) $(FUT_PACK_FILENAME)"
	$(Q)$(TAR) -cjf $(FUT_PACK_PATHNAME) -C $(dir $(FUTDIR)) $(notdir $(FUTDIR))

fut-make: $(UNIT_ALL_FUT_UNITS)

fut: fut-clean
	$(Q)$(MAKE) fut-make
	$(Q)$(MAKE) fut-store
