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

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "osn_types.h"
#include "osn_route_rule.h"
#include "memutil.h"
#include "log.h"
#include "util.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "ovsdb_update.h"
#include "ds_tree.h"

#define SCHEMA_STR_STATUS_DISABLED  "disabled"
#define SCHEMA_STR_STATUS_ENABLED   "enabled"
#define SCHEMA_STR_STATUS_ERROR     "error"

/*
 * Keep track of Policy_Routing_Rule configs
 */
struct nm2_route_rule_cfg
{
    char                   nr_name[C_MAXPATH_LEN];  /* Route rule name */

    osn_route_rule_cfg_t   nr_route_rule;           /* OSN route rule config */

    bool                   nr_enabled;              /* True if successfully added to the system */

    ds_tree_node_t         ti_tnode;
};

enum nm2_route_rule_status
{
    NM2_RRULE_STATUS_DISABLED,
    NM2_RRULE_STATUS_ENABLED,
    NM2_RRULE_STATUS_ERROR,
};

/*
 * The OVSDB table Policy_Routing_Rule we're handling here.
 */
static ovsdb_table_t table_Policy_Routing_Rule;

/* Keeping track of Policy_Routing_Rule configs */
static ds_tree_t nm2_route_rule_list = DS_TREE_INIT(ds_str_cmp, struct nm2_route_rule_cfg, ti_tnode);

/* Get the OSN route rule context object. */
static osn_route_rule_t *get_route_rule_obj(void)
{
    static osn_route_rule_t *route_rule;

    if (route_rule == NULL)
    {
        route_rule = osn_route_rule_new();
        if (route_rule == NULL)
        {
            LOG(ERR, "nm2_route_rule: Error creating OSN route rule context");
            return NULL;
        }
    }
    return route_rule;
}

static struct nm2_route_rule_cfg *nm2_route_rule_cfg_new(const char *name)
{
    struct nm2_route_rule_cfg *nm2_rrule_cfg;

    nm2_rrule_cfg = CALLOC(1, sizeof(*nm2_rrule_cfg));
    STRSCPY(nm2_rrule_cfg->nr_name, name);
    nm2_rrule_cfg->nr_route_rule = OSN_ROUTE_RULE_CFG_INIT;

    ds_tree_insert(&nm2_route_rule_list, nm2_rrule_cfg, nm2_rrule_cfg->nr_name);

    return nm2_rrule_cfg;
}

static bool nm2_route_rule_cfg_del(struct nm2_route_rule_cfg *nm2_rrule)
{
    ds_tree_remove(&nm2_route_rule_list, nm2_rrule);
    FREE(nm2_rrule);

    return true;
}

static struct nm2_route_rule_cfg *nm2_route_rule_cfg_get(const char *name)
{
    return ds_tree_find(&nm2_route_rule_list, name);
}

/* Update the rule's status in OVSDB. */
static bool nm2_route_rule_ovsdb_status_update(const char *name, enum nm2_route_rule_status new_status)
{
    struct schema_Policy_Routing_Rule schema_rrule;

    memset(&schema_rrule, 0, sizeof(schema_rrule));
    schema_rrule._partial_update = true;

    if (new_status == NM2_RRULE_STATUS_DISABLED)
    {
        STRSCPY(schema_rrule.status, SCHEMA_STR_STATUS_DISABLED);
    }
    else if (new_status == NM2_RRULE_STATUS_ENABLED)
    {
        STRSCPY(schema_rrule.status, SCHEMA_STR_STATUS_ENABLED);
    }
    else
    {
        STRSCPY(schema_rrule.status, SCHEMA_STR_STATUS_ERROR);
    }

    schema_rrule.status_exists = true;
    schema_rrule.status_present = true;

    if (!ovsdb_table_update_where(
            &table_Policy_Routing_Rule,
            ovsdb_where_simple(SCHEMA_COLUMN(Policy_Routing_Rule, name), name),
            &schema_rrule))
    {
        LOG(ERR, "nm2_route_rule: %s: Error updating Policy_Routing_Rule status", name);
        return false;
    }

    return true;
}

/* Add the policy routing rule to the system. */
static bool nm2_route_rule_add(struct nm2_route_rule_cfg *nm2_rrule_cfg)
{
    osn_route_rule_t *route_rule_obj = get_route_rule_obj();
    bool rv = true;

    if (route_rule_obj == NULL) return false;

    rv &= osn_route_rule_add(route_rule_obj, &nm2_rrule_cfg->nr_route_rule);

    rv &= osn_route_rule_apply(route_rule_obj);

    if (rv)
    {
        nm2_rrule_cfg->nr_enabled = true;
    }

    return rv;
}

