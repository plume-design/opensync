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

#ifndef ADT_UPNP_JSON_REPORT_H_INCLUDED
#define ADT_UPNP_JSON_REPORT_H_INCLUDED

#include <stddef.h>

#include "fsm.h"

struct adt_upnp_key_val
{
    char *key;
    char *value;
    size_t val_max_len;
};

struct adt_upnp_report
{
    struct adt_upnp_key_val *first;
    int nelems;
    struct fsm_dpi_adt_upnp_root_desc *url;
};

/**
 * @brief Creates a serialize JSON report for the captured data
 *
 * @param session used to extract information about the session.
 * @param to_report used to extract information about the session.
 *
 * @returns a serialized JSON containing the report summarizing the
 *          upnp information captured, or NULL in case of error.
 *
 * @note It is the responsibility of the caller to call json_free() on
 *       the returned buffer.
 */
char *
jencode_adt_upnp_report(struct fsm_session *session,
                        struct adt_upnp_report *to_report);

#endif /* ADT_UPNP_JSON_REPORT_H_INCLUDED */
