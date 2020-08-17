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
 * Openflow Manager - OVSDB monitoring
 */

#define  _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <stdarg.h>
#include <linux/types.h>

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "om.h"
#include "util.h"
#include "ovsdb_sync.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"


/*****************************************************************************/
#define MODULE_ID LOG_MODULE_ID_MAIN

/*****************************************************************************/
ovsdb_table_t table_Openflow_Local_Tag;
static ovsdb_update_monitor_t   om_monitor_config;
static ovsdb_update_monitor_t   om_monitor_tags;
static ovsdb_update_monitor_t   om_monitor_tag_groups;

static ds_list_t                range_rules = DS_LIST_INIT(struct om_rule_node,
                                                           lnode );

static bool                     om_range_recurse_parse(struct schema_Openflow_Config *sflow);

/******************************************************************************
 * Updates Openflow_State table to reflect the exit code of ovs-ofctl command
 *****************************************************************************/
static void
om_monitor_update_openflow_state( struct schema_Openflow_Config *ofconf,
                                  om_action_t type, bool ret )
{
    struct  schema_Openflow_State    ofstate;
    json_t                           *jrc;
    json_t                           *js_trans = NULL;
    json_t                           *js_where = NULL;
    json_t                           *js_row   = NULL;

    // memset ofstate to avoid conversion to schema failures
    memset( &ofstate, 0, sizeof( ofstate ) );

    STRSCPY(ofstate.bridge, ofconf->bridge);
    STRSCPY(ofstate.token, ofconf->token);

    // Reflect the result of ovs-ofctl command
    ofstate.success = ret;
    ofstate.success_exists = true;

    if( type == ADD ) {
        // A new flow was added successfully. Add a new row into Openflow_State so that
        // the cloud knows ovs-ofctl succeeded.
        if( !( js_row = schema_Openflow_State_to_json( &ofstate, NULL ))) {
            LOGE( "Failed to convert to schema" );
            goto err_exit;
        }

        if( !(js_trans = ovsdb_tran_multi( NULL, NULL,
                        SCHEMA_TABLE( Openflow_State ),
                        OTR_INSERT,
                        NULL,
                        js_row ))) {
            LOGE( "Failed to create TRANS object" );
            goto err_exit;
        }

        jrc = ovsdb_method_send_s(MT_TRANS, js_trans);
        if (jrc == NULL)
        {
            LOGE( "Openflow_State insert failed to send" );
            goto err_exit;
        }
        json_decref(jrc);

    } else if( type == DELETE ) {
        if( ret ) {
            // A flow was deleted successfully. Delete the corresponding
            // row from Openflow_State so that cloud knows ovs-ofctl succeeded.
            if( !( js_where = ovsdb_tran_cond( OCLM_STR, "token", OFUNC_EQ,
                            ofstate.token ))) {
                LOGE( "Failed to create DEL WHERE object" );
                goto err_exit;
            }

            if( !(js_trans = ovsdb_tran_multi( NULL, NULL,
                            SCHEMA_TABLE( Openflow_State ),
                            OTR_DELETE,
                            js_where,
                            NULL ))) {
                LOGE( "Failed to create DEL TRANS object" );
                goto err_exit;
            }

            jrc = ovsdb_method_send_s(MT_TRANS, js_trans );
            if (jrc == NULL)
            {
                LOGE( "Openstate delete failed to send" );
            }
            json_decref(jrc);

            return;
        } else {
            // ovs-ofctl failed to delete the flow. Don't delete the
            // row from Openflow_State
        }
    }

err_exit:
    if (js_trans) {
        json_decref(js_trans);
    } else {
        if (js_where) {
            json_decref(js_where);
        }
        if (js_row) {
            json_decref(js_row);
        }
    }

    return;
}

/******************************************************************************
 * Interact with port range list
 *****************************************************************************/
