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

#ifndef CM2_STABILITY_H_INCLUDED
#define CM2_STABILITY_H_INCLUDED

typedef enum {
    CM2_RESTORE_IP = 0,               // Bit 0
    CM2_RESTORE_SWITCH_DUMP_DATA,     // Bit 1
    CM2_RESTORE_SWITCH_FIX_PORT_MAP,  // Bit 2
    CM2_RESTORE_SWITCH_FIX_AUTON,     // Bit 3
    CM2_RESTORE_MAIN_LINK,            // Bit 4
} cm2_restore_con_t;

#ifndef CONFIG_CM2_STABILITY_USE_RESTORE_SWITCH_CFG
static inline void cm2_restore_switch_cfg_params(int counter, int thresh, cm2_restore_con_t *ropt)
{
}
static inline void cm2_restore_switch_cfg(cm2_restore_con_t opt)
{
}
#else
void cm2_restore_switch_cfg_params(int counter, int thresh, cm2_restore_con_t *ropt);
void cm2_restore_switch_cfg(cm2_restore_con_t opt);
#endif

#endif /* CM2_STABILITY_H_INCLUDED */
