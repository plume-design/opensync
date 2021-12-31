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
#include "os_util.h"
#include "reflink.h"
#include "synclist.h"
#include "ovsdb_sync.h"
#include "target.h"

#include "nm2.h"
#include "inet.h"

struct mcast_bridge
{
    char bridge[C_IFNAME_LEN];
    char static_mrouter[C_IFNAME_LEN];
    char pxy_upstream_if[C_IFNAME_LEN];
    char pxy_downstream_if[C_IFNAME_LEN];
    ovs_uuid_t _uuid;
    ds_tree_node_t node;
};

/*
 * ===========================================================================
 *  Globals and forward declarations
 * ===========================================================================
 */

static struct mcast_bridge igmp_bridge;
static struct mcast_bridge mld_bridge;

static ovsdb_table_t    table_IGMP_Config;
static void             callback_IGMP_Config(ovsdb_update_monitor_t *mon,
                                             struct schema_IGMP_Config *old,
                                             struct schema_IGMP_Config *new);

static ovsdb_table_t    table_MLD_Config;
static void             callback_MLD_Config(ovsdb_update_monitor_t *mon,
                                             struct schema_MLD_Config *old,
                                             struct schema_MLD_Config *new);

/*
 * Initialize table monitors
 */
void nm2_mcast_init(void)
{
    LOG(INFO, "Initializing NM Multicast sys config monitoring.");
    OVSDB_TABLE_INIT_NO_KEY(IGMP_Config);
    OVSDB_TABLE_MONITOR(IGMP_Config,  false);
    OVSDB_TABLE_INIT_NO_KEY(MLD_Config);
    OVSDB_TABLE_MONITOR(MLD_Config,  false);
    return;
}

/*
 * ===========================================================================
 *  IGMP
 * ===========================================================================
 */

static bool nm2_parse_igmp_schema(
        struct schema_IGMP_Config *schema,
        struct osn_igmp_snooping_config *snooping_config,
        struct osn_igmp_proxy_config *proxy_config,
        struct osn_igmp_querier_config *querier_config,
        struct osn_mcast_other_config *other_config,
        ovsdb_update_type_t action)
{
    int ii;

    /* If row was deleted, leave everything blank */
    if (action == OVSDB_UPDATE_DEL)
    {
        memset(snooping_config, 0, sizeof(*snooping_config));
        memset(proxy_config, 0, sizeof(*proxy_config));
        memset(querier_config, 0, sizeof(*querier_config));
        memset(other_config, 0, sizeof(*other_config));
        snooping_config->version = OSN_IGMPv2;
        return true;
    }

    /* Snooping */
    if (strcmp(schema->igmp_version, "IGMPv1") == 0)
        snooping_config->version = OSN_IGMPv1;
    if (strcmp(schema->igmp_version, "IGMPv2") == 0)
        snooping_config->version = OSN_IGMPv2;
    else
        snooping_config->version = OSN_IGMPv3;

    snooping_config->enabled = schema->snooping_enabled;
    snooping_config->bridge = schema->snooping_bridge;
    snooping_config->static_mrouter = schema->static_mrouter_port;
    if (strcmp(schema->unknown_mcast_group_behavior, "DROP") == 0)
        snooping_config->unknown_group = OSN_MCAST_UNKNOWN_DROP;
    else
        snooping_config->unknown_group = OSN_MCAST_UNKNOWN_FLOOD;
    snooping_config->robustness_value = schema->query_robustness_value;
    snooping_config->max_groups = schema->maximum_groups;
    snooping_config->max_sources = schema->maximum_sources;
    snooping_config->fast_leave_enable = schema->fast_leave_enable;

    snooping_config->mcast_exceptions_len = schema->mcast_group_exceptions_len;
    snooping_config->mcast_exceptions = CALLOC(schema->mcast_group_exceptions_len, sizeof(char *));
    for (ii = 0; ii < schema->mcast_group_exceptions_len; ii++)
    {
        snooping_config->mcast_exceptions[ii] = strdup(schema->mcast_group_exceptions[ii]);
        ASSERT(snooping_config->mcast_exceptions[ii] != NULL, "Error allocating mcast exception");
    }

    /* Proxy */
    proxy_config->enabled = schema->proxy_enabled;
    proxy_config->upstream_if = schema->proxy_upstream_if;
    proxy_config->downstream_if = schema->proxy_dowstream_if;

    proxy_config->group_exceptions_len = schema->proxy_group_exceptions_len;
    proxy_config->group_exceptions = CALLOC(schema->proxy_group_exceptions_len, sizeof(char *));
    for (ii = 0; ii < schema->proxy_group_exceptions_len; ii++)
    {
        proxy_config->group_exceptions[ii] = strdup(schema->proxy_group_exceptions[ii]);
        ASSERT(proxy_config->group_exceptions[ii] != NULL, "Error allocating proxy exception");
    }

    proxy_config->allowed_subnets_len = schema->proxy_allowed_subnets_len;
    proxy_config->allowed_subnets = CALLOC(schema->proxy_allowed_subnets_len, sizeof(char *));
    for (ii = 0; ii < schema->proxy_allowed_subnets_len; ii++)
    {
        proxy_config->allowed_subnets[ii] = strdup(schema->proxy_allowed_subnets[ii]);
        ASSERT(proxy_config->allowed_subnets[ii] != NULL, "Error allocating allowed subnet");
    }

    /* Querier */
    querier_config->enabled = schema->querier_enabled;
    querier_config->interval = schema->query_interval;
    querier_config->resp_interval = schema->query_response_interval;
    querier_config->last_member_interval = schema->last_member_query_interval;

    /* Other config */
    other_config->oc_len = schema->other_config_len;
    other_config->oc_config = CALLOC(schema->other_config_len, sizeof(struct osn_mcast_oc_kv_pair));

    for (ii = 0; ii < schema->other_config_len; ii++)
    {
        other_config->oc_config[ii].ov_key = strdup(schema->other_config_keys[ii]);
        ASSERT(other_config->oc_config[ii].ov_key != NULL, "Error allocating other_config key");
        other_config->oc_config[ii].ov_value = strdup(schema->other_config[ii]);
        ASSERT(other_config->oc_config[ii].ov_value != NULL, "Error allocating other_config value");
    }

    return true;
}

