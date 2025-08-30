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
#include <strings.h>
#include <jansson.h>

#include "osn_map.h"
#include "osn_map_v6plus.h"
#include "osn_types.h"

#include "memutil.h"
#include "log.h"
#include "util.h"
#include "const.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"
#include "schema.h"
#include "ds_tree.h"
#include "kconfig.h"
#include "os_random.h"
#include "osp_ps.h"
#include "ev.h"
#include "evx.h"

#define MAX_DHCPV6_OPT_LEN    8192

#define NM2_V6PLUS_STORE         "nm2_4in6_map_v6plus"
#define NM2_V6PLUS_STORE_RULES   "nm2_4in6_map_v6plus_rules"

#define NM2_V6PLUS_HGW_STATUS_RECHECK_TIME   5
#define NM2_V6PLUS_HGW_STATUS_RECONFIRM_NUM  4

typedef enum
{
    V6PLUS_STATUS_UNSET,
    V6PLUS_BOOTUP_SAVED_RULES,
    V6PLUS_SERV_REPLY_OK_V6PLUS_OK,   /* 200, rules OK, v6plus operational */
    V6PLUS_SERV_REPLY_OK_NO_RULE,     /* 200, empty rules OR rules received, but no rule matching */
    V6PLUS_SERV_REPLY_OK,             /* 200; intermediate state: turns either to V6PLUS_SERV_REPLY_OK_V6PLUS_OK or V6PLUS_SERV_REPLY_OK_NO_RULE */
    V6PLUS_SERV_ERR,                  /* 5xx OR timeout i.e. no response */
    V6PLUS_CLIENT_ERR                 /* 403 Access Forbidden */
} v6plus_status_t;

/* Keep track of MAP_Config configs */
struct nm2_mapcfg
{
    char              mc_if_name[C_MAXPATH_LEN];  /* MAP interface name */

    osn_map_t        *mc_map;                     /* OSN MAP config object */

    enum osn_map_type mc_map_type;                /* MAP type (MAP-E or MAP-T) */

    bool              mc_rules_dhcp;              /* MAP rules from DHCP options */
    bool              mc_rules_v6plus;            /* MAP rules from MAP rules distribution server (v6plus) */

    char              mc_rules_url[512];          /* MAP rules distribution server API endpoint URL */

    bool              mc_map_legacy_draft3;       /* Use MAP Legacy RFC draft 3 */

    bool              mc_enduser_prefix_override;
    bool              mc_enduser_prefix_ra;       /* Use RA for the end-user prefix */
    bool              mc_enduser_prefix_dhcpv6pd; /* Use DHCPv6-PD for the end-user prefix */

    /*
     * Same as above configs mc_enduser_*, but effective STATE. For enduser prefix CONFIG,
     * both can end up to be true which means "try to use both (first IA_PD)",
     * but for effective STATE none or exactly 1 may be true.
     */
    bool              mc_st_enduser_prefix_ra;
    bool              mc_st_enduser_prefix_dhcpv6pd;

    /* v6plus specific: */
    v6plus_status_t   mc_v6plus_last_status;      /* Last v6plus status */
    ev_debounce       mc_v6plus_job;              /* v6plus continues operation handler */
    bool              mc_v6plus_job_inited;       /* v6plus job inited? */
    osn_map_v6plus_rulelist_t
                     *mc_v6plus_saved_rules;      /* v6plus saved MAP rules */
    osn_map_v6plus_hgw_status_t
                      mc_v6plus_hgw_status;       /* Last determined HGW status */
    unsigned          mc_v6plus_hgw_failed_detect_cnt;

    ev_timer          mc_v6plus_hgw_checker;      /* Continuous HGW status checker */
    bool              mc_v6plus_hgw_checker_inited;

    ds_tree_node_t    mc_tnode;
};

struct nm2_map
{
    char    nm_dhcp_option_26[MAX_DHCPV6_OPT_LEN];  /* Cached DHCP_Option 26 (IA_PD) value */
    char    nm_dhcp_option_94[MAX_DHCPV6_OPT_LEN];  /* Cached DHCP_Option 94 (MAP-E) value */
    char    nm_dhcp_option_95[MAX_DHCPV6_OPT_LEN];  /* Cached DHCP_Option 96 (MAP-T) value */

    char    nm_uplink_intf[C_MAXPATH_LEN];          /* Uplink interface name */
};

/* The OVSDB tables MAP_Config and MAP_State: MAP-T/MAP-E configuration and state */
static ovsdb_table_t table_MAP_Config;
static ovsdb_table_t table_MAP_State;
/* DHCP_Option table: Getting MAP-T/MAP-E and IA_PD DHCPv6 options */
static ovsdb_table_t table_DHCP_Option;
/* Connection_Manager_Uplink table: to get info about the uplink: */
static ovsdb_table_t table_Connection_Manager_Uplink;
/* IP_Interface/IPv6_Address are used to figure out the prefix from RA: */
static ovsdb_table_t table_IP_Interface;
static ovsdb_table_t table_IPv6_Address;

/* Keeping track of MAP_Config configs */
static ds_tree_t nm2_mapcfg_list = DS_TREE_INIT(ds_str_cmp, struct nm2_mapcfg, mc_tnode);

static struct nm2_map *nm2_map_ctx;

static void nm2_map_ctx_dhcp_opt_update(int optcode, const char *value);
static bool nm2_mapcfg_set_update_map_rules_dhcp(struct nm2_mapcfg *mapcfg);
static bool nm2_mapcfg_set_update_map_rules_dhcp_all_apply(enum osn_map_type map_type);
static bool nm2_mapcfg_set_update_map_rules_cloud(struct nm2_mapcfg *mapcfg, const struct schema_MAP_Config *conf);
static bool nm2_mapcfg_v6plus_map_rules_get(struct nm2_mapcfg *mapcfg, osn_map_v6plus_rulelist_t **_rule_list);
static bool nm2_mapcfg_set_update_enduser_IPv6_prefix(struct nm2_mapcfg *mapcfg);
static const char *util_map_type_to_str(enum osn_map_type map_type);

void nm2_mapcfg_v6plus_operation_handler(struct nm2_mapcfg *mapcfg);
void nm2_mapcfg_v6plus_job_handler(struct ev_loop *loop, struct ev_debounce *ev, int revent);
void nm2_mapcfg_v6plus_job_stop(struct nm2_mapcfg *mapcfg);
void nm2_mapcfg_v6plus_job_schedule(struct nm2_mapcfg *mapcfg, double timeout);
bool nm2_mapcfg_v6plus_operation_report(
    struct nm2_mapcfg *mapcfg,
    osn_map_v6plus_rulelist_t *rule_list,
    osn_map_v6plus_status_action_t status_action,
    osn_map_v6plus_status_reason_t status_reason);
bool nm2_mapcfg_v6plus_saved_rules_set(struct nm2_mapcfg *mapcfg, const osn_map_v6plus_rulelist_t *rule_list);
bool nm2_mapcfg_v6plus_saved_rules_get(struct nm2_mapcfg *mapcfg, osn_map_v6plus_rulelist_t **rule_list);
bool nm2_mapcfg_v6plus_saved_rules_del(struct nm2_mapcfg *mapcfg);
void nm2_mapcfg_v6plus_hgw_checker_start(struct nm2_mapcfg *mapcfg);
void nm2_mapcfg_v6plus_hgw_checker_stop(struct nm2_mapcfg *mapcfg);

static struct nm2_mapcfg *nm2_mapcfg_new(const char *if_name)
{
    struct nm2_mapcfg *mapcfg;

    mapcfg = CALLOC(1, sizeof(*mapcfg));
    STRSCPY(mapcfg->mc_if_name, if_name);

    /* Create OSN layer MAP object instance: */
    mapcfg->mc_map = osn_map_new(if_name);

    ds_tree_insert(&nm2_mapcfg_list, mapcfg, mapcfg->mc_if_name);

    return mapcfg;
}

static bool nm2_mapcfg_del(struct nm2_mapcfg *mapcfg)
{
    bool rv = true;

    /* Delete OSN layer MAP object instance: */
    rv &= osn_map_del(mapcfg->mc_map);

    ds_tree_remove(&nm2_mapcfg_list, mapcfg);
    FREE(mapcfg);
    return rv;
}

static struct nm2_mapcfg *nm2_mapcfg_get(const char *if_name)
{
    return ds_tree_find(&nm2_mapcfg_list, if_name);
}

/* Adjust the v6plus URL: For any defined configuration variable references
 * (in the form of "${config_name}"), replace the variable references with
 * the actual configuration from Kconfig.
 */
static bool nm2_mapcfg_v6plus_url_adjust(char *url_buf, size_t url_buf_len)
{
    const char *token_manufacturer_code = "${manufacturer_code}";
    char tmp_buf[512];
    char *tok_begin;
    char *tok_end;

    if ((tok_begin = strstr(url_buf, token_manufacturer_code)) != NULL)
    {
        tok_end = strchr(tok_begin, '}');
        STRSCPY(tmp_buf, tok_end+1); // save temporarily

        *tok_begin = '\0';
        // Overwrite the placeholder token with the actual config:
        strscat(url_buf, CONFIG_OSN_MAP_V6PLUS_MANUFACTURER_CODE, url_buf_len);
        // Copy the saved string after the placeholder at the end:
        strscat (url_buf, tmp_buf, url_buf_len);
    }

    return true;
}

/*
 * Set MAP config from schema to OSN MAP object.
 */
