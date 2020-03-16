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


ifeq ($(ROOTFS_SOURCE_DIRS),)
# default rootfs layers: core/rootfs platform/roots vendor/rootfs
ifneq ($(wildcard rootfs),)
ROOTFS_SOURCE_DIRS += rootfs
endif
ifneq ($(wildcard $(PLATFORM_DIR)/rootfs),)
ROOTFS_SOURCE_DIRS += $(PLATFORM_DIR)/rootfs
endif
ifneq ($(INCLUDE_LAYERS),)
ROOTFS_SOURCE_DIRS += $(foreach _LAYER,$(INCLUDE_LAYERS),$(_LAYER)/rootfs )
endif
ifneq ($(wildcard $(VENDOR_DIR)/rootfs),)
ROOTFS_SOURCE_DIRS += $(VENDOR_DIR)/rootfs
endif
ifneq ($(ROOTFS_SERVICE_PROVIDERS_DIRS),)
# include specified service providers dirs
ROOTFS_SOURCE_DIRS += $(ROOTFS_SERVICE_PROVIDERS_DIRS)
endif
endif

# default ROOTFS_COMPONENTS defined in default.mk

# set ROOTFS_KCONFIG_COMPONENTS which is part of default ROOTFS_COMPONENTS
# * example konfig component: CONFIG_ABC=y -> kconfig/ABC
ROOTFS_KCONFIG_COMPONENTS := \
	$(foreach V, $(.VARIABLES),\
		$(if $(findstring __CONFIG_,__$(V)),\
			$(if $(filter y,$($(V))),\
				kconfig/$(subst CONFIG_,,$(V)))))

ROOTFS_HOOK_ENV += INSTALL_PREFIX=$(INSTALL_PREFIX)
ROOTFS_HOOK_ENV += OPENSYNC_LIB_NAME=$(OPENSYNC_LIB_NAME)

ifeq ($(V),1)
TARV=-v
else
TARV=
endif

ROOTFS_TAR_TRANSFORM += --transform=s,INSTALL_PREFIX,$(INSTALL_PREFIX),

ifeq ($(CONFIG_REMAP_LEGACY),y)
ifneq ($(ROOTFS_LEGACY_PREFIX),$(INSTALL_PREFIX))
ifneq ($(ROOTFS_LEGACY_PREFIX),)
export ROOTFS_LEGACY_PREFIX
ROOTFS_TAR_TRANSFORM += --transform=s,$(ROOTFS_LEGACY_PREFIX),$(INSTALL_PREFIX),
define rootfs_target_remove_legacy
	$(Q)echo "$(call color_install,remove) legacy $(call color_profile,$(INSTALL_ROOTFS_DIR)$(ROOTFS_LEGACY_PREFIX))"
	$(Q)rm -rf $(INSTALL_ROOTFS_DIR)$(ROOTFS_LEGACY_PREFIX)
endef
define rootfs_prepare_legacy
	$(NQ) "$(call color_install,symlink) legacy $(call color_profile,\
		$(BUILD_ROOTFS_DIR)$(ROOTFS_LEGACY_PREFIX) ->\
		$(BUILD_ROOTFS_DIR)$(INSTALL_PREFIX))"
	mkdir -p $(shell dirname $(BUILD_ROOTFS_DIR)$(ROOTFS_LEGACY_PREFIX))
	ln -f -s -T $(shell realpath $(INSTALL_PREFIX) --relative-to=`dirname $(ROOTFS_LEGACY_PREFIX)`)\
		$(BUILD_ROOTFS_DIR)$(ROOTFS_LEGACY_PREFIX)
endef
define rootfs_remap_legacy
	$(NQ) "$(call color_install,remap) legacy $(ROOTFS_LEGACY_PREFIX) -> $(INSTALL_PREFIX)"
	$(Q)grep -I -l -r $(ROOTFS_LEGACY_PREFIX) $(BUILD_ROOTFS_DIR) | sed "s/^/    /"
	$(Q)grep -I -l -r $(ROOTFS_LEGACY_PREFIX) $(BUILD_ROOTFS_DIR) | xargs -r sed -i s,$(ROOTFS_LEGACY_PREFIX),$(INSTALL_PREFIX),g
endef
endif
endif
endif

