# Copyright (c) 2020 Charter, Inc.
#
# This module contains unpublished, confidential, proprietary
# material. The use and dissemination of this material are
# governed by a license. The above copyright notice does not
# evidence any actual or intended publication of this material.
#
# Created: 05 February 2020
#

###############################################################################
#
# MAPT Manager
#
###############################################################################
UNIT_NAME := maptm

# Template type:
UNIT_TYPE := BIN

UNIT_SRC    := src/maptm_main.c
UNIT_SRC    += src/maptm_ovsdb.c
UNIT_SRC    += src/maptm_config.c
UNIT_SRC    += src/maptm_eligibility.c


# Unit specific CFLAGS
UNIT_CFLAGS := -I$(UNIT_PATH)/inc

UNIT_LDFLAGS := -lpthread
UNIT_LDFLAGS += -ljansson
UNIT_LDFLAGS += -ldl
UNIT_LDFLAGS += -lev
UNIT_LDFLAGS += -lrt

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_DEPS := src/lib/ovsdb
UNIT_DEPS += src/lib/pjs
UNIT_DEPS += src/lib/schema
UNIT_DEPS += src/lib/version
UNIT_DEPS += src/lib/evsched
UNIT_DEPS += src/lib/tailf
UNIT_DEPS += src/lib/common
