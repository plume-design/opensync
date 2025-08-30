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

#include "target.h"
#include "log.h"
#include "nfm_osfw.h"
#include "nfm_rule.h"
#include "osn_fw.h"
#include "nfm_rule.h"
#include <string.h>
#include <stdio.h>

#include "kconfig.h"
#include "ds_list.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define NFM_OSFW_RULE_LEN 512   /* Maximum length of schema rule string */

#define NFM_OSFW_PROTOCOL_INET4 "ipv4"
#define NFM_OSFW_PROTOCOL_INET6 "ipv6"
#define NFM_OSFW_PROTOCOL_BOTH "both"
#define NFM_OSFW_PROTOCOL_ETH "eth"

#define NFM_OSFW_TABLE_FILTER "filter"
#define NFM_OSFW_TABLE_NAT "nat"
#define NFM_OSFW_TABLE_MANGLE "mangle"
#define NFM_OSFW_TABLE_RAW "raw"
#define NFM_OSFW_TABLE_SECURITY "security"
#define NFM_OSFW_TABLE_BROUTE "broute"

#define NFM_OSFW_TARGET_ACCEPT "ACCEPT"
#define NFM_OSFW_TARGET_DROP "DROP"
#define NFM_OSFW_TARGET_RETURN "RETURN"
#define NFM_OSFW_TARGET_REJECT "REJECT"
#define NFM_OSFW_TARGET_QUEUE "QUEUE"

#define NFM_OSEB_TARGET_ACCEPT "ACCEPT"
#define NFM_OSEB_TARGET_DROP "DROP"
#define NFM_OSEB_TARGET_CONTINUE "CONTINUE"
#define NFM_OSEB_TARGET_RETURN "RETURN"

struct nfm_osfw_base nfm_osfw_base;
struct nfm_osfw_eb_base nfm_osfw_eb_base;

struct nfm_osfw_hook {
    target_om_hook_t hook;
    const char* rule;
    ds_list_node_t node;
};
static ds_list_t nfm_osfw_hook_list = DS_LIST_INIT(struct nfm_osfw_hook, node);
static ds_list_t nfm_osfw_eb_hook_list = DS_LIST_INIT(struct nfm_osfw_hook, node);

bool nfm_osfw_eb_is_valid_chain(const char *chain)
{
	if (strchr(chain, ' ')) {
		return false;
	} else if (!strncmp(chain, NFM_OSEB_TARGET_ACCEPT, sizeof(NFM_OSEB_TARGET_ACCEPT))) {
		return false;
	} else if (!strncmp(chain, NFM_OSEB_TARGET_DROP, sizeof(NFM_OSEB_TARGET_DROP))) {
		return false;
	} else if (!strncmp(chain, NFM_OSEB_TARGET_CONTINUE, sizeof(NFM_OSEB_TARGET_CONTINUE))) {
		return false;
	} else if (!strncmp(chain, NFM_OSEB_TARGET_RETURN, sizeof(NFM_OSEB_TARGET_RETURN))) {
		return false;
	}
	return true;
}

static bool nfm_osfw_is_valid_chain(const char *chain)
{
	if (strchr(chain, ' ')) {
		return false;
	} else if (!strncmp(chain, NFM_OSFW_TARGET_ACCEPT, sizeof(NFM_OSFW_TARGET_ACCEPT))) {
		return false;
	} else if (!strncmp(chain, NFM_OSFW_TARGET_DROP, sizeof(NFM_OSFW_TARGET_DROP))) {
		return false;
	} else if (!strncmp(chain, NFM_OSFW_TARGET_RETURN, sizeof(NFM_OSFW_TARGET_RETURN))) {
		return false;
	} else if (!strncmp(chain, NFM_OSFW_TARGET_REJECT, sizeof(NFM_OSFW_TARGET_REJECT))) {
		return false;
	} else if (!strncmp(chain, NFM_OSFW_TARGET_QUEUE, sizeof(NFM_OSFW_TARGET_QUEUE))) {
		return false;
	}
	return true;
}

