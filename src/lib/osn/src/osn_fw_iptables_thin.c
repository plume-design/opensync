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

#include <regex.h>

#include "const.h"
#include "execsh.h"
#include "kconfig.h"
#include "log.h"
#include "osn_fw_pri.h"
#include "util.h"
#include "ds_tree.h"

#define MODULE_ID LOG_MODULE_ID_TARGET

#define OSFW_IP4TABLES_CMD  "iptables"
#define OSFW_IP6TABLES_CMD  "ip6tables"

#define OSFW_IPTABLES_CMD(family) \
    (((family) == AF_INET6) ? OSFW_IP6TABLES_CMD : OSFW_IP4TABLES_CMD)

#define OSFW_FAMILY_STR(family) \
    (((family) == AF_INET6) ? "ipv6" : "ipv4")

struct osfw_table_def
{
    char               *name;           /**< Table name */
    char              **chains;         /**< Built-in table chains */
};

struct osfw_rule
{
    bool                fr_enabled;     /**< True if rule is enabled, false if pending deletion */
    int                 fr_family;      /**< Rule family */
    enum osfw_table     fr_table;       /**< Rule table name */
    char               *fr_chain;       /**< Rule chain name */
    int                 fr_priority;    /**< Rule priority */
    char               *fr_rule;        /**< Rule */
    char               *fr_target;      /**< Target */
    ds_tree_node_t      fr_tnode;
};

static bool osfw_iptables_chain_add(int family, enum osfw_table table, const char *chain);
static bool osfw_iptables_chain_del(int family, enum osfw_table table, const char *chain);

bool osfw_iptables_rule_add(
        int family,
        enum osfw_table table,
        const char *chain,
        int priority,
        const char *match,
        const char *target);

static struct osfw_table_def *osfw_table_get(enum osfw_table table);
static const char *osfw_table_str(enum osfw_table table);
static bool osfw_target_is_builtin(const char *target);
static ds_key_cmp_t osfw_rule_cmp;

static ds_tree_t osfw_rule_list = DS_TREE_INIT(osfw_rule_cmp, struct osfw_rule, fr_tnode);

static char *osfw_target_builtin[] =
{
    "ACCEPT",
    "CLASSIFY",
    "DNAT",
    "DROP",
    "DSCP",
    "ECN",
    "LOG",
    "MARK",
    "MASQUERADE",
    "MIRROR",
    "NETMAP",
    "NFLOG",
    "NFQUEUE",
    "QUEUE",
    "REDIRECT",
    "REJECT",
    "RETURN",
    "SAME",
    "SNAT",
    "TCPMSS",
    "TOS",
    "TTL",
    "ULOG"
};

/*
 * Built-in tables and chain list
 */
struct osfw_table_def osfw_table_list[] =
{
    [OSFW_TABLE_FILTER] =
    {
        .name = "filter",
        .chains = C_VPACK("INPUT", "OUTPUT", "FORWARD")
    },
    [OSFW_TABLE_NAT] =
    {
        .name = "nat",
        .chains = C_VPACK("PREROUTING", "OUTPUT", "POSTROUTING")
    },
    [OSFW_TABLE_MANGLE] =
    {
        .name = "mangle",
        .chains = C_VPACK("PREROUTING", "INPUT", "OUTPUT", "FORWARD", "POSTROUTING")
    },
    [OSFW_TABLE_RAW] =
    {
        .name = "raw",
        .chains = C_VPACK("PREROUTING", "OUTPUT")
    },
    [OSFW_TABLE_SECURITY] =
    {
        .name = "security",
        .chains = C_VPACK("INPUT", "OUTPUT", "FORWARD"),
    }
};

/* Built-in script for adding a chain */
static const char osfw_iptables_chain_add_cmd[] = _S(
        cmd="$1";
        table="$2";
        chain="$3";
        result=$("$cmd" -t "$table" -N "$chain" 2>&1) || [ "$result" == "iptables: Chain already exists." ]);

/* Built-in script for removing a chain */
static const char osfw_iptables_chain_del_cmd[] = _S(
        cmd="$1";
        table="$2";
        chain="$3";
        "$cmd" -t "$table" -X "$chain");