static bool nm2_mcast_enable_igmp_iface(
        char *ifname,
        bool is_bridge,
        bool enable)
{
    struct nm2_iface *iface;

    if (ifname[0] == '\0')
    {
        return false;
    }

    iface = nm2_iface_get_by_name(ifname);
    if (iface == NULL)
    {
        LOG(INFO, "nm2_mcast_enable_igmp_iface: Cannot find interface %s.", ifname);
        return false;
    }

    /* If this interface is used as a bridge in the multicast config, it should also actually be a bridge */
    if (is_bridge && iface->if_type != NM2_IFTYPE_BRIDGE)
    {
        LOG(ERR, "nm2_mcast_enable_igmp_iface: interface %s is not a bridge", ifname);
        return false;
    }

    LOG(DEBUG, "nm2_mcast_enable_igmp_iface: %s: %s to refs on IGMP unit",
            ifname, (enable) ? "+1" : "-1");

    inet_igmp_update_ref(iface->if_inet, enable);
    nm2_iface_apply(iface);
    return true;
}

/*
 * OVSDB monitor update callback for IGMP_Config
 */
void callback_IGMP_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_IGMP_Config *old,
        struct schema_IGMP_Config *new)
{
    struct osn_igmp_snooping_config snooping_config;
    struct osn_igmp_proxy_config proxy_config;
    struct osn_igmp_querier_config querier_config;
    struct osn_mcast_other_config other_config;

    if (mon->mon_type == OVSDB_UPDATE_ERROR)
    {
        LOGW("%s:mon upd error: %d", __func__, mon->mon_type);
        return;
    }

    /* Parse IGMP_Config schema */
    if (!nm2_parse_igmp_schema(new, &snooping_config, &proxy_config, &querier_config, &other_config, mon->mon_type))
    {
        LOGE("callback_IGMP_Config: Error parsing new IGMP_Config schema");
        return;
    }

