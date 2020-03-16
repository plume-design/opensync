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
 *  Reboot structure/JSON definition. The JSON will be stored to persistent
 *  storage in compact form.
 * ===========================================================================
 */

/*
 * Single reboot record
 */
#define PJS_DM_REBOOT_RECORD                                    \
    PJS(dm_reboot_record,                                       \
        PJS_INT(dr_bootid)                                      \
        PJS_STRING(dr_type, 64)                                 \
        PJS_STRING(dr_reason, 256))

/*
 * Reboot structure, contains up to 64 reboot records
 */
#define PJS_DM_REBOOT                                           \
    PJS(dm_reboot,                                              \
        PJS_INT(dr_counter)                                     \
        PJS_SUB_A(dr_records, dm_reboot_record, 64))

/*
 * Generate the PJS table
 */
#define PJS_GEN_TABLE PJS_DM_REBOOT_RECORD PJS_DM_REBOOT