/* Built-in script for flushing a chain */
static const char osfw_iptables_chain_flush_cmd[] = _S(
        cmd="$1";
        table="$2";
        chain="$3";
        "$cmd" -t "$table" -F "$chain");

/* Built-in script for adding a rule */
static const char osfw_iptables_rule_add_cmd[] = _S(
        cmd="$1";
        table="$2";
        chain="$3";
        match="$4";
        target="$5";
        "$cmd" -t "$table" -A "$chain" -j "$target" $match);


/*
 * ===========================================================================
 *  Public API implementation
 * ===========================================================================
 */
bool osfw_init(void)
{
    return true;
}

bool osfw_fini(void)
{
    return true;
}

/*
 * This is a no-op as chain adding is handled in osfw_apply(). Just do basic
 * error checking instead.
 */
bool osfw_chain_add(
        int family,
        enum osfw_table table,
        const char *chain)
{
    (void)family;
    (void)chain;

    struct osfw_table_def *tbl = osfw_table_get(table);
    if (tbl == NULL)
    {
        LOG(ERR, "osfw: Unknown table %d during chain add.", table);
        return false;
    }

    return true;
}

bool osfw_chain_del(
        int family,
        enum osfw_table table,
        const char *chain)
{
    struct osfw_rule *prule;

    /* Check if all rules in the chain were deleted */
    ds_tree_foreach(&osfw_rule_list, prule)
    {
        if ((prule->fr_table == table) &&
                (strcmp(prule->fr_chain, chain) == 0))
        {
            LOG(ERR, "osfw: %s.%s.%s: Unable to delete chain as it still contains rules.",
                    OSFW_FAMILY_STR(family), osfw_table_str(table), chain);
            return false;
        }
    }

    return osfw_iptables_chain_del(family, table, chain);

}

bool osfw_rule_add(
        int family,
        enum osfw_table table,
        const char *chain,
        int priority,
        const char *match,
        const char *target)
{
    struct osfw_rule *prule;

    LOG(INFO, "osfw: %s.%s.%s: Adding rule (priority %d): %s -> %s",
                OSFW_IPTABLES_CMD(family),
                osfw_table_str(table),
                chain,
                priority,
                match,
                target);
    /*
     * The main reason we're building a list of rules instead of inserting them
     * directly is to sort out the priority. iptables doesn't have a notion of
     * priority. Instead it only understands rule numbers, which are basically
     * just rule positions. Rule numbers cannot have holes and if a rule is
     * inserted in the middle of the list, it pushes down the list all rules
     * with a lower ranking. This makes it very difficult to translate fixed
     * priorities to rule numbers (positions).
     */
    prule = calloc(1, sizeof(struct osfw_rule));
    prule->fr_enabled = true;
    prule->fr_family = family;
    prule->fr_table = table;
    prule->fr_chain = strdup(chain);
    prule->fr_priority = priority;
    prule->fr_rule = strdup(match);
    prule->fr_target = strdup(target);

    ds_tree_insert(&osfw_rule_list, prule, prule);

    return true;
}

bool osfw_rule_del(
        int family,
        enum osfw_table table,
        const char *chain,
        int priority,
        const char *match,
        const char *target)
{
    struct osfw_rule *rule;
    struct osfw_rule key;

    LOG(INFO, "osfw: %s.%s.%s: Deleting rule (priority %d): %s -> %s",
                OSFW_IPTABLES_CMD(family),
                osfw_table_str(table),
                chain,
                priority,
                match,
                target);

    key.fr_family = family;
    key.fr_table = table;
    key.fr_chain = (char *)chain;
    key.fr_priority = priority;
    key.fr_rule = (char *)match;
    key.fr_target = (char *)target;

    rule = ds_tree_find(&osfw_rule_list, &key);
    if (rule == NULL)
    {
        LOG(WARN, "osfw: Rule does not exist.");
        return true;
    }

    ds_tree_remove(&osfw_rule_list, rule);

    free(rule->fr_chain);
    free(rule->fr_rule);
    free(rule->fr_target);

    free(rule);

    return true;
}

