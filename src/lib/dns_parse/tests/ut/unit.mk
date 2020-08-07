UNIT_DISABLE := $(if $(CONFIG_MANAGER_FSM),n,y)

UNIT_NAME := test_dns_parse

UNIT_TYPE := TEST_BIN

UNIT_CFLAGS += -Isrc/fsm/inc
UNIT_CFLAGS += -Isrc/lib/imc/inc

UNIT_SRC := test_dns_parse.c
UNIT_SRC += ../../../../fsm/src/fsm_ovsdb.c
UNIT_SRC += ../../../../fsm/src/fsm_pcap.c
UNIT_SRC += ../../../../fsm/src/fsm_event.c
UNIT_SRC += ../../../../fsm/src/fsm_service.c
UNIT_SRC += ../../../../fsm/src/fsm_dpi.c
UNIT_SRC += ../../../../fsm/src/fsm_oms.c

UNIT_DEPS := src/lib/log
UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/json_util
UNIT_DEPS += src/qm/qm_conn
UNIT_DEPS += src/lib/dns_parse
UNIT_DEPS += src/lib/unity
UNIT_DEPS += src/lib/fsm_utils
UNIT_DEPS += src/lib/fsm_policy
UNIT_DEPS += src/lib/oms