# $(1) = source rootfs base dir
# $(2) = component (profile or target dir)
define rootfs_prepare_dir
	$(Q)if [ -d $(1)/$(2) ]; then \
		echo "$(call color_install,prepare) rootfs $(call color_profile,$(1)/$(2)) -> $(BUILD_ROOTFS_DIR)"; \
		(cd $(1)/$(2) && tar cf - . --exclude=.keep) | (cd $(BUILD_ROOTFS_DIR) && tar xf - $(TARV) $(ROOTFS_TAR_TRANSFORM)); \
		build/templates.py --process-rootfs $(BUILD_ROOTFS_DIR); \
	fi

endef

define rootfs_prepare
	$(foreach DIR,$(ROOTFS_SOURCE_DIRS),\
		$(foreach COMPONENT,$(ROOTFS_COMPONENTS),\
			$(call rootfs_prepare_dir,$(DIR),$(COMPONENT))))
endef

# Checks for rootfs kconfig layer conflicts
define rootfs_prepare_kconfig_check
	$(eval ROOTFS_KCONFIG_CONFLICTS += \
		$(shell [ -d $(1) ] && cd $(1) && find $(ROOTFS_KCONFIG_COMPONENTS) -type f 2>/dev/null | cut -d/ -f3- | sort | uniq -d))
	$(foreach CONFLICT,$(ROOTFS_KCONFIG_CONFLICTS),\
		$(warning Conflict in $(1)/: $(shell cd $(1); find $(ROOTFS_KCONFIG_COMPONENTS) -type f -path */$(CONFLICT) 2>/dev/null)))
	$(if $(strip $(ROOTFS_KCONFIG_CONFLICTS)),\
		$(error Found conflicts in kconfig rootfs layer))
endef

define rootfs_prepare_check
	$(foreach DIR,$(ROOTFS_SOURCE_DIRS),\
		$(call rootfs_prepare_kconfig_check,$(DIR)))
endef

define rootfs_install_to_target
	$(call rootfs_target_remove_legacy)
	$(Q)echo "$(call color_install,install) rootfs $(call color_profile,$(BUILD_ROOTFS_DIR) => $(INSTALL_ROOTFS_DIR))"
	$(Q)if [ -L $(INSTALL_ROOTFS_DIR)$(INSTALL_PREFIX) ]; then rm $(INSTALL_ROOTFS_DIR)$(INSTALL_PREFIX); fi
	$(Q)if [ -L $(INSTALL_ROOTFS_DIR)$(ROOTFS_LEGACY_PREFIX) ]; then rm $(INSTALL_ROOTFS_DIR)$(ROOTFS_LEGACY_PREFIX); fi
	$(Q)cp --remove-destination --archive $(BUILD_ROOTFS_DIR)/. $(INSTALL_ROOTFS_DIR)/.

endef

# $1 = target rootfs dir
# $2 = script path-name (post-install)
define rootfs_run_hook_script
	$(Q)if [ -x "$2" ]; then \
		echo "$(call color_install,hooks) rootfs $(call color_profile,$2) in $1"; \
		$(ROOTFS_HOOK_ENV) $2 $1; \
	fi
endef

# $1 = target rootfs dir
# $2 = hooks dir
# $3 = script-name (post-install)
define rootfs_run_hooks_in_dir
	$(call rootfs_run_hook_script,$1,"$2/$3")
	$(call rootfs_run_hook_script,$1,"$2/$3.$(TARGET)")

endef

# $1 = target rootfs dir
# $2 = source rootfs base dir
# $3 = component (profile or target dir)
# $4 = script-name (post-install)
define rootfs_run_hooks_in
	$(call rootfs_run_hooks_in_dir,$1,$2/hooks/$3,$4)
endef

# $1 = target rootfs dir
# $2 = script-name (post-install)
define rootfs_run_hooks
	$(foreach DIR,$(ROOTFS_SOURCE_DIRS),\
		$(foreach COMPONENT,$(ROOTFS_COMPONENTS),\
			$(call rootfs_run_hooks_in,$1,$(DIR),$(COMPONENT),$2)))
endef

ROOTFS_PREPARE_HOOK_SCRIPT ?= pre-install
ROOTFS_INSTALL_HOOK_SCRIPT ?= post-install