static bool nm2_mapcfg_config_set(
        struct nm2_mapcfg *mapcfg,
        const struct schema_MAP_Config *conf,
        ovsdb_update_monitor_t *mon)
{
    enum osn_map_type map_type;
    int rule_method_count = 0;
    int ii;

    /* MAP type: MAP-E or MAP-T: */
    if (strcmp(conf->map_type, "map-t") == 0)
    {
        map_type = OSN_MAP_TYPE_MAP_T;
    }
    else if (strcmp(conf->map_type, "map-e") == 0)
    {
        map_type = OSN_MAP_TYPE_MAP_E;
    }
    else
    {
        map_type = OSN_MAP_TYPE_NOT_SET;
    }
    mapcfg->mc_map_type = map_type;
    osn_map_type_set(mapcfg->mc_map, map_type);

    /* Validate that only 1 and exactly 1 MAP rules provisioning method configured: */
    if (conf->map_rules_dhcp_exists && conf->map_rules_dhcp)
    {
        rule_method_count++;
    }
    if (conf->map_rules_v6plus_url_exists)
    {
        rule_method_count++;
    }
    if (conf->BMR_IPv6_prefix_exists
            || conf->BMR_IPv4_prefix_exists
            || conf->BMR_ea_len_exists
            || conf->BMR_psid_offset_exists
            || conf->DMR_exists)
    {
        rule_method_count++;
    }
    if (rule_method_count != 1)
    {
        LOG(ERR, "nm2_4in6_map: %s: Exactly 1 MAP rules provisioning method must be configured: "
                    "DHCP, v6plus or cloud-pushed MAP rule", mapcfg->mc_if_name);
        return false;
    }

    /* Use legacy MAP RFC draft3? */
    if (conf->map_legacy_draft3_exists && conf->map_legacy_draft3)
    {
        mapcfg->mc_map_legacy_draft3 = true;
    }
    else
    {
        mapcfg->mc_map_legacy_draft3 = false;
    }
    osn_map_use_legacy_map_draft3(mapcfg->mc_map, mapcfg->mc_map_legacy_draft3);

    /* Set uplink interface name: */
    if (nm2_map_ctx->nm_uplink_intf[0] != '\0')
    {
        osn_map_uplink_set(mapcfg->mc_map, nm2_map_ctx->nm_uplink_intf);
    }
    else
    {
        LOG(WARN, "nm2_4in6_map: %s: Uplink interface name not known", mapcfg->mc_if_name);
        osn_map_uplink_set(mapcfg->mc_map, NULL);
    }

    /*
     * Extract (optional) end-user IPv6 prefix config from other_config
     */
    mapcfg->mc_enduser_prefix_ra = false;
    mapcfg->mc_enduser_prefix_dhcpv6pd = false;

    for (ii = 0; ii < conf->other_config_len; ii++)
    {
        if (strcmp("end_user_prefix", conf->other_config_keys[ii]) == 0)
        {
            if (strcmp("ra", conf->other_config[ii]) == 0)
            {
                mapcfg->mc_enduser_prefix_ra = true;
                LOG(INFO, "nm2_4in6_map: %s: Explicit end-user IPv6 prefix config: use RA", mapcfg->mc_if_name);
            }
            else if (strcmp("dhcpv6-pd", conf->other_config[ii]) == 0)
            {
                mapcfg->mc_enduser_prefix_dhcpv6pd = true;
                LOG(INFO, "nm2_4in6_map: %s: Explicit end-user IPv6 prefix config: use IA_PD", mapcfg->mc_if_name);
            }
        }
    }
    /*
     * If at this point exactly one of mapcfg->mc_enduser_prefix_ra, mapcfg->mc_enduser_prefix_dhcpv6pd
     * is true, that means we have a config override: explicit config option to either use IA_PD
     * or RA as end-user IPv6 prefix.
     */
    mapcfg->mc_enduser_prefix_override = (mapcfg->mc_enduser_prefix_dhcpv6pd ^ mapcfg->mc_enduser_prefix_ra);

    /*
     * If no end-user IPv6 prefix config options were set (both false), use the default, which is:
     * try both, but first try IA_PD, if IA_PD not received use RA.
     */
    if (!mapcfg->mc_enduser_prefix_ra && !mapcfg->mc_enduser_prefix_dhcpv6pd)
    {
        mapcfg->mc_enduser_prefix_ra = true;
        mapcfg->mc_enduser_prefix_dhcpv6pd = true;
    }

    /* MAP rules provisioning method: DHCP: */
    if (conf->map_rules_dhcp_exists && conf->map_rules_dhcp)
    {
        mapcfg->mc_rules_dhcp = true;

        LOG(INFO, "nm2_4in6_map: %s: Configuring MAP: type:%s, MAP rules method:DHCP",
                mapcfg->mc_if_name,
                util_map_type_to_str(mapcfg->mc_map_type));

        /* Get MAP rules from DHCP option and set them to OSN MAP config: */
        if (!nm2_mapcfg_set_update_map_rules_dhcp(mapcfg))
        {
            return false;
        }
        /* Determine End-user IPv6 prefix and set it to OSN MAP config: */
        if (!nm2_mapcfg_set_update_enduser_IPv6_prefix(mapcfg))
        {
            return false;
        }
        return true;
    }
    else
    {
        mapcfg->mc_rules_dhcp = false;
    }

    /* MAP rules provisioning method: MAP rules distribution server (v6plus): */
    if (conf->map_rules_v6plus_url_exists)
    {
        mapcfg->mc_rules_v6plus = true;
        STRSCPY(mapcfg->mc_rules_url, conf->map_rules_v6plus_url);

        /* Adjust the URL: Replace any config vars with values from Kconfig: */
        nm2_mapcfg_v6plus_url_adjust(mapcfg->mc_rules_url, sizeof(mapcfg->mc_rules_url));

        LOG(INFO, "nm2_4in6_map: %s: Configuring MAP: type:%s, MAP rules method:v6plus",
                mapcfg->mc_if_name,
                util_map_type_to_str(mapcfg->mc_map_type));

        /*
         * For other non-v6plus MAP types, at this point we would retrieve MAP rules, set them
         * to OSN object and determine end-user IPv6 prefix. For v6plus the logic is a bit more
         * complex, so we return here as it will be (along with additional logic) handled separately.
         */
        return true;
    }
    else
    {
        mapcfg->mc_rules_v6plus = false;
        STRSCPY(mapcfg->mc_rules_url, "");
    }

    /* Cloud-pushed MAP BMR rule and DMR: */
    if ( conf->BMR_IPv6_prefix_exists
            && conf->BMR_IPv4_prefix_exists
            && conf->BMR_ea_len_exists
            && conf->BMR_psid_offset_exists
            && conf->DMR_exists )
    {
        LOG(INFO, "nm2_4in6_map: %s: Configuring MAP: type:%s, MAP rules method:cloud-configured MAP rule",
                mapcfg->mc_if_name,
                util_map_type_to_str(mapcfg->mc_map_type));

        /* Take the cloud-pushed static MAP rule and configure it to OSN MAP config: */
        if (!nm2_mapcfg_set_update_map_rules_cloud(mapcfg, conf))
        {
            return false;
        }
        /* Determine End-user IPv6 prefix and set it to OSN MAP config: */
        if (!nm2_mapcfg_set_update_enduser_IPv6_prefix(mapcfg))
        {
            return false;
        }
        return true;
    }

    return false;
}

/* Construct PORT SETS string from port sets structure.
 * @return number of characters in the string.
 *         If the returned value is equal or grater than 'size',
 *         that means that the string was truncated.
 */
static int util_portsets_tostr(
    char *buf,
    int size,
    const struct osn_map_portset *portsets,
    unsigned num_portsets)
{
    unsigned idx;
    int n = 0;

    for (idx = 0; idx < num_portsets; idx++)
    {
        if (idx > 0)
        {
            n += snprintf(buf+n, size-n, ",");
            if (n >= size) return n;
        }
        n += snprintf(buf+n, size-n, "%u-%u", portsets[idx].op_from, portsets[idx].op_to);
        if (n >= size) return n;
    }
    return n;
}

/* Set any v6plus-specific state fields to MAP_State schema object, preparing for an upsert. */
static bool nm2_mapcfg_v6plus_ovsdb_state_set(struct schema_MAP_State *MAP_State, struct nm2_mapcfg *mapcfg)
{
    SCHEMA_UNSET_FIELD(MAP_State->v6plus_hgw_status);
    SCHEMA_UNSET_FIELD(MAP_State->v6plus_rules_status);

    if (!mapcfg->mc_rules_v6plus)
    {
        return true;
    }

    /* Last determined HGW status: */
    if (mapcfg->mc_v6plus_hgw_status == OSN_MAP_V6PLUS_UNKNOWN)
    {
        SCHEMA_SET_STR(MAP_State->v6plus_hgw_status, "unknown");
    }
    else if (mapcfg->mc_v6plus_hgw_status == OSN_MAP_V6PLUS_MAP_OFF)
    {
        SCHEMA_SET_STR(MAP_State->v6plus_hgw_status, "map_off");
    }
    else if (mapcfg->mc_v6plus_hgw_status == OSN_MAP_V6PLUS_MAP_ON)
    {
        SCHEMA_SET_STR(MAP_State->v6plus_hgw_status, "map_on");
    }

    /* Last v6plus MAP rules status: */
    switch (mapcfg->mc_v6plus_last_status)
    {
        case V6PLUS_BOOTUP_SAVED_RULES:
            SCHEMA_SET_STR(MAP_State->v6plus_rules_status, "init_saved_rules");
        break;

        case V6PLUS_SERV_REPLY_OK_V6PLUS_OK:
            SCHEMA_SET_STR(MAP_State->v6plus_rules_status, "serv_reply_ok_v6plus_ok");
        break;

        case V6PLUS_SERV_REPLY_OK_NO_RULE:
            SCHEMA_SET_STR(MAP_State->v6plus_rules_status, "serv_reply_ok_no_rule");
        break;

        case V6PLUS_SERV_ERR:
            SCHEMA_SET_STR(MAP_State->v6plus_rules_status, "serv_err");
        break;

        case V6PLUS_CLIENT_ERR:
            SCHEMA_SET_STR(MAP_State->v6plus_rules_status, "client_err");
        break;

        default:
            // Nothing. Other defined 'v6plus_status_t' options are only transitory.
        break;
    }

    return true;
}

/* Upsert MAP_State according to MAP_Config and according to actual OSN MAP object state. */
static bool nm2_mapcfg_ovsdb_state_upsert(struct nm2_mapcfg *mapcfg)
{
    struct schema_MAP_State MAP_State;
    struct osn_map_portset portsets[OSN_MAP_PORT_SETS_MAX];
    unsigned num_portsets;
    char portsets_str[4096];
    osn_map_rule_t matched_rule;
    osn_ip6_addr_t ipv6addr;
    osn_ip_addr_t ipv4addr;
    int psid_len;
    int psid;

    memset(&MAP_State, 0, sizeof(MAP_State));

    /* First, parameters that are just copied from MAP_Config to MAP_State: */

    SCHEMA_SET_STR(MAP_State.if_name, mapcfg->mc_if_name);

    if (mapcfg->mc_map_type == OSN_MAP_TYPE_MAP_T)
    {
        SCHEMA_SET_STR(MAP_State.map_type, "map-t");
    }
    else if (mapcfg->mc_map_type == OSN_MAP_TYPE_MAP_E)
    {
        SCHEMA_SET_STR(MAP_State.map_type, "map-e");
    }
    else
    {
        return false; // should not really happen
    }

    SCHEMA_SET_BOOL(MAP_State.map_legacy_draft3, mapcfg->mc_map_legacy_draft3);

    if (mapcfg->mc_rules_dhcp)
    {
        SCHEMA_SET_BOOL(MAP_State.map_rules_dhcp, true);
    }
    else if (mapcfg->mc_rules_v6plus)
    {
        STRSCPY(MAP_State.map_rules_v6plus_url, mapcfg->mc_rules_url);
        MAP_State.map_rules_v6plus_url_exists = true;
        MAP_State.map_rules_v6plus_url_present = true;
    }

    /* Effective End-user IPv6 prefix: */
    SCHEMA_UNSET_FIELD(MAP_State.end_user_prefix);
    if (mapcfg->mc_st_enduser_prefix_dhcpv6pd)
    {
        SCHEMA_SET_STR(MAP_State.end_user_prefix, "dhcpv6-pd");
    } else if (mapcfg->mc_st_enduser_prefix_ra)
    {
        SCHEMA_SET_STR(MAP_State.end_user_prefix, "ra");
    }

    /* If MAP type v6plus: Set any v6plus-specific state fields: */
    nm2_mapcfg_v6plus_ovsdb_state_set(&MAP_State, mapcfg);

    /* Also report the actual MAP state parameters from the OSN MAP object */

    /* Set MAP rule parameters in MAP_State if there was a matching MAP rule: */
    if (osn_map_rule_matched_get(mapcfg->mc_map, &matched_rule))
    {
        snprintf(MAP_State.BMR_IPv6_prefix, sizeof(MAP_State.BMR_IPv6_prefix),
                    "%s", FMT_osn_ip6_addr(matched_rule.om_ipv6prefix));
        MAP_State.BMR_IPv6_prefix_exists = true;

        snprintf(MAP_State.BMR_IPv4_prefix, sizeof(MAP_State.BMR_IPv4_prefix),
                 "%s", FMT_osn_ip_addr(matched_rule.om_ipv4prefix));
        MAP_State.BMR_IPv4_prefix_exists = true;

        SCHEMA_SET_INT(MAP_State.BMR_ea_len, matched_rule.om_ea_len);
        SCHEMA_SET_INT(MAP_State.BMR_psid_offset, matched_rule.om_psid_offset);

        snprintf(MAP_State.DMR, sizeof(MAP_State.DMR),
                 "%s", FMT_osn_ip6_addr(matched_rule.om_dmr));
        MAP_State.DMR_exists = true;

        if (matched_rule.om_psid_len > 0) /* Explicit PSID and PSID_len: */
        {
            SCHEMA_SET_INT(MAP_State.psid, matched_rule.om_psid);
            SCHEMA_SET_INT(MAP_State.psid_len, matched_rule.om_psid_len);
        }
        else /* Calculated PSID and PSID_len: */
        {
            if (osn_map_psid_get(mapcfg->mc_map, &psid_len, &psid))
            {
                SCHEMA_SET_INT(MAP_State.psid, psid);
                SCHEMA_SET_INT(MAP_State.psid_len, psid_len);
            }
        }
    }

    /* MAP IPv4 address: */
    if (osn_map_ipv4_addr_get(mapcfg->mc_map, &ipv4addr))
    {
        SCHEMA_SET_STR(MAP_State.map_ipv4_addr, FMT_osn_ip_addr(ipv4addr));
    }
    /* MAP IPv6 address: */
    if (osn_map_ipv6_addr_get(mapcfg->mc_map, &ipv6addr))
    {
        SCHEMA_SET_STR(MAP_State.map_ipv6_addr, FMT_osn_ip6_addr(ipv6addr));
    }
    /* Port sets: */
    if (osn_map_port_sets_get(mapcfg->mc_map, portsets, &num_portsets))
    {
        if (util_portsets_tostr(portsets_str, sizeof(portsets_str), portsets, num_portsets) >= (int)sizeof(portsets_str))
        {
            LOG(WARN, "nm2_4in6_map: %s: Port sets string truncated", mapcfg->mc_if_name);
        }
        LOG(DEBUG, "nm2_4in6_map: %s: Port sets: '%s'", mapcfg->mc_if_name, portsets_str);
        SCHEMA_SET_STR(MAP_State.port_sets, portsets_str);
    }

    /* Upsert MAP_State: */
    if (!ovsdb_table_upsert_where(
            &table_MAP_State,
            ovsdb_where_simple(SCHEMA_COLUMN(MAP_State, if_name), mapcfg->mc_if_name),
            &MAP_State, false))
    {
        LOG(ERR, "nm2_4in6_map: %s: Error upserting MAP_State", mapcfg->mc_if_name);
        return false;
    }
    return true;
}

