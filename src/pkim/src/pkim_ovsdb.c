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

#include "log.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"
#include "schema.h"

#include "pkim_cert.h"
#include "pkim_ovsdb.h"

static ovsdb_table_t table_PKI_Config;
static void callback_PKI_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_PKI_Config *old,
        struct schema_PKI_Config *new);

/*
 * =============================================================================
 * Public API
 * =============================================================================
 */

/*
 * Initialize OVSDB
 */
bool pkim_ovsdb_init(void)
{
    if (!ovsdb_init_loop(EV_DEFAULT, "PKIM"))
    {
        LOG(ERR, "pkim: Error connecting to OVSDB.");
        return false;
    }

    OVSDB_TABLE_INIT_NO_KEY(PKI_Config);
    OVSDB_TABLE_MONITOR(PKI_Config, false);

    return true;
}

/*
 * Set the status of the row with label `label` to `status`.
 */
bool pkim_ovsdb_status_set(const char *label, const char *status)
{
    int rc;

    struct schema_PKI_Config config = {0};

    if (status != NULL)
    {
        SCHEMA_SET_STR(config.status, status);
    }

    rc = ovsdb_table_update_where_f(
            &table_PKI_Config,
            ovsdb_where_simple(SCHEMA_COLUMN(PKI_Config, label), label),
            &config,
            (char *[]){"+", SCHEMA_COLUMN(PKI_Config, status), NULL});
    if (rc < 1)
    {
        LOG(WARN, "pkim: %s: Error updating status to `%s`.", label, status);
        return false;
    }

    return true;
}

/*
 * =============================================================================
 * Support functions and callbacks
 * =============================================================================
 */
static void callback_PKI_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_PKI_Config *old,
        struct schema_PKI_Config *new)
{
    bool add = false;
    bool del = false;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            add = true;
            break;

        case OVSDB_UPDATE_MODIFY:
            if (old->label_exists || old->server_url_exists) add = del = true;
            break;

        case OVSDB_UPDATE_DEL:
            del = true;
            break;

        case OVSDB_UPDATE_ERROR:
            LOG(ERR, "pkim: OVSDB error occurred while monitoring PKI_Config.");
            return;
    }

    if (del && !pkim_cert_del(old->label_exists ? old->label : new->label))
    {
        LOG(WARN, "pkim: Error stopping certificate management: %s", old->label);
    }

    if (add && !pkim_cert_add(new->label, new->server_url))
    {
        LOG(WARN, "pkim: Error starting certificate management: %s", new->label);
    }

    /* Handle a renew/reenroll request -- the weird formatting is unfortunately
     * produced by clang-format */
    if (new != NULL &&new->renew_changed &&new->renew)
    {
        pkim_cert_renew(new->label);
    }
}
