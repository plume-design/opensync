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

#ifndef TIMEVT_H_INCLUDED
#define TIMEVT_H_INCLUDED

#include <stdbool.h>

#include "telog.h"

/* This file is a common part of time-event management system used by server and clients */
#include "time_event.pb-c.h"

// Default server socket name in abstract namespace
#define TESRV_SOCKET_ADDR "@timevt-server"

// Default client socket name in abstract namespace
#define TECLI_SOCKET_ADDR "@timevt-client"

// Default client step name
#define TECLI_DEFAULT_STEP "SINGLE"

// Maximal supported message size transmitted from client(s) to server in bytes
#define TIMEVT_MAX_MSG_LEN 2048

// Log message header string
#define TIMEVT_HEADER "$TEL"

/**
 * @brief global time-event client init - one for process
 * @param procname process name this client belongs to
 * @param server_addr addr of the server collecting all events for remote logging (mqtt, other)
 * or NULL when events are to be logged locally only
 * @return initialization result
 */
bool te_client_init(const char *procname, const char *server_addr);

/**
 * @brief Destructs global time event-client and releases used resources 
 */
void te_client_deinit(void);

/* Time-event report presentation helpers */

void print_time_event(FILE *f, const Sts__TimeEvent *te);
void print_device_id(FILE *f, const Sts__DeviceID *did);
void print_time_event_report(FILE *f, const Sts__TimeEventsReport *rep);

size_t print_utc_time(char *dst, size_t dstsize, uint64_t time_ms);
size_t print_mono_time(char *dst, size_t dstsize, uint64_t time_ms);

/* common time-event functions */

/* Computes and returns 32-bit checksum of data in the src buffer */
uint32_t te_crc32_compute(uint8_t *src, size_t length);
/* Computes checksum of src data and appends it after the buffer, returns length of appended csum */
size_t te_crc32_append(uint8_t *src, size_t length);
/* Reads and returns checksum from message byte src stream */
uint32_t te_crc32_read(const uint8_t *src);

#endif