static enum osfw_table nfm_osfw_convert_table(const char *table)
{
	enum osfw_table value = OSFW_TABLE_FILTER;

	if (!strncmp(table, NFM_OSFW_TABLE_FILTER, sizeof(NFM_OSFW_TABLE_FILTER))) {
		value = OSFW_TABLE_FILTER;
	} else if (!strncmp(table, NFM_OSFW_TABLE_NAT, sizeof(NFM_OSFW_TABLE_NAT))) {
		value = OSFW_TABLE_NAT;
	} else if (!strncmp(table, NFM_OSFW_TABLE_MANGLE, sizeof(NFM_OSFW_TABLE_MANGLE))) {
		value = OSFW_TABLE_MANGLE;
	} else if (!strncmp(table, NFM_OSFW_TABLE_RAW, sizeof(NFM_OSFW_TABLE_RAW))) {
		value = OSFW_TABLE_RAW;
	} else if (!strncmp(table, NFM_OSFW_TABLE_SECURITY, sizeof(NFM_OSFW_TABLE_SECURITY))) {
		value = OSFW_TABLE_SECURITY;
	} else if (kconfig_enabled(CONFIG_TARGET_ENABLE_EBTABLES) &&
               !strncmp(table, NFM_OSFW_TABLE_BROUTE, sizeof(NFM_OSFW_TABLE_BROUTE))) {
		value = OSFW_TABLE_BROUTE;
	}else {
		LOGE("Convert firewall table: invalid table %s", table);
	}
	return value;
}

static void nfm_osfw_on_reschedule(struct ev_loop *loop, ev_timer *watcher, int revents)
{
	bool errcode = true;

	errcode = osfw_apply();
	if (!errcode) {
		LOGE("Apply firewall configuration failed");
	}
}

static void nfm_osfw_eb_on_reschedule(struct ev_loop *loop, ev_timer *watcher, int revents)
{
	bool errcode;

	errcode = osfw_eb_apply();
	if (!errcode) {
		LOGE("Apply ebtables configuration failed");
	}
}

static bool nfm_osfw_reschedule(void)
{
	ev_timer_start(nfm_osfw_base.loop, &nfm_osfw_base.timer);
	return true;
}

static bool nfm_osfw_eb_reschedule(void)
{
	ev_timer_start(nfm_osfw_eb_base.loop, &nfm_osfw_eb_base.timer);
	return true;
}

static void nfm_osfw_hook_schedule(osfw_hook_target_t target)
{
    struct nfm_osfw_hook *hook;
    ds_list_iter_t iter;
    ds_list_t *list;

    /*
     * When NFM starts, it clears all previous rules in the system.
     * At that time rules cache is empty (there is no specific rule addition/removal
     * caused by Netfilter or Openflow_Tag callback) so there is no MAC to flush.
     */
    switch (target)
    {
        case OSFW_HOOK_IPTABLES:
            if (!ds_list_is_empty(&nfm_osfw_hook_list)) list = &nfm_osfw_hook_list;
            else
            {
                LOGD("Iptables hook list is empty, no MACs to flush");
                return;
            }
            break;

        case OSFW_HOOK_EBTABLES:
            if (!ds_list_is_empty(&nfm_osfw_eb_hook_list)) list = &nfm_osfw_eb_hook_list;
            else
            {
                LOGD("Ebtables hook list is empty, no MACs to flush");
                return;
            }
            break;

        default:
            LOGE("Unknown target, MACs flush failed");
            return;
    }

    for (hook = ds_list_ifirst(&iter, list); hook != NULL; hook = ds_list_inext(&iter))
    {
        LOGT("MAC flush scheduled for rule: %s", hook->rule);
        target_om_hook(hook->hook, hook->rule);

        ds_list_iremove(&iter);
        FREE(hook->rule);
        FREE(hook);
    }
}

