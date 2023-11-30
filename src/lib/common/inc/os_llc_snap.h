/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef OS_LLC_SNAP_H_INCLUDED
#define OS_LLC_SNAP_H_INCLUDED

#include <os_ouis.h>
#include <inttypes.h>
#include <stdbool.h>

#define OS_LLC_SNAP_SSAP 0xAA
#define OS_LLC_SNAP_DSAP 0xAA
#define OS_LLC_SNAP_CONTROL 0x03

struct os_llc_header {
    uint8_t dsap;
    uint8_t ssap;
    uint8_t control;
} __attribute__((packed));

struct os_llc_snap_extension {
    uint8_t oui[3];
    uint16_t protocol;
} __attribute__((packed));

struct os_llc_snap_header {
    struct os_llc_header llc;
    struct os_llc_snap_extension snap;
} __attribute__((packed));

/* 
 * This is using Plume OUI for LLC/SNAP namespace,
 * because currently there is no OpenSync OUI.
 */
#define OS_LLC_SNAP_OUI OS_OUI_PLUME

/* Various protocols */
#define OS_LLC_SNAP_PROTOCOL_FDBUF 0

void
os_llc_snap_header_fill(struct os_llc_snap_header *header,
                        uint16_t protocol);

bool
os_llc_snap_header_matches(const struct os_llc_snap_header *header,
                           uint16_t protocol);

#endif /* OS_LLC_SNAP_H_INCLUDED */
