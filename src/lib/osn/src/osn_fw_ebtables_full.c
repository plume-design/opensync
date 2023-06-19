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
#include "osn_fw_pri.h"
#include "os.h"
#include "const.h"
#include "util.h"
#include "memutil.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>


#include "kconfig.h"

struct osfw_ebinet
{
    int family;
    bool ismodified;
    struct
    {
        struct osfw_nftable filter;
        struct osfw_nftable nat;
        struct osfw_nftable broute;
    } tables;
};

struct osfw_ebbase
{
    struct osfw_ebinet inet;
    osfw_fn_t *osfw_eb_fn;
};

static struct osfw_ebbase osfw_ebbase;

struct osfw_ebbase *
osfw_eb_get_base(void)
{
	return &osfw_ebbase;
}

static char *osfw_eb_target_builtin[] =
{
	"ACCEPT",
	"DROP",
	"CONTINUE",
	"RETURN",
	"arpreply",
	"dnat",
	"mark",
	"redirect",
	"snat",
};

static const char *osfw_eb_convert_family(int family)
{
	const char *str = OSFW_STR_UNKNOWN;

	if (family == AF_BRIDGE) {
		str = OSFW_STR_FAMILY_ETH;
	} else {
		LOGN("Invalid family: %d", family);
	}

	return str;
}

static const char *osfw_eb_convert_cmd(int family)
{
	const char *cmd = OSFW_STR_UNKNOWN;

	if (family == AF_BRIDGE) {
		cmd = OSFW_STR_CMD_EBTABLES_RESTORE;
	} else {
		LOGN("family: %d, not supported", family);
	}

	return cmd;
}

static const char *osfw_eb_convert_table(enum osfw_table table)
{
	const char *str = OSFW_STR_UNKNOWN;

	switch (table) {
	case OSFW_TABLE_FILTER:
		str = OSFW_STR_TABLE_FILTER;
		break;

	case OSFW_TABLE_NAT:
		str = OSFW_STR_TABLE_NAT;
		break;

	case OSFW_TABLE_BROUTE:
		str = OSFW_STR_TABLE_BROUTE;
		break;

	default:
		LOGE("Convert ebtable: Invalid table: %d", table);
		break;
	}

	return str;
}

static bool osfw_eb_is_builtin_chain(enum osfw_table table, const char *chain)
{
	int ci;

	for (ci = 0; ci < ARRAY_LEN(osfw_eb_target_builtin); ci++) {
		if (strcmp(chain, osfw_eb_target_builtin[ci]) == 0) {
			return true;
		}
	}

	switch (table) {
	case OSFW_TABLE_FILTER:
		if (!strncmp(chain, OSFW_STR_CHAIN_INPUT, sizeof(OSFW_STR_CHAIN_INPUT))) {
			return true;
		} else if (!strncmp(chain, OSFW_STR_CHAIN_FORWARD, sizeof(OSFW_STR_CHAIN_FORWARD))) {
			return true;
		} else if (!strncmp(chain, OSFW_STR_CHAIN_OUTPUT, sizeof(OSFW_STR_CHAIN_OUTPUT))) {
			return true;
		}
		break;

	case OSFW_TABLE_NAT:
		if (!strncmp(chain, OSFW_STR_CHAIN_PREROUTING, sizeof(OSFW_STR_CHAIN_PREROUTING))) {
			return true;
		} else if (!strncmp(chain, OSFW_STR_CHAIN_OUTPUT, sizeof(OSFW_STR_CHAIN_OUTPUT))) {
			return true;
		} else if (!strncmp(chain, OSFW_STR_CHAIN_POSTROUTING, sizeof(OSFW_STR_CHAIN_POSTROUTING))) {
			return true;
		}
		break;

	case OSFW_TABLE_BROUTE:
		if (!strncmp(chain, OSFW_STR_CHAIN_BROUTING, sizeof(OSFW_STR_CHAIN_BROUTING))) {
			return true;
		}
		break;

	default:
		LOGE("Is built-in ebtable chain: Invalid table: %d", table);
		break;
	}

	return false;
}

static bool osfw_eb_is_valid_chain(const char *chain)
{
	if (strchr(chain, ' ')) {
		return false;
	}
	return true;
}