bool osfw_apply(void)
{
    struct osfw_rule *prule;

    /*
     * Scan the rule list -- the rules are sorted in the following order:
     *  * family
     *  * table name
     *  * chain name
     *  * priority
     *
     * It is guaranteed that rules with the same chan/table are clustered
     * together.
     */

    int cfamily = -1;   /* Current family */
    const char *ctable = "";  /* Current table */
    const char *cchain = "";  /* Current chain */

    ds_tree_foreach(&osfw_rule_list, prule)
    {
        const char *table = osfw_table_str(prule->fr_table);

        if ((cfamily != prule->fr_family) ||
                (strcmp(ctable, osfw_table_str(prule->fr_table)) != 0) ||
                (strcmp(cchain, prule->fr_chain) != 0))
        {
            /* Table or chain was changed, add/flush the current chain */
            if (!osfw_iptables_chain_add(
                    prule->fr_family,
                    prule->fr_table,
                    prule->fr_chain))
            {
                return false;
            }
        }

        cfamily = prule->fr_family;
        ctable = table;
        cchain = prule->fr_chain;

        /* Add the chain */
        if (!osfw_iptables_rule_add(
                prule->fr_family,
                prule->fr_table,
                prule->fr_chain,
                prule->fr_priority,
                prule->fr_rule,
                prule->fr_target))
        {
            return false;
        }
    }

    return true;
}


/*
 * ===========================================================================
 *  Private implementation
 * ===========================================================================
 */

bool osfw_iptables_chain_add(
        int family,
        enum osfw_table table,
        const char *chain)
{
    bool chain_builtin;
    char **pchain;
    int rc;

    struct osfw_table_def *tbl = osfw_table_get(table);
    if (tbl == NULL)
    {
        LOG(ERR, "osfw: Unknown table %d during chain add.", table);
        return false;
    }

    /*
     * Chain must not share names with built-in targets
     */
    if (osfw_target_is_builtin(chain))
    {
        return true;
    }

    /* Check if chain is built-in */
    chain_builtin = false;
    for (pchain = tbl->chains; *pchain != NULL; pchain++)
    {
        if (strcmp(chain, *pchain) == 0)
        {
            chain_builtin = true;
        }
    }

    if (!chain_builtin)
    {
        LOG(INFO, "osfw: %s.%s.%s: Adding chain.",
                OSFW_FAMILY_STR(family), tbl->name, chain);

        rc = execsh_log(
                LOG_SEVERITY_DEBUG,
                osfw_iptables_chain_add_cmd,
                OSFW_IPTABLES_CMD(family),
                tbl->name,
                (char *)chain);
        if (rc != 0)
        {
            LOG(ERR, "osfw: %s.%s.%s: Error adding chain.",
                    OSFW_FAMILY_STR(family), tbl->name, chain);
            return false;
        }
    }

    LOG(INFO, "osfw: %s.%s.%s: Flushing chain (post-add).",
            OSFW_FAMILY_STR(family), tbl->name, chain);
    rc = execsh_log(
            LOG_SEVERITY_DEBUG,
            osfw_iptables_chain_flush_cmd,
            OSFW_IPTABLES_CMD(family),
            tbl->name,
            (char *)chain);
    if (rc != 0)
    {
        LOG(ERR, "osfw: %s.%s.%s: Error flushing chain.",
                OSFW_FAMILY_STR(family), tbl->name, chain);
        return false;
    }

    return true;
}

/*
 * Flush and delete an iptables chain
 */
