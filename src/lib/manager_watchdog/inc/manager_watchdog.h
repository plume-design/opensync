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

#ifndef MANAGER_WATCHDOG_H
#define MANAGER_WATCHDOG_H

#include <ev.h>

/**
 * @brief Initializes and starts the event loop watchdog.
 *
 * The internal kick timer will be automatically set to half of the
 * provided timeout value. For example, if wdog_timeout_sec is 90.0, the
 * watchdog will be reset every 45.0 seconds. The function is only meant
 * to be called once per event loop. Calling it multiple times with the
 * same arguments is allowed and has no effect on the initial watchdog loop.
 * Calling it with a different timeout terminates the program.
 *
 * @param loop The active libev event loop to monitor.
 * @param wdog_timeout_sec The total time in seconds of unresponsiveness
 * before the program is terminated. If set to 0, the watchdog will not
 * be started.
 */
void manager_watchdog_init(struct ev_loop *loop, const uint32_t wdog_timeout_sec);

#endif  // MANAGER_WATCHDOG_H
