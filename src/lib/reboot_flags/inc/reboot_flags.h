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

/*
 * ===========================================================================
 *  reboot_flags: API to request a critical task not to be disrupted by reboot
 * ===========================================================================
 */
#ifndef REBOOT_FLAGS_H_INCLUDED
#define REBOOT_FLAGS_H_INCLUDED

#include <stdbool.h>

/* no_reboot_set: do not reboot from now on -
   critical task in module <module_name> just about to start */
bool no_reboot_set(char *module_name);

/* no_reboot_clear: allow to go back to reboot regime as usually -
   critical task in module <module_name> just finished */
bool no_reboot_clear(char *module_name);

/* no_reboot_get:
   check if module <module_name> has active do not reboot request */
bool no_reboot_get(char *module_name);

/* no_reboot_clear_all:
   caution: this discards all the modules active do not reboot requests */
bool no_reboot_clear_all(void);

/* dump modules with active do not reboot requests */
bool no_reboot_dump_modules(void);

#endif /* REBOOT_FLAGS_H_INCLUDED */