/*
 * Delete a MAP_State row for the specified MAP interface.
 */
static bool nm2_mapcfg_ovsdb_state_delete(const char *if_name)
{
    if (ovsdb_table_delete_where(
            &table_MAP_State,
            ovsdb_where_simple(SCHEMA_COLUMN(MAP_State, if_name), if_name)) == -1)
    {
        LOG(ERR, "nm2_4in6_map: %s: Error deleting MAP_State row", if_name);
        return false;
    }
    return true;
}

/*
 * Note: The following prototypes are intentionally not placed in a header file as the current
 * OpenSync-driven IPv6 relay solution is temporary and subject to be replaced in the near
 * future when proper OpenSync and controller-driven solution for IPv6 relay services will
 * be implemented.
 */
extern bool nm2_ipv6_relay_start(char *master_if_name, char *slave_if_name);
extern bool nm2_ipv6_relay_stop(void);

/*
 * Configure relaying of IPv6 management protocols.
 *
 * This is needed when MAP is configured but no delegated prefixes are available, to relay
 * RA, DHCPv6 and NDP between routed (non-bridged) interfaces.
 *
 * Note: This code is subject to be removed in the near future when proper OpenSync and
 * controller-driven solution for IPv6 relay services will be implemented.
 */
static bool nm2_4in6_map_ipv6_relay_configure(bool enable)
{
    if (enable)
    {
        if (nm2_map_ctx->nm_uplink_intf[0] == '\0')
        {
            LOG(WARN, "nm2_4in6_map: Uplink interface not known, cannot configure IPv6 relay");
            return false;
        }

        if (!nm2_ipv6_relay_start(nm2_map_ctx->nm_uplink_intf, CONFIG_TARGET_LAN_BRIDGE_NAME))
        {
            LOG(WARN, "nm2_4in6_map: Error configuring IPv6 relay service");
            return false;
        }
        LOG(NOTICE, "nm2_4in6_map: IPv6 relay service enabled");
    }
    else
    {
        nm2_ipv6_relay_stop();
        LOG(NOTICE, "nm2_4in6_map: IPv6 relay service disabled");
    }
    return true;
}

/*
 * OVSDB monitor update callback for MAP_Config table
 */
static void callback_MAP_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_MAP_Config *old,
        struct schema_MAP_Config *new)
{
    struct nm2_mapcfg *mapcfg;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(NOTICE, "nm2_4in6_map: %s: MAP_Config update: NEW row", new->if_name);

            if (nm2_mapcfg_get(new->if_name) != NULL)
            {
                LOG(WARN, "nm2_4in6_map: %s: A MAP config with the name already exists. Ignoring.", new->if_name);
                return;
            }
            mapcfg = nm2_mapcfg_new(new->if_name);

            break;
        case OVSDB_UPDATE_MODIFY:
            LOG(INFO, "nm2_4in6_map: %s: MAP_Config update: MODIFY row", new->if_name);

            mapcfg = nm2_mapcfg_get(new->if_name);

            break;
        case OVSDB_UPDATE_DEL:
            LOG(INFO, "nm2_4in6_map: %s: MAP_Config update: DELETE row", new->if_name);

            mapcfg = nm2_mapcfg_get(new->if_name);
            if (mapcfg == NULL)
            {
                LOG(ERROR, "nm2_4in6_map: %s: Cannot delete MAP config: not found", new->if_name);
                return;
            }

            /* Stop any previously run v6plus jobs: */
            nm2_mapcfg_v6plus_hgw_checker_stop(mapcfg);
            nm2_mapcfg_v6plus_job_stop(mapcfg);

            /* If v6plus MAP type, delete any v6plus saved rules from persistence: */
            if (mapcfg->mc_rules_v6plus)
            {
                nm2_mapcfg_v6plus_saved_rules_del(mapcfg);
            }

            /* Delete this MAP config: */
            if (!nm2_mapcfg_del(mapcfg))
            {
                LOG(ERROR, "nm2_4in6_map: %s: Error deleting MAP config", new->if_name);
            }

            /* When a MAP_Config row is deleted, delete the corresponding
             * MAP_State row as well: */
            nm2_mapcfg_ovsdb_state_delete(new->if_name);

            LOG(NOTICE, "nm2_4in6_map: %s: MAP config deleted", new->if_name);

            return;
        default:
            LOG(ERROR, "nm2_4in6_map: Monitor update error.");
            return;
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW ||
        mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        if (mapcfg == NULL)
        {
            LOG(ERROR, "nm2_4in6_map: %s: Could not obtain MAP config handle", new->if_name);
            return;
        }

        /* Stop any previously run v6plus jobs: */
        nm2_mapcfg_v6plus_hgw_checker_stop(mapcfg);
        nm2_mapcfg_v6plus_job_stop(mapcfg);

        /* Set MAP config and apply it to OSN: */
        if (!nm2_mapcfg_config_set(mapcfg, new, mon))
        {
            LOG(ERR, "nm2_4in6_map: %s: Error setting OVSDB MAP config", new->if_name);

            /* Update MAP OVSDB state (Map_State): */
            nm2_mapcfg_ovsdb_state_upsert(mapcfg);
            return;
        }

        /*
         * If MAP rule acquisition type is v6plus, this requires additional logic and
         * continuous checks. Start v6plus operation job here:
         */
        if (mapcfg->mc_rules_v6plus)
        {
            nm2_mapcfg_v6plus_job_schedule(mapcfg, 0.25);
            return;
        }

        /*
         * If non-v6plus, simply apply the MAP config and upsert MAP_State accordingly:
         */

        /* Enable/apply this MAP config: */
        if (osn_map_apply(mapcfg->mc_map))
        {
            LOG(NOTICE, "nm2_4in6_map: %s: MAP config applied", new->if_name);
        }
        else
        {
            LOG(ERR, "nm2_4in6_map: %s: Error applying OVSDB MAP config", new->if_name);
        }

        /* Update MAP OVSDB state (Map_State): */
        nm2_mapcfg_ovsdb_state_upsert(mapcfg);
    }
}

/* Get a string representation of enum osn_map_type */
static const char *util_map_type_to_str(enum osn_map_type map_type)
{
    if (map_type == OSN_MAP_TYPE_MAP_E) return "MAP-E";
    else if (map_type == OSN_MAP_TYPE_MAP_T) return "MAP-T";
    else return "NA";
}

/* Update the cached DHCPv6 options values: */
static void nm2_map_ctx_dhcp_opt_update(int optcode, const char *value)
{
    if (optcode == 26)
    {
        STRSCPY(nm2_map_ctx->nm_dhcp_option_26, value);
    }
    else if (optcode == 94)
    {
        STRSCPY(nm2_map_ctx->nm_dhcp_option_94, value);
    }
    else if (optcode == 95)
    {
        STRSCPY(nm2_map_ctx->nm_dhcp_option_95, value);
    }
}

/*
 * Read DHCP_Option OVSDB tag 26,rx,v6.
 *  - buf_len must be at least 8k.
 *  - if multiple rows, we simply take the first one.
 */
static bool nm2_map_get_dhcp_option_26(char *opt_26_str, size_t buf_len)
{
    struct schema_DHCP_Option *dhcp_option = NULL;
    const json_int_t tag = 26;
    int count = 0;

    json_t *where = ovsdb_where_multi(
        ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCP_Option, tag), &tag, OCLM_INT),
        ovsdb_where_simple(SCHEMA_COLUMN(DHCP_Option, type), "rx"),
        ovsdb_where_simple(SCHEMA_COLUMN(DHCP_Option, version), "v6"),
        ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCP_Option, enable), "true", OCLM_BOOL),
        NULL
    );

    dhcp_option = ovsdb_table_select_where(&table_DHCP_Option, where, &count);
    if (dhcp_option != NULL && count > 0)
    {
        strscpy(opt_26_str, dhcp_option[0].value, buf_len);

        LOG(DEBUG, "nm2_4in6_map: Read DHCP_Option 26,rx,v6: value=='%s'", dhcp_option[0].value);
    }
    else
    {
        *opt_26_str = '\0';

        LOG(DEBUG, "nm2_4in6_map: No RX DHCPv6 option 26 in DHCP_Option");
    }

    FREE(dhcp_option);

    return true;
}

/* Get the current delegated IPv6 prefix, if received. */
static bool nm2_map_get_IA_PD(osn_ip6_addr_t *ia_pd, bool *prefix_received)
{
    char ia_pd_str[8192];
    char *c;

    /* Read DHCPv6 option 26 value from OVSDB: */
    if (!nm2_map_get_dhcp_option_26(ia_pd_str, sizeof(ia_pd_str)))
    {
        return false;
    }

    /*
     * DHCP OPTION 26 value is in the following format:
     * <prefix>,<pref_lft>,<valid_lft> <...>
     * eg: 2001:ee2:1704:99ff::/64,54000,86400
     *
     * If more than 1 option 26 (separated with whitespace), we take the first one.
     */

    if (*ia_pd_str == '\0')
    {
        *prefix_received = false;
        return true;
    }
    else
    {
        *prefix_received = true;
    }

    /* Keep only the prefix value: */
    c = index(ia_pd_str, ',');
    if (c != NULL)
    {
        *c = '\0';
    }

    /* Parse to osn_ip6addr: */
    if (!osn_ip6_addr_from_str(ia_pd, ia_pd_str))
    {
        LOG(ERR, "nm2_4in6_map: Failed parsing as IPv6 prefix: '%s'", ia_pd_str);
        return false;
    }
    return true;
}

