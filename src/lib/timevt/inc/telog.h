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

#ifndef TELOG_H_INCLUDED
#define TELOG_H_INCLUDED

#include <stdio.h>

/**
 * @brief Logs time-event message
 * 
 * @param cat event category
 * @param subject event subject name (e.g module, interface name) or NULL to ignore it
 * @param step event step name (begin/start, step-2, resolving, connecting, end, stop, etc) or NULL for single events
 * @param fmt log message format or NULL when message not provided
 * @param ... message parameters
 * @return true when message logged; false otherwise
 */
bool te_client_log(const char *cat, const char *subject, const char *step, const char *fmt, ... );

/* Example event categories:
    BOOT            : target booting sequence until OpenSync is started
    UTC_TIME        : UTC time set (event message contains boot time stamp)
    WIFI_LINK       : WIFI link starting / ready / stopping / stopped
    GRE_LINK        : GRE link starting / ready / stopping / stopped
    WDS_LINK        : WDS link starting / ready / stopping / stopped
    DHCP_CLIENT     : DHCP client address assignment: starting / ready (addr leased) / stopped
    DHCP_SERVER     : DHCP server events: starting / ready (addr leased) / stopped
    REDIRECTOR      : Redirector link resolving / connecting / ready / stopped
    CONTROLLER      : Controller link resolving / connecting / ready / stopped
 */

/**
 * @brief Logs one-shot time-event message
 * @param category event category : see example above
 * @param subject event subject name (e.g module, interface name) or NULL to ignore it
 * @param ... event message format and list of params or NULL to ignore the message
 * @return 'true' when message logged; 'false' otherwise
 */
#define TELOG_ONE(category, subject, ...) te_client_log(category, subject, NULL, __VA_ARGS__)

/**
 * @brief Logs time-event message belonging to the group of events
 * @param category event category: see example above
 * @param subject event subject name (e.g module, interface name) or NULL to ignore it
 * @param step step name in the group of events : (begin/start, step-2, resolving, connecting, end, stop, etc)
 * @param ... event message format and list of params or NULL to ignore the message
 * @return 'true' when message logged; 'false' otherwise
 */
#define TELOG_STEP(category, subject, step, ...) te_client_log(category, subject, step, __VA_ARGS__)

#endif
