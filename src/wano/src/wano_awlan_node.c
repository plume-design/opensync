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

#include "ovsdb_table.h"
#include "schema.h"

static ovsdb_table_t table_AWLAN_Node;

static char redirector_addr[256] = {0};

static void callback_AWLAN_Node(
        ovsdb_update_monitor_t *self,
        struct schema_AWLAN_Node *old,
        struct schema_AWLAN_Node *new);

static bool wano_awlan_copy_addr(const char *src, char *dst, size_t size);

bool wano_awlan_node_init(void)
{
    /* Register to Connection_Manager_Uplink */
    OVSDB_TABLE_INIT(AWLAN_Node, redirector_addr);
    if (OVSDB_TABLE_MONITOR(AWLAN_Node, true) != true)
    {
        LOG(INFO, "wano: Error monitoring AWLAN_Node");
        return false;
    }

    return true;
}

/*
 * AWLAN_Node OVSDB monitor function
 */
static void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old,
        struct schema_AWLAN_Node *new)
{
    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if (ovsdb_update_changed(mon, "redirector_addr") == true)
            {
                if (wano_awlan_copy_addr(new->redirector_addr, redirector_addr, sizeof(redirector_addr)) == false)
                {
                    LOGD("wano: Failed extracting hostname from the new AWLAN_Node::redirector_addr");
                    redirector_addr[0] = '\0';
                }

                LOGD("wano: cached hostname for DNS probe: %s", redirector_addr);
            }

            break;

        case OVSDB_UPDATE_DEL:
        default:
            return;
    }
}

static bool wano_awlan_copy_addr(const char *src, char *dst, size_t size)
{
    const char *read = src;
    char *write = dst;

    if (size < 1)
    {
        LOGD("wano: ERROR: wano_awlan_copy() passed size == %zu", size);
        return false;
    }

    /* Skip everything until the first delimiting ':' */
    for (;;)
    {
        switch (*read)
        {
            /* Found the delimiter, start copying at next char */
            case ':':
                read++;
                break;

            /* If there's no ':' just copy the whole string */
            case '\0':
                read = src;
                break;

            /* Use continue to persist in the loop */
            default:
                read++;
                continue;
        }

        break;
    }

    /* Copy everything up to the second delimiting ':' */
    for (;;)
    {
        switch (*read)
        {
            case ':':
            case '\0':
                *write = '\0';
                break;

            default:
                /* Are we at the end of the buffer? */
                if ((write - dst) == (long)(size - 1))
                {
                    LOGW("wano: redirector_addr buffer too small");
                    *write = '\0';
                    return false;
                }

                /* Use continue to persist in the loop */
                *write++ = *read++;
                continue;
        }

        return true;
    }
}

const char *wano_awlan_redirector_addr(void)
{
    return (const char *)redirector_addr;
}
