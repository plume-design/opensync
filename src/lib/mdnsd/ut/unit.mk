UNIT_DISABLE := $(if $(CONFIG_MANAGER_FSM),n,y)

UNIT_NAME := test_mdnsd

UNIT_TYPE := TEST_BIN

UNIT_SRC := test_mdnsd.c
UNIT_SRC += addr.c
UNIT_SRC += conf.c

UNIT_CFLAGS := -I$(UNIT_PATH)/../inc
UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)

UNIT_LDFLAGS := -lev -ljansson -lpcap -lmnl
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)


UNIT_DEPS := src/lib/log
UNIT_DEPS += src/lib/osa
UNIT_DEPS += src/lib/const
UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/unity
UNIT_DEPS += src/lib/mdnsd
