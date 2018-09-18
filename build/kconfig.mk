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

KCONFIG_DEFAULT:=kconfig/targets/config_default
#KCONFIG_TARGET?=kconfig/targets/config_$(TARGET)

# For locally installed kconfiglib using PIP
export PATH:=${PATH}:${HOME}/.local/bin
export MENUCONFIG_STYLE:=
MENUCONFIG_STYLE+=list=fg:\#3b396f,bg:\#efefef
MENUCONFIG_STYLE+=body=fg:\#3b396f,bg:\#ffffff
MENUCONFIG_STYLE+=selection=fg:\#ffffff,bg:\#b4ad99
MENUCONFIG_STYLE+=path=fg:\#000000,bg:\#c6c6c6
MENUCONFIG_STYLE+=frame=fg:\#ffffff,bg:\#6168fd
MENUCONFIG_STYLE+=inv-list=fg:\#17e3ad,bg:\#efefef
MENUCONFIG_STYLE+=inv-selection=fg:\#17e3ad,bg:\#948d79
MENUCONFIG_STYLE+=text=list
MENUCONFIG_STYLE+=help=path
MENUCONFIG_STYLE+=separator=frame
MENUCONFIG_STYLE+=edit=path
MENUCONFIG_STYLE+=jump-edit=path


ifeq ($(wildcard $(KCONFIG_TARGET)),)
$(warning $(call COLOR_YELLOW,No kconfig for target $(TARGET).) Using default: $(KCONFIG_DEFAULT))
KCONFIG:=$(KCONFIG_DEFAULT)
else
KCONFIG:=$(KCONFIG_TARGET)
endif

include $(KCONFIG)

.PHONY: _kconfig_target_check
_kconfig_target_check:
	$(Q)[ -n "$(KCONFIG_TARGET)" ] || { echo "$(call COLOR_RED,ERROR:) KCONFIG_TARGET not set. Please define KCONFIG_TARGET for your target before calling menuconfig or oldconfig."; exit 1; }

.PHONY: _kconfig_update _kconfig_update_cleanup _kconfig_update_min _kconfig_update_h
_kconfig_update_cleanup:
	@echo "$(call COLOR_GREEN,Generating kconfig configuration: $(KCONFIG))"
	$(Q)sed -i -re '/# CONFIG_PLATFORM_IS_|# CONFIG_VENDOR_IS_/d' "$(KCONFIG)"

_kconfig_update_min:
# Write out minimized config
	@echo "$(call COLOR_GREEN,Generating minimized configuration: $(KCONFIG).diff)"
	$(Q)kconfig/diffconfig --config "$(KCONFIG)" --out "$(KCONFIG).diff" kconfig/Kconfig

_kconfig_update_h:
	@echo "$(call COLOR_GREEN,Generating kconfig header file: $(KCONFIG).h)"
	$(Q)KCONFIG_CONFIG="$(KCONFIG)" genconfig kconfig/Kconfig --header-path "$(KCONFIG).h"

_kconfig_update: _kconfig_update_cleanup _kconfig_update_min _kconfig_update_h

.PHONY: menuconfig _menuconfig
_menuconfig:
	echo $$MENUCONFIG_STYLE
	$(Q)KCONFIG_CONFIG="$(KCONFIG)" menuconfig kconfig/Kconfig

menuconfig: KCONFIG:=$(KCONFIG_TARGET)
menuconfig: _kconfig_target_check _menuconfig _kconfig_update

.PHONY: oldconfig _oldconfig
_oldconfig:
	$(Q)KCONFIG_CONFIG="$(KCONFIG)" oldconfig kconfig/Kconfig

oldconfig: KCONFIG:=$(KCONFIG_TARGET)
oldconfig: _kconfig_target_check _oldconfig _kconfig_update

.PHONY: refreshconfig _refreshconfig
_refreshconfig:
	$(Q)[ -e "$(KCONFIG).diff" ] || { echo "$(call COLOR_RED,ERROR:) Diffconfig \"$(KCONFIG).diff\" doesn't exist. Cannot refresh config."; exit 1; }
	@echo "$(call COLOR_YELLOW,Refreshing config file: $(KCONFIG))"
	$(Q)kconfig/diffconfig --unminimize --config "$(KCONFIG).diff" --out "$(KCONFIG)" kconfig/Kconfig

refreshconfig: KCONFIG:=$(KCONFIG_TARGET)
refreshconfig: _kconfig_target_check _refreshconfig _kconfig_update_cleanup _kconfig_update_h

.PHONY: update_default_config _update_defaul_config
_update_default_config:
	@echo "$(call COLOR_GREEN,Updating default kconfig: $(KCONFIG))"
	$(Q)KCONFIG_CONFIG="$(KCONFIG)" alldefconfig kconfig/Kconfig

update_default_config: KCONFIG:=$(KCONFIG_DEFAULT)
update_default_config: _update_default_config _kconfig_update

kconfiglib_install:
	pip3 install --user kconfiglib==10.7.0