/* Get the RA prefix. */
static bool nm2_map_get_RA_prefix(osn_ip6_addr_t *ra_prefix, bool *prefix_obtained)
{
    struct schema_IPv6_Address ipv6_address;
    struct schema_IP_Interface ip_interface;
    const char *uplink_if_name;
    int i;

    *prefix_obtained = false;

    if (nm2_map_ctx->nm_uplink_intf[0] == '\0')
    {
        LOG(WARN, "nm2_4in6_map: Uplink interface name not known");
        return false;
    }

    /* We'll figure out the RA prefix from the RA address (if any) on the uplink interface: */
    uplink_if_name = nm2_map_ctx->nm_uplink_intf;

    if (!ovsdb_table_select_one_where(
            &table_IP_Interface,
            ovsdb_where_simple(SCHEMA_COLUMN(IP_Interface, if_name), uplink_if_name),
            &ip_interface))
    {
        LOG(ERR, "nm2_4in6_map: Error selecting uplink %s row from IP_Interface", uplink_if_name);
        return false;
    }

    /* Go through all IPv6 addresses of the uplink interface: */
    for (i = 0; i < ip_interface.ipv6_addr_len; i++)
    {
        osn_ip6_addr_t addr;
        osn_ip6_addr_t prefix;

        if (!ovsdb_table_select_one_where(
                &table_IPv6_Address,
                ovsdb_where_uuid("_uuid", ip_interface.ipv6_addr[i].uuid),
                &ipv6_address))
        {
            LOG(WARN, "nm2_4in6_map: Cannot find IPv6_Address row with uuid: %s",
                    ip_interface.ipv6_addr[i].uuid);
            continue;
        }

        /* The address must be auto_configured, global IPv6 address and not from DHCPv6 stateful */
        if (strcmp(ipv6_address.origin, "auto_configured") != 0)
        {
            continue;
        }
        if (!osn_ip6_addr_from_str(&addr, ipv6_address.address))
        {
            LOG(WARN, "nm2_4in6_map: Invalid IPv6 address: %s", ipv6_address.address);
            continue;
        }
        if (osn_ip6_addr_type(&addr) != OSN_IP6_ADDR_GLOBAL)
        {
            LOG(DEBUG, "nm2_4in6_map: Not a global IPv6 address: %s", ipv6_address.address);
            continue;
        }
        if (addr.ia6_prefix == 128) // DHCPv6 stateful address, skip
        {
            LOG(DEBUG, "nm2_4in6_map: Not a RA IPv6 address: %s", ipv6_address.address);
            continue;
        }

        LOG(DEBUG, "nm2_4in6_map: Determined RA address on uplink: %s", FMT_osn_ip6_addr(addr));

        /* RA IPv6 address, figure out the RA prefix: */

        prefix = osn_ip6_addr_subnet(&addr);
        *ra_prefix = prefix;
        *prefix_obtained = true;

        LOG(DEBUG, "nm2_4in6_map: Determined RA prefix: %s", FMT_osn_ip6_addr(*ra_prefix));

        break;
    }
    return true;
}

/* Parse one DHCP_Option MAP rule string to osn_map_rule_t object */
static bool nm2_map_parse_one_DHCP_Option_MAP_rule(const char *_rule_str, osn_map_rule_t *rule)
{
    char str_ipv4prefix[OSN_IP_ADDR_LEN] = "";
    char str_ipv6prefix[OSN_IP6_ADDR_LEN] = "";
    char str_prefix4len[8] = "";
    char str_prefix6len[8] = "";
    char rule_str[MAX_DHCPV6_OPT_LEN];
    char *tok;
    char *saveptr1 = NULL;
    char *saveptr2 = NULL;

    STRSCPY(rule_str, _rule_str);

    *rule = OSN_MAP_RULE_INIT;

    /*
     * The MAP rule string is in the following format:
     * MAP-T: e.g.:
     *     "type=map-t,ealen=16,prefix4len=24,prefix6len=48,ipv4prefix=192.0.2.0,\
     *      ipv6prefix=2001:ee2:1703::,offset=0,psidlen=0,psid=0,dmr=64:ff9b::/64"
     * MAP-E: e.g.:
     *     "type=map-e,ealen=16,prefix4len=24,prefix6len=48,ipv4prefix=192.0.2.0,\
     *      ipv6prefix=2001:ee2:1703::,offset=0,psidlen=0,psid=0,br=64:ff9b:1:223::1"
     *
     * MAP-E + FMR flag: e.g:
     *     "fmr,type=map-e,ealen=16,prefix4len=24,prefix6len=48,ipv4prefix=192.0.2.0,\
     *      ipv6prefix=2001:ee2:1703::,offset=0,psidlen=0,psid=0,br=64:ff9b:1:223::1"
     */

    for (tok = strtok_r(rule_str, ",", &saveptr1); tok != NULL; tok = strtok_r(NULL, ",", &saveptr1))
    {
        char *name;
        char *val;

        if (strcmp(tok, "fmr") == 0)
        {
            rule->om_is_fmr = true;
            continue;
        }

        name = strtok_r(tok, "=", &saveptr2);
        if (name == NULL) continue;
        val = strtok_r(NULL, "\0", &saveptr2);
        if (val == NULL) continue;

        if (strcmp(name, "ealen") == 0)
        {
            rule->om_ea_len = atoi(val);
        }
        else if (strcmp(name, "prefix4len") == 0)
        {
            STRSCPY(str_prefix4len, val);
        }
        else if (strcmp(name, "prefix6len") == 0)
        {
            STRSCPY(str_prefix6len, val);
        }
        else if (strcmp(name, "ipv4prefix") == 0)
        {
            STRSCPY(str_ipv4prefix, val);
        }
        else if (strcmp(name, "ipv6prefix") == 0)
        {
            STRSCPY(str_ipv6prefix, val);
        }
        else if (strcmp(name, "offset") == 0)
        {
            rule->om_psid_offset = atoi(val);
        }
        else if (strcmp(name, "psidlen") == 0)
        {
            rule->om_psid_len = atoi(val);
        }
        else if (strcmp(name, "psid") == 0)
        {
            rule->om_psid = atoi(val);
        }
        else if (strcmp(name, "dmr") == 0 || strcmp(name, "br") == 0)
        {
            if (!osn_ip6_addr_from_str(&rule->om_dmr, val))
            {
                LOG(ERR, "nm2_4in6_map: Failed parsing DMR string as IPv6: '%s'", val);
                return false;
            }
        }

        if (*str_ipv4prefix && *str_prefix4len) // when both strings ready
        {
            STRSCAT(str_ipv4prefix, "/");
            STRSCAT(str_ipv4prefix, str_prefix4len);
            if (!osn_ip_addr_from_str(&rule->om_ipv4prefix, str_ipv4prefix))
            {
                LOG(ERR, "nm2_4in6_map: Failed parsing MAP rule IPv4 prefix string: '%s'", str_ipv4prefix);
                return false;
            }
        }
        if (*str_ipv6prefix && *str_prefix6len) // when both strings ready
        {
            STRSCAT(str_ipv6prefix, "/");
            STRSCAT(str_ipv6prefix, str_prefix6len);
            if (!osn_ip6_addr_from_str(&rule->om_ipv6prefix, str_ipv6prefix))
            {
                LOG(ERR, "nm2_4in6_map: Failed parsing MAP rule IPv6 prefix string: '%s'", str_ipv6prefix);
            }
        }
    }

    return true;
}

/* Get the specified MAP type option string from cached DHCP_Option value
 * and parse it to osn_map_rule_list_t object, i.e. to a list of osn_map_rule_t objects.
 *
 * @return   false on error
 *           true on success.
 *
 * Note: If no DHCP MAP option string of specified type received, success is returned with an empty list.
 */
static bool nm2_map_get_rules_from_dhcp(enum osn_map_type map_type, osn_map_rulelist_t **_rule_list)
{
    char opt_str[MAX_DHCPV6_OPT_LEN];
    osn_map_rulelist_t *rule_list;
    char *tok;
    char *saveptr = NULL;

    /* Take the cached DHCP_Option 94 or 95 string value: */
    if (map_type == OSN_MAP_TYPE_MAP_E)
    {
        STRSCPY(opt_str, nm2_map_ctx->nm_dhcp_option_94);
    }
    else if (map_type == OSN_MAP_TYPE_MAP_T)
    {
        STRSCPY(opt_str, nm2_map_ctx->nm_dhcp_option_95);
    }

    // Create a new MAP rule list and parse MAP rules from the DHCP_Option value string,
    // adding them to the list:
    rule_list = osn_map_rulelist_new();

    if (*opt_str == '\0')
    {
        LOG(TRACE, "nm2_4in6_map: No %s DHCP option string received", util_map_type_to_str(map_type));
        goto end;
    }

    /* Multiple MAP rules in the DHCP_Option value string are delimited with a ' ': */
    for (tok = strtok_r(opt_str, " ", &saveptr); tok != NULL; tok = strtok_r(NULL, " ", &saveptr))
    {
        osn_map_rule_t map_rule;

        LOG(TRACE, "nm2_4in6_map: Parsing DHCPv6 %s option string: '%s'", util_map_type_to_str(map_type), tok);

        if (!nm2_map_parse_one_DHCP_Option_MAP_rule(tok, &map_rule))
        {
            LOG(WARN, "nm2_4in6_map: Failed parsing DHCPv6 %s option string '%s'. Skipping this rule",
                    util_map_type_to_str(map_type), tok);
            continue;
        }

        /* Add the parsed MAP rule to the list: */
        osn_map_rulelist_add_rule(rule_list, &map_rule);
    }

end:
    *_rule_list = rule_list;
    return true;
}

/**
 * From the schema string take the cloud-pushed static MAP rule config parameters,
 * construct an osn_map_rule_t object and set it to OSN MAP config.
 */
static bool nm2_mapcfg_set_update_map_rules_cloud(
    struct nm2_mapcfg *mapcfg,
    const struct schema_MAP_Config *conf)
{
    osn_map_rule_t map_rule = OSN_MAP_RULE_INIT;

    if (!(conf->BMR_IPv6_prefix_exists
            && conf->BMR_IPv4_prefix_exists
            && conf->BMR_ea_len_exists
            && conf->BMR_psid_offset_exists
            && conf->DMR_exists))
    {
        return false;
    }

    /* MAP rule IPv6 prefix: */
    if (!osn_ip6_addr_from_str(&map_rule.om_ipv6prefix, conf->BMR_IPv6_prefix))
    {
        LOG(ERR, "nm2_4in6_map: MAP_Config:BMR_IPv6_prefix: Cannot parse '%s' as IPv6 prefix", conf->BMR_IPv6_prefix);
        return false;
    }
    if (map_rule.om_ipv6prefix.ia6_prefix == -1)
    {
        LOG(ERR, "nm2_4in6_map: MAP_Config:BMR_IPv6_prefix: Not a prefix: %s", conf->BMR_IPv6_prefix);
        return false;
    }

    /* MAP rule IPv4 prefix: */
    if (!osn_ip_addr_from_str(&map_rule.om_ipv4prefix, conf->BMR_IPv4_prefix))
    {
        LOG(ERR, "nm2_4in6_map: MAP_Config:BMR_IPv4_prefix: Cannot parse '%s' as IPv4 prefix", conf->BMR_IPv4_prefix);
        return false;
    }
    if (map_rule.om_ipv4prefix.ia_prefix == -1)
    {
        LOG(ERR, "nm2_4in6_map: MAP_Config:BMR_IPv4_prefix: Not a prefix: %s", conf->BMR_IPv4_prefix);
        return false;
    }

    /* MAP rule EA-bits length: */
    map_rule.om_ea_len = conf->BMR_ea_len;

    /* MAP rule PSID offset: */
    map_rule.om_psid_offset = conf->BMR_psid_offset;

    /* DMR: */
    if (!osn_ip6_addr_from_str(&map_rule.om_dmr, conf->DMR))
    {
        LOG(ERR, "nm2_4in6_map: MAP_Config:DMR: Cannot parse '%s' as IPv6 address", conf->DMR);
        return false;
    }

    /* MAP rule object prepared. Configure it to OSN MAP object: */
    if (!osn_map_rule_set(mapcfg->mc_map, &map_rule))
    {
        LOG(ERR, "nm2_4in6_map: %s: Error setting MAP rule to OSN MAP config", mapcfg->mc_if_name);
        return false;
    }
    return true;
}

