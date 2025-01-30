###############################################################################
#
# DNS Parsing library
#
###############################################################################
UNIT_NAME := fsm_dns

UNIT_DISABLE := $(if $(CONFIG_LIB_LEGACY_FSM_DNS_PARSER),n,y)

ifeq ($(CONFIG_FSM_NO_DSO),y)
    UNIT_TYPE := LIB
else
    UNIT_TYPE := SHLIB
    UNIT_DIR := lib
endif

UNIT_SRC := src/dns_parse.c
UNIT_SRC += src/network.c
UNIT_SRC += src/rtypes.c
UNIT_SRC += src/strutils.c

UNIT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := -lpcap

UNIT_DEPS := src/lib/log
UNIT_DEPS += src/lib/const
UNIT_DEPS += src/lib/ustack
UNIT_DEPS += src/lib/datapipeline
UNIT_DEPS += src/qm/qm_conn
UNIT_DEPS += src/lib/osa
UNIT_DEPS += src/lib/fsm_policy
UNIT_DEPS += src/lib/fsm_utils
UNIT_DEPS += src/lib/network_telemetry
UNIT_DEPS += src/lib/json_mqtt
UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/neigh_table
UNIT_DEPS += src/lib/dns_cache
UNIT_DEPS += src/lib/gatekeeper_cache
UNIT_DEPS += src/lib/network_metadata
UNIT_DEPS += src/lib/fsm_utils
