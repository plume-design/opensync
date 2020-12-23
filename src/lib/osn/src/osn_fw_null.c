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

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"
#include "osn_fw_pri.h"
#include "os.h"
#include "util.h"
#include "const.h"

#include "kconfig.h"

#define MODULE_ID LOG_MODULE_ID_TARGET

const char * const osfw_table_str_list[] =
{
    [OSFW_TABLE_FILTER] = "filter",
    [OSFW_TABLE_NAT] = "nat",
    [OSFW_TABLE_MANGLE] = "mangle",
    [OSFW_TABLE_RAW] = "raw",
    [OSFW_TABLE_SECURITY] = "security"
};

#define OSFW_FAMILY_STR(family) \
    (((family) == AF_INET6) ? "ipv6" : "ipv4")

static const char *osfw_table_str(enum osfw_table tbl)
{
    if ((int)tbl >= ARRAY_LEN(osfw_table_str_list))
    {
        return "(unknown)";
    }

    return osfw_table_str_list[tbl];
}

bool osfw_init(void)
{
    LOG(INFO, "osfw: null init");

    return true;
}

bool osfw_fini(void)
{
    LOG(INFO, "osfw: null fini");

    return true;
}

bool osfw_chain_add(
        int family,
        enum osfw_table table,
        const char *chain)
{
    LOG(INFO, "osfw: %s.%s.%s: null chain add",
            OSFW_FAMILY_STR(family), osfw_table_str(table), chain);

    return true;
}

bool osfw_chain_del(
        int family,
        enum osfw_table table,
        const char *chain)
{
    LOG(INFO, "osfw: %s.%s.%s: null chain del",
            OSFW_FAMILY_STR(family), osfw_table_str(table), chain);

    return true;
}

bool osfw_rule_add(
        int family,
        enum osfw_table table,
        const char *chain,
        int priority,
        const char *match,
        const char *target)
{
    LOG(INFO,
            "osfw: %s.%s.%s.: null rule add: [%d] %s -> %s",
            OSFW_FAMILY_STR(family),
            osfw_table_str(table),
            chain,
            priority,
            match,
            target);

    return true;
}

bool osfw_rule_del(
        int family,
        enum osfw_table table,
        const char *chain,
        int priority,
        const char *match,
        const char *target)
{
    LOG(INFO,
            "osfw: %s.%s.%s.: null rule del: [%d] %s -> %s",
            OSFW_FAMILY_STR(family),
            osfw_table_str(table),
            chain,
            priority,
            match,
            target);

    return true;
}

bool osfw_apply(void)
{
    LOG(INFO, "osfw: null apply");

    return true;
}

