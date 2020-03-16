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

#ifndef OSPS_H_INCLUDED
#define OSPS_H_INCLUDED

#include "ds_dlist.h"

#define OSPS_CLI_ERROR   256

#define OSPS_COMMAND_INIT(name, fn, brief, help)    \
(struct osps_command)                               \
{                                                   \
    .oc_name = (name),                              \
    .oc_fn = (fn),                                  \
    .oc_brief = (brief),                            \
    .oc_help = (help),                              \
}

typedef int osps_command_fn_t(int argc, char *argv[]);

struct osps_command
{
    const char             *oc_name;
    osps_command_fn_t      *oc_fn;
    const char             *oc_brief;
    const char             *oc_help;
    ds_dlist_node_t         oc_dnode;
};

void osps_command_register(struct osps_command *oc);
void osps_usage(const char *cmd, const char *fmt, ...);

/** set to true if the -p global flag is set */
extern bool osps_preserve;

#endif /* OSPS_H_INCLUDED */
