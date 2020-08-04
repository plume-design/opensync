###############################################################################
#
#  Public statistics library
#
###############################################################################
UNIT_NAME := stats_pub

UNIT_DISABLE := $(if $(CONFIG_SM_PUBLIC_API),n,y)

UNIT_TYPE := LIB

UNIT_SRC := src/$(UNIT_NAME).c
UNIT_SRC += src/$(UNIT_NAME).pb-c.c
UNIT_SRC += src/$(UNIT_NAME)_survey.c
UNIT_SRC += src/$(UNIT_NAME)_device.c
UNIT_SRC += src/$(UNIT_NAME)_client.c

UNIT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -Isrc/lib/datapipeline/inc
UNIT_CFLAGS += -Isrc/lib/schema/inc
UNIT_LDFLAGS := -lprotobuf-c

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_DEPS := src/lib/log