    /* If this is a new row, put the the interfaces in cache first */
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        /* If bridge is already configured and we have a new row, deconfigure it first */
        if (igmp_bridge.bridge[0] != '\0')
        {
            /* Disable IGMP unit on all old interfaces */
            LOG(INFO, "callback_IGMP_Config: Two rows configuring the same bridge, overriding the old row.");
            nm2_mcast_enable_igmp_iface(igmp_bridge.bridge, true, false);
            nm2_mcast_enable_igmp_iface(igmp_bridge.static_mrouter, false, false);
            nm2_mcast_enable_igmp_iface(igmp_bridge.pxy_upstream_if, false, false);
            nm2_mcast_enable_igmp_iface(igmp_bridge.pxy_downstream_if, false, false);
        }

        STRSCPY(igmp_bridge._uuid.uuid, new->_uuid.uuid);
        STRSCPY(igmp_bridge.bridge, new->snooping_bridge);
        STRSCPY(igmp_bridge.static_mrouter, new->static_mrouter_port);
        STRSCPY(igmp_bridge.pxy_upstream_if, new->proxy_upstream_if);
        STRSCPY(igmp_bridge.pxy_downstream_if, new->proxy_dowstream_if);

        /* Enable IGMP unit on all interfaces */
        nm2_mcast_enable_igmp_iface(igmp_bridge.bridge, true, true);
        nm2_mcast_enable_igmp_iface(igmp_bridge.static_mrouter, false, true);
        nm2_mcast_enable_igmp_iface(igmp_bridge.pxy_upstream_if, false, true);
        nm2_mcast_enable_igmp_iface(igmp_bridge.pxy_downstream_if, false, true);

        LOGI("callback_IGMP_Config: Registered new IGMP bridge\n");
        goto finish;
    }

    /* If the configured bridge is not of this table row, ignore it */
    if (strncmp(igmp_bridge._uuid.uuid, old->_uuid.uuid, 37) != 0)
    {
        LOG(INFO, "callback_IGMP_Config: row ignored, wrong uuid %s\n", old->_uuid.uuid);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        /* Disable IGMP unit on all interfaces */
        nm2_mcast_enable_igmp_iface(igmp_bridge.bridge, true, false);
        nm2_mcast_enable_igmp_iface(igmp_bridge.static_mrouter, false, false);
        nm2_mcast_enable_igmp_iface(igmp_bridge.pxy_upstream_if, false, false);
        nm2_mcast_enable_igmp_iface(igmp_bridge.pxy_downstream_if, false, false);

        memset(&igmp_bridge, 0, sizeof(igmp_bridge));

        LOGI("callback_IGMP_Config: Deleted IGMP bridge\n");
        goto finish;
    }

    if (new->snooping_bridge_changed)
    {
        nm2_mcast_enable_igmp_iface(igmp_bridge.bridge, true, false);
        STRSCPY(igmp_bridge.bridge, new->snooping_bridge);
        nm2_mcast_enable_igmp_iface(new->snooping_bridge, true, true);
    }

    if (new->static_mrouter_port_changed)
    {
        nm2_mcast_enable_igmp_iface(igmp_bridge.static_mrouter, false, false);
        STRSCPY(igmp_bridge.static_mrouter, new->static_mrouter_port);
        nm2_mcast_enable_igmp_iface(new->static_mrouter_port, false, true);
    }

    if (new->proxy_upstream_if_changed)
    {
        nm2_mcast_enable_igmp_iface(igmp_bridge.pxy_upstream_if, false, false);
        STRSCPY(igmp_bridge.pxy_upstream_if, new->proxy_upstream_if);
        nm2_mcast_enable_igmp_iface(new->proxy_upstream_if, false, true);
    }

    if (new->proxy_dowstream_if_changed)
    {
        nm2_mcast_enable_igmp_iface(igmp_bridge.pxy_downstream_if, false, false);
        STRSCPY(igmp_bridge.pxy_downstream_if, new->proxy_dowstream_if);
        nm2_mcast_enable_igmp_iface(new->proxy_dowstream_if, false, true);
    }

