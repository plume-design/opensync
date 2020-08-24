UNIT_DISABLE := $(if $(CONFIG_TARGET_MANAGER_FSM),n,y)

UNIT_NAME := test_iotm

UNIT_TYPE := TEST_BIN

UNIT_SRC := test_iotm.c
UNIT_SRC += test_iotm_ovsdb.c
UNIT_SRC += test_iotm_session.c
UNIT_SRC += test_iotm_ev.c
UNIT_SRC += test_iotm_event.c
UNIT_SRC += test_iotm_router.c
UNIT_SRC += test_iotm_rule.c
UNIT_SRC += test_iotm_list.c
UNIT_SRC += test_iotm_tree.c
UNIT_SRC += test_iotm_plug_event.c
UNIT_SRC += test_iotm_plug_command.c
UNIT_SRC += test_iotm_tag.c
UNIT_SRC += test_iotm_tl.c
UNIT_SRC += test_iotm_data_types.c
UNIT_SRC += ../src/iotm_ovsdb.c
UNIT_SRC += ../src/iotm_service.c
UNIT_SRC += ../src/iotm_ev.c
UNIT_SRC += ../src/iotm_event.c
UNIT_SRC += ../src/iotm_router.c
UNIT_SRC += ../src/iotm_session.c
UNIT_SRC += ../src/iotm_rule.c
UNIT_SRC += ../src/iotm_list.c
UNIT_SRC += ../src/iotm_tree.c
UNIT_SRC += ../src/iotm_plug_command.c
UNIT_SRC += ../src/iotm_plug_event.c
UNIT_SRC += ../src/iotm_tag.c
UNIT_SRC += ../src/iotm_tl.c
UNIT_SRC += ../src/iotm_data_types.c

UNIT_CFLAGS := -I$(UNIT_PATH)/../inc
UNIT_CFLAGS += -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -DUNIT_TESTS
UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)

UNIT_LDFLAGS := -lev -ljansson -lcurl -lssl -lcrypto
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/ds
UNIT_DEPS := src/lib/ovsdb
UNIT_DEPS += src/lib/pjs
UNIT_DEPS += src/lib/schema
UNIT_DEPS += src/lib/version
UNIT_DEPS += src/lib/evsched
UNIT_DEPS += src/lib/datapipeline
UNIT_DEPS += src/lib/json_util
UNIT_DEPS += src/lib/unity
