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
# Platform Manager
#
###############################################################################
UNIT_NAME := pm
UNIT_TYPE := BIN
UNIT_DISABLE := $(if $(CONFIG_MANAGER_PM),n,y)

UNIT_SRC := src/pm_main.c

$(eval $(if $(CONFIG_PM_ENABLE_CLIENT_FREEZE),      UNIT_SRC += src/pm_client_freeze.c))
$(eval $(if $(CONFIG_PM_ENABLE_CLIENT_NICKNAME),    UNIT_SRC += src/pm_client_nickname.c))
$(eval $(if $(CONFIG_PM_ENABLE_LED),                UNIT_SRC += src/pm_led.c))
$(eval $(if $(CONFIG_PM_ENABLE_LM),                 UNIT_SRC += src/pm_lm.c))

ifeq ($(CONFIG_PM_ENABLE_TM),y)
UNIT_SRC += src/pm_tm.c
UNIT_SRC += src/pm_tm_ovsdb.c
endif

ifeq ($(CONFIG_PM_ENABLE_OBJM),y)
UNIT_SRC += src/pm_objm.c
UNIT_DEPS += src/lib/oms
endif

ifeq ($(CONFIG_PM_GW_OFFLINE_CFG),y)
UNIT_SRC += src/pm_gw_offline_cfg.c
endif

UNIT_CFLAGS  += -I$(UNIT_PATH)/inc
UNIT_LDFLAGS += -lev

UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/osp
UNIT_DEPS += src/lib/ovsdb
UNIT_DEPS += src/lib/schema
UNIT_DEPS += src/lib/module