finish:
    /* Push config down to osn layer */
    inet_igmp_set_config(&snooping_config,
                         &proxy_config,
                         &querier_config,
                         &other_config);

    return;
}

/*
 * ===========================================================================
 *  MLD
 * ===========================================================================
 */

static bool nm2_parse_mld_schema(
        struct schema_MLD_Config *schema,
        struct osn_mld_snooping_config *snooping_config,
        struct osn_mld_proxy_config *proxy_config,
        struct osn_mld_querier_config *querier_config,
        struct osn_mcast_other_config *other_config,
        ovsdb_update_type_t action)
{
    int ii;

    /* If row was deleted, leave everything blank */
    if (action == OVSDB_UPDATE_DEL)
    {
        memset(snooping_config, 0, sizeof(*snooping_config));
        memset(proxy_config, 0, sizeof(*proxy_config));
        memset(querier_config, 0, sizeof(*querier_config));
        memset(other_config, 0, sizeof(*other_config));
        snooping_config->version = OSN_MLDv2;
        return true;
    }

    /* Snooping */
    if (strcmp(schema->mld_version, "MLDv1") == 0)
        snooping_config->version = OSN_MLDv1;
    else
        snooping_config->version = OSN_MLDv2;

    snooping_config->enabled = schema->snooping_enabled;
    snooping_config->bridge = schema->snooping_bridge;
    snooping_config->static_mrouter = schema->static_mrouter_port;
    if (strcmp(schema->unknown_mcast_group_behavior, "DROP") == 0)
        snooping_config->unknown_group = OSN_MCAST_UNKNOWN_DROP;
    else
        snooping_config->unknown_group = OSN_MCAST_UNKNOWN_FLOOD;
    snooping_config->robustness_value = schema->query_robustness_value;
    snooping_config->max_groups = schema->maximum_groups;
    snooping_config->max_sources = schema->maximum_sources;
    snooping_config->fast_leave_enable = schema->fast_leave_enable;

    snooping_config->mcast_exceptions_len = schema->mcast_group_exceptions_len;
    snooping_config->mcast_exceptions = CALLOC(schema->mcast_group_exceptions_len, sizeof(char *));
    for (ii = 0; ii < schema->mcast_group_exceptions_len; ii++)
    {
        snooping_config->mcast_exceptions[ii] = strdup(schema->mcast_group_exceptions[ii]);
        ASSERT(snooping_config->mcast_exceptions[ii] != NULL, "Error allocating mcast exception");
    }

    /* Proxy */
    proxy_config->enabled = schema->proxy_enabled;
    proxy_config->upstream_if = schema->proxy_upstream_if;
    proxy_config->downstream_if = schema->proxy_dowstream_if;

    proxy_config->group_exceptions_len = schema->proxy_group_exceptions_len;
    proxy_config->group_exceptions = CALLOC(schema->proxy_group_exceptions_len, sizeof(char *));
    for (ii = 0; ii < schema->proxy_group_exceptions_len; ii++)
    {
        proxy_config->group_exceptions[ii] = strdup(schema->proxy_group_exceptions[ii]);
        ASSERT(proxy_config->group_exceptions[ii] != NULL, "Error allocating proxy exception");
    }

    proxy_config->allowed_subnets_len = schema->proxy_allowed_subnets_len;
    proxy_config->allowed_subnets = CALLOC(schema->proxy_allowed_subnets_len, sizeof(char *));
    for (ii = 0; ii < schema->proxy_allowed_subnets_len; ii++)
    {
        proxy_config->allowed_subnets[ii] = strdup(schema->proxy_allowed_subnets[ii]);
        ASSERT(proxy_config->allowed_subnets[ii] != NULL, "Error allocating allowed subnet");
    }

    /* Querier */
    querier_config->enabled = schema->querier_enabled;
    querier_config->interval = schema->query_interval;
    querier_config->resp_interval = schema->query_response_interval;
    querier_config->last_member_interval = schema->last_member_query_interval;

    /* Other config */
    other_config->oc_len = schema->other_config_len;
    other_config->oc_config = CALLOC(schema->other_config_len, sizeof(struct osn_mcast_oc_kv_pair));

    for (ii = 0; ii < schema->other_config_len; ii++)
    {
        other_config->oc_config[ii].ov_key = strdup(schema->other_config_keys[ii]);
        ASSERT(other_config->oc_config[ii].ov_key != NULL, "Error allocating other_config key");
        other_config->oc_config[ii].ov_value = strdup(schema->other_config[ii]);
        ASSERT(other_config->oc_config[ii].ov_value != NULL, "Error allocating other_config value");
    }

    return true;
}

