###############################################################################
#
# mDNSD library
#
###############################################################################
UNIT_NAME := mdnsd
UNIT_DISABLE := $(if $(CONFIG_MANAGER_FSM),n,y)

# Template type:
UNIT_TYPE := LIB
UNIT_DIR := lib

UNIT_SRC := src/1035.c
UNIT_SRC += src/log.c
UNIT_SRC += src/mdnsd.c
UNIT_SRC += src/sdtxt.c
UNIT_SRC += src/xht.c

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
UNIT_DEPS += src/lib/network_telemetry
UNIT_DEPS += src/lib/json_mqtt
