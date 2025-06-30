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
UNIT_SRC += $(if $(CONFIG_OSP_LED),src/osp_led.c,src/osp_led_null.c)
UNIT_SRC += $(if $(CONFIG_OSP_LED),src/osp_led_tgt.c)
UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/log
UNIT_DEPS += src/lib/schema
UNIT_DEPS_CFLAGS += src/lib/target

UNIT_SRC += $(if $(CONFIG_OSP_UPG_NULL),src/osp_upgrade_null.c)
UNIT_SRC += $(if $(CONFIG_OSP_UPG_GEN),src/osp_upgrade_generic.c)
UNIT_SRC += $(if $(CONFIG_OSP_DL_NULL),src/osp_dl_null.c)
UNIT_SRC += $(if $(CONFIG_OSP_DL_CURL),src/osp_dl_curl.c)
UNIT_EXPORT_LDFLAGS += $(if ($CONFIG_OSP_DL_CURL),-lcurl -lssl)

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

ifeq ($(CONFIG_OSP_SEC_OPENSSL),y)
UNIT_SRC += src/osp_sec_openssl.c
UNIT_SRC += $(if $(CONFIG_OSP_SEC_KEY_DEFAULT),src/osp_sec_key.c)
endif

UNIT_SRC += $(if $(CONFIG_OSP_L2SWITCH_NULL),src/osp_l2switch_null.c)
UNIT_SRC += $(if $(CONFIG_OSP_L2SWITCH_SWCONFIG),src/osp_l2switch_swconfig.c)

UNIT_SRC += $(if $(CONFIG_OSP_L2UF_NULL),src/osp_l2uf_null.c)
UNIT_SRC += $(if $(CONFIG_OSP_L2UF_LIBPCAP),src/osp_l2uf_pcap.c)

UNIT_SRC += $(if $(CONFIG_OSP_BLE_NULL),src/osp_ble_null.c)

ifeq ($(CONFIG_OSP_OTBR_NULL),y)
UNIT_SRC += src/osp_otbr_null.c
UNIT_DEPS += src/lib/osn
else ifeq ($(CONFIG_OSP_OTBR_CLI_LIB),y)
UNIT_SRC += src/osp_otbr_cli_lib.c
UNIT_DEPS += src/lib/osn
UNIT_DEPS += src/lib/otbr_cli
UNIT_DEPS += src/lib/const
endif

UNIT_SRC += src/osp_temp.c
UNIT_SRC += src/osp_temp_srcs.c

ifeq ($(CONFIG_OSP_PKI_PS),y)
UNIT_SRC += src/osp_pki_ps.c
UNIT_DEPS += src/lib/execssl
UNIT_DEPS += src/lib/arena
endif

UNIT_SRC += $(if $(CONFIG_PM_ENABLE_TM),src/osp_tm.c,src/osp_tm_null.c)

ifeq ($(CONFIG_OSP_TM_SENSORS_NULL),y)
UNIT_SRC += src/osp_tm_sensors_null.c
endif