/**
 * Check if we have received a corresponding MAP type DHCP option with MAP rules.
 *
 * Set (or unset) the MAP rules to the OSN MAP config.
 */
static bool nm2_mapcfg_set_update_map_rules_dhcp(struct nm2_mapcfg *mapcfg)
{
    osn_map_rulelist_t *rule_list = NULL;
    bool rv = true;

    if (!mapcfg->mc_rules_dhcp)
    {
        LOG(WARN, "nm2_4in6_map: %s: %s called but this MAP config not DHCP enabled",
                mapcfg->mc_if_name, __func__);
    }

    if (!nm2_map_get_rules_from_dhcp(mapcfg->mc_map_type, &rule_list) || rule_list == NULL)
    {
        LOG(ERR, "nm2_4in6_map: %s: Failed getting %s rules from DHCP options",
                mapcfg->mc_if_name, util_map_type_to_str(mapcfg->mc_map_type));
        return false;
    }

    /* MAP rule list now set (may be empty if no DHCP options received), set it to the OSN MAP object: */
    if (!osn_map_rule_list_set(mapcfg->mc_map, rule_list))
    {
        LOG(ERR, "nm2_4in6_map: %s: Failed setting MAP rule list to OSN MAP config", mapcfg->mc_if_name);
        rv = false;
    }

    osn_map_rulelist_del(rule_list);

    return rv;
}

static bool nm2_mapcfg_set_update_map_rules_dhcp_all_apply(enum osn_map_type map_type)
{
    struct nm2_mapcfg *mapcfg;
    bool rv = true;

    ds_tree_foreach(&nm2_mapcfg_list, mapcfg)
    {
        if (!mapcfg->mc_rules_dhcp || mapcfg->mc_map_type != map_type)
        {
            continue;
        }

        LOG(NOTICE, "nm2_4in6_map: Updating %s DHCP options MAP rules"
                    " for existing MAP config %s and reapplying",
                    util_map_type_to_str(map_type), mapcfg->mc_if_name);

        if (nm2_mapcfg_set_update_map_rules_dhcp(mapcfg))
        {
            rv &= osn_map_apply(mapcfg->mc_map);
        }
        else
        {
            rv &= false;
        }
    }

    return rv;
}

/**
 * Acquire MAP rules from MAP rules distribution server API endpoint (v6plus).
 *
 * Returns MAP rules in *_rule_list and sets v6plus rules access status to mapcfg->mc_v6plus_last_status.
 *
 * According to different MAP rules server statuses, different action may then be taken.
 *
 * Only V6PLUS_SERV_REPLY_OK status means that non-empty rule list was successfully received from
 * the MAP rule server. This function does not however check if at least one rule is actually valid
 * and v6plus could be configured, so this is up to the caller to do at a later point.
 *
 * false is returned only on critical errors such as arguments not valid. Success is returned in
 * most other cases and the actual map rules status can be determined by mapcfg->mc_v6plus_last_status
 * and checking the rule list itself for further validity.
 */
static bool nm2_mapcfg_v6plus_map_rules_get(struct nm2_mapcfg *mapcfg, osn_map_v6plus_rulelist_t **_rule_list)
{
    struct osn_map_v6plus_cfg endpoint_cfg = { 0 };
    osn_map_v6plus_rulelist_t *rule_list = NULL;
    long response_code;

    if (!mapcfg->mc_rules_v6plus || mapcfg->mc_rules_url[0] == '\0')
    {
        LOG(ERR, "nm2_4in6_map: %s: v6plus: %s called but MAP rules server API URL not configured",
                mapcfg->mc_if_name, __func__);
        return false;
    }

    /* Prepare API endpoint URL for accessing MAP rule server: */
    STRSCPY(endpoint_cfg.vp_endpoint_url, mapcfg->mc_rules_url);
    if (mapcfg->mc_v6plus_saved_rules != NULL && mapcfg->mc_v6plus_saved_rules->pl_user_id[0] != '\0')
    {
        // If we've previously acquired MAP rules and we have the unique user ID,
        // we'll include it in the URL parameters:

        STRSCPY(endpoint_cfg.vp_user_id, mapcfg->mc_v6plus_saved_rules->pl_user_id);
    }

    /* Fetch MAP rules from the server API endpoint: */
    if (!osn_map_v6plus_fetch_rules(&response_code, &rule_list, &endpoint_cfg))
    {
        /* Connection failure, no response or other critical error. Assume server error. */

        LOG(ERR, "nm2_4in6_map: %s: v6plus: Failed fetching MAP rules from v6plus endpoint: %s",
                mapcfg->mc_if_name,
                mapcfg->mc_rules_url);

        mapcfg->mc_v6plus_last_status = V6PLUS_SERV_ERR;
        goto end;
    }

    /* Connection and response from MAP rule server OK, check the response code: */

    if (response_code >= 500 && response_code <= 599)
    {
        /* Server error */
        mapcfg->mc_v6plus_last_status = V6PLUS_SERV_ERR;
    }
    else if (response_code >= 400 && response_code <= 499)
    {
        /* Client error (e.g. access forbidden from the current prefix, etc) */
        mapcfg->mc_v6plus_last_status = V6PLUS_CLIENT_ERR;
    }
    else if (response_code == 200)
    {
        /*
         * Server reply OK.
         */

        if (osn_map_rulelist_is_empty((osn_map_rulelist_t *)rule_list))
        {
            /* Empty rules --> no valid rule */
            mapcfg->mc_v6plus_last_status = V6PLUS_SERV_REPLY_OK_NO_RULE;
        }
        else
        {
            /*
             * Non-empty rules.
             *
             * We still need to check at a later point (at map apply) if rules are valid
             * (a rule matches to end-user IPv6 prefix), so this state is intermediate and may
             * transition to either V6PLUS_SERV_REPLY_OK_V6PLUS_OK (v6plus operational) or
             * V6PLUS_SERV_REPLY_OK_NO_RULE (no valid rule)
             */
            mapcfg->mc_v6plus_last_status = V6PLUS_SERV_REPLY_OK;
        }
    }
    else
    {
        LOG(WARN, "nm2_4in6_map: %s: v6plus: Unexpected MAP rule server status code=%ld. "
                    "Assuming server error for taking further actions",
                        mapcfg->mc_if_name,
                        response_code);
        mapcfg->mc_v6plus_last_status = V6PLUS_SERV_ERR;
    }

end:
    *_rule_list = rule_list;
    return true;
}

static bool nm2_mapcfg_enduser_IPv6_prefix_adjust(
    osn_ip6_addr_t *prefix,
    const struct nm2_mapcfg *mapcfg)
{
    osn_ip6_addr_t enduser_prefix;

    enduser_prefix = *prefix;

    /* Determine the actual End-user IPv6 prefix if MAP type v6plus: */
    if (mapcfg->mc_rules_v6plus)
    {
        if (mapcfg->mc_st_enduser_prefix_dhcpv6pd && prefix->ia6_prefix < 64)
        {

            /*
             * DHCPv6-PD prefix and < 64.
             *
             * For v6plus we need to: use the first top /64 from the received /<n> prefix
             * for MAP calculation.
             *
             * (IPv6 subnet for LAN would usually be at least /64, but if it happens that
             *  IA_PD would be >= 64, just leave it as it is.)
             */

            enduser_prefix.ia6_prefix = 64;

            LOG(INFO, "nm2_4in6_map: v6plus: DHCPv6-PD prefix: %s"
                " --> Use prefix %s as End-user IPv6 prefix for MAP calculations",
                FMT_osn_ip6_addr(*prefix),
                FMT_osn_ip6_addr(enduser_prefix));

            *prefix = enduser_prefix;
        }
    }
    return true;
}

/**
 * Set (or unset) the end-user IPv6 prefix to the OSN MAP config.
 *
 * If no explicit end-user IPv6 prefix config: the default behavior is:
 * - Check if we have received an IA_PD DHCPv6 option: If yes, IA_PD is used as End-user IPv6 prefix.
 * - Otherwise check if we have a prefix from RA: If yes, RA prefix is used as End-user IPv6 prefix.
 *
 * If explicit end-user IPv6 prefix config: either use IA_PD or RA as end-user IPv6 prefix.
 */
static bool nm2_mapcfg_set_update_enduser_IPv6_prefix(struct nm2_mapcfg *mapcfg)
{
    bool prefix_obtained = false;
    osn_ip6_addr_t enduser_prefix;
    bool rv = true;

    mapcfg->mc_st_enduser_prefix_dhcpv6pd = false;
    mapcfg->mc_st_enduser_prefix_ra = false;

    /* Check for IA_PD: */
    if (!prefix_obtained && mapcfg->mc_enduser_prefix_dhcpv6pd)
    {
        LOG(DEBUG, "nm2_4in6_map: %s: Obtaining End-user IPv6 prefix from IA_PD...", mapcfg->mc_if_name);

        if (!nm2_map_get_IA_PD(&enduser_prefix, &prefix_obtained))
        {
            LOG(ERR, "nm2_4in6_map: %s: Failed checking if delegated IPv6 prefix received", mapcfg->mc_if_name);
            return false;
        }
        if (prefix_obtained)
        {
            mapcfg->mc_st_enduser_prefix_dhcpv6pd = true;

            LOG(INFO, "nm2_4in6_map: %s: Using DHCPv6-PD prefix %s for MAP End-user IPv6 prefix",
                    mapcfg->mc_if_name,
                    FMT_osn_ip6_addr(enduser_prefix));
        }
    }
    /* If no IA_PD, check for RA prefix: */
    if (!prefix_obtained && mapcfg->mc_enduser_prefix_ra)
    {
        LOG(DEBUG, "nm2_4in6_map: %s: Obtaining End-user IPv6 prefix from RA...", mapcfg->mc_if_name);

        if (!nm2_map_get_RA_prefix(&enduser_prefix, &prefix_obtained))
        {
            LOG(ERR, "nm2_4in6_map: %s: Failed checking for RA prefix", mapcfg->mc_if_name);
            return false;
        }
        if (prefix_obtained)
        {
            mapcfg->mc_st_enduser_prefix_ra = true;

            LOG(INFO, "nm2_4in6_map: %s: Using RA prefix %s for MAP End-user IPv6 prefix",
                    mapcfg->mc_if_name,
                    FMT_osn_ip6_addr(enduser_prefix));
        }
    }

    if (prefix_obtained)
    {
        /* First, any End-user IPv6 prefix further determinations for some MAP types: */
        nm2_mapcfg_enduser_IPv6_prefix_adjust(&enduser_prefix, mapcfg);

        /* End-user IPv6 prefix determined. Set it to the OSN MAP object: */
        rv &= osn_map_enduser_IPv6_prefix_set(mapcfg->mc_map, &enduser_prefix);
    }
    else
    {
        LOG(INFO, "nm2_4in6_map: %s: End-user IPv6 prefix not obtained", mapcfg->mc_if_name);

        rv &= osn_map_enduser_IPv6_prefix_set(mapcfg->mc_map, NULL);
    }

    if (!rv)
    {
        LOG(ERR, "nm2_4in6_map: %s: Failed setting End-user IPv6 prefix to OSN MAP config", mapcfg->mc_if_name);
    }

    return rv;
}

static void callback_DHCP_Option(
        ovsdb_update_monitor_t *mon,
        struct schema_DHCP_Option *old,
        struct schema_DHCP_Option *new)
{
    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if ( (new->tag == 26 || new->tag == 94 || new->tag == 95)
                    && strcmp(new->version, "v6") ==  0
                    && strcmp(new->type, "rx") == 0 )
            {
                if (mon->mon_type == OVSDB_UPDATE_NEW)
                {
                    LOG(INFO, "nm2_4in6_map: DHCP_Option_26: NEW: val=='%s'", new->value);
                }

                if (mon->mon_type == OVSDB_UPDATE_MODIFY)
                {
                    LOG(INFO, "nm2_4in6_map: DHCP_Option_26: MODIFY: old_val=='%s' -->  new_val=='%s'",
                            old->value, new->value);
                }

                nm2_map_ctx_dhcp_opt_update(new->tag, new->value);
            }
            break;