/* Remove/delete the policy routing rule from the system. */
static bool nm2_route_rule_remove(struct nm2_route_rule_cfg *nm2_rrule_cfg)
{
    osn_route_rule_t *route_rule_obj = get_route_rule_obj();
    bool rv = true;

    if (route_rule_obj == NULL) return false;

    rv &= osn_route_rule_remove(route_rule_obj, &nm2_rrule_cfg->nr_route_rule);

    rv &= osn_route_rule_apply(route_rule_obj);

    if (rv)
    {
        /* Not really necessary as this rule config will be removed
         * from our cache anyway, but set the flag for consistency: */
        nm2_rrule_cfg->nr_enabled = false;
    }

    return rv;
}

/*
 * Set policy routing rule config from schema to OSN struct.
 */
static bool nm2_route_rule_config_set(
        struct nm2_route_rule_cfg *nm2_rrule,
        const struct schema_Policy_Routing_Rule *conf)
{
    osn_route_rule_cfg_t *rule_cfg = &nm2_rrule->nr_route_rule;

    if (strcmp(conf->addr_family, "ipv4") == 0)
    {
        rule_cfg->rc_addr_family = AF_INET;
    }
    else
    {
        rule_cfg->rc_addr_family = AF_INET6;
    }

    if (conf->priority_exists)
    {
        rule_cfg->rc_priority = conf->priority;
    }
    else
    {
        rule_cfg->rc_priority = 0;
    }

    if (conf->selector_not_exists && conf->selector_not)
    {
        rule_cfg->rc_selector.rs_negate_rule = true;
    }
    else
    {
        rule_cfg->rc_selector.rs_negate_rule = false;
    }

    if (conf->selector_src_prefix_exists)
    {
        if (!osn_ipany_addr_from_str(&rule_cfg->rc_selector.rs_src, conf->selector_src_prefix))
        {
            LOG(ERR, "nm2_route_rule: Error parsing selector_src_prefix: %s", conf->selector_src_prefix);
            return false;
        }
        rule_cfg->rc_selector.rs_src_set = true;
    }
    else
    {
        rule_cfg->rc_selector.rs_src_set = false;
    }

    if (conf->selector_dst_prefix_exists)
    {
        if (!osn_ipany_addr_from_str(&rule_cfg->rc_selector.rs_dst, conf->selector_dst_prefix))
        {
            LOG(ERR, "nm2_route_rule: Error parsing selector_dst_prefix: %s", conf->selector_dst_prefix);
            return false;
        }
        rule_cfg->rc_selector.rs_dst_set = true;
    }
    else
    {
        rule_cfg->rc_selector.rs_dst_set = false;
    }

    if (conf->selector_input_intf_exists)
    {
        STRSCPY(rule_cfg->rc_selector.rs_input_if, conf->selector_input_intf);
    }
    else
    {
        rule_cfg->rc_selector.rs_input_if[0] = '\0';
    }

    if (conf->selector_output_intf_exists)
    {
        STRSCPY(rule_cfg->rc_selector.rs_output_if, conf->selector_output_intf);
    }
    else
    {
        rule_cfg->rc_selector.rs_output_if[0] = '\0';
    }

    if (conf->selector_fwmark_exists)
    {
        rule_cfg->rc_selector.rs_fwmark = conf->selector_fwmark;
        rule_cfg->rc_selector.rs_fwmark_set = true;
    }
    else
    {
        rule_cfg->rc_selector.rs_fwmark_set = false;
    }

    if (conf->selector_fwmask_exists)
    {
        rule_cfg->rc_selector.rs_fwmask = conf->selector_fwmask;
        rule_cfg->rc_selector.rs_fwmask_set = true;
    }
    else
    {
        rule_cfg->rc_selector.rs_fwmask_set = false;
    }

    if (conf->action_lookup_table_exists)
    {
        rule_cfg->rc_action.ra_lookup_table = conf->action_lookup_table;
    }
    else
    {
        rule_cfg->rc_action.ra_lookup_table = 0;
    }

    if (conf->action_suppress_preflen_exists)
    {
        rule_cfg->rc_action.ra_suppress_prefixlength = conf->action_suppress_preflen;
        rule_cfg->rc_action.ra_suppress_prefixlength_set = true;
    }
    else
    {
        rule_cfg->rc_action.ra_suppress_prefixlength_set = false;
    }

    return true;
}

