include build/color.mk

# Default configuration
include build/default.mk

# Modify default configuration for external vendors
include build/vendor.mk

# Include target-specific configuration
include build/target-arch.mk

# Include vendor-specific configuration
include build/vendor-arch.mk

# Create CFG_DEFINES based on configuration options
include build/cfg-defines.mk

all: build_all

world: build_all
	$(MAKE) openwrt_all

MAKEFLAGS := --no-print-directory

CP       = cp
MKDIR    = mkdir -p
DIRNAME  = dirname
CAT      = cat
SED      = sed
MV       = mv -f
RM       = rm -f
CHMOD    = chmod
INSTALL  = install -D
TAR      = tar
GREP     = grep

SRCEXT   = cpp
SRCEXT.c = c
DEPEXT   = d
OBJEXT   = o

SRCDIR   = src
WORKDIR  = work/$(TARGET)
OBJDIR   = $(WORKDIR)/obj
BINDIR   = $(WORKDIR)/bin
LIBDIR   = $(WORKDIR)/lib
BUILD_ROOTFS_DIR   = $(WORKDIR)/rootfs
APP_ROOTFS        ?= $(BUILD_ROOTFS_DIR)
IMAGEDIR = images
WORKDIRS = $(WORKDIR) $(OBJDIR) $(LIBDIR) $(BINDIR) $(BUILD_ROOTFS_DIR) $(IMAGEDIR)

CFLAGS   := -Wall -Wextra -Wno-unused-parameter -Wno-unused-label -Werror
CFLAGS   += $(CFG_DEFINES)
CFLAGS   += -fasynchronous-unwind-tables -rdynamic

# Below are global settings that need to be used
DEBUG    := -g
OPTIMIZE :=

LDFLAGS  := -rdynamic

# Include architecture specific makefile
include $(ARCH_MK)

# Controls whether complete compilation lines are printed or not
ifeq ($(V),1)
Q=
NQ=@echo
Q_NULL=
else
Q=@
NQ=@echo
Q_STDOUT = >/dev/null
Q_STDERR = 2>/dev/null
endif

CFLAGS := $(CFLAGS) $(OPTIMIZE) $(DEBUG) $(DEFINES) $(INCLUDES)
LIBS   := $(LIBS) -lpthread

TARGET_DEF := TARGET_$(shell echo -n "$(TARGET)" | tr -sc '[A-Za-z0-9]' _ | tr '[a-z]' '[A-Z]')
CFLAGS += -D$(TARGET_DEF) -DTARGET_NAME="\"$(TARGET)\""

.PHONY: build_all workdirs clean distclean FORCE

include build/version.mk
include build/git.mk
include build/unit-build.mk
include build/tags.mk
include build/app_install.mk
include build/ovsdb.mk
include build/rootfs.mk

$(WORKDIRS):
	$(Q)mkdir -p $@

workdirs: $(WORKDIRS)

build_all: workdirs schema-check unit-install # rootfs

clean: unit-clean
	$(NQ) " $(call color_clean,clean)   [$(call COLOR_BOLD,workdir)] $(WORKDIR)"
	$(Q)$(RM) -r $(WORKDIR)

.PHONY: schema-check
schema-check: 
	$(Q)build/schema.sh check || { echo "The OVS schema was changed but the version was not updated. Please run make schema-update"; exit 1; }

.PHONY: schema-update
schema-update:
	@build/schema.sh update

DISTCLEAN_TARGETS := clean

distclean: $(DISTCLEAN_TARGETS)
	$(NQ) " cleanup all artifacts"
	$(Q)$(RM) -r $(WORKDIRS) tags cscope.out files.idx .files.idx.dep

ifneq ($(filter-out $(OS_TARGETS),$(TARGET)),)
$(error Unsupported TARGET ($(TARGET)). Supported targets are: \
	$(COL_CFG_GREEN)$(OS_TARGETS)$(COL_CFG_NONE))
endif

# Include makefile for vendor-specific rules, if it exists
-include $(VENDOR_DIR)/Makefile

# backward compatibility
SDK_DIR    ?= $(OWRT_ROOT)
SDK_ROOTFS ?= $(OWRT_ROOTFS)
INSTALL_ROOTFS_DIR ?= $(SDK_ROOTFS)
ifeq ($(INSTALL_ROOTFS_DIR),)
INSTALL_ROOTFS_DIR = $(WORKDIR)/rootfs-install
endif

DEVSHELL ?= $(SHELL)
devshell:
	$(NQ) "make: running devshell TARGET=$(TARGET) DEVSHELL=$(DEVSHELL)"
	@PS1='TARGET=$(TARGET) $$PWD $$ ' $(DEVSHELL)
	$(NQ) "make: exit devshell TARGET=$(TARGET)"


help:
	$(NQ) "Makefile help"
	$(NQ) ""
	$(NQ) "Makefile commands:"
	$(NQ) "   all                       builds all enabled units"
	$(NQ) "   os                        builds device image based on specified SDK"
	$(NQ) "   tags                      Creates CTAGS for src directories"
	$(NQ) "   cscope                    Creates CSCOPE for the same directories as ctags"
	$(NQ) "   clean                     Removes generated, compiled objects"
	$(NQ) "   distclean                 Invokes clean and also cleans the ctags and cscope files"
	$(NQ) "   devshell                  Run shell with all environment variables set for TARGET"
	$(NQ) ""
	$(NQ) "Build Unit commands:"
	$(NQ) "   unit-all                  Build ALL active units"
	$(NQ) "   unit-install              Build and install ALL active units"
	$(NQ) "   unit-clean                Clean ALL active units"
	$(NQ) "   unit-list                 List ALL active units"
	$(NQ) ""
	$(NQ) "   UNIT_PATH/clean           Clean a single UNIT"
	$(NQ) "   UNIT_PATH/rclean          Clean a single UNIT and its dependencies"
	$(NQ) "   UNIT_PATH/compile         Compile a single UNIT and its dependencies"
	$(NQ) "   UNIT_PATH/install         Install UNIT products to target rootfs"
	$(NQ) ""
	$(NQ) "Control variables:"
	$(NQ) "   V                         make verbose level. (values: 0, 1)"
	$(NQ) "                               default = 0"
	$(NQ) "   TARGET                    Target identifier. See Supported targets."
	$(NQ) "                               default: $(DEFAULT_TARGET)"
	$(NQ) "                               current: $(TARGET)"
	$(NQ) "                             Supported targets:"
	@for x in $(OS_TARGETS); do echo "                               "$$x; done
	$(NQ) "   IMAGE_TYPE                squashfs (FLASH) or initramfs(BOOTP),"
	$(NQ) "                               default: $(DEFAULT_IMAGE_TYPE)"
	$(NQ) "   IMAGE_DEPLOYMENT_PROFILE  Supported deployment profiles:"
	@for x in $(VALID_IMAGE_DEPLOYMENT_PROFILES); do echo "                               "$$x; done
	$(NQ) ""

