/*
* Copyright (c) 2019, Sagemcom.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include "log.h"
#include "memutil.h"
#include "nfm_ovsdb.h"
#include "nfm_rule.h"
#include "nfm_trule.h"
#include "policy_tags.h"
#include "schema.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB

struct nfm_interface_role
{
	char 			*ir_name;		/* Interface name */
	char 			*ir_role;		/* Interface role  */
    ds_tree_node_t	 ir_tnode;		/* Tree node */
};

struct ovsdb_table table_Openflow_Tag;
struct ovsdb_table table_Openflow_Tag_Group;
struct ovsdb_table table_Openflow_Local_Tag;
struct ovsdb_table table_Netfilter;
struct ovsdb_table table_Wifi_Inet_Config;

static ds_tree_t nfm_interface_role_list = DS_TREE_INIT(ds_str_cmp, struct nfm_interface_role, ir_tnode);

void callback_Netfilter(ovsdb_update_monitor_t *mon, struct schema_Netfilter *old,
		struct schema_Netfilter *record)
{
	bool errcode = true;

	if (!mon) {
		LOGE("Netfilter OVSDB event: invalid parameter");
		return;
	}

	switch (mon->mon_type) {
	case OVSDB_UPDATE_NEW:
		if (nfm_trule_is_template(record)) {
			errcode = nfm_trule_new(record);
			if (!errcode) {
				LOGE("Netfilter OVSDB event: new template rule %s failed",
					record ? record->name : "-");
			}
		} else {
			errcode = nfm_rule_new(record);
			if (!errcode) {
				LOGE("Netfilter OVSDB event: new rule %s failed",
					record ? record->name : "-");
			}
		}
		break;

	case OVSDB_UPDATE_DEL:
		if (nfm_trule_is_template(old)) {
			errcode = nfm_trule_del(old);
			if (!errcode) {
				LOGE("Netfilter OVSDB event: delete template rule %s failed",
						old ? old->name : "-");
			}
		} else {
			errcode = nfm_rule_del(old);
			if (!errcode) {
				LOGE("Netfilter OVSDB event: delete rule %s failed",
						old ? old->name : "-");
			}
		}
		break;

	case OVSDB_UPDATE_MODIFY:
		/* if old rule is not present, add/modify template rule or
		 * non-template rule based on new record's rule type
		 *
		 * if old rule is present, add/modify template rule or
		 * non-template rule based on old record's rule type */
		if ((!(strcmp(old->rule, "")) && (nfm_trule_is_template(record))) ||
		    (nfm_trule_is_template(old))) {
			errcode = nfm_trule_modify(record);
			if (!errcode) {
				LOGE("Netfilter OVSDB event: modify template rule %s failed",
						record ? record->name : "-");
			}
		} else {
			errcode = nfm_rule_modify(record);
			if (!errcode) {
				LOGE("Netfilter OVSDB event: modify rule %s failed",
						record ? record->name : "-");
			}
		}
		break;

	default:
		LOGE("Netfilter OVSDB event: unknown type %d", mon->mon_type);
		break;
	}
}

static void callback_Wifi_Inet_Config(
		ovsdb_update_monitor_t *mon,
		struct schema_Wifi_Inet_Config *old,
		struct schema_Wifi_Inet_Config *new)
{
	struct nfm_interface_role *ir;

	/*
	 * The ovsdb_update_ API uses empty records in case of the two events below.
	 * The logic to handle interface role becomes much simpler if they are
	 * actually NULL instead
	 */
	switch (mon->mon_type)
	{
		case OVSDB_UPDATE_NEW:
			old = NULL;
			break;

		case OVSDB_UPDATE_MODIFY:
			break;

		case OVSDB_UPDATE_DEL:
			new = NULL;
			break;

		default:
			LOG(ERR, "Wifi_Inet_Config OVSDB event: unknown type %d", mon->mon_type);
			break;
	}

	if (old != NULL)
	{
		ir = ds_tree_find(&nfm_interface_role_list, old->if_name);
		if (ir != NULL && (new == NULL || strcmp(old->if_name, new->if_name) != 0))
		{
			ds_tree_remove(&nfm_interface_role_list, ir);
			FREE(ir->ir_name);
			FREE(ir->ir_role);
			FREE(ir);
		}
	}

	if (new != NULL)
	{
		ir = ds_tree_find(&nfm_interface_role_list, new->if_name);
		if (ir == NULL)
		{
			ir = CALLOC(sizeof(struct nfm_interface_role), 1);
			ir->ir_name = STRDUP(new->if_name);
			ds_tree_insert(&nfm_interface_role_list, ir, ir->ir_name);
		}

		/* Update the rule */
		if (ir->ir_role != NULL) FREE(ir->ir_role);
		ir->ir_role = STRDUP(new->role);
	}
}

/*
 * Return the interface `role` field as present in Wifi_Inet_Config
 */
const char *nfm_interface_role(const char *ifname)
{
	struct nfm_interface_role *ir;

	ir = ds_tree_find(&nfm_interface_role_list, (char *)ifname);
	if (ir == NULL) return NULL;

	return ir->ir_role;
}

bool nfm_ovsdb_init(void)
{
    LOGD("Initializing Netfilter OVSDB tables");
    OVSDB_TABLE_INIT(Openflow_Tag, name);
    OVSDB_TABLE_INIT(Openflow_Tag_Group, name);
    OVSDB_TABLE_INIT(Openflow_Local_Tag, name);
    OVSDB_TABLE_INIT(Netfilter, name);
    OVSDB_TABLE_INIT(Wifi_Inet_Config, if_name);
    om_standard_callback_openflow_tag(&table_Openflow_Tag);
    om_standard_callback_openflow_tag_group(&table_Openflow_Tag_Group);
    om_standard_callback_openflow_local_tag(&table_Openflow_Local_Tag);
    OVSDB_TABLE_MONITOR(Netfilter, false);
    OVSDB_TABLE_MONITOR(Wifi_Inet_Config, false);
    return true;
}