/*
 * OVSDB monitor update callback for Policy_Routing_Rule
 */
void callback_Policy_Routing_Rule(
        ovsdb_update_monitor_t *mon,
        struct schema_Policy_Routing_Rule *old,
        struct schema_Policy_Routing_Rule *new)
{

    struct nm2_route_rule_cfg *nm2_rrule;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(INFO, "nm2_route_rule: %s: Policy_Routing_Rule update: NEW row", new->name);

            nm2_rrule = nm2_route_rule_cfg_new(new->name);
            if (!nm2_route_rule_config_set(nm2_rrule, new))
            {
                nm2_route_rule_ovsdb_status_update(new->name, NM2_RRULE_STATUS_ERROR);
                LOG(ERR, "nm2_route_rule: %s: Error parsing OVSDB config", new->name);
                return;
            }
            if (!nm2_route_rule_add(nm2_rrule))
            {
                nm2_route_rule_ovsdb_status_update(new->name, NM2_RRULE_STATUS_DISABLED);
                LOG(ERR, "nm2_route_rule: %s: Error adding route rule to the system", new->name);
                return;
            }

            nm2_route_rule_ovsdb_status_update(new->name, NM2_RRULE_STATUS_ENABLED);
            break;

        case OVSDB_UPDATE_MODIFY:
            LOG(INFO, "nm2_route_rule: %s: Policy_Routing_Rule update: MODIFY row", new->name);

            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Policy_Routing_Rule, status)))
            {
                /* Ignore OVSDB update callbacks for column 'status' as those are initiated by us */
                return;
            }

            /* Find the existing route rule by name: */
            nm2_rrule = nm2_route_rule_cfg_get(new->name);
            if (nm2_rrule == NULL)
            {
                LOG(ERROR, "nm2_route_rule: %s: Existing rule not found", new->name);
                return;
            }

            /* First, delete the existing route rule (if it was actually installed): */
            if (nm2_rrule->nr_enabled && !nm2_route_rule_remove(nm2_rrule))
            {
                LOG(ERROR, "nm2_route_rule: %s: Error removing route rule from the system", new->name);
            }
            nm2_route_rule_cfg_del(nm2_rrule);

            /* Then create a new route rule config: */
            nm2_rrule = nm2_route_rule_cfg_new(new->name);
            if (!nm2_route_rule_config_set(nm2_rrule, new))
            {
                nm2_route_rule_ovsdb_status_update(new->name, NM2_RRULE_STATUS_ERROR);
                LOG(ERR, "nm2_route_rule: %s: Error parsing OVSDB config", new->name);
                return;
            }
            /* And add the new route rule to the system: */
            if (!nm2_route_rule_add(nm2_rrule))
            {
                nm2_route_rule_ovsdb_status_update(new->name, NM2_RRULE_STATUS_DISABLED);
                LOG(ERR, "nm2_route_rule: %s: Error adding route rule to the system", new->name);
                return;
            }

            nm2_route_rule_ovsdb_status_update(new->name, NM2_RRULE_STATUS_ENABLED);
            break;

        case OVSDB_UPDATE_DEL:
            LOG(INFO, "nm2_route_rule: %s: Policy_Routing_Rule update: DELETE row", new->name);

            nm2_rrule = nm2_route_rule_cfg_get(new->name);
            if (nm2_rrule == NULL)
            {
                LOG(ERROR, "nm2_route_rule: %s: Cannot delete policy routing rule: not found", new->name);
                return;
            }

            /* Delete the rule (if it was actually installed): */
            if (nm2_rrule->nr_enabled && !nm2_route_rule_remove(nm2_rrule))
            {
                LOG(ERROR, "nm2_route_rule: %s: Error removing route rule from the system", new->name);
            }
            nm2_route_rule_cfg_del(nm2_rrule);

            break;

        default:
            LOG(ERROR, "nm2_route_rule: Monitor update error.");
            return;
    }
}

/* Initialize NM2 Policy Routing Rule handling. */
bool nm2_route_rule_init(void)
{
    LOG(INFO, "Initializing NM2 Policy_Routing_Rule monitoring.");

    OVSDB_TABLE_INIT(Policy_Routing_Rule, name);
    OVSDB_TABLE_MONITOR(Policy_Routing_Rule, false);

    return true;
}
