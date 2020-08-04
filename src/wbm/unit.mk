##############################################################################
#
# Wifi Blaster Manager
#
##############################################################################
UNIT_NAME := wbm

UNIT_DISABLE := $(if $(CONFIG_MANAGER_WBM),n,y)

UNIT_TYPE := BIN

UNIT_SRC := src/$(UNIT_NAME)_main.c
UNIT_SRC += src/$(UNIT_NAME)_ovsdb.c
UNIT_SRC += src/$(UNIT_NAME)_engine.c
UNIT_SRC += src/$(UNIT_NAME)_stats.c
UNIT_SRC += src/$(UNIT_NAME)_report.c
UNIT_SRC += src/$(UNIT_NAME)_traffic_gen.c
UNIT_SRC += src/$(UNIT_NAME).pb-c.c

UNIT_CFLAGS += -I$(TOP_DIR)/src/lib/common/inc/
UNIT_CFLAGS += -I$(TOP_DIR)/src/lib/pktgen/inc/
UNIT_CFLAGS += -I$(TOP_DIR)/src/lib/stats_pub/inc/

UNIT_LDFLAGS := -lev

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/ovsdb
UNIT_DEPS += src/lib/pjs
UNIT_DEPS += src/lib/schema
UNIT_DEPS += src/lib/version
UNIT_DEPS += src/lib/evsched
UNIT_DEPS += src/lib/datapipeline
UNIT_DEPS += src/lib/json_util
UNIT_DEPS += src/lib/schema
UNIT_DEPS += src/lib/osn
UNIT_DEPS += src/lib/pktgen
UNIT_DEPS += src/lib/stats_pub
UNIT_DEPS += src/lib/const
UNIT_DEPS += src/qm/qm_conn