static bool osfw_eb_nfchain_set(struct osfw_nfchain *self, struct ds_dlist *parent, const char *chain)
{
	memset(self, 0, sizeof(*self));
	self->parent = parent;
	ds_dlist_insert_tail(self->parent, self);
	STRSCPY(self->chain, chain);
	return true;
}

static bool osfw_eb_nfchain_unset(struct osfw_nfchain *self)
{
	ds_dlist_remove(self->parent, self);
	memset(self, 0, sizeof(*self));
	return true;
}

static bool oseb_nfchain_del(struct osfw_nfchain *self)
{
	bool errcode = true;
	TRACE();

	errcode = osfw_eb_nfchain_unset(self);
	if (!errcode) {
		return false;
	}

	FREE(self);
	return true;
}

static struct osfw_nfchain *osfw_eb_nfchain_add(struct ds_dlist *parent, const char *chain)
{
	struct osfw_nfchain *self = NULL;
	bool errcode = true;

	TRACE();

	if (!parent || !chain) {
		LOGE("Add ebtable chain: invalid parameter");
		return NULL;
	}

	self = MALLOC(sizeof(*self));

	errcode = osfw_eb_nfchain_set(self, parent, chain);
	if (!errcode) {
		oseb_nfchain_del(self);
		return NULL;
	}
	return self;
}

static bool osfw_eb_nfchain_match(const struct osfw_nfchain *self, const char *chain)
{
	if (!self || !chain) {
		return false;
	} else if (strncmp(self->chain, chain, sizeof(self->chain))) {
		return false;
	}
	return true;
}

static void osfw_eb_nfchain_print(const struct osfw_nfchain *self, FILE *stream)
{
	if (!self || !stream) {
		return;
	}
	fprintf(stream, "-N %s\n", self->chain);
}

static bool osfw_eb_nfrule_set(struct osfw_nfrule *self, struct ds_dlist *parent, const char *chain,
		int prio, const char *match, const char *target)
{
	struct osfw_nfrule *nfrule = NULL;

	memset(self, 0, sizeof(*self));
	self->prio = prio;
	self->parent = parent;

	ds_dlist_foreach(self->parent, nfrule) {
		if (self->prio < nfrule->prio) {
			break;
		}
	}
	if (nfrule) {
		ds_dlist_insert_before(self->parent, nfrule, self);
	} else {
		ds_dlist_insert_tail(self->parent, self);
	}

	STRSCPY(self->chain, chain);
	STRSCPY(self->match, match);
	STRSCPY(self->target, target);
	return true;
}

static bool osfw_eb_nfrule_unset(struct osfw_nfrule *self)
{
	ds_dlist_remove(self->parent, self);
	memset(self, 0, sizeof(*self));
	return true;
}

static bool osfw_eb_nfrule_del(struct osfw_nfrule *self)
{
	bool errcode = true;

	errcode = osfw_eb_nfrule_unset(self);
	if (!errcode) {
		return false;
	}

	FREE(self);
	return true;
}
static struct osfw_nfrule *osfw_eb_nfrule_add(struct ds_dlist *parent, const char *chain,
		int prio, const char *match, const char *target)
{
	struct osfw_nfrule *self = NULL;
	bool errcode = true;

    TRACE();

	if (!parent || !chain || !match || !target) {
		LOGE("Add ebtable rule: invalid parameter");
		return NULL;
	}

	self = MALLOC(sizeof(*self));

	errcode = osfw_eb_nfrule_set(self, parent, chain, prio, match, target);
	if (!errcode) {
		osfw_eb_nfrule_del(self);
		return NULL;
	}
	return self;
}

static bool osfw_eb_nfrule_match(const struct osfw_nfrule *self, const char *chain, int prio,
		const char *match, const char *target)
{
	if (!self || !chain || !match || !target) {
		return false;
	} else if (self->prio != prio) {
		return false;
	} else if (strncmp(self->chain, chain, sizeof(self->chain))) {
		return false;
	} else if (strncmp(self->match, match, sizeof(self->match))) {
		return false;
	} else if (strncmp(self->target, target, sizeof(self->target))) {
		return false;
	}
	return true;
}

static void osfw_eb_nfrule_print(const struct osfw_nfrule *self, FILE *stream)
{
	if (!self || !stream) {
		return;
	}
	fprintf(stream, "-A %s -j %s %s \n", self->chain, self->target, self->match);
}