        case OVSDB_UPDATE_DEL:
            if ( (new->tag == 26 || new->tag == 94 || new->tag == 95)
                    && strcmp(new->version, "v6") ==  0
                    && strcmp(new->type, "rx") == 0 )
            {
                LOG(INFO, "nm2_4in6_map: DHCP_Option_26: DEL: val=='%s'", old->value);

                nm2_map_ctx_dhcp_opt_update(new->tag, "");
            }
            break;

        default:

            LOG(ERROR, "nm2_4in6_map: Monitor update error.");
            return;
    }

    if ((new->tag == 94 || new->tag == 95)
            && strcmp(new->version, "v6") ==  0
            && strcmp(new->type, "rx") == 0)
    {
        enum osn_map_type map_type = new->tag == 94 ? OSN_MAP_TYPE_MAP_E : OSN_MAP_TYPE_MAP_T;

        /* In the unlikely event that MAP-E or MAP-T DHCP option would change
         * we need to update all the existing MAP configs with enabled DHCP: */
        if (!nm2_mapcfg_set_update_map_rules_dhcp_all_apply(map_type))
        {
            LOG(ERR, "nm2_4in6_map: Error updating %s DHCP options"
                " to existing OSN MAP configs", util_map_type_to_str(map_type));
        }
    }
}

static void callback_Connection_Manager_Uplink(
        ovsdb_update_monitor_t *mon,
        struct schema_Connection_Manager_Uplink *old,
        struct schema_Connection_Manager_Uplink *new)
{
    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if (new->is_used_exists && new->is_used)
            {
                STRSCPY(nm2_map_ctx->nm_uplink_intf, new->if_name);
                break;
            }

            /* FALLTHROUGH */
        case OVSDB_UPDATE_DEL:

            if (strcmp(nm2_map_ctx->nm_uplink_intf, new->if_name) == 0)
            {
                nm2_map_ctx->nm_uplink_intf[0] = '\0';
            }
            break;

        default:

            LOG(ERROR, "nm2_4in6_map: Monitor update error.");
            return;
    }
}

/* Initialize NM2 4in6 MAP (MAP-T/MAP-E) handling. */
bool nm2_4in6_map_init(void)
{
    LOG(INFO, "nm2_4in6_map: Initializing NM2 4in6_map");

    OVSDB_TABLE_INIT(MAP_Config, if_name);
    OVSDB_TABLE_INIT(MAP_State, if_name);
    OVSDB_TABLE_INIT_NO_KEY(DHCP_Option);
    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);
    OVSDB_TABLE_INIT(IP_Interface, name);
    OVSDB_TABLE_INIT(IPv6_Address, _uuid);

    OVSDB_TABLE_MONITOR(MAP_Config, false);
    OVSDB_TABLE_MONITOR(DHCP_Option, false);
    OVSDB_TABLE_MONITOR(Connection_Manager_Uplink, false);

    nm2_map_ctx = CALLOC(1, sizeof(*nm2_map_ctx));

    return true;
}

bool nm2_mapcfg_v6plus_saved_rules_store(const osn_map_v6plus_rulelist_t *rule_list)
{
    char *saved_rules_str = NULL;
    json_t *saved_rules_js = NULL;
    json_error_t js_error;
    osp_ps_t *ps = NULL;
    size_t data_size;
    bool rv = false;

    if (rule_list->pl_raw_str == NULL)
    {
        LOG(DEBUG, "nm2_4in6_map: v6plus: saving rules: no string to save");
        return true;
    }

    /* Remove unique user ID from JSON string, as ID should not be saved to persistence */
    saved_rules_js = json_loads(rule_list->pl_raw_str, 0, &js_error);
    if (saved_rules_js == NULL)
    {
        LOG(ERR, "nm2_4in6_map: v6plus: saving rules: Error parsing json: '%s'", rule_list->pl_raw_str);
        return false;
    }
    json_object_del(saved_rules_js, "id");
    saved_rules_str = json_dumps(saved_rules_js, 0);
    if (saved_rules_str == NULL)
    {
        LOG(ERR, "nm2_4in6_map: v6plus: saving rules: Error converting JSON back to string");
        goto end;
    }

    /* Now save the JSON string with MAP rules (but without user ID) to persistence */

    ps = osp_ps_open(NM2_V6PLUS_STORE, OSP_PS_WRITE | OSP_PS_PRESERVE);
    if (ps == NULL)
    {
        LOG(ERR, "nm2_4in6_map: v6plus: saving rules: Error opening persistent store %s", NM2_V6PLUS_STORE);
        return false;
    }

    data_size = strlen(saved_rules_str) + 1;
    if (osp_ps_set(ps, NM2_V6PLUS_STORE_RULES, saved_rules_str, data_size) < (ssize_t) data_size)
    {
        LOG(ERR, "nm2_4in6_map: v6plus: saving rules: Error storing MAP rules to persistent storage.");
        goto end;
    }

    LOG(INFO, "nm2_4in6_map: v6plus: MAP rules saved to persistent storage.");
    rv = true;
end:
    if (ps != NULL) osp_ps_close(ps);
    if (saved_rules_str != NULL) json_free(saved_rules_str);
    if (saved_rules_js != NULL) json_decref(saved_rules_js);
    return rv;
}

bool nm2_mapcfg_v6plus_saved_rules_load(osn_map_v6plus_rulelist_t **rule_list)
{
    char *saved_rules_str = NULL;
    osp_ps_t *ps = NULL;
    ssize_t data_size;
    bool rv = false;

    *rule_list = NULL;

    ps = osp_ps_open(NM2_V6PLUS_STORE, OSP_PS_READ | OSP_PS_PRESERVE);
    if (ps == NULL)
    {
        LOG(DEBUG, "nm2_4in6_map: v6plus: saved rules: Failed opening persistent store. It may not exist yet");
        return true;
    }

    data_size = osp_ps_get(ps, NM2_V6PLUS_STORE_RULES, NULL, 0);
    if (data_size < 0)
    {
        LOG(ERR, "nm2_4in6_map: v6plus: saved rules: Error fetching key %s size.", NM2_V6PLUS_STORE_RULES);
        goto end;
    }
    else if (data_size == 0)
    {
        LOG(DEBUG, "nm2_4in6_map: v6plus: saved rules: Read 0 bytes for key %s from persistent storage."
                    " The record does not exist yet.", NM2_V6PLUS_STORE_RULES);
        rv = true;
        goto end;
    }

    saved_rules_str = MALLOC(data_size);
    if (osp_ps_get(ps, NM2_V6PLUS_STORE_RULES, saved_rules_str, (size_t)data_size) != data_size)
    {
        LOG(ERR, "nm2_4in6_map: v6plus: saved rules: Error reading saved rules from persistent storage.");
        goto end;
    }
    LOG(DEBUG, "nm2_4in6_map: v6plus: saved rules: Loaded saved rules string from persistent storage. str='%s'", saved_rules_str);

    if (!osn_map_v6plus_rulelist_parse(saved_rules_str, rule_list))
    {
        LOG(ERR, "nm2_4in6_map: v6plus: saved rules: Error parsing saved rules string: str='%s'", saved_rules_str);
        *rule_list = NULL;
        goto end;
    }

    LOG(INFO, "nm2_4in6_map: v6plus: Loaded saved rules from persistent storage.");
    rv = true;
end:
    if (saved_rules_str != NULL) FREE(saved_rules_str);
    if (ps != NULL) osp_ps_close(ps);
    return rv;
}

bool nm2_mapcfg_v6plus_saved_rules_erase(void)
{
    if (!osp_ps_erase_store_name(NM2_V6PLUS_STORE, OSP_PS_PRESERVE))
    {
        LOG(ERR, "nm2_4in6_map: v6plus: Error deleting saved rules. Failed erasing store: %s", NM2_V6PLUS_STORE);
        return false;
    }

    LOG(INFO, "nm2_4in6_map: v6plus: Deleted saved rules from persistent storage.");
    return true;
}

/*
 * Save v6plus MAP rules to persistent storage.
 */
bool nm2_mapcfg_v6plus_saved_rules_set(struct nm2_mapcfg *mapcfg, const osn_map_v6plus_rulelist_t *rule_list)
{
    osn_map_v6plus_rulelist_del(mapcfg->mc_v6plus_saved_rules);
    mapcfg->mc_v6plus_saved_rules = osn_map_v6plus_rulelist_copy(rule_list);

    // Save to persistence:
    if (!nm2_mapcfg_v6plus_saved_rules_store(rule_list))
    {
        LOG(ERR, "nm2_4in6_map: v6plus: Error saving MAP rules to persistent storage.");
    }

    return true;
}

/*
 * Load saved v6plus MAP rules from persistent storage. If no saved rules exist, *rule_list is set to NULL.
 */
bool nm2_mapcfg_v6plus_saved_rules_get(struct nm2_mapcfg *mapcfg, osn_map_v6plus_rulelist_t **rule_list)
{
    if (mapcfg->mc_v6plus_saved_rules == NULL)
    {
        // Load saved MAP rules from persistence (if there are any):
        if (!nm2_mapcfg_v6plus_saved_rules_load(&mapcfg->mc_v6plus_saved_rules))
        {
            LOG(ERR, "nm2_4in6_map: v6plus: Error loading MAP rules from persistent storage.");
            mapcfg->mc_v6plus_saved_rules = NULL;
        }

    }

    *rule_list = osn_map_v6plus_rulelist_copy(mapcfg->mc_v6plus_saved_rules);
    return true;
}

/*
 * Delete saved v6plus MAP rules from persistent storage.
 */
bool nm2_mapcfg_v6plus_saved_rules_del(struct nm2_mapcfg *mapcfg)
{
    osn_map_v6plus_rulelist_del(mapcfg->mc_v6plus_saved_rules);
    mapcfg->mc_v6plus_saved_rules = NULL;

    if (!nm2_mapcfg_v6plus_saved_rules_erase())
    {
        LOG(ERR, "nm2_4in6_map: v6plus: Error deleting MAP rules from persistent storage.");
        return false;
    }
    return true;
}

/*
 * Get a random value between min and max seconds.
 */
long nm2_mapcfg_v6plus_map_server_nextcheck_get(long min, long max)
{
    return os_random_range(min, max);
}

/*
 * Operation report to v6plus MAP rule server.
 */
bool nm2_mapcfg_v6plus_operation_report(
    struct nm2_mapcfg *mapcfg,
    osn_map_v6plus_rulelist_t *rule_list,
    osn_map_v6plus_status_action_t status_action,
    osn_map_v6plus_status_reason_t status_reason)
{
    struct osn_map_v6plus_cfg cfg = { 0};
    char *tok;

    /* Create an URL for MAP activity operation reporting from get_rules URL: */
    STRSCPY(cfg.vp_endpoint_url, mapcfg->mc_rules_url);
    tok = strstr(cfg.vp_endpoint_url, "/get_rules");
    if (tok == NULL)
    {
        LOG(ERR, "nm2_4in6_map: v6plus: Failed creating acct_report URL from get_rules URL %s: "
                    "does not contain '/get_rules'",
                    cfg.vp_endpoint_url);
        return false;
    }
    snprintf(tok, (sizeof(cfg.vp_endpoint_url) - (tok - cfg.vp_endpoint_url)), "/acct_report");

    if (rule_list != NULL && rule_list->pl_user_id[0] != '\0')
    {
        /* If we've previously acquired MAP rules and we have the unique user ID,
         * we'll include it in the URL parameters: */
        STRSCPY(cfg.vp_user_id, rule_list->pl_user_id);
    }

    if (!osn_map_v6plus_operation_report(status_action, status_reason, &cfg))
    {
        return false;
    }

    return true;
}

