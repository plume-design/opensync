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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "revssh.h"

#include "ovsdb_table.h"
#include "schema.h"
#include "module.h"
#include "const.h"
#include "util.h"
#include "memutil.h"
#include "evx.h"
#include "log.h"

MODULE(g_dm_revssh, dm_revssh_init, dm_revssh_fini)

struct dm_revssh
{
    revssh_t             *dr_revssh;

    char                  dr_node_pubkey[2048];

    struct revssh_status  dr_tun_status;

};

/* OVSDB table RevSSH */
static ovsdb_table_t table_RevSSH;

static struct dm_revssh *g_dm_revssh = NULL;

static void dm_revssh_ovsdb_init(void);

static struct dm_revssh *dm_revssh_new(void);
static bool dm_revssh_del(struct dm_revssh *dm_revssh);
static struct dm_revssh *dm_revssh_get();
static bool dm_revssh_config_set(struct dm_revssh *dm_revssh, struct schema_RevSSH *new, ovsdb_update_monitor_t *mon);
static void dm_revssh_tunnel_status_cb(const revssh_t *self, const struct revssh_status *tunnel_status);
static bool dm_revssh_ovsdb_state_update(struct dm_revssh *dm_revssh);

/**
 * Initialize the revssh module
 */
void dm_revssh_init(void *data)
{
    LOG(INFO, "dm_revssh: Initializing.");

    /* For security reasons, immediately at manager start, try to detect and previous
     * dangling RevSSH sessions not closed perhaps due to manager crash/restart or similar
     * and clean them up: */
    revssh_cleanup_dangling_sessions();

    dm_revssh_ovsdb_init();
}

static struct dm_revssh *dm_revssh_new(void)
{
    struct dm_revssh *rssh;
    rssh = CALLOC(1, sizeof(*rssh));

    /* Create and initialize OpenSync RevSSH API object: */
    rssh->dr_revssh = revssh_new();
    if (rssh->dr_revssh == NULL)
    {
        FREE(rssh);
        g_dm_revssh = NULL;
        return NULL;
    }

    g_dm_revssh = rssh;
    return rssh;
}

static bool dm_revssh_del(struct dm_revssh *rssh)
{
    bool rv = true;

    /* Deinitialize and destroy OpenSync RevSSH API boject: */
    rv &= revssh_del(rssh->dr_revssh);
    FREE(rssh);

    g_dm_revssh = NULL;
    return rv;
}

static struct dm_revssh *dm_revssh_get()
{
    return g_dm_revssh;
}

/* Update state fields in RevSSH OVSDB table. */
static bool dm_revssh_ovsdb_state_update(struct dm_revssh *dm_revssh)
{
    struct schema_RevSSH schema_RevSSH;

    memset(&schema_RevSSH, 0, sizeof(schema_RevSSH));
    schema_RevSSH._partial_update = true;

    /* Report node's public key: */
    if (dm_revssh->dr_node_pubkey[0] != '\0')
    {
        SCHEMA_SET_STR(schema_RevSSH.node_pubkey, dm_revssh->dr_node_pubkey);
    }
    else
    {
        SCHEMA_UNSET_FIELD(schema_RevSSH.node_pubkey);
    }

    /* RevSSH tunnel status: */
    SCHEMA_SET_STR(schema_RevSSH.tunnel_status, revssh_tunnel_status_tostr(dm_revssh->dr_tun_status.rs_tun_status));

    /* Last error message (if set): */
    if (dm_revssh->dr_tun_status.rs_last_err_msg[0] != '\0')
    {
        SCHEMA_SET_STR(schema_RevSSH.log_last_err_msg, dm_revssh->dr_tun_status.rs_last_err_msg);
    }
    else
    {
        SCHEMA_UNSET_FIELD(schema_RevSSH.log_last_err_msg);
    }

    ovsdb_table_update(&table_RevSSH, &schema_RevSSH);
    return true;
}

/* RevSSH tunnel status change notification callback. */
static void dm_revssh_tunnel_status_cb(const revssh_t *revssh, const struct revssh_status *tunnel_status)
{
    struct dm_revssh *dm_revssh = dm_revssh_get();

    dm_revssh->dr_tun_status = *tunnel_status;

    dm_revssh_ovsdb_state_update(dm_revssh);
}