static bool nm2_mcast_enable_mld_iface(
        char *ifname,
        bool is_bridge,
        bool enable)
{
    struct nm2_iface *iface;

    if (ifname[0] == '\0')
    {
        return false;
    }

    iface = nm2_iface_get_by_name(ifname);
    if (iface == NULL)
    {
        LOG(INFO, "nm2_mcast_enable_mld_iface: Cannot find interface %s.", ifname);
        return false;
    }

    /* If this interface is used as a bridge in the multicast config, it should also actually be a bridge */
    if (is_bridge && iface->if_type != NM2_IFTYPE_BRIDGE)
    {
        LOG(ERR, "nm2_mcast_enable_mld_iface: interface %s is not a bridge", ifname);
        return false;
    }

    LOG(DEBUG, "nm2_mcast_enable_mld_iface: %s: %s to refs on MLD unit",
            ifname, (enable) ? "+1" : "-1");

    inet_mld_update_ref(iface->if_inet, enable);
    nm2_iface_apply(iface);
    return true;
}

/*
 * OVSDB monitor update callback for MLD_Config
 */
void callback_MLD_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_MLD_Config *old,
        struct schema_MLD_Config *new)
{
    struct osn_mld_snooping_config snooping_config;
    struct osn_mld_proxy_config proxy_config;
    struct osn_mld_querier_config querier_config;
    struct osn_mcast_other_config other_config;

    if (mon->mon_type == OVSDB_UPDATE_ERROR)
    {
        LOGW("%s:mon upd error: %d", __func__, mon->mon_type);
        return;
    }

    /* Parse MLD_Config schema */
    if (!nm2_parse_mld_schema(new, &snooping_config, &proxy_config, &querier_config, &other_config, mon->mon_type))
    {
        LOGE("callback_MLD_Config: Error parsing new MLD_Config schema");
        return;
    }

    /* If this is a new row, put the the interfaces in cache first */
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        /* If bridge is already configured and we have a new row, deconfigure it first */
        if (mld_bridge.bridge[0] != '\0')
        {
            /* Disable MLD unit on all old interfaces */
            LOG(INFO, "callback_MLD_Config: Two rows configuring the same bridge, overriding the old row.");
            nm2_mcast_enable_mld_iface(mld_bridge.bridge, true, false);
            nm2_mcast_enable_mld_iface(mld_bridge.static_mrouter, false, false);
            nm2_mcast_enable_mld_iface(mld_bridge.pxy_upstream_if, false, false);
            nm2_mcast_enable_mld_iface(mld_bridge.pxy_downstream_if, false, false);
        }

        STRSCPY(mld_bridge._uuid.uuid, new->_uuid.uuid);
        STRSCPY(mld_bridge.bridge, new->snooping_bridge);
        STRSCPY(mld_bridge.static_mrouter, new->static_mrouter_port);
        STRSCPY(mld_bridge.pxy_upstream_if, new->proxy_upstream_if);
        STRSCPY(mld_bridge.pxy_downstream_if, new->proxy_dowstream_if);

        /* Enable MLD unit on all interfaces */
        nm2_mcast_enable_mld_iface(mld_bridge.bridge, true, true);
        nm2_mcast_enable_mld_iface(mld_bridge.static_mrouter, false, true);
        nm2_mcast_enable_mld_iface(mld_bridge.pxy_upstream_if, false, true);
        nm2_mcast_enable_mld_iface(mld_bridge.pxy_downstream_if, false, true);