bool
om_range_add_range_rule(struct schema_Openflow_Config *rule)
{
    struct om_rule_node *add_rule = NULL;

    add_rule = calloc(1, sizeof(*add_rule));

    memcpy(&add_rule->rule, rule, sizeof(*rule));
    ds_list_insert_head(&range_rules, add_rule);

    return true;
}

bool
om_range_clear_range_rules(void)
{
    struct om_rule_node *data;
    ds_list_iter_t      iter;

    for (data = ds_list_ifirst(&iter, &range_rules);
         data != NULL; data = ds_list_inext(&iter))
    {
        ds_list_iremove(&iter);
        free(data);
    }

    return true;
}

ds_list_t *
om_range_get_range_rules()
{
    return &range_rules;
}

/******************************************************************************
 * Range Conversion functions
 ******************************************************************************/

static bool
om_range_is_range_specified(char *str_range)
{
    if ( strstr(str_range, TEMPLATE_RANGE) != NULL ) {
        return true;
    }

    return false;
}

static RANGE
om_range_get_range_type(char *str_range)
{
    if ( strstr(str_range, TEMPLATE_IPV4_RANGE_SRC) != NULL ) {
        return IPV4_RANGE_SRC;
    } else if (  strstr(str_range, TEMPLATE_IPV4_RANGE_DST) != NULL  ) {
        return IPV4_RANGE_DST;
    } else if (  strstr(str_range, TEMPLATE_IPV6_RANGE_SRC) != NULL  ) {
        return IPV6_RANGE_SRC;
    } else if (  strstr(str_range, TEMPLATE_IPV6_RANGE_DST) != NULL  ) {
        return IPV6_RANGE_DST;
    } else if (  strstr(str_range, TEMPLATE_PORT_RANGE_SRC)  != NULL ) {
        return PORT_RANGE_SRC;
    } else if (  strstr(str_range, TEMPLATE_PORT_RANGE_DST)  != NULL ) {
        return PORT_RANGE_DST;
    } else {
        return NONE;
    }
}

// Convert IP string into integer
static uint32_t
om_range_dot_to_long_ip(char* ipstring)
{
    if (ipstring == NULL) {
        return 0;
    }

    uint32_t ip = ntohl(inet_addr(ipstring));
    return ip;
}

// Convert IP integer into string
static char*
om_range_long_to_dot_ip(uint32_t ipnum)
{
    uint8_t bytes[4];
    char* buf = malloc (sizeof (char) * 16);
    bytes[0] = (ipnum >> 24) & 0xFF;
    bytes[1] = (ipnum >> 16) & 0xFF;
    bytes[2] = (ipnum >> 8) & 0xFF;
    bytes[3] = ipnum & 0xFF;
    snprintf(buf, (sizeof (char) * 16), "%d.%d.%d.%d", bytes[0], bytes[1], bytes[2], bytes[3]);
    return buf;
}

//  Remove substring from string ( remove $<range> from rule )
static void
om_range_rmv_substr(char *s,const char *toremove)
{
    while( (s=strstr(s,toremove)) ) {
        memmove(s,s+strlen(toremove),1+strlen(s+strlen(toremove)));
    }
}

// Pull out the start and end values for a string,
// Example: tcp,tp_src=$<1-4> turns into (1, 4, tcp)
static bool
om_range_extract(struct schema_Openflow_Config *sflow, char *pattern,
        char *start, char *end, size_t str_size, struct schema_Openflow_Config *out)
{
    char *token, *str, *iter;
    char removing[1024];
    char *orig_str;
    bool rc = false;

    memcpy(out, sflow, sizeof(*out));

    iter = strstr(sflow->rule, pattern);
    iter = strstr(iter, "=");
    iter = iter + 3;

    str   = strdup(iter);  // We own str's memory now.
    orig_str = str;

    token = strsep(&str, "-");
    if (!token) {
        rc = false;
        goto err;
    }
    strscpy(start, token, str_size);

    token = strsep(&str, ">");
    if (!token) {
        rc = false;
        goto err;
    }
    strscpy(end, token, str_size);

    // Remove the range specification from the rule
    sprintf(removing, ",%s%s-%s>", pattern, start, end);
    if ( strstr(sflow->rule, removing) != NULL ) {
        STRSCPY(out->rule, sflow->rule);
        om_range_rmv_substr(out->rule, removing);
        rc = true;
    }
err:
    if (orig_str) free(orig_str);
    return rc;
}