bool nm2_mapcfg_v6plus_ipv6_relay_manage(struct nm2_mapcfg *mapcfg)
{
    osn_ip6_addr_t prefix;
    bool ia_pd_obtained = false;

    if (!nm2_map_get_IA_PD(&prefix, &ia_pd_obtained))
    {
        return false;
    }

    if (mapcfg->mc_v6plus_hgw_status == OSN_MAP_V6PLUS_MAP_ON)
    {
        nm2_4in6_map_ipv6_relay_configure(false);
    }
    else // OSN_MAP_V6PLUS_MAP_OFF or OSN_MAP_V6PLUS_UNKNOWN
    {
        /* Ad-hoc handling of router mode w/o DHCPv6-PD case explicitly for v6plus: */
        if (!ia_pd_obtained) /* No DHCPv6-PD */
        {
            /*
             * In this case MAP is being configured and no delegated prefix received over
             * DHCPv6-PD, the node is to be in router mode (for IPv4, separate network on the LAN side),
             * but IPv6 needs to be configured as passthrough: IPv6 network extended from the WAN to
             * the LAN side by configuring IPv6 relay service: Relaying IPv6 management protocols:
             * NDP (RA, RS, NA, NS), and DHCPv6.
             */
            nm2_4in6_map_ipv6_relay_configure(true);
        }
        else
        {
            nm2_4in6_map_ipv6_relay_configure(false);
        }
    }

    return true;
}

static const char *util_v6plus_hgw_status_to_str(osn_map_v6plus_hgw_status_t hgw_status)
{
    if (hgw_status == OSN_MAP_V6PLUS_UNKNOWN) return "HGW_STATUS_UNKNOWN";
    else if (hgw_status == OSN_MAP_V6PLUS_MAP_OFF) return "HGW_STATUS_MAP_OFF";
    else if (hgw_status == OSN_MAP_V6PLUS_MAP_ON) return "HGW_STATUS_MAP_ON";
    else return "HGW_STATUS_UNSET";
}

/*
 * HGW status change action handler: To be called when the actual HGW status change is determined
 * to take appropriate actions: restart MAP configuration to reconfigure according to the
 * new HGW status.
 */
static void nm2_mapcfg_v6plus_hgw_status_change_action(struct nm2_mapcfg *mapcfg)
{
    LOG(NOTICE, "nm2_4in6_map: v6plus: Restarting v6plus MAP configuration ... restart v6plus job handler");
    nm2_mapcfg_v6plus_job_stop(mapcfg);
    nm2_mapcfg_v6plus_job_schedule(mapcfg, 0.25);
}

/* Acquire current HGW status and take actions if needed. */
bool nm2_mapcfg_v6plus_hgw_status_handle(struct nm2_mapcfg *mapcfg)
{
    osn_map_v6plus_hgw_status_t hgw_status_prev;
    osn_map_v6plus_hgw_status_t hgw_status_curr;

    LOG(TRACE, "nm2_4in6_map: v6plus: HGW status check started");

    /* Previous HGW status: */
    hgw_status_prev = mapcfg->mc_v6plus_hgw_status;

    /* Get current NTT HGW status: */
    if (!osn_map_v6plus_ntt_hgw_status_get(&hgw_status_curr, NULL))
    {
        LOG(ERR, "nm2_4in6_map: v6plus: Error checking NTT HGW status");
        return false;
    }
    LOG(TRACE, "nm2_4in6_map: v6plus: HGW status determined as: %s",
        util_v6plus_hgw_status_to_str(hgw_status_curr));

    if (hgw_status_curr != OSN_MAP_V6PLUS_UNKNOWN)
    {
        /* current status "HGW detected", reset "HGW failed detect counter": */
        mapcfg->mc_v6plus_hgw_failed_detect_cnt = 0;
    }

    /* If HGW status changed (but not initial HGW status determination): */
    if (hgw_status_prev != OSN_MAP_V6PLUS_UNSET && hgw_status_curr != hgw_status_prev)
    {
        if (hgw_status_curr != OSN_MAP_V6PLUS_UNKNOWN)
        {
            LOG(NOTICE,
                "nm2_4in6_map: v6plus: Detected HGW status change: %s --> %s",
                util_v6plus_hgw_status_to_str(hgw_status_prev),
                util_v6plus_hgw_status_to_str(hgw_status_curr));

            mapcfg->mc_v6plus_hgw_status = hgw_status_curr;

            /* Status change to --> HGW now detected: Take immediate action: */
            nm2_mapcfg_v6plus_hgw_status_change_action(mapcfg);
        }
        else
        {
            LOG(INFO,
                "nm2_4in6_map: v6plus: Potential HGW status change: %s --> %s (needs to be confirmed)",
                util_v6plus_hgw_status_to_str(hgw_status_prev),
                util_v6plus_hgw_status_to_str(hgw_status_curr));

            /* Status change --> HGW NOT detected: Postpone action, reconfirm HGW NOT detected. */
            if (mapcfg->mc_v6plus_hgw_failed_detect_cnt == 0)
            {
                mapcfg->mc_v6plus_hgw_failed_detect_cnt = 1;
            }
        }
    }

    LOG(TRACE, "nm2_4in6_map: v6plus: HGW status check: failed_detect_cnt=%u",
            mapcfg->mc_v6plus_hgw_failed_detect_cnt);

    /* Handle reconfirming of current status HGW not detected. */
    if (hgw_status_curr == OSN_MAP_V6PLUS_UNKNOWN
        && mapcfg->mc_v6plus_hgw_failed_detect_cnt >= 1
        && mapcfg->mc_v6plus_hgw_failed_detect_cnt <= NM2_V6PLUS_HGW_STATUS_RECONFIRM_NUM)
    {
        if (mapcfg->mc_v6plus_hgw_failed_detect_cnt++ == NM2_V6PLUS_HGW_STATUS_RECONFIRM_NUM)
        {
            LOG(NOTICE,
                "nm2_4in6_map: v6plus: HGW status change to %s confirmed for %d times (recheck interval %d seconds).",
                util_v6plus_hgw_status_to_str(hgw_status_curr),
                NM2_V6PLUS_HGW_STATUS_RECONFIRM_NUM, NM2_V6PLUS_HGW_STATUS_RECHECK_TIME);

            /* 'HGW NOT detected' confirmed. Take status change action: */
            mapcfg->mc_v6plus_hgw_status = OSN_MAP_V6PLUS_UNKNOWN;

            nm2_mapcfg_v6plus_hgw_status_change_action(mapcfg);
            nm2_mapcfg_v6plus_ipv6_relay_manage(mapcfg);
        }
    }

    if (hgw_status_curr != hgw_status_prev)
    {
        /* On any change, but not to --> HGW not detected (needs to be reconfirmed):
         * Unless initial HGW not detected determination: */
        if (hgw_status_curr != OSN_MAP_V6PLUS_UNKNOWN
            || (hgw_status_prev == OSN_MAP_V6PLUS_UNSET && hgw_status_curr == OSN_MAP_V6PLUS_UNKNOWN))
        {
            mapcfg->mc_v6plus_hgw_status = hgw_status_curr;

            /* At HGW status change, determine the new IPv6 relay enablement config: */
            nm2_mapcfg_v6plus_ipv6_relay_manage(mapcfg);
        }
    }

    return true;
}

void nm2_mapcfg_v6plus_hgw_status_checker_handler(struct ev_loop *loop, ev_timer *w, int revents)
{
    struct nm2_mapcfg *mapcfg = CONTAINER_OF(w, struct nm2_mapcfg, mc_v6plus_hgw_checker);

    /* Run HGW status check: */
    nm2_mapcfg_v6plus_hgw_status_handle(mapcfg);
}

/*
 * v6plus operation handler job.
 * It is scheduled to run at different random intervals
 * depending on the last MAP rule server access status and last MAP rule validity status.
 */