/* Set RevSSH config from schema to OpenSync RevSSH object. */
static bool dm_revssh_config_set(
    struct dm_revssh *dm_revssh,
    struct schema_RevSSH *new,
    ovsdb_update_monitor_t *mon)
{
    osn_ipany_addr_t remote_bind_addr;
    osn_ipany_addr_t local_addr;
    bool rv = true;
    int i;

    /* Set RevSSH tunnel status notification callback: */
    rv &= revssh_notify_status_callback_set(dm_revssh->dr_revssh, dm_revssh_tunnel_status_cb);

    /* Set RevSSH server parameters: */
    rv &= revssh_server_params_set(
            dm_revssh->dr_revssh,
            new->server_host,
            new->server_port_exists ? new->server_port : -1,
            new->server_user);

    /* Configure public keys to be temporarily added to node's authorized_keys: */
    for (i = 0; i < new->server_pubkey_len; i++)
    {
        rv &= revssh_authorized_keys_add(dm_revssh->dr_revssh, new->server_pubkey[i]);
    }

    /* SSH reverse tunnel parameters: */

    if (new->tunnel_remote_bind_addr_exists)
    {
        if (!osn_ipany_addr_from_str(&remote_bind_addr, new->tunnel_remote_bind_addr))
        {
            LOG(ERR, "dm_revssh: Error parsing tunnel_remote_bind_addr: %s", new->tunnel_remote_bind_addr);
            return false;
        }
    }

    if (new->tunnel_local_addr_exists)
    {
        if (!osn_ipany_addr_from_str(&local_addr, new->tunnel_local_addr))
        {
            LOG(ERR, "dm_revssh: Error parsing tunnel_local_addr: %s", new->tunnel_local_addr);
            return false;
        }
    }

    rv &= revssh_tunnel_params_set(
            dm_revssh->dr_revssh,
            new->tunnel_remote_bind_addr_exists ? &remote_bind_addr : NULL,
            new->tunnel_remote_bind_port,
            new->tunnel_local_addr_exists ? &local_addr : NULL,
            new->tunnel_local_port_exists ? new->tunnel_local_port : -1);

    /* RevSSH session timeout values: */
    rv &= revssh_timeout_set(
            dm_revssh->dr_revssh,
            new->session_max_time_exists ? new->session_max_time : -1,
            new->idle_timeout_exists ? new->idle_timeout : -1);

    /* Optional request for generating temporary node keypair: */
    if (new->node_gen_key_type_exists)
    {
        rv &= revssh_tmpkeygen_set(
                dm_revssh->dr_revssh,
                revssh_keytype_fromstr(new->node_gen_key_type),
                new->node_gen_key_bits_exists ? new->node_gen_key_bits : -1);
    }

end:
    return rv;
}

/*
 * OVSDB monitor update callback for RevSSH OVSDB table.
 */
static void callback_RevSSH(
        ovsdb_update_monitor_t *mon,
        struct schema_RevSSH *old,
        struct schema_RevSSH *new)
{
    struct dm_revssh *dm_revssh = NULL;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(INFO, "dm_revssh: RevSSH update: NEW row");

            dm_revssh = dm_revssh_new();
            if (dm_revssh == NULL)
            {
                LOG(ERR, "dm_revssh: Failed creating new RevSSH config.");
                return;
            }

            break;
        case OVSDB_UPDATE_MODIFY:
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(RevSSH, node_pubkey))
                || ovsdb_update_changed(mon, SCHEMA_COLUMN(RevSSH, tunnel_status))
                || ovsdb_update_changed(mon, SCHEMA_COLUMN(RevSSH, log_last_err_msg)))
            {
                /* These fields are updated by us. Ignore. */
                return;
            }

            LOG(INFO, "dm_revssh: RevSSH update: MODIFY row");

            dm_revssh = dm_revssh_get();
            if (dm_revssh == NULL)
            {
                LOG(WARN, "dm_revssh: Cannot modify RevSSH config: object not found");
                return;
            }
            /*
             * No runtime modification possible of existing RevSSH session parameters.
             * The existing session must be broken down and a new session initiated.
             */
            LOG(NOTICE,
                "dm_revssh: Breaking down the existing RevSSH session"
                " and starting a new one with modified config.");

            dm_revssh_del(dm_revssh);
            dm_revssh = dm_revssh_new();
            if (dm_revssh == NULL)
            {
                LOG(ERR, "dm_revssh: Failed creating new RevSSH config.");
                return;
            }

            break;
        case OVSDB_UPDATE_DEL:
            LOG(INFO, "dm_revssh: RevSSH update: DELETE row");

            dm_revssh = dm_revssh_get();
            if (dm_revssh == NULL)
            {
                LOG(WARN, "dm_revssh: Cannot delete RevSSH config: object not found");
                return;
            }
            dm_revssh_del(dm_revssh);

            return;
        default:
            LOG(ERROR, "dm_revssh: Monitor update error.");
            return;
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW
        && (new->tunnel_status_exists || new->node_pubkey_exists || new->log_last_err_msg_exists))
    {
        LOG(NOTICE,
            "dm_revssh: Dangling RevSSH OVSDB row detected. "
            "Perhaps after manager crash/restart? Ignoring for security reasons.");
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW || mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        if (dm_revssh == NULL)
        {
            return;
        }

        /* Set RevSSH config parameters from OVSDB to OpenSync RevSSH object: */
        if (!dm_revssh_config_set(dm_revssh, new, mon))
        {
            LOG(ERR, "Error setting RevSSH configuration to OpenSync RevSSH API object");

            dm_revssh->dr_tun_status.rs_tun_status = REVSSH_TUN_STATUS_INIT_ERR;
            goto ovsdb_state_update;
        }

        /* Initiate RevSSH: */
        if (revssh_start(dm_revssh->dr_revssh))
        {
            if (!revssh_node_pubkey_get(
                        dm_revssh->dr_revssh,
                        dm_revssh->dr_node_pubkey,
                        sizeof(dm_revssh->dr_node_pubkey)))
            {
                dm_revssh->dr_tun_status.rs_tun_status = REVSSH_TUN_STATUS_INIT_ERR;
                LOG(ERR, "Error getting node's public key");
            }
        }
        else
        {
            dm_revssh->dr_tun_status.rs_tun_status = REVSSH_TUN_STATUS_INIT_ERR;
            LOG(ERR, "Error starting RevSSH");
        }

ovsdb_state_update:

        /* Initial OVSDB state reporting (mainly to report node's public key): */
        dm_revssh_ovsdb_state_update(dm_revssh);
    }
}

/*
 * Initialize the DM RevSSH OVSDB interface.
 */
static void dm_revssh_ovsdb_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(RevSSH);
    OVSDB_TABLE_MONITOR(RevSSH, false);
}

/*
 * Deinitialize the revssh module.
 */
void dm_revssh_fini(void *data)
{
    LOG(INFO, "dm_revssh: Finishing.");
}