static bool
om_range_generate_ipv6_rules( char *s, char *e, struct schema_Openflow_Config *sflow, bool is_src)
{
    struct schema_Openflow_Config out;

    char            output[64], rule[1024];
    struct in6_addr sn, en;
    int             octet;
    bool            ret = true;

    memset(&out, 0, sizeof(out));
    memcpy(&out, sflow, sizeof(out));

    inet_pton(AF_INET6,s,&sn);
    inet_pton(AF_INET6,e,&en);

    for ( ; ; ) {
        memset(output, 0, (sizeof(char)*64));
        memset(rule,   0, (sizeof(char)*1024));

        /* print the address */
        if (!inet_ntop(AF_INET6, &sn, output, sizeof(output))) {
            LOGE("inet_ntop failed, errno = %d", errno);
            break;
        }

        if (is_src) {
            sprintf(rule, "%s,ipv6_src=%s", sflow->rule, output);
        } else {
            sprintf(rule, "%s,ipv6_dst=%s", sflow->rule, output);
        }

        STRSCPY(out.rule, rule);
        ret = ret && om_range_recurse_parse(&out);

        /* break if we hit the last address or (sn > en) */
        if (memcmp(sn.s6_addr, en.s6_addr, 16) >= 0) {
            break;
        }

        /* increment sn, and move towards en */
        for (octet = 15; octet >= 0; --octet) {
            if (sn.s6_addr[octet] < 255) {
                sn.s6_addr[octet]++;
                break;
            } else {
                sn.s6_addr[octet] = 0;
            }
        }

        if (octet < 0) {
            break; /* top of logical address range */
        }
    }

    return ret;
}

static bool
om_range_generate_ipv4_rules( char *start, char *end,
                              struct schema_Openflow_Config *sflow, bool is_src)
{
    struct schema_Openflow_Config   out;
    bool                            ret = true;
    char                            rule[1024];
    char                            *ipv4buff;

    unsigned long                   startIP = om_range_dot_to_long_ip(start);
    unsigned long                   endIP = om_range_dot_to_long_ip(end);

    unsigned int                    iterator;
    
    memcpy(&out, sflow, sizeof(out));

    for (iterator=startIP; iterator <= endIP; iterator++)
    {
        memset( rule, 0, sizeof(char) * 1024);

        ipv4buff = om_range_long_to_dot_ip(iterator);

        if (is_src) {
            sprintf(rule, "%s,nw_src=%s", sflow->rule, ipv4buff);
        } else {
            sprintf(rule, "%s,nw_dst=%s", sflow->rule, ipv4buff);
        }

        free(ipv4buff);

        STRSCPY(out.rule, rule);
        ret = ret && om_range_recurse_parse(&out);
    }

    return ret;
}

static bool
om_range_generate_port_rules( int start, int end,
                              struct schema_Openflow_Config *sflow, bool is_src)
{
    struct schema_Openflow_Config   out;
    bool                            ret = true;
    int                             i = 0;
    char                            rule[1024];

    memset(&out, 0, sizeof(out));
    memcpy(&out, sflow, sizeof(out));

    for (i = start; i <= end; i++) {
        memset(rule, 0, sizeof(char) * 1024);

        if (is_src) {
            sprintf(rule, "%s,tp_src=%d", sflow->rule, i);
        } else {
            sprintf(rule, "%s,tp_dst=%d", sflow->rule, i);
        }

        STRSCPY(out.rule, rule);

        // Set ret to false if it is ever false
        ret = om_range_recurse_parse(&out) && ret;
    }

    return ret;
}