define rootfs_run_hooks_prepare
	$(if $(ROOTFS_PREPARE_HOOK_SCRIPT),$(call rootfs_run_hooks,$(BUILD_ROOTFS_DIR),$(ROOTFS_PREPARE_HOOK_SCRIPT)))
endef

define rootfs_run_hooks_install
	$(if $(ROOTFS_INSTALL_HOOK_SCRIPT),$(call rootfs_run_hooks,$(INSTALL_ROOTFS_DIR),$(ROOTFS_INSTALL_HOOK_SCRIPT)))
endef

.PHONY: rootfs rootfs-make rootfs-prepare rootfs-install rootfs-install-only
.PHONY: rootfs-prepare-prepend rootfs-prepare-main rootfs-prepare-append
.PHONY: rootfs-install-prepend rootfs-install-main rootfs-install-append

# rootfs: create rootfs in work-area (BUILD_ROOTFS_DIR)
#   - unit-install: build and install bins and libs
#   - ovsdb-create: create ovsdb
#   - rootfs-prepare: copy source rootfs skeleton

rootfs-clean:
	$(NQ) "$(call color_install,clean) plume-rootfs $(call color_profile,$(BUILD_ROOTFS_DIR))"
	$(Q)rm -rf $(BUILD_ROOTFS_DIR)

rootfs-make: build_all rootfs-prepare ovsdb-create

rootfs: rootfs-clean
	$(MAKE) rootfs-make

# empty targets -prepend and -append allow inserting additional commands
rootfs-prepare-prepend: workdirs

rootfs-prepare-main: rootfs-prepare-prepend
	$(call rootfs_prepare_legacy)
	$(call rootfs_prepare)
	$(call rootfs_prepare_check)
	$(Q)$(call rootfs-version-stamp,$(BUILD_ROOTFS_DIR))
	$(Q)$(call rootfs-kconfig-env-file,$(BUILD_ROOTFS_DIR))
	$(call rootfs_run_hooks_prepare)
	$(call rootfs_remap_legacy)

rootfs-prepare-append: rootfs-prepare-main

rootfs-prepare: rootfs-prepare-prepend rootfs-prepare-main rootfs-prepare-append

# plume-rootfs-pack = plume-only part of rootfs (work/target/rootfs)
plume-rootfs-pack: rootfs plume-rootfs-pack-only

ifeq ($(PLUME_ROOTFS_PACK_VERSION),)
PLUME_ROOTFS_PACK_VERSION := $(shell $(call version-gen,make,$(PLUME_ROOTFS_PACK_VER_OPT)))
endif
PLUME_ROOTFS_PACK_FILENAME ?= plume-rootfs-$(TARGET)-$(PLUME_ROOTFS_PACK_VERSION).tgz
PLUME_ROOTFS_PACK_PATHNAME ?= $(IMAGEDIR)/$(PLUME_ROOTFS_PACK_FILENAME)

plume-rootfs-pack-only: workdirs
	$(NQ) "$(call color_install,pack) plume-rootfs $(call color_profile,$(BUILD_ROOTFS_DIR) => $(PLUME_ROOTFS_PACK_PATHNAME))"
	$(Q)tar czf $(PLUME_ROOTFS_PACK_PATHNAME) -C $(BUILD_ROOTFS_DIR) .

# optional include plume-rootfs-pack in rootfs:
ifeq ($(BUILD_PLUME_ROOTFS_PACK),y)
rootfs: plume-rootfs-pack-only
endif

# rootfs-install:
#   - copy work-area rootfs to INSTALL_ROOTFS_DIR (can be SDK_ROOTFS)
#   - run post-install hooks on INSTALL_ROOTFS_DIR

.PHONY: rootfs-install-dir
rootfs-install-dir:
	$(Q)mkdir -p $(INSTALL_ROOTFS_DIR)

rootfs-install-prepend: rootfs-install-dir

rootfs-copy-only:
	$(call rootfs_install_to_target)

rootfs-post-install-hooks-only:
	$(call rootfs_run_hooks_install)

rootfs-install-main: rootfs-install-prepend
	$(call rootfs_install_to_target)
	$(call rootfs_run_hooks_install)

rootfs-install-append: rootfs-install-main

rootfs-install-only: rootfs-install-prepend rootfs-install-main rootfs-install-append

rootfs-install: rootfs
	$(MAKE) rootfs-install-only