static void nfm_osfw_hook_list_add(target_om_hook_t action, const char *rule, osfw_hook_target_t target)
{
    struct nfm_osfw_hook *hook = NULL;

    hook = CALLOC(1, sizeof(*hook));
    hook->hook = action;
    hook->rule = STRDUP(rule);

    LOGT("Adding rule '%s' to flush list", rule);
    if (target == OSFW_HOOK_IPTABLES) ds_list_insert_head(&nfm_osfw_hook_list, hook);
    else if (target == OSFW_HOOK_EBTABLES) ds_list_insert_head(&nfm_osfw_eb_hook_list, hook);
    else LOGE("Unknown target, adding rule to the list failed");
}

bool nfm_osfw_init(struct ev_loop *loop)
{
	bool errcode = true;

	memset(&nfm_osfw_base, 0, sizeof(nfm_osfw_base));
	nfm_osfw_base.loop = loop;
	ev_timer_init(&nfm_osfw_base.timer, nfm_osfw_on_reschedule, 0, 0);

	errcode = osfw_init(nfm_rule_apply_status_cb, nfm_osfw_hook_schedule);
	if (!errcode) {
		LOGE("Initialize the OpenSync firewall API failed");
		return false;
	}

	return true;
}

bool nfm_osfw_eb_init(struct ev_loop *loop)
{
	bool errcode = true;

	memset(&nfm_osfw_eb_base, 0, sizeof(nfm_osfw_eb_base));
	nfm_osfw_eb_base.loop = loop;
	ev_timer_init(&nfm_osfw_eb_base.timer, nfm_osfw_eb_on_reschedule, 0, 0);

	errcode = osfw_eb_init(nfm_rule_apply_status_cb, nfm_osfw_hook_schedule);
	if (!errcode) {
		LOGE("Initialize the OpenSync ebtables API failed");
		return false;
	}

	return true;
}

bool nfm_osfw_fini(void)
{
	bool errcode = true;

	errcode = osfw_fini();
	if (!errcode) {
		LOGE("Finalize the OpenSync firewall API failed");
		return false;
	}

	ev_timer_stop(nfm_osfw_base.loop, &nfm_osfw_base.timer);
	memset(&nfm_osfw_base, 0, sizeof(nfm_osfw_base));

	return true;
}

bool nfm_osfw_eb_fini(void)
{
	bool errcode = true;

	errcode = osfw_eb_fini();
	if (!errcode) {
		LOGE("Finalize the OpenSync firewall API failed");
		return false;
	}

	ev_timer_stop(nfm_osfw_eb_base.loop, &nfm_osfw_eb_base.timer);
	memset(&nfm_osfw_eb_base, 0, sizeof(nfm_osfw_eb_base));
	return true;
}

bool nfm_osfw_is_inet4(const char *protocol)
{
	if (!strncmp(protocol, NFM_OSFW_PROTOCOL_INET4, sizeof(NFM_OSFW_PROTOCOL_INET4))) {
		return true;
	} else if (!strncmp(protocol, NFM_OSFW_PROTOCOL_BOTH, sizeof(NFM_OSFW_PROTOCOL_BOTH))) {
		return true;
	}
	return false;
}

bool nfm_osfw_is_inet6(const char *protocol)
{
	if (!strncmp(protocol, NFM_OSFW_PROTOCOL_INET6, sizeof(NFM_OSFW_PROTOCOL_INET6))) {
		return true;
	} else if (!strncmp(protocol, NFM_OSFW_PROTOCOL_BOTH, sizeof(NFM_OSFW_PROTOCOL_BOTH))) {
		return true;
	}
	return false;
}

bool nfm_osfw_is_eth(const char *protocol)
{
	if (!strncmp(protocol, NFM_OSFW_PROTOCOL_ETH, sizeof(NFM_OSFW_PROTOCOL_ETH))) {
		return true;
	}

	return false;
}