static bool
om_range_recurse_parse(struct schema_Openflow_Config *sflow)
{
    struct schema_Openflow_Config   out;
    char                            start[256];
    char                            end[sizeof(start)];
    RANGE                           range_type;

    // Default condition to end the recursive calls
    if (!om_range_is_range_specified(sflow->rule)) {
        return om_range_add_range_rule(sflow);
    }

    memset(&out, 0, sizeof(out));
    memcpy(&out, sflow, sizeof(out));

    range_type = om_range_get_range_type(sflow->rule);
    switch ( range_type ) {
        case IPV4_RANGE_DST:
        {
            if (!om_range_extract(sflow, TEMPLATE_IPV4_RANGE_DST, start, end, sizeof(start), &out)) {
                return false;
            }

            return om_range_generate_ipv4_rules(start, end, &out, false);
        }

        case IPV4_RANGE_SRC:
        {
            if (!om_range_extract(sflow, TEMPLATE_IPV4_RANGE_SRC, start, end, sizeof(start), &out)) {
                return false;
            }

            return om_range_generate_ipv4_rules(start, end, &out, true);
        }

        case IPV6_RANGE_DST:
        {
            if (!om_range_extract(sflow, TEMPLATE_IPV6_RANGE_DST, start, end, sizeof(start), &out)) {
                return false;
            }

            return om_range_generate_ipv6_rules(start, end, &out, false);
        }

        case IPV6_RANGE_SRC:
        {
            if (!om_range_extract(sflow, TEMPLATE_IPV6_RANGE_SRC, start, end, sizeof(start), &out)) {
                return false;
            }

            return om_range_generate_ipv6_rules(start, end, &out, true);
        }

        case PORT_RANGE_DST:
        {
            if (!om_range_extract(sflow, TEMPLATE_PORT_RANGE_DST, start, end, sizeof(start), &out)) {
                return false;
            }

            return om_range_generate_port_rules(atoi(start), atoi(end), &out, false);
        }

        case PORT_RANGE_SRC:
        {
            if (!om_range_extract(sflow, TEMPLATE_PORT_RANGE_SRC, start, end, sizeof(start), &out)) {
                return false;
            }

            return om_range_generate_port_rules(atoi(start), atoi(end), &out, true);
        }

        default:
            return false;
    }

    return false;
}

bool
om_range_generate_range_rules(struct schema_Openflow_Config *ofconf)
{
    if (om_range_is_range_specified(ofconf->rule)) {
        return om_range_recurse_parse(ofconf);
    } else {
        return om_range_add_range_rule(ofconf);
    }

    return false;
}

/******************************************************************************
 * Adds/deletes flows based on additions/deletions to Openflow_Config table
 *****************************************************************************/
static bool
om_monitor_update_flows_parsed(om_action_t type, struct schema_Openflow_Config *ofconf)
{
    bool                             is_template;
    bool                             ret = false;

    is_template = om_tflow_rule_is_template(ofconf->rule);

    switch( type ) {
        case ADD:
            if (is_template) {
                ret = om_tflow_add_from_schema(ofconf);
            }
            else {

                ret = om_add_flow( ofconf->token, ofconf );
                LOGN("[%s] Static flow insertion %s (%s, %u, %u, \"%s\", \"%s\")",
                     ofconf->token,
                     (ret == true) ? "succeeded" : "failed",
                     ofconf->bridge, ofconf->table,
                     ofconf->priority, ofconf->rule,
                     ofconf->action);
            }
            break;

        case UPDATE:
            LOGE("Cloud attempted to update a flow (token '%s') -- this is not supported!", ofconf->token);
            return false;

        case DELETE:
            if (is_template) {
                ret = om_tflow_remove_from_schema(ofconf);
            }
            else {
                ret = om_del_flow( ofconf->token, ofconf );
                LOGN("[%s] Static flow deletion %s (%s, %u, %u, \"%s\", \"%s\")",
                     ofconf->token,
                     (ret == true) ? "succeeded" : "failed",
                     ofconf->bridge, ofconf->table,
                     ofconf->priority, ofconf->rule,
                     ofconf->action);
            }
            break;
    }

    return ret;
}