bool osfw_iptables_chain_del(
        int family,
        enum osfw_table table,
        const char *chain)
{
    bool chain_builtin;
    char **pchain;
    int rc;
    struct osfw_table_def *tbl = osfw_table_get(table);

    if (tbl == NULL)
    {
        LOG(ERR, "osfw: Unknown table %d during chain del.", table);
        return false;
    }

    /*
     * Chain must not share names with built-in targets
     */
    if (osfw_target_is_builtin(chain))
    {
        return true;
    }

    /* Check if chain is built-in */
    chain_builtin = false;
    for (pchain = tbl->chains; *pchain != NULL; pchain++)
    {
        if (strcmp(chain, *pchain) == 0)
        {
            chain_builtin = true;
        }
    }

    LOG(INFO, "osfw: %s.%s.%s: Flushing chain (pre-del).",
            OSFW_FAMILY_STR(family), tbl->name, chain);
    rc = execsh_log(
            LOG_SEVERITY_DEBUG,
            osfw_iptables_chain_flush_cmd,
            OSFW_IPTABLES_CMD(family),
            tbl->name,
            (char *)chain);
    if (rc != 0)
    {
        LOG(ERR, "osfw: %s.%s.%s: Error flushing chain (pre-del).",
                OSFW_FAMILY_STR(family), tbl->name, chain);
        return false;
    }

    if (!chain_builtin)
    {
        LOG(INFO, "osfw: %s.%s.%s: Deleting chain.",
                OSFW_FAMILY_STR(family), tbl->name, chain);
        rc = execsh_log(
                LOG_SEVERITY_DEBUG,
                osfw_iptables_chain_del_cmd,
                OSFW_IPTABLES_CMD(family),
                tbl->name,
                (char *)chain);
        if (rc != 0)
        {
            LOG(ERR, "osfw: %s.%s.%s: Error deleting chain.",
                    OSFW_FAMILY_STR(family), tbl->name, chain);
            return false;
        }
    }

    return true;
}

bool osfw_iptables_rule_add(
        int family,
        enum osfw_table table,
        const char *chain,
        int priority,
        const char *match,
        const char *target)
{
    (void)priority;

    int rc;

    struct osfw_table_def *tbl = osfw_table_get(table);
    if (tbl == NULL)
    {
        LOG(ERR, "osfw: Unknown table %d during rule add.", table);
        return false;
    }

    rc = execsh_log(
            LOG_SEVERITY_DEBUG,
            osfw_iptables_rule_add_cmd,
            OSFW_IPTABLES_CMD(family),
            tbl->name,
            (char *)chain,
            (char *)match,
            (char *)target);
    if (rc != 0)
    {
        LOG(ERR, "osfw: %s.%s.%s: Error adding rule '%s' target '%s'",
                OSFW_FAMILY_STR(family), tbl->name, chain, match, target);
        return false;
    }

    return true;
}

/*
 * ===========================================================================
 *  Helper functions
 * ===========================================================================
 */

struct osfw_table_def *osfw_table_get(enum osfw_table table)
{
    if ((int)table >= ARRAY_LEN(osfw_table_list))
    {
        return NULL;
    }

    if (osfw_table_list[table].name == NULL)
    {
        return NULL;
    }

    return &osfw_table_list[table];
}

static const char *osfw_table_str(enum osfw_table table)
{
    struct osfw_table_def *tbl;

    tbl = osfw_table_get(table);
    if (tbl == NULL)
    {
        return "(unknown table)";
    }

    return tbl->name;
}

/*
 * Return true if @p target is a built-in
 */
bool osfw_target_is_builtin(const char *target)
{
    int ti;

    for (ti = 0; ti < ARRAY_LEN(osfw_target_builtin); ti++)
    {
        if (strcmp(target, osfw_target_builtin[ti]) == 0)
        {
            return true;
        }
    }

    return false;
}

int osfw_rule_cmp(void *_a, void *_b)
{
    struct osfw_rule *a = _a;
    struct osfw_rule *b = _b;
    int rc;

    /* Sort by family */
    rc = (int)a->fr_family - (int)b->fr_family;
    if (rc != 0) return rc;

    /* Sort by table id */
    rc = (int)a->fr_table - (int)b->fr_table;
    if (rc != 0) return rc;

    /* Sort by chain name */
    rc = strcmp(a->fr_chain, b->fr_chain);
    if (rc != 0) return rc;

    /* Sort by priority */
    rc = (int)a->fr_priority - (int)b->fr_priority;
    if (rc != 0) return rc;

    /* Sort by target */
    rc = strcmp(a->fr_target, b->fr_target);
    if (rc != 0) return rc;

    /* Sort by rule */
    rc = strcmp(a->fr_rule, b->fr_rule);
    if (rc != 0) return rc;

    return 0;
}