bool nfm_osfw_add_chain(int family, const char *table, const char *chain)
{
	bool errcode = true;


	if (((family != AF_INET) && (family != AF_INET6) && (family != AF_BRIDGE)) || !table || !table[0] || !chain || !chain[0]) {
		LOGE("Add firewall chain: invalid parameters");
		return false;
	}

	if (kconfig_enabled(CONFIG_TARGET_ENABLE_EBTABLES) && family == AF_BRIDGE) {
		if (!nfm_osfw_eb_is_valid_chain(chain)) {
			LOGD("Add ebtable chain: %s is not a valid chain - ignore it", chain);
			return true;
		}
	} else {
		if (!nfm_osfw_is_valid_chain(chain)) {
			LOGD("Add firewall chain: %s is not a valid chain - ignore it", chain);
			return true;
		}
	}

	if (kconfig_enabled(CONFIG_TARGET_ENABLE_EBTABLES) && family == AF_BRIDGE) {
		errcode = osfw_eb_chain_add(family, nfm_osfw_convert_table(table), chain);
		if (!errcode) {
			LOGE("Add ebtables chain failed");
			return false;
		}
	} else {
		errcode = osfw_chain_add(family, nfm_osfw_convert_table(table), chain);
		if (!errcode) {
			LOGE("Add firewall chain failed");
			return false;
		}
	}

	if (kconfig_enabled(CONFIG_TARGET_ENABLE_EBTABLES) && family == AF_BRIDGE) {
		errcode = nfm_osfw_eb_reschedule();
		if (!errcode) {
			LOGE("Ask for a ebtable reschedule failed");
			return false;
		}
	} else {
		errcode = nfm_osfw_reschedule();
		if (!errcode) {
			LOGE("Ask for a reschedule failed");
			return false;
		}
	}
	return true;
}

bool nfm_osfw_del_chain(int family, const char *table, const char *chain)
{
	bool errcode = true;

	if (((family != AF_INET) && (family != AF_INET6) && (family != AF_BRIDGE)) || !table || !table[0] || !chain || !chain[0]) {
		LOGE("Delete firewall chain: invalid parameters");
		return false;
	} else if (!nfm_osfw_is_valid_chain(chain)) {
		LOGD("Delete firewall chain: %s is not a valid chain - ignore it", chain);
		return true;
	}

	if (kconfig_enabled(CONFIG_TARGET_ENABLE_EBTABLES) && family == AF_BRIDGE) {
		errcode = osfw_eb_chain_del(family, nfm_osfw_convert_table(table), chain);
		if (!errcode) {
			LOGE("Delete firewall chain failed");
			return false;
		}
	} else {
		errcode = osfw_chain_del(family, nfm_osfw_convert_table(table), chain);
		if (!errcode) {
			LOGE("Delete firewall chain failed");
			return false;
		}
	}

	if (kconfig_enabled(CONFIG_TARGET_ENABLE_EBTABLES) && family == AF_BRIDGE) {
		errcode = nfm_osfw_eb_reschedule();
		if (!errcode) {
			LOGE("Ask for a ebtable reschedule failed");
			return false;
		}
	} else {
		errcode = nfm_osfw_reschedule();
		if (!errcode) {
			LOGE("Ask for a reschedule failed");
			return false;
		}
	}

	return true;
}