static void
om_monitor_update_flows(om_action_t type, json_t *js)
{
    struct  schema_Openflow_Config   ofconf;
    struct  schema_Openflow_Config   ofconf_cpy;
    struct  om_rule_node             *data;
    ds_list_t                        *rules;
    pjs_errmsg_t                     perr;
    bool                             ret;

    memset( &ofconf, 0, sizeof( ofconf ) );
    memset( &ofconf_cpy, 0, sizeof( ofconf_cpy ) );

    if( !schema_Openflow_Config_from_json( &ofconf, js, false, perr )) {
        LOGE( "Failed to parse new Openflow_Config row: %s", perr );
        return;
    }

    memcpy(&ofconf_cpy, &ofconf, sizeof(ofconf_cpy));

    // Handle the condition where a range exists in the rule
    if (om_range_is_range_specified(ofconf.rule)) {
        (void)om_range_clear_range_rules();

        ret = om_range_generate_range_rules(&ofconf);
        if (ret) {
            rules = om_range_get_range_rules();

            ds_list_foreach(rules,data) {
                /* NOTE: Due to the security aspect of the use case when ranges
                 *       are involved, rules are still added partially when errors
                 *       are encountered, and are not rolled back completely.
                 *       
                 *       For eg: For rule: tcp,tp_src=$<1-5>, if addition "tcp, tp_src=3"
                 *       fails, all others(1,2,4,5) are still added.
                 *      
                 *       The arguement is, if ports 1-5 are to be allowed, and 3 fails,
                 *       1,2,4,5 are still allowed. And similarly for blocked ports.
                 *       For these use cases, something is always better than nothing.
                 */
                ret = ret && om_monitor_update_flows_parsed(type, &data->rule);
            }
        } else {
            LOGE("%s: Failed to generate rules for insertion", __func__);
            (void)om_range_clear_range_rules();
        }
    } else {
        ret = om_monitor_update_flows_parsed(type, &ofconf);
    }

    // Update the result in Openflow_State table so the cloud knows
    om_monitor_update_openflow_state( &ofconf, type, ret );

    return;
}

/******************************************************************************
 * Takes appropriate actions based on cloud updates to Openflow_Config table
 *****************************************************************************/
static void
om_monitor_config_cb(ovsdb_update_monitor_t *self)
{
    switch(self->mon_type) {

    case OVSDB_UPDATE_NEW:
        om_monitor_update_flows(ADD, self->mon_json_new);
        break;

    case OVSDB_UPDATE_MODIFY:
        om_monitor_update_flows(UPDATE, self->mon_json_new);
        break;

    case OVSDB_UPDATE_DEL:
        om_monitor_update_flows(DELETE, self->mon_json_old);
        break;

    default:
        break;

    }

    return;
}

/******************************************************************************
 * Adds/deletes/updates tags based on Openflow_Tag table
 *****************************************************************************/
static void
om_monitor_update_tags(om_action_t type, json_t *js)
{
    struct  schema_Openflow_Tag      stag;
    pjs_errmsg_t                     perr;

    memset(&stag, 0, sizeof(stag));

    if(!schema_Openflow_Tag_from_json(&stag, js, false, perr)) {
        LOGE("Failed to parse Openflow_Tag row: %s", perr);
        return;
    }

    switch(type) {

    case ADD:
        om_tag_add_from_schema(&stag);
        break;

    case DELETE:
        om_tag_remove_from_schema(&stag);
        break;

    case UPDATE:
        om_tag_update_from_schema(&stag);
        break;

    default:
        return;

    }

    return;
}

/******************************************************************************
 * Adds/deletes/updates local tags based on Openflow_Local_Tag table
 *****************************************************************************/