void nm2_mapcfg_v6plus_operation_handler(struct nm2_mapcfg *mapcfg)
{
    osn_map_v6plus_rulelist_t *rule_list = NULL;
    static bool v6plus_bootup_init_done = false;
    long rules_next_check = 0;

    LOG(NOTICE, "nm2_4in6_map: v6plus: v6plus operation handler job started");

    mapcfg->mc_v6plus_last_status = V6PLUS_STATUS_UNSET;

    if (!mapcfg->mc_v6plus_hgw_checker_inited)
    {
        /* First HGW status check to be done immediately: */
        if (!nm2_mapcfg_v6plus_hgw_status_handle(mapcfg))
        {
            goto end;
        }

        /* Schedule HGW checks to be done continuously at regular intervals: */
        nm2_mapcfg_v6plus_hgw_checker_start(mapcfg);
    }

    /* Different inferred effective configurations depending on HGW status: */
    if (mapcfg->mc_v6plus_hgw_status == OSN_MAP_V6PLUS_MAP_ON)
    {
        /*
         * There's HGW upstream and v6plus MAP turned ON in it. We should not enable v6plus MAP.
         */

        LOG(NOTICE, "nm2_4in6_map: v6plus: Under upstream HGW with v6plus ON. Will disable MAP");

        /*
         * Set empty rules (which is invalid config) and apply to OSN MAP.
         * This will effectively result in v6plus to be later disabled.
         */
        osn_map_rule_list_set(mapcfg->mc_map, NULL);
        osn_map_apply(mapcfg->mc_map);

        /* The following is just for correct effective End-user prefix reporting when we disable v6plus: */
        mapcfg->mc_st_enduser_prefix_dhcpv6pd = false;
        mapcfg->mc_st_enduser_prefix_ra = false;

        goto end; // Done, just report _State, and that's it.
    }
    else if (mapcfg->mc_v6plus_hgw_status == OSN_MAP_V6PLUS_MAP_OFF)
    {
        LOG(NOTICE, "nm2_4in6_map: v6plus: Under upstream HGW with v6plus OFF");

        /*
         * When under HGW (with MAP turned OFF): We need to use RA for end-user IPv6 prefix
         * regardless if IA_PD is actually received or not.
         *
         * (Except if there was explicit end-user IPv6 prefix OVSDB config override)
         */
        if (!mapcfg->mc_enduser_prefix_override)
        {
            mapcfg->mc_enduser_prefix_dhcpv6pd = false;
            mapcfg->mc_enduser_prefix_ra = true;
        }
    }
    else
    {
        LOG(NOTICE, "nm2_4in6_map: v6plus: NO upstream HGW detected");

        /*
         * When no upstream HGW: If we receive IA_PD, then we use it for end-user IPv6 prefix
         * otherwise we use RA for end-user IPv6 prefix.
         * Enable both, as the logic is: first try IA_PD, then RA:
         *
         * (Except if there was explicit end-user IPv6 prefix OVSDB config override)
         */
        if (!mapcfg->mc_enduser_prefix_override)
        {
            mapcfg->mc_enduser_prefix_dhcpv6pd = true;
            mapcfg->mc_enduser_prefix_ra = true;
        }
    }

    if (!v6plus_bootup_init_done)
    {
        v6plus_bootup_init_done = true;

        /* At first run: Do we have saved rules? */
        if (!nm2_mapcfg_v6plus_saved_rules_get(mapcfg, &rule_list))
        {
            LOG(ERR, "nm2_4in6_map: v6plus: Bootup init: Error getting v6plus saved rules");
            /* On error getting saved rules, assume we don't have saved rules */
        }

        /* If we have saved rules, we'll use them */
        if (!osn_map_rulelist_is_empty((osn_map_rulelist_t *)rule_list))
        {
            LOG(NOTICE, "nm2_4in6_map: v6plus: First run. Have saved rules.");
            mapcfg->mc_v6plus_last_status = V6PLUS_BOOTUP_SAVED_RULES;
        }
        else
        {
            LOG(NOTICE, "nm2_4in6_map: v6plus: First run. No saved rules. Acquire them immediately.");
        }
    }

    if (osn_map_rulelist_is_empty((osn_map_rulelist_t *)rule_list))
    {
        /*
         * Try to acquire MAP rules
         */

        LOG(INFO, "nm2_4in6_map: v6plus: Acquire MAP rules from MAP server");

        if (!nm2_mapcfg_v6plus_map_rules_get(mapcfg, &rule_list))
        {
            LOG(ERR, "nm2_4in6_map: v6plus: Error acquiring MAP rules");
            /* This would only happen on critical errors such as arguments not valid,
             * just assume client error here: */
            mapcfg->mc_v6plus_last_status = V6PLUS_CLIENT_ERR;
        }

        if (mapcfg->mc_v6plus_last_status == V6PLUS_SERV_ERR)
        {
            // In this case: check if we have saved rules, and if yes, use those:
            if (nm2_mapcfg_v6plus_saved_rules_get(mapcfg, &rule_list)
                    && !osn_map_rulelist_is_empty((osn_map_rulelist_t *)rule_list))
            {
                LOG(NOTICE, "nm2_4in6_map: v6plus: MAP rule server error. We have saved rules. Use them.");
            }
            else
            {
                LOG(INFO, "nm2_4in6_map: v6plus: MAP rule server error. We do not have saved rules");
            }
        }
    }

    if (!osn_map_rulelist_is_empty((osn_map_rulelist_t *)rule_list))
    {
        // Non-empty rules received (or saved rules to be used), try to apply to MAP.

        /* MAP rule list now set, set it to the OSN MAP object: */

        LOG(INFO, "nm2_4in6_map: v6plus: Setting MAP rules");

        if (!osn_map_rule_list_set(mapcfg->mc_map, (osn_map_rulelist_t *)rule_list))
        {
            LOG(ERR, "nm2_4in6_map: v6plus: %s: Failed setting MAP rule list to OSN MAP config", mapcfg->mc_if_name);
        }

        /* Determine End-user IPv6 prefix and set it to OSN MAP config: */

        LOG(INFO, "nm2_4in6_map: v6plus: Determining end-user IPv6 prefix");

        if (!nm2_mapcfg_set_update_enduser_IPv6_prefix(mapcfg))
        {
            LOG(ERR, "nm2_4in6_map: v6plus: Failed determining end-user IPv6 prefix and/or setting it");
        }

        /* Enable/apply this MAP config. May be valid or invalid at this point. A valid config
         * will result in a properly populated MAP_State, an invalid config will result in
         * non-populated MAP_State later on, which will be a signal to controller to act accordingly. */
        if (osn_map_apply(mapcfg->mc_map))
        {
            /* MAP applied with these rules, v6plus operational: */
            if (mapcfg->mc_v6plus_last_status == V6PLUS_SERV_REPLY_OK)
                mapcfg->mc_v6plus_last_status = V6PLUS_SERV_REPLY_OK_V6PLUS_OK;

            LOG(NOTICE, "nm2_4in6_map: v6plus: %s: MAP config applied", mapcfg->mc_if_name);

            /* If MAP apply was OK, rules are valid, save them: */
            if (!nm2_mapcfg_v6plus_saved_rules_set(mapcfg, rule_list))
            {
                LOG(ERR, "nm2_4in6_map: v6plus: Error saving MAP rules");
            }
        }
        else
        {
            /* MAP failed applying with these rules, rules invalid: */
            if (mapcfg->mc_v6plus_last_status == V6PLUS_SERV_REPLY_OK)
                mapcfg->mc_v6plus_last_status = V6PLUS_SERV_REPLY_OK_NO_RULE;

            LOG(ERR, "nm2_4in6_map: v6plus: %s: Error applying OVSDB MAP config", mapcfg->mc_if_name);
        }
    }

    /*
     * At this point, now the MAP rule status is fully known.
     * Depending on the status, take different actions.
     */

    if (mapcfg->mc_v6plus_last_status == V6PLUS_BOOTUP_SAVED_RULES)
    {
        // Recheck rules in 1 min - 10 min.

        rules_next_check = nm2_mapcfg_v6plus_map_server_nextcheck_get(1*60, 10*60);

        LOG(NOTICE, "nm2_4in6_map: v6plus: Bootup run completed. Recheck rules in [1min-10min]: %lu seconds",
                    rules_next_check);
    }
    else if (mapcfg->mc_v6plus_last_status == V6PLUS_SERV_REPLY_OK_V6PLUS_OK)
    {
        // v6plus operational.
        // Recheck rules in 3h-24h.

        rules_next_check = nm2_mapcfg_v6plus_map_server_nextcheck_get(3*60*60, 24*60*60);

        LOG(NOTICE, "nm2_4in6_map: v6plus: v6plus operational. Recheck rules in [3h-24h]: %lu seconds",
                    rules_next_check);

        nm2_mapcfg_v6plus_operation_report(mapcfg, rule_list, OSN_MAP_V6PLUS_ACTION_STARTED, OSN_MAP_V6PLUS_REASON_NORMAL_OPERATION);
    }
    else if (mapcfg->mc_v6plus_last_status == V6PLUS_SERV_REPLY_OK_NO_RULE)
    {
        // No rules or invalid rules. v6plus outage, break any existing v6plus, delete saved rules.
        // Recheck rules in 10 min - 30 min.

        rules_next_check = nm2_mapcfg_v6plus_map_server_nextcheck_get(10*60, 30*60);

        LOG(NOTICE, "nm2_4in6_map: v6plus: No rules or invalid rules. v6plus disable. "
                    "Recheck rules in [10min-30min]: %lu seconds. Delete saved rules.",
                        rules_next_check);

        /*
         * Set empty rules (which is invalid config) and apply to OSN MAP.
         * This will effectively result in v6plus to be later disabled.
         */
        osn_map_rule_list_set(mapcfg->mc_map, NULL);
        osn_map_apply(mapcfg->mc_map);

        nm2_mapcfg_v6plus_operation_report(mapcfg, rule_list, OSN_MAP_V6PLUS_ACTION_STOPPED, OSN_MAP_V6PLUS_REASON_RULE_MISMATCH);

        // Delete saved rules
        nm2_mapcfg_v6plus_saved_rules_del(mapcfg);
    }
    else if (mapcfg->mc_v6plus_last_status == V6PLUS_SERV_ERR)
    {
        // If there is past information, v6plus operation continues with saved rules.
        // Recheck in 1 min - 10 min.

        rules_next_check = nm2_mapcfg_v6plus_map_server_nextcheck_get(1*60, 10*60);

        LOG(NOTICE, "nm2_4in6_map: v6plus: Server error. Recheck rules in [1min-10min]: %lu seconds",
                    rules_next_check);
    }
    else if (mapcfg->mc_v6plus_last_status == V6PLUS_CLIENT_ERR)
    {
        // v6plus outage, break any existing v6plus, delete saved rules.
        // Recheck rules in 3h - 24h.

        rules_next_check = nm2_mapcfg_v6plus_map_server_nextcheck_get(3*60*60, 24*60*60);

        LOG(NOTICE, "nm2_4in6_map: v6plus: Client error. v6plus disable. "
                    "Recheck rules in [3h-24h]: %lu seconds. Delete saved rules.",
                        rules_next_check);

        /*
         * Set empty rules (which is invalid config) and apply to OSN MAP.
         * This will effectively result in v6plus to be later disabled.
         */
        osn_map_rule_list_set(mapcfg->mc_map, NULL);
        osn_map_apply(mapcfg->mc_map);

        nm2_mapcfg_v6plus_operation_report(mapcfg, rule_list, OSN_MAP_V6PLUS_ACTION_STOPPED, OSN_MAP_V6PLUS_REASON_NORMAL_OPERATION);

        // Delete saved rules.
        nm2_mapcfg_v6plus_saved_rules_del(mapcfg);
    }
    else
    {
        rules_next_check = nm2_mapcfg_v6plus_map_server_nextcheck_get(1*60, 10*60);

        LOG(WARN, "nm2_4in6_map: v6plus: Unexpected MAP rule server status: %d. "
                    "Assume server error. Recheck rules in [1min-10min]: %lu seconds ",
                    mapcfg->mc_v6plus_last_status,
                    rules_next_check);
    }

end:
    /*
     * Update MAP OVSDB state (Map_State):
     *
     * At this point MAP was either:
     *  - Successfully applied with a valid config: MAP_State will be properly populated, controller
     *    will further configure other MAP and network components (or keep the current state
     *    if already operational)
     *  - or if v6plus should be turned off according to MAP rule server respond: MAP was applied
     *    with invalid config resulting in MAP_State to not be populated with all the needed
     *    fields which will be a signal to the controller to further break things down and v6plus
     *    will be effectively disabled.
     */
    nm2_mapcfg_ovsdb_state_upsert(mapcfg);

    LOG(NOTICE, "nm2_4in6_map: v6plus: v6plus operation handler job finished");

    /* Schedule to run the next check job: */
    if (rules_next_check != 0)
    {
        nm2_mapcfg_v6plus_job_schedule(mapcfg, rules_next_check);
    }

    /* Free-up rulelist object: */
    osn_map_v6plus_rulelist_del(rule_list);
}

void nm2_mapcfg_v6plus_job_handler(struct ev_loop *loop, struct ev_debounce *ev, int revent)
{
    struct nm2_mapcfg *mapcfg = CONTAINER_OF(ev, struct nm2_mapcfg, mc_v6plus_job);

    /* Run v6plus operation handler job: */
    nm2_mapcfg_v6plus_operation_handler(mapcfg);
}

void nm2_mapcfg_v6plus_job_stop(struct nm2_mapcfg *mapcfg)
{
    if (!mapcfg->mc_v6plus_job_inited)
    {
        return;
    }

    ev_debounce_stop(EV_DEFAULT, &mapcfg->mc_v6plus_job);

    LOG(DEBUG, "nm2_4in6_map: v6plus: Stopped v6plus job");
}

void nm2_mapcfg_v6plus_job_schedule(struct nm2_mapcfg *mapcfg, double timeout)
{
    if (!mapcfg->mc_v6plus_job_inited)
    {
        ev_debounce_init(&mapcfg->mc_v6plus_job, nm2_mapcfg_v6plus_job_handler, timeout);
        mapcfg->mc_v6plus_job_inited = true;
    }

    ev_debounce_stop(EV_DEFAULT, &mapcfg->mc_v6plus_job);
    ev_debounce_set(&mapcfg->mc_v6plus_job, timeout);

    ev_debounce_start(EV_DEFAULT, &mapcfg->mc_v6plus_job);

    LOG(INFO, "nm2_4in6_map: v6plus: Next v6plus operation handler job scheduled to run in %lf seconds",
                timeout);
}

void nm2_mapcfg_v6plus_hgw_checker_start(struct nm2_mapcfg *mapcfg)
{
    /* HGW checks to be done continuously at regular intervals: */

    if (!mapcfg->mc_v6plus_hgw_checker_inited)
    {
        ev_timer_init(
            &mapcfg->mc_v6plus_hgw_checker,
            nm2_mapcfg_v6plus_hgw_status_checker_handler,
            NM2_V6PLUS_HGW_STATUS_RECHECK_TIME,
            NM2_V6PLUS_HGW_STATUS_RECHECK_TIME);

        mapcfg->mc_v6plus_hgw_checker_inited = true;
    }

    if (!ev_is_active(&mapcfg->mc_v6plus_hgw_checker))
    {
        ev_timer_start(EV_DEFAULT, &mapcfg->mc_v6plus_hgw_checker);
    }
}

void nm2_mapcfg_v6plus_hgw_checker_stop(struct nm2_mapcfg *mapcfg)
{
    if (!mapcfg->mc_v6plus_hgw_checker_inited)
    {
        return;
    }

    ev_timer_stop(EV_DEFAULT, &mapcfg->mc_v6plus_hgw_checker);

    LOG(DEBUG, "nm2_4in6_map: v6plus: Stopped HGW checker timer");

    mapcfg->mc_v6plus_hgw_checker_inited = false;
}