        LOGI("callback_MLD_Config: Registered new MLD bridge\n");
        goto finish;
    }

    /* If the configured bridge is not of this table row, ignore it */
    if (strncmp(mld_bridge._uuid.uuid, old->_uuid.uuid, 37) != 0)
    {
        LOG(INFO, "callback_MLD_Config: row ignored, wrong uuid %s\n", old->_uuid.uuid);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        /* Disable MLD unit on all interfaces */
        nm2_mcast_enable_mld_iface(mld_bridge.bridge, true, false);
        nm2_mcast_enable_mld_iface(mld_bridge.static_mrouter, false, false);
        nm2_mcast_enable_mld_iface(mld_bridge.pxy_upstream_if, false, false);
        nm2_mcast_enable_mld_iface(mld_bridge.pxy_downstream_if, false, false);

        memset(&mld_bridge, 0, sizeof(mld_bridge));

        LOGI("callback_MLD_Config: Deleted MLD bridge\n");
        goto finish;
    }

    if (new->snooping_bridge_changed)
    {
        nm2_mcast_enable_mld_iface(mld_bridge.bridge, true, false);
        STRSCPY(mld_bridge.bridge, new->snooping_bridge);
        nm2_mcast_enable_mld_iface(new->snooping_bridge, true, true);
    }

    if (new->static_mrouter_port_changed)
    {
        nm2_mcast_enable_mld_iface(mld_bridge.static_mrouter, false, false);
        STRSCPY(mld_bridge.static_mrouter, new->static_mrouter_port);
        nm2_mcast_enable_mld_iface(new->static_mrouter_port, false, true);
    }

    if (new->proxy_upstream_if_changed)
    {
        nm2_mcast_enable_mld_iface(mld_bridge.pxy_upstream_if, false, false);
        STRSCPY(mld_bridge.pxy_upstream_if, new->proxy_upstream_if);
        nm2_mcast_enable_mld_iface(new->proxy_upstream_if, false, true);
    }

    if (new->proxy_dowstream_if_changed)
    {
        nm2_mcast_enable_mld_iface(mld_bridge.pxy_downstream_if, false, false);
        STRSCPY(mld_bridge.pxy_downstream_if, new->proxy_dowstream_if);
        nm2_mcast_enable_mld_iface(new->proxy_dowstream_if, false, true);
    }

finish:
    /* Push config down to osn layer */
    inet_mld_set_config(&snooping_config,
                        &proxy_config,
                        &querier_config,
                        &other_config);

    return;
}

/*
 * ===========================================================================
 *  MISC
 * ===========================================================================
 */

void nm2_mcast_init_ifc(struct nm2_iface *iface)
{
    /* Interface was just created, check if it is already used by any bridge */

    if (strncmp(iface->if_name, igmp_bridge.bridge, C_IFNAME_LEN) == 0)
        nm2_mcast_enable_igmp_iface(iface->if_name, true, true);
    if (strncmp(iface->if_name, igmp_bridge.static_mrouter, C_IFNAME_LEN) == 0)
        nm2_mcast_enable_igmp_iface(iface->if_name, false, true);
    if (strncmp(iface->if_name, igmp_bridge.pxy_upstream_if, C_IFNAME_LEN) == 0)
        nm2_mcast_enable_igmp_iface(iface->if_name, false, true);
    if (strncmp(iface->if_name, igmp_bridge.pxy_downstream_if, C_IFNAME_LEN) == 0)
        nm2_mcast_enable_igmp_iface(iface->if_name, false, true);

    if (strncmp(iface->if_name, mld_bridge.bridge, C_IFNAME_LEN) == 0)
        nm2_mcast_enable_mld_iface(iface->if_name, true, true);
    if (strncmp(iface->if_name, mld_bridge.static_mrouter, C_IFNAME_LEN) == 0)
        nm2_mcast_enable_mld_iface(iface->if_name, false, true);
    if (strncmp(iface->if_name, mld_bridge.pxy_upstream_if, C_IFNAME_LEN) == 0)
        nm2_mcast_enable_mld_iface(iface->if_name, false, true);
    if (strncmp(iface->if_name, mld_bridge.pxy_downstream_if, C_IFNAME_LEN) == 0)
        nm2_mcast_enable_mld_iface(iface->if_name, false, true);

    return;
}