bool nfm_osfw_add_rule(const struct schema_Netfilter *conf)
{
	bool errcode = true;
	bool change = false;
	size_t len;
	if (!conf) {
		LOGE("Add firewall rule: invalid parameter");
		return false;
	}

	if (nfm_osfw_is_inet4(conf->protocol)) {
		errcode = osfw_rule_add(AF_INET, nfm_osfw_convert_table(conf->table),
				conf->chain, conf->priority, conf->rule, conf->target, conf->name);
		if (!errcode) {
			LOGE("Add IPv4 firewall rule failed");
			return false;
		}
		change = true;
	}

	if (nfm_osfw_is_inet6(conf->protocol)) {
		errcode = osfw_rule_add(AF_INET6, nfm_osfw_convert_table(conf->table),
				conf->chain, conf->priority, conf->rule, conf->target, conf->name);
		if (!errcode) {
			LOGE("Add IPv6 firewall rule failed");
			return false;
		}
		change = true;
	}

	if (kconfig_enabled(CONFIG_TARGET_ENABLE_EBTABLES) &&
        nfm_osfw_is_eth(conf->protocol)) {
		errcode = osfw_eb_rule_add(AF_BRIDGE, nfm_osfw_convert_table(conf->table),
				conf->chain, conf->priority, conf->rule, conf->target, conf->name);
		if (!errcode) {
			LOGE("Add ebtables firewall rule failed");
			return false;
		}
		change = true;
	}

	if (change) {
		len = strnlen(conf->rule, NFM_OSFW_RULE_LEN);
		if (kconfig_enabled(CONFIG_TARGET_ENABLE_EBTABLES) &&
            nfm_osfw_is_eth(conf->protocol)) {
			if (len) {
				nfm_osfw_hook_list_add(TARGET_OM_POST_ADD, conf->rule, OSFW_HOOK_EBTABLES);
			}
			errcode = nfm_osfw_eb_reschedule();
		} else {
			if (len) {
				nfm_osfw_hook_list_add(TARGET_OM_POST_ADD, conf->rule, OSFW_HOOK_IPTABLES);
			}
			errcode = nfm_osfw_reschedule();
		}
		if (!errcode) {
			LOGE("Ask for a reschedule failed");
			return false;
		}
	}
	return true;
}

bool nfm_osfw_del_rule(const struct schema_Netfilter *conf)
{
	bool errcode = true;
	bool change = false;
	size_t len;

	if (!conf) {
		LOGE("Delete firewall rule: invalid parameter");
		return false;
	}

	if (nfm_osfw_is_inet4(conf->protocol)) {
		errcode = osfw_rule_del(AF_INET, nfm_osfw_convert_table(conf->table),
				conf->chain, conf->priority, conf->rule, conf->target);
		if (!errcode) {
			LOGE("Delete IPv4 firewall rule failed");
			return false;
		}
		change = true;
	}

	if (nfm_osfw_is_inet6(conf->protocol)) {
		errcode = osfw_rule_del(AF_INET6, nfm_osfw_convert_table(conf->table),
				conf->chain, conf->priority, conf->rule, conf->target);
		if (!errcode) {
			LOGE("Delete IPv6 firewall rule failed");
			return false;
		}
		change = true;
	}

    if (kconfig_enabled(CONFIG_TARGET_ENABLE_EBTABLES) &&
        nfm_osfw_is_eth(conf->protocol)) {
		errcode = osfw_eb_rule_del(AF_BRIDGE, nfm_osfw_convert_table(conf->table),
				conf->chain, conf->priority, conf->rule, conf->target);
		if (!errcode) {
			LOGE("Delete ebtables rule failed");
			return false;
		}
		change = true;
	}

	if (change) {
		len = strnlen(conf->rule, NFM_OSFW_RULE_LEN);
    if (kconfig_enabled(CONFIG_TARGET_ENABLE_EBTABLES) &&
        nfm_osfw_is_eth(conf->protocol)) {
			if (len) {
				nfm_osfw_hook_list_add(TARGET_OM_POST_DEL, conf->rule, OSFW_HOOK_EBTABLES);
			}
			errcode = nfm_osfw_eb_reschedule();
		} else {
			if (len) {
				nfm_osfw_hook_list_add(TARGET_OM_POST_DEL, conf->rule, OSFW_HOOK_IPTABLES);
			}
			errcode = nfm_osfw_reschedule();
		}
		if (!errcode) {
			LOGE("Ask for a reschedule failed");
			return false;
		}
	}
	return true;
}

