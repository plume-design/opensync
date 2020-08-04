##############################################################################
#
# Pktgen library
#
##############################################################################
UNIT_NAME := pktgen

UNIT_DISABLE := $(if $(CONFIG_PKTGEN_LIB_ENABLED),n,y)

UNIT_TYPE := LIB
UNIT_SRC += src/pktgen.c

UNIT_CFLAGS += -I$(UNIT_PATH)/inc

UNIT_DEPS += src/lib/log
UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/osn

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)
