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
# OpenSync Platform low level API
#
##############################################################################

UNIT_NAME := osp
UNIT_TYPE := LIB

UNIT_CFLAGS += -I$(UNIT_PATH)/inc
UNIT_EXPORT_CFLAGS := -I$(UNIT_PATH)/inc

UNIT_SRC += $(if $(CONFIG_OSP_UNIT_DEFAULT),src/osp_unit_default.c)
UNIT_SRC += $(if $(CONFIG_OSP_LED),src/osp_led.c)
UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/log
UNIT_DEPS += src/lib/schema
UNIT_DEPS_CFLAGS += src/lib/target

ifeq ($(CONFIG_OSP_REBOOT_PSTORE),y)
UNIT_SRC += src/osp_reboot_pstore.c
UNIT_DEPS += src/lib/execsh
endif

ifeq ($(CONFIG_OSP_PS_PSFS),y)
UNIT_SRC += src/osp_ps_psfs.c
UNIT_DEPS += src/lib/psfs
endif

ifeq ($(CONFIG_OSP_OBJM_OBJMFS), y)
UNIT_SRC += src/osp_objm_objmfs.c
UNIT_DEPS += src/lib/objmfs
endif

UNIT_SRC += $(if $(CONFIG_OSP_L2SWITCH_NULL),src/osp_l2switch_null.c)
UNIT_SRC += $(if $(CONFIG_OSP_L2SWITCH_SWCONFIG),src/osp_l2switch_swconfig.c)