void callback_Openflow_Local_Tag(
        ovsdb_update_monitor_t *mon,
        struct schema_Openflow_Local_Tag *old_rec,
        struct schema_Openflow_Local_Tag *conf)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        om_local_tag_add_from_schema(conf);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        om_local_tag_remove_from_schema(old_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        om_local_tag_update_from_schema(conf);
    }
}


/******************************************************************************
 * Handle Openflow_Tag callbacks
 ******************************************************************************/
static void
om_monitor_tags_cb(ovsdb_update_monitor_t *self)
{
    switch(self->mon_type) {

    case OVSDB_UPDATE_NEW:
        om_monitor_update_tags(ADD, self->mon_json_new);
        break;

    case OVSDB_UPDATE_MODIFY:
        om_monitor_update_tags(UPDATE, self->mon_json_new);
        break;

    case OVSDB_UPDATE_DEL:
        om_monitor_update_tags(DELETE, self->mon_json_old);
        break;

    default:
        break;

    }

    return;
}

/******************************************************************************
 * Adds/deletes/updates tag groups based on Openflow_Tag_Group table
 *****************************************************************************/
static void
om_monitor_update_tag_groups(om_action_t type, json_t *js)
{
    struct schema_Openflow_Tag_Group    sgroup;
    pjs_errmsg_t                        perr;

    memset(&sgroup, 0, sizeof(sgroup));

    if(!schema_Openflow_Tag_Group_from_json(&sgroup, js, false, perr)) {
        LOGE("Failed to parse Openflow_Tag_Group row: %s", perr);
        return;
    }

    switch(type) {

    case ADD:
        om_tag_group_add_from_schema(&sgroup);
        break;

    case DELETE:
        om_tag_group_remove_from_schema(&sgroup);
        break;

    case UPDATE:
        om_tag_group_update_from_schema(&sgroup);
        break;

    default:
        return;

    }

    return;
}

/******************************************************************************
 * Handle Openflow_Tag_Group callbacks
 *****************************************************************************/
static void
om_monitor_tag_groups_cb(ovsdb_update_monitor_t *self)
{
    switch(self->mon_type) {

    case OVSDB_UPDATE_NEW:
        om_monitor_update_tag_groups(ADD, self->mon_json_new);
        break;

    case OVSDB_UPDATE_MODIFY:
        om_monitor_update_tag_groups(UPDATE, self->mon_json_new);
        break;

    case OVSDB_UPDATE_DEL:
        om_monitor_update_tag_groups(DELETE, self->mon_json_old);
        break;

    default:
        break;

    }

    return;
}

/******************************************************************************
 * Sets up Openflow_Config table monitoring
 *****************************************************************************/
bool
om_monitor_init(void)
{
    LOGI( "Openflow Monitoring initialization" );

    // Start OVSDB monitoring
    if(!ovsdb_update_monitor(&om_monitor_config,
                             om_monitor_config_cb,
                             SCHEMA_TABLE(Openflow_Config),
                             OMT_ALL)) {
        LOGE("Failed to monitor OVSDB table '%s'", SCHEMA_TABLE( Openflow_Config ) );
        return false;
    }

    if(!ovsdb_update_monitor(&om_monitor_tags,
                             om_monitor_tags_cb,
                             SCHEMA_TABLE(Openflow_Tag),
                             OMT_ALL)) {
        LOGE("Failed to monitor OVSDB table '%s'", SCHEMA_TABLE(Openflow_Tag));
        return false;
    }

    if(!ovsdb_update_monitor(&om_monitor_tag_groups,
                             om_monitor_tag_groups_cb,
                             SCHEMA_TABLE(Openflow_Tag_Group),
                             OMT_ALL)) {
        LOGE("Failed to monitor OVSDB table '%s'", SCHEMA_TABLE(Openflow_Tag_Group));
        return false;
    }

    OVSDB_TABLE_INIT_NO_KEY(Openflow_Local_Tag);
    OVSDB_TABLE_MONITOR(Openflow_Local_Tag, false);

    return true;
}
