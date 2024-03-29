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

# default target
TARGET ?= $(DEFAULT_TARGET)

# append list of all supported targets
OS_TARGETS += native
OS_TARGETS += bsal_sim
OS_TARGETS += alltargets

ifeq ($(TARGET),native)
ARCH = native
ARCH_MK = build/$(ARCH).mk
KCONFIG_TARGET := kconfig/targets/config_$(TARGET)
CPU_TYPE    := $(shell uname -m)
DIST_NAME   := $(shell if [ -e /etc/os-release ]; then . /etc/os-release; echo $$ID$$VERSION_ID; fi)
ifneq ($(DIST_NAME),)
WORKDIR  = work/$(TARGET)-$(CC)-$(DIST_NAME)-$(CPU_TYPE)
endif
endif

ifeq ($(TARGET),bsal_sim)
ARCH = native
ARCH_MK = build/$(ARCH).mk
KCONFIG_TARGET := kconfig/targets/config_$(TARGET)
CPU_TYPE    := $(shell uname -m)
DIST_NAME   := $(shell if [ -e /etc/os-release ]; then . /etc/os-release; echo $$ID$$VERSION_ID; fi)
ifneq ($(DIST_NAME),)
WORKDIR  = work/$(TARGET)-$(DIST_NAME)-$(CPU_TYPE)
endif
endif

ifeq ($(TARGET),alltargets)
ARCH = native
ARCH_MK = build/$(ARCH).mk
INCLUDE_LAYERS += $(wildcard vendor/* platform/*)
KCONFIG_TARGET := kconfig/targets/config_default
CPU_TYPE := $(shell uname -m)
DIST_NAME := $(shell if [ -e /etc/os-release ]; then . /etc/os-release; echo $$ID$$VERSION_ID; fi)
ifneq ($(DIST_NAME),)
WORKDIR = work/$(TARGET)-$(DIST_NAME)-$(CPU_TYPE)
endif
endif

