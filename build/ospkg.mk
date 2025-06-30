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

# ospkg is used for in-place upgrade
# enabled with: CONFIG_IN_PLACE_UPGRADE

BUILD_OSPKG_PREP_ROOTFS_HOOK ?= ospkg-prepare-rootfs
BUILD_OSPKG_PREP_PKG_HOOK    ?= ospkg-prepare-pkg
BUILD_OSPKG_PREINIT_HOOK     ?= ospkg-preinit-env
BUILD_OSPKG_SDK_ROOTFS_CLEAN ?= $(WORKDIR)/ospkg_rootfs_sdk_clean
BUILD_OSPKG_SDK_ROOTFS_DEST  ?= $(WORKDIR)/ospkg_rootfs_sdk_dest
BUILD_OSPKG_SDK_ROOTFS_DELTA ?= $(WORKDIR)/ospkg_rootfs_sdk_delta
BUILD_OSPKG_ROOTFS_DIR       ?= $(WORKDIR)/ospkg_rootfs
BUILD_OSPKG_PKG_INFO         ?= $(WORKDIR)/ospkg_info
BUILD_OSPKG_PKG_DIR          ?= $(WORKDIR)/ospkg_pkg
BUILD_OSPKG_PKG_FILE         ?= $(IMAGEDIR)/opensync-$(TARGET)-$(ROOTFS_PACK_VERSION).ospkg

# Order and target dir of build hooks when in-place upgrade is enabeld:
# 1. hooks/pre-install          on work/rootfs
# 2. hooks/ospkg-prepare-rootfs on sdk/rootfs
# 3. hooks/post-install         on work/ospkg_rootfs
# 4. hooks/ospkg-prepare-pkg    on work/ospkg_rootfs
# 5. hooks/ospkg-preinit-env    on sdk/rootfs

