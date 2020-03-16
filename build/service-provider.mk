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

# Service Providers
#
# This sections defines service providers connection settings such as:
# - controller address
# - credentials
# - deployment profiles
# - rootfs: profile specific files, certificates, keys
#
# Environment variable SERVICE_PROVIDERS defines the list of service providers
# that will be included. If not defined this section is skipped. Value ALL
# can be used to include all providers found in the service-provider path.
# When using the ALL value, the providers are included in alphabetic order,
# to adjust the order the priority tokens can be specified before ALL
# example: SERVICE_PROVIDERS = "primary ALL"
#
# For each PROVIDER specified in SERVICE_PROVIDERS, a provider specific file is included
#   - service-provider/PROVIDER/build/provider.mk
# and a provider specific rootfs dir will be included for rootfs preparation
#   - service-provider/PROVIDER/rootfs
#
# The provider.mk can define some of these variables:
#
# VALID_IMAGE_DEPLOYMENT_PROFILES : append list of valid profile names
# CONTROLLER_ADDR
# BACKHAUL_SSID
# BACKHAUL_PASS
# PROVIDER_BACKHAUL_CREDS : space delimited list of SSID:PASS pairs
#   Note: old format was ';' delimited list of SSID;PASS pairs
#         and is converted automatically to the new format
#   All provider defined PROVIDER_BACKHAUL_CREDS are automatically appended
#   to MULTI_BACKHAUL_CREDS
# ROOTFS_PROFILE_COMPONENTS : additional, profile or provider specific rootfs components
#
# Note: profile names across all specified service providers must be unique otherwise
# the settings would get overwritten. If a duplicate profile is detected the build
# is aborted with an error

ALL_SERVICE_PROVIDERS := $(sort $(notdir $(wildcard service-provider/*)))

ifneq ($(SERVICE_PROVIDERS),)

# set initial value
BACKHAUL_SSID := ""
BACKHAUL_PASS := ""

define _sp_uniq
$(if $1,$(firstword $1) $(call _sp_uniq,$(filter-out $(firstword $1),$1)))
endef

ifneq ($(filter ALL,$(SERVICE_PROVIDERS)),)
# expand ALL token in service providers
SERVICE_PROVIDERS := $(patsubst ALL,$(ALL_SERVICE_PROVIDERS),$(SERVICE_PROVIDERS))
SERVICE_PROVIDERS := $(call _sp_uniq,$(SERVICE_PROVIDERS))
endif

# check if all specified service providers exists
$(foreach _SP,$(SERVICE_PROVIDERS),$(if $(wildcard service-provider/$(_SP)),,$(error Not found: service-provider/$(_SP))))

# include specified service providers rootfs dirs
ROOTFS_SERVICE_PROVIDERS_DIRS := \
    $(foreach _SP,$(SERVICE_PROVIDERS),$(wildcard service-provider/$(_SP)/rootfs))

define _sp_unquote
$(patsubst "%",%,$1)
endef

# convert old style delimiters (;;) to (:' ')
define _sp_delim
$(shell echo -n "$1" | sed ':A;s/;/:/;s/;/ /;tA')
endef

define include_sp
PROVIDER_BACKHAUL_CREDS :=
$(eval -include service-provider/$1/build/provider.mk)
_sp_multi_creds += $(call _sp_delim,$(call _sp_unquote,$(PROVIDER_BACKHAUL_CREDS)))
PROVIDER_BACKHAUL_CREDS :=
endef

# include all specified service providers
_sp_multi_creds :=
$(foreach _SP,$(SERVICE_PROVIDERS),$(eval $(call include_sp,$(_SP))))

# remove duplicates and trailing space
_sp_multi_creds := $(strip $(call _sp_uniq,$(_sp_multi_creds)))

# add quotes
MULTI_BACKHAUL_CREDS := "$(_sp_multi_creds)"


# check for duplicate profile names
ifneq ($(words $(VALID_IMAGE_DEPLOYMENT_PROFILES)),$(words $(sort $(VALID_IMAGE_DEPLOYMENT_PROFILES))))
$(error Duplicate deployment profiles: $(VALID_IMAGE_DEPLOYMENT_PROFILES))
endif

# check IMAGE_DEPLOYMENT_PROFILE
ifneq ($(filter-out $(VALID_IMAGE_DEPLOYMENT_PROFILES),$(IMAGE_DEPLOYMENT_PROFILE)),)
$(error TARGET=$(TARGET): Unsupported IMAGE_DEPLOYMENT_PROFILE ($(IMAGE_DEPLOYMENT_PROFILE)). \
        Supported profiles are: \
        $(COL_CFG_GREEN)$(VALID_IMAGE_DEPLOYMENT_PROFILES)$(COL_CFG_NONE))
endif

# check CONTROLLER_ADDR
ifeq ($(CONTROLLER_ADDR),)
$(error TARGET=$(TARGET): Please add IMAGE_DEPLOYMENT_PROFILE section for $(IMAGE_DEPLOYMENT_PROFILE))
endif

endif # SERVICE_PROVIDERS

service-provider/info:
	@echo
	@echo ALL_SERVICE_PROVIDERS='$(ALL_SERVICE_PROVIDERS)'
	@echo SERVICE_PROVIDERS='$(SERVICE_PROVIDERS)'
	@echo VALID_IMAGE_DEPLOYMENT_PROFILES='$(VALID_IMAGE_DEPLOYMENT_PROFILES)'
	@echo IMAGE_DEPLOYMENT_PROFILE='$(IMAGE_DEPLOYMENT_PROFILE)'
	@echo ROOTFS_PROFILE_COMPONENTS='$(ROOTFS_PROFILE_COMPONENTS)'
	@echo CONTROLLER_ADDR='$(CONTROLLER_ADDR)'
	@echo BACKHAUL_SSID='$(BACKHAUL_SSID)'
	@echo BACKHAUL_PASS='$(BACKHAUL_PASS)'
	@echo MULTI_BACKHAUL_CREDS='$(MULTI_BACKHAUL_CREDS)'
	@echo

