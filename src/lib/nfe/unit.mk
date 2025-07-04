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

###############################################################################
#
# Library to enable network flow engine
#
###############################################################################
UNIT_NAME := nfe

UNIT_TYPE := LIB

UNIT_SRC := src/nfe.c
UNIT_SRC += src/nfe_conntrack.c
UNIT_SRC += src/nfe_ether.c
UNIT_SRC += src/nfe_icmp.c
UNIT_SRC += src/nfe_ipv6.c
UNIT_SRC += src/nfe_udp.c
UNIT_SRC += src/nfe_config.c
UNIT_SRC += src/nfe_flow.c
UNIT_SRC += src/nfe_input.c
UNIT_SRC += src/nfe_proto.c
UNIT_SRC += src/nfe_vlan.c
UNIT_SRC += src/nfe_conn.c
UNIT_SRC += src/nfe_gre.c
UNIT_SRC += src/nfe_ipv4.c
UNIT_SRC += src/nfe_tcp.c

UNIT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -fPIC

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)

UNIT_DEPS := src/lib/common
UNIT_DEPS := src/lib/log