define ospkg_build_cmd
	@echo "ospkg: build package $(BUILD_OSPKG_PKG_FILE)"
	@# clean any artifacts from previous build
	$(Q)rm -rf $(BUILD_OSPKG_SDK_ROOTFS_CLEAN)
	$(Q)rm -rf $(BUILD_OSPKG_SDK_ROOTFS_DEST)
	$(Q)rm -rf $(BUILD_OSPKG_SDK_ROOTFS_DELTA)
	$(Q)rm -rf $(BUILD_OSPKG_ROOTFS_DIR)
	$(Q)rm -rf $(BUILD_OSPKG_PKG_INFO)
	$(Q)rm -rf $(BUILD_OSPKG_PKG_DIR)
	$(Q)rm -f  $(BUILD_OSPKG_PKG_FILE)
	@# run hooks: ospkg-prepare-rootfs
	$(Q)$(call rootfs_run_hooks,$(INSTALL_ROOTFS_DIR),$(BUILD_OSPKG_PREP_ROOTFS_HOOK))
	@# prepare two copies of the sdk roofs: a clean copy and a destination for install
	$(Q)rsync -a $(INSTALL_ROOTFS_DIR)/ $(BUILD_OSPKG_SDK_ROOTFS_CLEAN)/
	$(Q)rsync -a $(BUILD_OSPKG_SDK_ROOTFS_CLEAN)/ $(BUILD_OSPKG_SDK_ROOTFS_DEST)/
	@# apply post-install scripts
	$(MAKE) rootfs-install-to-target-dir INSTALL_ROOTFS_DIR=$(BUILD_OSPKG_SDK_ROOTFS_DEST)
	@# extract changes made by post-install hooks
	@# --compare-dest=DIR can be absolute or relative to DEST
	$(Q)rsync -a --compare-dest=../$$(basename $(BUILD_OSPKG_SDK_ROOTFS_CLEAN)) $(BUILD_OSPKG_SDK_ROOTFS_DEST)/ $(BUILD_OSPKG_SDK_ROOTFS_DELTA)/
	@# base opensync rootfs
	$(Q)rsync -a $(BUILD_ROOTFS_DIR)/ $(BUILD_OSPKG_ROOTFS_DIR)/
	@# apply changes, skip empty dirs
	$(Q)rsync -a --prune-empty-dirs $(BUILD_OSPKG_SDK_ROOTFS_DELTA)/ $(BUILD_OSPKG_ROOTFS_DIR)/
	@# mark deleted files/dirs with mknod c 0 0
	@# this is overlayfs method to identify deletes
	$(Q)rsync --dry-run -a -v --delete $(BUILD_OSPKG_SDK_ROOTFS_DEST)/ $(BUILD_OSPKG_SDK_ROOTFS_CLEAN)/ | \
		grep '^deleting' | while read DEL LINE; do \
		echo "ospkg mark delete: $$LINE"; \
		NAME=$${LINE%/}; \
		PARENT=$$(dirname "$$NAME"); \
		rm -rf "$(BUILD_OSPKG_ROOTFS_DIR)/$$NAME"; \
		mkdir -p "$(BUILD_OSPKG_ROOTFS_DIR)/$$PARENT"; \
		mknod "$(BUILD_OSPKG_ROOTFS_DIR)/$$NAME" c 0 0; \
		done
	@# append version to ospkg.info
	$(Q)( \
		VERSION_LONG=$$(cat $(BUILD_OSPKG_ROOTFS_DIR)/$(VERSION_STAMP_DIR)/.version); \
		VERSION_SHORT=$$(echo "$$VERSION_LONG" | cut -d' ' -f1); \
		BUILD_INFO=$$(echo "$$VERSION_LONG" | cut -d' ' -f2-); \
		SDK_COMMIT=$$(grep ^SDK_COMMIT: $(BUILD_OSPKG_ROOTFS_DIR)/$(VERSION_STAMP_DIR)/.versions 2>/dev/null | cut -d: -f2); \
		echo "PKG_VERSION=\"$$VERSION_SHORT\""; \
		echo "PKG_BUILD_INFO=\"$$BUILD_INFO\""; \
		echo "PKG_TARGET_MODEL=\"$(CONFIG_TARGET_MODEL)\""; \
		echo "PKG_SUPPORTED_MODELS=\"$(CONFIG_TARGET_MODEL)\""; \
		[ -z "$$SDK_COMMIT" ] || echo "PKG_SDK_COMMIT=\"$$SDK_COMMIT\""; \
	) >> $(BUILD_OSPKG_ROOTFS_DIR)/OSPKG_INFO/ospkg.info
	$(Q)cp $(BUILD_OSPKG_ROOTFS_DIR)/$(VERSION_STAMP_DIR)/.version $(BUILD_OSPKG_ROOTFS_DIR)/OSPKG_INFO/version.txt
	$(Q)cp $(BUILD_OSPKG_ROOTFS_DIR)/$(VERSION_STAMP_DIR)/.versions $(BUILD_OSPKG_ROOTFS_DIR)/OSPKG_INFO/versions.txt
	@# run hooks: ospkg-prepare-pkg
	$(Q)$(call rootfs_run_hooks,$(BUILD_OSPKG_ROOTFS_DIR),$(BUILD_OSPKG_PREP_PKG_HOOK))
	@# move ROOTFS_DIR/OSPKG_INFO/* to PKG_INFO/
	$(Q)mkdir -p $(BUILD_OSPKG_PKG_INFO)
	$(Q)cp -a $(BUILD_OSPKG_ROOTFS_DIR)/OSPKG_INFO/. $(BUILD_OSPKG_PKG_INFO)/
	$(Q)rm -rf $(BUILD_OSPKG_ROOTFS_DIR)/OSPKG_INFO
	@# create package: info + fs.sqfs to tgz
	$(Q)mkdir -p $(BUILD_OSPKG_PKG_DIR)
	$(Q)cp -a $(BUILD_OSPKG_PKG_INFO)/. $(BUILD_OSPKG_PKG_DIR)/
	$(Q)test -n "$(SDK_MKSQUASHFS_CMD)"
	$(SDK_MKSQUASHFS_CMD) $(BUILD_OSPKG_ROOTFS_DIR) $(BUILD_OSPKG_PKG_DIR)/fs.sqfs -no-progress $(SDK_MKSQUASHFS_ARGS) > $(WORKDIR)/ospkg-mksquashfs.log
	@# md5sum
	$(Q)cd $(BUILD_OSPKG_PKG_DIR) && md5sum $$(find . -type f) > md5sum.txt
	@# generate archive
	$(Q)tar czf $(BUILD_OSPKG_PKG_FILE) -C $(BUILD_OSPKG_PKG_DIR) .
	@# remove temporary copies
	$(Q)rm -rf $(BUILD_OSPKG_SDK_ROOTFS_CLEAN)
	$(Q)rm -rf $(BUILD_OSPKG_SDK_ROOTFS_DEST)
	$(Q)rm -rf $(BUILD_OSPKG_SDK_ROOTFS_DELTA)
endef

# encrypt .ospkg to .eospkg and .eospkg.key
BUILD_OSPKG_PKG_ENC_FILE ?= $(addsuffix .eospkg,$(basename $(BUILD_OSPKG_PKG_FILE)))
define ospkg_build_post_cmd
	@echo "ospkg: encrypt $(BUILD_OSPKG_PKG_FILE) -> $(BUILD_OSPKG_PKG_ENC_FILE)"
	@ENCKEY="$$(openssl rand -base64 32)" && \
		openssl enc -aes-256-cbc -pass pass:$${ENCKEY} -md sha256 -nosalt -in "$(BUILD_OSPKG_PKG_FILE)" -out "$(BUILD_OSPKG_PKG_ENC_FILE)" && \
		echo $${ENCKEY} > "$(BUILD_OSPKG_PKG_ENC_FILE).key" && \
		(cd $(dir $(BUILD_OSPKG_PKG_ENC_FILE)) && md5sum "$(notdir $(BUILD_OSPKG_PKG_ENC_FILE))" > "$(notdir $(BUILD_OSPKG_PKG_ENC_FILE)).md5.save")