#define KCONFIG_DEFAULT_POLICY2(kconfig) \
    kconfig_enabled(kconfig##_ACCEPT) ? OSFW_STR_TARGET_ACCEPT : \
            (kconfig_enabled(kconfig##_REJECT) ? OSFW_STR_TARGET_REJECT : \
                OSFW_STR_TARGET_DROP)

#define KCONFIG_DEFAULT_POLICY(table, chain) \
    KCONFIG_DEFAULT_POLICY2(CONFIG_OSN_FW_EBTABLES_POLICY_##table##_##chain)

static void osfw_eb_nftable_print_header(const struct osfw_nftable *self, FILE *stream)
{
	if (!self || !stream) {
		return;
	}
	fprintf(stream, "*%s\n", osfw_eb_convert_table(self->table));

	switch (self->table) {
	case OSFW_TABLE_FILTER:
		fprintf(stream, ":%s %s\n", OSFW_STR_CHAIN_INPUT, KCONFIG_DEFAULT_POLICY(FILTER, INPUT));
		fprintf(stream, ":%s %s\n", OSFW_STR_CHAIN_FORWARD, KCONFIG_DEFAULT_POLICY(FILTER, FORWARD));
		fprintf(stream, ":%s %s\n", OSFW_STR_CHAIN_OUTPUT, KCONFIG_DEFAULT_POLICY(FILTER, OUTPUT));
		break;

	case OSFW_TABLE_NAT:
		fprintf(stream, ":%s %s\n", OSFW_STR_CHAIN_PREROUTING, KCONFIG_DEFAULT_POLICY(NAT, PREROUTING));
		fprintf(stream, ":%s %s\n", OSFW_STR_CHAIN_OUTPUT, KCONFIG_DEFAULT_POLICY(NAT, OUTPUT));
		fprintf(stream, ":%s %s\n", OSFW_STR_CHAIN_POSTROUTING, KCONFIG_DEFAULT_POLICY(NAT, POSTROUTING));
		break;

	case OSFW_TABLE_BROUTE:
		fprintf(stream, ":%s %s\n", OSFW_STR_CHAIN_BROUTING, KCONFIG_DEFAULT_POLICY(BROUTE, BROUTING));
		break;

	default:
		LOGE("Invalid table: %d", self->table);
		break;
	}
}

static void osfw_eb_nftable_print(struct osfw_nftable *self, bool nfrules, struct osfw_nfrule *nfrule, FILE *stream)
{
	struct osfw_nfchain *nfchain = NULL;

	if (!self) {
		return;
	} else if (self->isinitialized && !self->issupported) {
		return;
	}

	osfw_eb_nftable_print_header(self, stream);
	ds_dlist_foreach(&self->chains, nfchain) {
		osfw_eb_nfchain_print(nfchain, stream);
	}

	if (nfrules) {
		nfrule = NULL;
		ds_dlist_foreach(&self->rules, nfrule) {
			osfw_eb_nfrule_print(nfrule, stream);
		}
	} else if (nfrule) {
		osfw_eb_nfrule_print(nfrule, stream);
	}
}

static bool osfw_eb_nftable_check(struct osfw_nftable *self, struct osfw_nfrule *nfrule)
{
    TRACE();
	bool errcode = true;
	int err = 0;
	char cmd[OSFW_SIZE_CMD*2];
	char path[OSFW_SIZE_CMD];
	FILE *stream = NULL;

	TRACE();
	snprintf(path,
			sizeof(path) - 1,
			"/tmp/osfw-%s-%s.%d",
			osfw_eb_convert_family(self->family),
			osfw_eb_convert_table(self->table),
			(int) getpid());

    path[sizeof(path) - 1] = '\0';
	stream = fopen(path, "w+");
	if (!stream) {
		LOGE("Open %s failed: %d - %s", path, errno, strerror(errno));
		return false;
	}
	osfw_eb_nftable_print(self, false, nfrule, stream);
	fclose(stream);
	TRACE();

	snprintf(cmd, sizeof(cmd) - 1, "cat %s | %s", path, osfw_eb_convert_cmd(self->family));
	cmd[sizeof(cmd) - 1] = '\0';

	err = cmd_log(cmd);
	if (err) {
		if (self->isinitialized) {
			LOGN("Check ebtables %s %s configuration failed", osfw_eb_convert_family(self->family),
					osfw_eb_convert_table(self->table));
		}
		errcode = false;
	}

	unlink(path);
	return errcode;
}

static bool osfw_eb_nftable_set(struct osfw_nftable *self, int family, enum osfw_table table)
{
	TRACE();
	memset(self, 0, sizeof(*self));
	self->isinitialized = false;
	self->family = family;
	self->table = table;
	ds_dlist_init(&self->chains, struct osfw_nfchain, elt);
	ds_dlist_init(&self->rules, struct osfw_nfrule, elt);
	self->issupported = osfw_eb_nftable_check(self, NULL);
	TRACE();
	self->isinitialized = true;
	return true;
}

static bool osfw_eb_nftable_unset(struct osfw_nftable *self)
{
	bool errcode = true;
	struct osfw_nfchain *nfchain = NULL;
	struct osfw_nfchain *nfchain_tmp = NULL;
	struct osfw_nfrule *nfrule = NULL;
	struct osfw_nfrule *nfrule_tmp = NULL;

	nfrule = ds_dlist_head(&self->rules);
	while (nfrule) {
		nfrule_tmp = ds_dlist_next(&self->rules, nfrule);
		errcode = osfw_eb_nfrule_del(nfrule);
		if (!errcode) {
			return false;
		}
		nfrule = nfrule_tmp;
	}

	nfchain = ds_dlist_head(&self->chains);
	while (nfchain) {
		nfchain_tmp = ds_dlist_next(&self->chains, nfchain);
		errcode = oseb_nfchain_del(nfchain);
		if (!errcode) {
			return false;
		}
		nfchain = nfchain_tmp;
	}
	return true;
}

static struct osfw_nfchain *osfw_eb_nftable_get_nfchain(struct osfw_nftable *self, const char *chain)
{
	struct osfw_nfchain *nfchain = NULL;

	ds_dlist_foreach(&self->chains, nfchain) {
		if (osfw_eb_nfchain_match(nfchain, chain)) {
			return nfchain;
		}
	}
	return NULL;
}

static struct osfw_nfrule *osfw_eb_nftable_get_nfrule(struct osfw_nftable *self, const char *chain,
		int prio, const char *match, const char *target)
{
	struct osfw_nfrule *nfrule = NULL;

	ds_dlist_foreach(&self->rules, nfrule) {
		if (osfw_eb_nfrule_match(nfrule, chain, prio, match, target)) {
			return nfrule;
		}
	}
	return NULL;
}

static bool osfw_eb_nftable_add_nfchain(struct osfw_nftable *self, const char *chain)
{
    TRACE();
	bool errcode = true;
	struct osfw_nfchain *nfchain = NULL;

	LOGT("%s(): table: %d, family: %u", __func__, self->table, self->family);
	if (!self->issupported) {
		LOGN("OSFW ebtable add chain: table %s %s is not supported",
				osfw_eb_convert_family(self->family), osfw_eb_convert_table(self->table));
		return false;
	}

	nfchain = osfw_eb_nfchain_add(&self->chains, chain);
	if (!nfchain) {
		return false;
	}

	errcode = osfw_eb_nftable_check(self, NULL);
	if (!errcode) {
		oseb_nfchain_del(nfchain);
		return false;
	}
    return true;
}

static bool osfw_eb_nftable_del_nfchain(struct osfw_nftable *self, const char *chain)
{
	bool errcode;
	struct osfw_nfchain *nfchain;

	TRACE();
	nfchain = osfw_eb_nftable_get_nfchain(self, chain);
	if (!nfchain) {
		LOGE("Chain not found");
		return false;
	}

	errcode = oseb_nfchain_del(nfchain);
	if (!errcode) {
		return false;
	}
	return true;
}

static bool osfw_eb_nftable_add_ebrule(struct osfw_nftable *self, const char *chain, int prio,
		const char *match, const char *target)
{
	bool errcode = true;
	struct osfw_nfrule *nfrule = NULL;

	TRACE();
	if (!self->issupported) {
		LOGE("OSFW eb table add rule: table %s %s is not supported",
				osfw_eb_convert_family(self->family), osfw_eb_convert_table(self->table));
		return false;
	}

	nfrule = osfw_eb_nfrule_add(&self->rules, chain, prio, match, target);
	if (!nfrule) {
		return false;
	}

	errcode = osfw_eb_nftable_check(self, nfrule);
	if (!errcode) {
		osfw_eb_nfrule_del(nfrule);
		return false;
	}
	return true;
}

static bool osfw_eb_nftable_del_nfrule(struct osfw_nftable *self, const char *chain, int prio,
		const char *match, const char *target)
{
	bool errcode = true;
	struct osfw_nfrule *nfrule = NULL;

	TRACE();
	nfrule = osfw_eb_nftable_get_nfrule(self, chain, prio, match, target);
	if (!nfrule) {
		LOGD("Rule not found");
		return true;
	}

	errcode = osfw_eb_nfrule_del(nfrule);
	if (!errcode) {
		return false;
	}
	return true;
}

static bool osfw_eb_nfinet_set(struct osfw_ebinet *self, int family)
{
	bool errcode = true;

	memset(self, 0, sizeof(*self));
	self->family = family;
	self->ismodified = true;

	TRACE();

	errcode = osfw_eb_nftable_set(&self->tables.filter, self->family, OSFW_TABLE_FILTER);
	if (!errcode) {
		return false;
	}

	errcode = osfw_eb_nftable_set(&self->tables.nat, self->family, OSFW_TABLE_NAT);
	if (!errcode) {
		return false;
	}

	errcode = osfw_eb_nftable_set(&self->tables.broute, self->family, OSFW_TABLE_BROUTE);
	if (!errcode) {
		return false;
	}

	return true;
}

static bool osfw_eb_nfinet_unset(struct osfw_ebinet *self)
{
	bool errcode = true;

	self->ismodified = true;
	errcode = osfw_eb_nftable_unset(&self->tables.filter);
	if (!errcode) {
		return false;
	}

	errcode = osfw_eb_nftable_unset(&self->tables.nat);
	if (!errcode) {
		return false;
	}

	errcode = osfw_eb_nftable_unset(&self->tables.broute);
	if (!errcode) {
		return false;
	}

	return true;
}
static struct osfw_nftable *osfw_eb_inet_get_nftable(struct osfw_ebinet *self, enum osfw_table table)
{
	struct osfw_nftable *nftable = NULL;

	switch (table) {
	case OSFW_TABLE_FILTER:
		nftable = &self->tables.filter;
		break;

	case OSFW_TABLE_NAT:
		nftable = &self->tables.nat;
		break;

	case OSFW_TABLE_BROUTE:
		nftable = &self->tables.broute;
		break;

	default:
		LOGE("OSFW ebtable get table: invalid table %d", table);
		break;
	}
	return nftable;
}

static bool osfw_eb_nfinet_add_nfchain(struct osfw_ebinet *self, enum osfw_table table, const char *chain)
{
	bool errcode = true;
	struct osfw_nftable *nftable = NULL;

	TRACE();

	nftable = osfw_eb_inet_get_nftable(self, table);
	if (!nftable) {
		LOGE("Add ebtable chain: %s %s table not found", osfw_eb_convert_family(self->family),
				osfw_eb_convert_table(table));
		return false;
	}

	errcode = osfw_eb_nftable_add_nfchain(nftable, chain);
	if (!errcode) {
		return false;
	}

	self->ismodified = true;
	return true;
}

static bool osfw_eb_nfinet_del_nfchain(struct osfw_ebinet *self, enum osfw_table table, const char *chain)
{
	bool errcode = true;
	struct osfw_nftable *nftable = NULL;

	nftable = osfw_eb_inet_get_nftable(self, table);
	if (!nftable) {
		LOGE("Delete ebtable chain: %s %s table not found", osfw_eb_convert_family(self->family),
				osfw_eb_convert_table(table));
		return false;
	}

	errcode = osfw_eb_nftable_del_nfchain(nftable, chain);
	if (!errcode) {
		return false;
	}

	self->ismodified = true;
	return true;
}

static bool osfw_eb_inet_add_nfrule(struct osfw_ebinet *self, enum osfw_table table, const char *chain,
		int prio, const char *match, const char *target)
{
	TRACE();
	bool errcode = true;
	struct osfw_nftable *ebtable = NULL;

	ebtable = osfw_eb_inet_get_nftable(self, table);
	if (!ebtable) {
		LOGE("Add OSFW rule: %s %s table not found", osfw_eb_convert_family(self->family),
				osfw_eb_convert_table(table));
		return false;
	}

	errcode = osfw_eb_nftable_add_ebrule(ebtable, chain, prio, match, target);
	if (!errcode) {
		return false;
	}

	self->ismodified = true;
	return true;
}

static bool osfw_eb_nfinet_del_nfrule(struct osfw_ebinet *self, enum osfw_table table, const char *chain,
		int prio, const char *match, const char *target)
{
	bool errcode = true;
	struct osfw_nftable *nftable = NULL;

	nftable = osfw_eb_inet_get_nftable(self, table);
	if (!nftable) {
		LOGE("Delete ebtable rule: %s %s table not found", osfw_eb_convert_family(self->family),
				osfw_eb_convert_table(table));
		return false;
	}

	errcode = osfw_eb_nftable_del_nfrule(nftable, chain, prio, match, target);
	if (!errcode) {
		TRACE();
		return false;
	}

	self->ismodified = true;
	return true;
}

static bool osfw_eb_inet_apply(struct osfw_ebinet *self)
{
	bool errcode = true;
	int err = 0;
	char path[OSFW_SIZE_CMD];
	char cmd[OSFW_SIZE_CMD*2 + 128];
	FILE *stream = NULL;

	TRACE();
	if (!self->ismodified) {
		return true;
	}

	snprintf(path, sizeof(path) - 1, "/tmp/osfw-%s.%d", osfw_eb_convert_family(self->family), (int) getpid());
	path[sizeof(path) - 1] = '\0';
	stream = fopen(path, "w+");
	if (!stream) {
		LOGE("Open %s failed: %d - %s", path, errno, strerror(errno));
		return false;
	}
	osfw_eb_nftable_print(&self->tables.filter, true, NULL, stream);
	osfw_eb_nftable_print(&self->tables.nat, true, NULL, stream);
	osfw_eb_nftable_print(&self->tables.broute, true, NULL, stream);
	fclose(stream);

	snprintf(cmd, sizeof(cmd) - 1, "cat %s | %s", path, osfw_eb_convert_cmd(self->family));
	cmd[sizeof(cmd) - 1] = '\0';

	err = cmd_log(cmd);
	if (err) {
		LOGE("Apply OSFW configuration failed");
		errcode = false;
		snprintf(cmd, sizeof(cmd) - 1, "cp %s %s.error", path, path);
		cmd[sizeof(cmd) - 1] = '\0';
		cmd_log(cmd);
	}

	unlink(path);
	self->ismodified = false;
	return errcode;
}


static bool osfw_eb_base_set(struct osfw_ebbase *self, osfw_fn_t *fn)
{
	bool errcode = true;

	memset(self, 0, sizeof(*self));
	osfw_ebbase.osfw_eb_fn = fn;
	errcode = osfw_eb_nfinet_set(&self->inet, AF_BRIDGE);
	if (!errcode) {
		LOGE("Set OSEB base: set OSEB inet failed");
		return false;
	}
	return true;
}

static bool osfw_eb_base_unset(struct osfw_ebbase *self)
{
	bool errcode = true;

	TRACE();
	errcode = osfw_eb_nfinet_unset(&self->inet);
	if (!errcode) {
		LOGE("Unset OSFW base: unset OSFW inet failed");
		return false;
	}

	return true;
}

static bool osfw_eb_base_apply(struct osfw_ebbase *self)
{
	bool errcode = true;

	errcode = osfw_eb_inet_apply(&self->inet);
	if (!errcode) {
		LOGE("Apply OSFW ebtables base: apply OSFW ebtables failed");
		return false;
	}
	return true;
}

static struct osfw_ebinet *osfw_eb_base_get_inet(struct osfw_ebbase *self, int family)
{
	struct osfw_ebinet *ebinet = NULL;

	if (family == AF_BRIDGE) return &self->inet;

	LOGE("OSFW ebtable base get table: invalid family %d", family);

	return ebinet;
}

bool osfw_eb_init(osfw_fn_t *osfw_eb_status_fn)
{
	bool errcode = true;

	errcode = osfw_eb_base_set(&osfw_ebbase, osfw_eb_status_fn);
	if (!errcode) {
		LOGE("Initialize ebtable: set base failed");
		return false;
	}

	errcode = osfw_eb_apply();
	if (!errcode) {
		LOGE("Initialize ebtable: apply failed");
		return false;
	}
	return true;
}

bool osfw_eb_fini(void)
{
	struct osfw_ebbase *osfw_ebbase;
	bool errcode = true;

	osfw_ebbase = osfw_eb_get_base();

	errcode = osfw_eb_base_unset(osfw_ebbase);
	if (!errcode) {
		LOGE("Finalize ebtable: unset base failed");
		return false;
	}

	errcode = osfw_eb_apply();
	if (!errcode) {
		LOGE("Finalize ebtable: apply failed");
		return false;
	}

	memset(osfw_ebbase, 0, sizeof(*osfw_ebbase));
	return true;
}

bool osfw_eb_chain_add(int family, enum osfw_table table, const char *chain)
{
	struct osfw_ebinet *ebinet;
	bool errcode;

	TRACE();
	if (!chain || !chain[0]) {
		LOGE("Add ebtable chain: invalid parameters");
		return false;
	} else if (!osfw_eb_is_valid_chain(chain)) {
		LOGE("Add ebtable chain: %s is not a valid chain", chain);
		return false;
	} else if (osfw_eb_is_builtin_chain(table, chain)) {
		LOGD("Add ebtable chain: %s %s is built-in chain", osfw_eb_convert_table(table), chain);
		return true;
	}

	ebinet = osfw_eb_base_get_inet(&osfw_ebbase, family);
	if (!ebinet) {
		LOGE("Add ebtable chain: %s not found", osfw_eb_convert_family(family));
		return false;
	}

	errcode = osfw_eb_nfinet_add_nfchain(ebinet, table, chain);
	if (!errcode) {
		LOGE("Add OSFW chain: add chain failed");
		return false;
	}

	return true;
}

bool osfw_eb_chain_del(int family, enum osfw_table table, const char *chain)
{
	struct osfw_ebinet *ebinet;
	bool errcode;

	if (!chain || !chain[0]) {
		LOGE("Delete ebtable chain: invalid parameters");
		return false;
	} else if (!osfw_eb_is_valid_chain(chain)) {
		LOGE("Delete ebtable chain: %s is not a valid chain", chain);
		return false;
	} else if (osfw_eb_is_builtin_chain(table, chain)) {
		LOGD("Delete ebtable chain: %s %s is built-in chain", osfw_eb_convert_table(table), chain);
		return true;
	}

	ebinet = osfw_eb_base_get_inet(&osfw_ebbase, family);
	if (!ebinet) {
		LOGE("Delete ebtable chain: %s not found", osfw_eb_convert_family(family));
		return false;
	}

	errcode = osfw_eb_nfinet_del_nfchain(ebinet, table, chain);
	if (!errcode) {
		LOGE("Delete ebtable chain: delete chain failed");
		return false;
	}
	return true;
}

bool osfw_eb_rule_del(int family, enum osfw_table table, const char *chain,
		int prio, const char *match, const char *target)
{
	struct osfw_ebinet *nfinet = NULL;
	bool errcode;

	TRACE();

	if (!chain || !chain[0] || !match || !target || !target[0]) {
		LOGE("Delete ebtable rule: invalid parameters");
		return false;
	}

	nfinet = osfw_eb_base_get_inet(&osfw_ebbase, family);
	if (!nfinet) {
		LOGE("Delete ebtable rule: %s not found", osfw_eb_convert_family(family));
		return false;
	}

	errcode = osfw_eb_nfinet_del_nfrule(nfinet, table, chain, prio, match, target);
	if (!errcode) {
		LOGE("Delete ebtable rule: delete rule failed");
		return false;
	}

	return true;
}

bool osfw_eb_rule_add(int family, enum osfw_table table, const char *chain,
		int prio, const char *match, const char *target, const char *name)
{
	struct osfw_ebinet *ebinet;
	bool errcode;

	TRACE();

	if (!chain || !chain[0] || !match || !target || !target[0]) {
		LOGE("Add ebtables rule: invalid parameters");
		return false;
	}

	ebinet = osfw_eb_base_get_inet(&osfw_ebbase, family);
	if (!ebinet) {
		LOGE("Add ebtable rule: %s not found", osfw_eb_convert_family(family));
		return false;
	}

	errcode = osfw_eb_inet_add_nfrule(ebinet, table, chain, prio, match, target);
	if (!errcode) {
		LOGE("Add OSFW rule: add rule failed");
		return false;
	}

	return true;
}


bool osfw_eb_apply(void)
{
	bool errcode = true;

	TRACE();

	errcode = osfw_eb_base_apply(&osfw_ebbase);
	if (!errcode) {
		LOGE("Apply ebtable configuration failed");
		return false;
	}
	return true;
}
