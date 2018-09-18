default: all

include build/color.mk

# Default configuration
include build/default.mk
include build/flags.mk

# Modify default configuration for external vendors
include build/vendor.mk

# Include target-specific configuration
include build/target-arch.mk

# Include vendor-specific configuration
include build/vendor-arch.mk

# KConfig configuration
include build/kconfig.mk

# Create CFG_DEFINES based on configuration options
include build/cfg-defines.mk

# Vendor defines
include build/vendor-defines.mk

ifneq ($(_OVERRIDE_MAIN_MAKEFILE),1)

.PHONY: all build_all clean distclean FORCE

all: build_all

world: build_all
	$(MAKE) openwrt_all

# Include architecture specific makefile
include $(ARCH_MK)

include build/flags2.mk
include build/dirs.mk
include build/verbose.mk
include build/version.mk
include build/git.mk
include build/unit-build.mk
include build/tags.mk
include build/app_install.mk
include build/ovsdb.mk
include build/rootfs.mk
include build/schema.mk
include build/devshell.mk
include build/help.mk
include build/doc.mk

build_all: workdirs schema-check unit-install

clean: unit-clean
	$(NQ) " $(call color_clean,clean)   [$(call COLOR_BOLD,workdir)] $(WORKDIR)"
	$(Q)$(RM) -r $(WORKDIR)

DISTCLEAN_TARGETS := clean

distclean: $(DISTCLEAN_TARGETS)
	$(NQ) " cleanup all artifacts"
	$(Q)$(RM) -r $(WORKDIRS) tags cscope.out files.idx .files.idx.dep

ifneq ($(filter-out $(OS_TARGETS),$(TARGET)),)
$(error Unsupported TARGET ($(TARGET)). Supported targets are: \
	$(COL_CFG_GREEN)$(OS_TARGETS)$(COL_CFG_NONE))
endif

# Include makefile for target-specific rules, if it exists
TARGET_MAKEFILE ?= $(VENDOR_DIR)/Makefile
-include $(TARGET_MAKEFILE)

# backward compatibility
SDK_DIR    ?= $(OWRT_ROOT)
SDK_ROOTFS ?= $(OWRT_ROOTFS)
INSTALL_ROOTFS_DIR ?= $(SDK_ROOTFS)
ifeq ($(INSTALL_ROOTFS_DIR),)
INSTALL_ROOTFS_DIR = $(WORKDIR)/rootfs-install
endif

endif # _OVERRIDE_MAIN_MAKEFILE