endef

define ospkg_install_to_rootfs_cmd
	@echo "rootfs: install ospkg to $(INSTALL_ROOTFS_DIR)"
	@# copy unpacked package info + rootfs
	$(Q)mkdir -p $(INSTALL_ROOTFS_DIR)/ospkg/builtin/fs
	$(Q)cp -a $(BUILD_OSPKG_PKG_INFO)/. $(INSTALL_ROOTFS_DIR)/ospkg/builtin/.
	$(Q)cp -a $(BUILD_OSPKG_ROOTFS_DIR)/. $(INSTALL_ROOTFS_DIR)/ospkg/builtin/fs/.
	$(Q)sed -i 's/^PKG_FS_TYPE=.*$$/PKG_FS_TYPE="unpacked"/' $(INSTALL_ROOTFS_DIR)/ospkg/builtin/ospkg.info
	@# minimal target rootfs changes:
	@# .version*
	$(Q)mkdir -p $(INSTALL_ROOTFS_DIR)/$(VERSION_STAMP_DIR)
	$(Q)cp -a $(BUILD_OSPKG_ROOTFS_DIR)/$(VERSION_STAMP_DIR)/.version* $(INSTALL_ROOTFS_DIR)/$(VERSION_STAMP_DIR)
	$(Q)cp -a $(BUILD_OSPKG_ROOTFS_DIR)/.version* $(INSTALL_ROOTFS_DIR)/ 2>/dev/null || true
	@# prepare the preinit environment
	$(Q)mkdir -p $(INSTALL_ROOTFS_DIR)/lib
	$(Q)mkdir -p $(INSTALL_ROOTFS_DIR)/ospkg/tools
	$(Q)mkdir -p $(INSTALL_ROOTFS_DIR)/ospkg/scripts
	$(Q)mkdir -p $(INSTALL_ROOTFS_DIR)/ospkg/etc
	$(Q)mkdir -p $(INSTALL_ROOTFS_DIR)/ospkg/mnt
	$(Q)mkdir -p $(INSTALL_ROOTFS_DIR)/ospkg/data
	$(Q)cp -a $(BUILD_OSPKG_ROOTFS_DIR)/$(INSTALL_PREFIX)/tools/ospkg $(INSTALL_ROOTFS_DIR)/ospkg/tools
	$(Q)cp -a $(BUILD_OSPKG_ROOTFS_DIR)/$(INSTALL_PREFIX)/scripts/opensync_functions.sh $(INSTALL_ROOTFS_DIR)/ospkg/scripts/
	$(Q)cp -a $(BUILD_OSPKG_ROOTFS_DIR)/$(INSTALL_PREFIX)/scripts/ospkg_functions.sh $(INSTALL_ROOTFS_DIR)/ospkg/scripts/
	$(Q)cp -a $(BUILD_OSPKG_ROOTFS_DIR)/$(INSTALL_PREFIX)/scripts/functions.d $(INSTALL_ROOTFS_DIR)/ospkg/scripts/
	$(Q)cp -a $(BUILD_OSPKG_ROOTFS_DIR)/$(INSTALL_PREFIX)/scripts/ospkg.d $(INSTALL_ROOTFS_DIR)/ospkg/scripts/
	$(Q)cp -a $(BUILD_OSPKG_ROOTFS_DIR)/$(INSTALL_PREFIX)/etc/kconfig $(INSTALL_ROOTFS_DIR)/ospkg/etc/
	$(Q)sed -i '/^INSTALL_PREFIX=/s/=.*$$/=\/ospkg/' $(INSTALL_ROOTFS_DIR)/ospkg/scripts/opensync_functions.sh
	$(Q)ln -sf ../ospkg/scripts/opensync_functions.sh $(INSTALL_ROOTFS_DIR)/lib/opensync_functions.sh
	@# prepare mount points for overlay and rom
	$(Q)mkdir -p $(INSTALL_ROOTFS_DIR)/overlay
	$(Q)mkdir -p $(INSTALL_ROOTFS_DIR)/rom
	@# run hooks: ospkg-preinit-env
	$(Q)$(call rootfs_run_hooks,$(INSTALL_ROOTFS_DIR),$(BUILD_OSPKG_PREINIT_HOOK))
endef

.PHONY: ospkg-build ospkg-install-to-rootfs

ospkg-build:
	$(call ospkg_build_cmd)
	$(call ospkg_build_post_cmd)

ospkg-install-to-rootfs:
	$(call ospkg_install_to_rootfs_cmd)
