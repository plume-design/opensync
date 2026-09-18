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
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "log.h"
#include "osn_fw_pri.h"
#include "os.h"
#include "const.h"
#include "evx.h"
#include "ds_tree.h"
#include "util.h"
#include "memutil.h"
#include "kconfig.h"


#define OSFW_EBTABLES_CMD  "ebtables"

/*
 * ebtables atomic apply: when EBTABLES_ATOMIC_FILE is exported, ebtables
 * operates on the referenced file instead of the live kernel table. We
 * --atomic-save a table (snapshotting current kernel state, including rules
 * we don't manage), replay all chain/rule operations against the file and
 * --atomic-commit it back in a single atomic step. This removes the window
 * where a managed chain is momentarily empty during a rebuild.
 *
 * The work file lives inside a freshly created private directory (mkdtemp(),
 * mode 0700, randomized name) rather than at a predictable path. ebtables
 * opens EBTABLES_ATOMIC_FILE by path itself, so a fixed name in world-writable
 * /tmp would let a local user pre-plant a symlink and trick root-owned ebtables
 * into writing elsewhere. A 0700 directory with an unpredictable name closes
 * that vector regardless of how ebtables opens the file.
 */
#define OSFW_EB_ATOMIC_ENV       "EBTABLES_ATOMIC_FILE"
#define OSFW_EB_ATOMIC_DIR_TMPL  "/tmp/osfw-eb-atomic.XXXXXX"
#define OSFW_EB_ATOMIC_FILE_NAME "table"

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
    char               *fr_name;        /**< Config Name */
    ds_tree_node_t      fr_tnode;
};

/*
 * Chain definition
 */
struct osfw_chain
{
    int                 fc_family;      /**< Chain family */
    enum osfw_table     fc_table;       /**< Chain table */
    char               *fc_chain;       /**< Chain name */
    bool                fc_active;      /**< True if active, false if pending for deletion */
    ds_tree_node_t      fc_tnode;
};

static bool osfw_ebtables_chain_flush(int family, enum osfw_table table, const char *chain);
static bool osfw_ebtables_chain_add(int family, enum osfw_table table, const char *chain);
static bool osfw_ebtables_chain_del(int family, enum osfw_table table, const char *chain);

static bool osfw_ebtables_rule_add(
        int family,
        enum osfw_table table,
        const char *chain,
        int priority,
        const char *match,
        const char *target,
        const char *name);

static struct osfw_table_def *osfw_table_get(enum osfw_table table);
static const char *osfw_table_str(enum osfw_table table);
static bool osfw_eb_is_builtin_chain(enum osfw_table table, const char *chain);
static void osfw_debounce_fn(struct ev_loop *loop, ev_debounce *w, int revent);
static ds_key_cmp_t osfw_rule_cmp;
static ds_key_cmp_t osfw_chain_cmp;

static bool osfw_eb_atomic_probe(void);
static bool osfw_eb_atomic_begin(void);
static void osfw_eb_atomic_end(void);
static bool osfw_eb_atomic_save(enum osfw_table table);
static bool osfw_eb_atomic_commit(enum osfw_table table);
static bool osfw_eb_table_apply_atomic_on_table(enum osfw_table table);
static bool osfw_eb_table_apply_atomic(void);
static void osfw_eb_table_apply_legacy(void);

static ev_debounce osfw_debounce_timer;
static ds_tree_t osfw_rule_list = DS_TREE_INIT(osfw_rule_cmp, struct osfw_rule, fr_tnode);
static ds_tree_t osfw_chain_list = DS_TREE_INIT(osfw_chain_cmp, struct osfw_chain, fc_tnode);

/* Cached result of the one-time ebtables atomic-file support probe. */
enum osfw_eb_atomic_support
{
    OSFW_EB_ATOMIC_UNKNOWN = -1,  /* not probed yet */
    OSFW_EB_ATOMIC_UNSUPPORTED = 0,  /* atomic apply unsupported */
    OSFW_EB_ATOMIC_SUPPORTED = 1  /* atomic apply supported */
};
static enum osfw_eb_atomic_support osfw_eb_atomic_supported = OSFW_EB_ATOMIC_UNKNOWN;
/* Private per-apply work directory (mkdtemp) and the file within it. Both are
 * empty strings when no apply is in progress. */
static char osfw_eb_atomic_dir[OSFW_SIZE_CMD];
static char osfw_eb_atomic_path[OSFW_SIZE_CMD];

struct osfw_ebbase
{
    osfw_fn_t *status_fn;
    osfw_hook_fn_t *osfw_hook_fn;
};

static struct osfw_ebbase osfw_ebbase;
/*
 * Built-in tables and chain list
 */
static struct osfw_table_def osfw_table_list[] =
{
    [OSFW_TABLE_FILTER] =
    {
        .name = OSFW_STR_TABLE_FILTER,
        .chains = C_VPACK(OSFW_STR_CHAIN_INPUT, OSFW_STR_CHAIN_OUTPUT, OSFW_STR_CHAIN_FORWARD)
    },
    [OSFW_TABLE_NAT] =
    {
        .name = OSFW_STR_TABLE_NAT,
        .chains = C_VPACK(OSFW_STR_CHAIN_PREROUTING, OSFW_STR_CHAIN_OUTPUT, OSFW_STR_CHAIN_POSTROUTING)
    },
    [OSFW_TABLE_BROUTE] =
    {
        .name = OSFW_STR_TABLE_BROUTE,
        .chains = C_VPACK(OSFW_STR_CHAIN_BROUTING)
    }
};

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
    CONFIG_OSN_SKIP_ACCEL,
};

static const char *osfw_eb_convert_family(int family)
{
    const char *str = OSFW_STR_UNKNOWN;

    if (family == AF_BRIDGE) {
        str = OSFW_STR_FAMILY_ETH;
    } else {
        LOGN("Invalid ebtables family: %d", family);
    }

    return str;
}

static const char *osfw_eb_convert_cmd(int family)
{
    const char *cmd = OSFW_STR_UNKNOWN;

    if (family == AF_BRIDGE) {
        cmd = OSFW_EBTABLES_CMD;
    } else {
        LOGN("ebtables family: %d, not supported", family);
    }

    return cmd;
}

/*
 * ===========================================================================
 *  Public API implementation
 * ===========================================================================
 */

bool osfw_eb_init(osfw_fn_t *status_fn, osfw_hook_fn_t *osfw_hook_fn)
{
    osfw_ebbase.status_fn = status_fn;
    osfw_ebbase.osfw_hook_fn = osfw_hook_fn;
    /*
     * The default debounce timer is 0.3ms with a maximum timeout of 2 seconds
     */
    ev_debounce_init2(
            &osfw_debounce_timer,
            osfw_debounce_fn,
            0.3, 2.0);

    return true;
}

bool osfw_eb_fini(void)
{
    /* Make sure no stale atomic work file/dir is left behind */
    osfw_eb_atomic_end();
    return true;
}

bool osfw_eb_apply(void)
{
    ev_debounce_start(EV_DEFAULT, &osfw_debounce_timer);
    return true;
}

static bool
osn_ebtables_chain_add(struct osfw_rule *prule, char *chain)
{
    struct osfw_chain *pchain;
    struct osfw_chain kchain;
    bool ok = true;

    kchain.fc_family = prule->fr_family;
    kchain.fc_table = prule->fr_table;
    kchain.fc_chain = chain;

    pchain = ds_tree_find(&osfw_chain_list, &kchain);
    if (pchain == NULL)
    {
        pchain = CALLOC(1, sizeof(*pchain));
        pchain->fc_family = prule->fr_family;
        pchain->fc_table = prule->fr_table;
        pchain->fc_chain = STRDUP(chain);
        ds_tree_insert(&osfw_chain_list, pchain, pchain);
    }

    if (!pchain->fc_active)
    {
        pchain->fc_active = true;
        /* osfw_ebtables_chain_add() flushes or adds the chain */
        ok = osfw_ebtables_chain_add(pchain->fc_family, pchain->fc_table, pchain->fc_chain);
    }

    return ok;
}

/*
 * Create a private, unpredictable work directory (mkdtemp(), mode 0700) and
 * export EBTABLES_ATOMIC_FILE pointing at a file within it, so every subsequent
 * ebtables invocation (run via cmd_log()->popen(), which inherits environ)
 * operates on that file. The randomized 0700 directory prevents a local user
 * from pre-planting a symlink at the work path and redirecting root-owned
 * ebtables writes. Returns false on error (leaving nothing behind).
 */
static bool osfw_eb_atomic_begin(void)
{
    char tmpl[] = OSFW_EB_ATOMIC_DIR_TMPL;

    /* Guard against a double begin() with no intervening end() */
    if (osfw_eb_atomic_dir[0] != '\0' || osfw_eb_atomic_path[0] != '\0')
    {
        LOG(WARN, "osfw: ebtables atomic begin() called again without end(); "
                "cleaning up stale work dir (%s)", osfw_eb_atomic_dir);
        osfw_eb_atomic_end();
    }

    osfw_eb_atomic_dir[0] = '\0';
    osfw_eb_atomic_path[0] = '\0';

    if (mkdtemp(tmpl) == NULL)
    {
        LOGE("osfw: Unable to create ebtables atomic work dir (%s): %d - %s",
                OSFW_EB_ATOMIC_DIR_TMPL, errno, strerror(errno));
        return false;
    }

    snprintf(osfw_eb_atomic_dir, sizeof(osfw_eb_atomic_dir), "%s", tmpl);
    snprintf(osfw_eb_atomic_path, sizeof(osfw_eb_atomic_path), "%s/%s",
            osfw_eb_atomic_dir, OSFW_EB_ATOMIC_FILE_NAME);

    /* Without EBTABLES_ATOMIC_FILE in the environment, subsequent ebtables
     * invocations would edit the live kernel tables non-atomically instead of
     * the work file. Rather than silently degrade to that unsafe path, fail so
     * the caller falls back to the legacy apply. Tear down the work dir we just
     * created (osfw_eb_atomic_end() also unsetenv()s, harmless if unset). */
    if (setenv(OSFW_EB_ATOMIC_ENV, osfw_eb_atomic_path, 1) != 0)
    {
        LOGE("osfw: Unable to set %s=%s: %d - %s", OSFW_EB_ATOMIC_ENV,
                osfw_eb_atomic_path, errno, strerror(errno));
        osfw_eb_atomic_end();
        return false;
    }

    return true;
}

/* Stop routing ebtables to the file and remove the work file and its private
 * directory. Safe to call unconditionally / repeatedly. */
static void osfw_eb_atomic_end(void)
{
    unsetenv(OSFW_EB_ATOMIC_ENV);
    if (osfw_eb_atomic_path[0] != '\0')
    {
        unlink(osfw_eb_atomic_path);
        osfw_eb_atomic_path[0] = '\0';
    }
    if (osfw_eb_atomic_dir[0] != '\0')
    {
        rmdir(osfw_eb_atomic_dir);
        osfw_eb_atomic_dir[0] = '\0';
    }
}

/*
 * Probe once whether the platform's ebtables supports atomic-file operations.
 * Legacy ebtables implements --atomic-save/--atomic-commit and honours
 * EBTABLES_ATOMIC_FILE; ebtables-nft does not. We run a harmless
 * "--atomic-save" against the filter table (always present) and check the exit
 * code. The result is cached by the caller.
 */
static bool osfw_eb_atomic_probe(void)
{
    char cmd[OSFW_SIZE_CMD*2];
    int rc;

    if (!osfw_eb_atomic_begin()) return false;

    snprintf(cmd, sizeof(cmd) - 1, "%s -t %s --atomic-save 2>&1",
            OSFW_EBTABLES_CMD, OSFW_STR_TABLE_FILTER);
    cmd[sizeof(cmd) - 1] = '\0';
    rc = cmd_log(cmd);

    osfw_eb_atomic_end();

    if (rc != 0)
    {
        LOG(NOTICE, "osfw: ebtables atomic-file unsupported (rc=%d); "
                "using non-atomic apply", rc);
        return false;
    }

    LOG(INFO, "osfw: ebtables atomic-file supported; using atomic apply");
    return true;
}

/* Snapshot the current kernel table into the atomic work file. This preserves
 * rules/chains we do not manage (thin backend semantics).
 * --atomic-save copies the live kernel table into the file, so SWAN rules and
 * any other externally managed chains are included. --atomic-init would create
 * an empty table, destroying those rules on commit. */
static bool osfw_eb_atomic_save(enum osfw_table table)
{
    char cmd[OSFW_SIZE_CMD*2];
    struct osfw_table_def *tbl = osfw_table_get(table);

    if (tbl == NULL)
    {
        LOG(ERR, "osfw: Unknown ebtables table %d during atomic-save.", table);
        return false;
    }
    if (!is_input_shell_safe(tbl->name)) return false;

    snprintf(cmd, sizeof(cmd) - 1, "%s -t %s --atomic-save 2>&1",
            OSFW_EBTABLES_CMD, tbl->name);
    cmd[sizeof(cmd) - 1] = '\0';

    if (cmd_log(cmd))
    {
        LOGE("osfw: %s: Unable to atomic-save ebtables table", tbl->name);
        return false;
    }
    return true;
}

/* Atomically swap the edited work file back into the kernel table. */
static bool osfw_eb_atomic_commit(enum osfw_table table)
{
    char cmd[OSFW_SIZE_CMD*2];
    struct osfw_table_def *tbl = osfw_table_get(table);

    if (tbl == NULL)
    {
        LOG(ERR, "osfw: Unknown ebtables table %d during atomic-commit.", table);
        return false;
    }
    if (!is_input_shell_safe(tbl->name)) return false;

    snprintf(cmd, sizeof(cmd) - 1, "%s -t %s --atomic-commit 2>&1",
            OSFW_EBTABLES_CMD, tbl->name);
    cmd[sizeof(cmd) - 1] = '\0';

    if (cmd_log(cmd))
    {
        LOGE("osfw: %s: Unable to atomic-commit ebtables table", tbl->name);
        return false;
    }
    return true;
}

/*
 * Rebuild a single table atomically. All chain/rule operations below run
 * against the atomic work file (EBTABLES_ATOMIC_FILE is exported by the
 * caller), so the live kernel table only changes at atomic-commit -- managed
 * chains are never momentarily empty.
 */
static bool osfw_eb_table_apply_atomic_on_table(enum osfw_table table)
{
    struct osfw_chain *pchain;
    struct osfw_rule *prule;
    ds_tree_iter_t iter;
    bool ok = true;

    /* Snapshot kernel state into the work file. On failure leave the kernel
     * untouched (no leak) and skip this table for this cycle. */
    if (!osfw_eb_atomic_save(table))
    {
        LOG(ERR, "osfw: %s: atomic-save failed; table left unchanged this cycle",
                osfw_table_str(table));
        return false;
    }

    /* Clear the chain status for this table only */
    ds_tree_foreach(&osfw_chain_list, pchain)
    {
        if (pchain->fc_table != table) continue;
        pchain->fc_active = false;
    }

    /* (Re)create the chains referenced by this table's rules */
    ds_tree_foreach(&osfw_rule_list, prule)
    {
        if (prule->fr_table != table) continue;
        if (!osn_ebtables_chain_add(prule, prule->fr_chain))  ok = false;
        if (!osn_ebtables_chain_add(prule, prule->fr_target)) ok = false;
    }

    /* Delete this table's unused chains (fc_active == false) from the work
     * file. Do NOT remove them from the in-memory tracking tree yet: that is
     * deferred until the commit below succeeds. If the atomic apply fails at a
     * later step and the caller falls back to the legacy path, that path walks
     * osfw_chain_list to clean the kernel and must still see these chains. */
    ds_tree_foreach(&osfw_chain_list, pchain)
    {
        if (pchain->fc_table != table) continue;
        if (pchain->fc_active) continue;

        if (!osfw_ebtables_chain_del(pchain->fc_family, pchain->fc_table, pchain->fc_chain))
        {
            LOG(WARN, "osfw: %d.%s.%s: Error deleting ebtables chain.",
                    pchain->fc_family,
                    osfw_table_str(pchain->fc_table),
                    pchain->fc_chain);
            ok = false;
        }
    }

    /* Chains are fully created by now, so re-add this table's rules.
     * For built-in chains (FORWARD, INPUT, OUTPUT, etc.) the atomic-save
     * snapshot already contains the rule, so delete it first to avoid
     * duplicates. Custom chains were flushed above, so no delete needed. */
    ds_tree_foreach(&osfw_rule_list, prule)
    {
        if (prule->fr_table != table) continue;
        if (osfw_eb_is_builtin_chain(table, prule->fr_chain))
        {
            struct osfw_table_def *tbl = osfw_table_get(table);
            if (tbl != NULL
                && is_input_shell_safe(tbl->name)
                && is_input_shell_safe(prule->fr_chain)
                && is_input_shell_safe(prule->fr_rule)
                && is_input_shell_safe(prule->fr_target))
            {
                char cmd[OSFW_SIZE_CMD*2];
                snprintf(cmd, sizeof(cmd) - 1, "%s -t %s -D %s -j %s %s 2>&1",
                        osfw_eb_convert_cmd(prule->fr_family),
                        tbl->name, prule->fr_chain,
                        prule->fr_target, prule->fr_rule);
                cmd[sizeof(cmd) - 1] = '\0';
                cmd_log(cmd);
            }
        }
        if (!osfw_ebtables_rule_add(
                prule->fr_family,
                prule->fr_table,
                prule->fr_chain,
                prule->fr_priority,
                prule->fr_rule,
                prule->fr_target,
                prule->fr_name)) ok = false;
    }

    /*
     * Only commit if every operation on the work file succeeded. A partial
     * work file must never be committed: doing so could install an incomplete
     * ruleset (e.g. a missing INET_ONLY DROP) and reintroduce the very leak
     * this path is meant to close. On failure we skip the commit, so the
     * kernel table keeps its previous, complete ruleset; the caller discards
     * the work file. The next debounce cycle retries from a fresh snapshot.
     */
    if (!ok)
    {
        LOG(ERR, "osfw: %s: one or more ebtables operations failed; "
                "skipping atomic-commit, kernel table left unchanged",
                osfw_table_str(table));
        return false;
    }

    /* Atomic swap the work file into the kernel */
    if (!osfw_eb_atomic_commit(table))
    {
        LOG(ERR, "osfw: %s: atomic-commit failed; kernel table unchanged",
                osfw_table_str(table));
        return false;
    }

    /* Commit succeeded: the kernel now matches our intended state, so it is
     * finally safe to drop this table's unused chains from the tracking tree.
     * Doing this only on the success path keeps osfw_chain_list intact for the
     * legacy fallback whenever the atomic apply bails out above. */
    ds_tree_foreach_iter(&osfw_chain_list, pchain, &iter)
    {
        if (pchain->fc_table != table) continue;
        if (pchain->fc_active) continue;

        ds_tree_iremove(&iter);
        FREE(pchain->fc_chain);
        FREE(pchain);
    }

    return true;
}

/*
 * Non-atomic rebuild: the original behaviour. Flushes every managed chain up
 * front and only re-adds rules in a later pass, so managed chains are
 * momentarily empty. Used when ebtables atomic-file is unavailable or when
 * OSN_FW_EBTABLES_THIN_ATOMIC is disabled.
 */
static void osfw_eb_table_apply_legacy(void)
{
    struct osfw_chain *pchain;
    struct osfw_rule *prule;
    ds_tree_iter_t iter;

    /*
     * Clear the chain status
     */
    ds_tree_foreach(&osfw_chain_list, pchain)
    {
        pchain->fc_active = false;
    }

    /*
     * Scan the rule list and figure out what chains need to be created.
     */
    ds_tree_foreach(&osfw_rule_list, prule)
    {
        osn_ebtables_chain_add(prule, prule->fr_chain);
        osn_ebtables_chain_add(prule, prule->fr_target);
    }
    /* Scan the chain list again and remove unused chains (fc_active == false) */
    ds_tree_foreach_iter(&osfw_chain_list, pchain, &iter)
    {
        if (pchain->fc_active) continue;

        if (!osfw_ebtables_chain_del(pchain->fc_family, pchain->fc_table, pchain->fc_chain))
        {
            LOG(WARN, "osfw: %d.%s.%s: Error deleting ebtables chain.",
                    pchain->fc_family,
                    osfw_table_str(pchain->fc_table),
                    pchain->fc_chain);
        }
        ds_tree_iremove(&iter);
        FREE(pchain->fc_chain);
        FREE(pchain);
    }

    /*
     * The chains should be fully created by now, so scan the rule list and apply them.
     */
    ds_tree_foreach(&osfw_rule_list, prule)
    {
        /* Add the rule */
        osfw_ebtables_rule_add(
                prule->fr_family,
                prule->fr_table,
                prule->fr_chain,
                prule->fr_priority,
                prule->fr_rule,
                prule->fr_target,
                prule->fr_name);
    }
}

/*
 * Attempt to apply every table using the atomic path.
 *
 * Returns true if the atomic path handled the apply (the ebtables hook has
 * been invoked and the caller must not run the legacy fallback), or false if
 * atomic apply is unsupported or could not be started, in which case the
 * caller should fall through to osfw_eb_table_apply_legacy().
 */
static bool osfw_eb_table_apply_atomic(void)
{
    struct osfw_rule *prule;
    struct osfw_chain *pchain;
    bool need[ARRAY_LEN(osfw_table_list)];
    int t;
    bool all_ok = true;

    if (osfw_eb_atomic_supported == OSFW_EB_ATOMIC_UNKNOWN)
    {
        osfw_eb_atomic_supported = osfw_eb_atomic_probe()
                ? OSFW_EB_ATOMIC_SUPPORTED
                : OSFW_EB_ATOMIC_UNSUPPORTED;
    }

    if (osfw_eb_atomic_supported != OSFW_EB_ATOMIC_SUPPORTED) return false;

    memset(need, 0, sizeof(need));

    /* A table needs work if it has any rule OR any tracked chain (the
     * latter so chains that just became unused still get deleted).
     * Bounds-check the table index defensively: a stray/invalid enum
     * value in the lists must never turn into an out-of-bounds write. */
    ds_tree_foreach(&osfw_rule_list, prule)
    {
        int tbl = (int)prule->fr_table;
        if (tbl >= 0 && tbl < (int)ARRAY_LEN(osfw_table_list)) need[tbl] = true;
    }
    ds_tree_foreach(&osfw_chain_list, pchain)
    {
        int tbl = (int)pchain->fc_table;
        if (tbl >= 0 && tbl < (int)ARRAY_LEN(osfw_table_list)) need[tbl] = true;
    }

    /* If the private work dir can't be created, fall through to the
     * non-atomic path so config still gets applied this cycle. */
    if (!osfw_eb_atomic_begin()) return false;

    for (t = 0; t < (int)ARRAY_LEN(osfw_table_list); t++)
    {
        if (!need[t] || osfw_table_get(t) == NULL) continue;
        if (!osfw_eb_table_apply_atomic_on_table(t)) all_ok = false;
    }

    osfw_eb_atomic_end();

    if (!all_ok)
    {
        /* Partial failure: fall back to the non-atomic apply so config still
         * gets applied this cycle. Let the caller invoke the hook after the
         * legacy apply to avoid firing it twice. */
        LOG(WARN, "osfw: one or more ebtables tables failed atomic apply, falling back to legacy apply");
        return false;
    }

    if (osfw_ebbase.osfw_hook_fn) osfw_ebbase.osfw_hook_fn(OSFW_HOOK_EBTABLES);
    return true;
}

static void osfw_debounce_fn(struct ev_loop *loop, ev_debounce *w, int revent)
{
    (void)loop;
    (void)w;
    (void)revent;

    if (kconfig_enabled(CONFIG_OSN_FW_EBTABLES_THIN_ATOMIC))
    {
        if (osfw_eb_table_apply_atomic()) return;
    }

    /* Fallback: non-atomic apply */
    osfw_eb_table_apply_legacy();

    if (osfw_ebbase.osfw_hook_fn) osfw_ebbase.osfw_hook_fn(OSFW_HOOK_EBTABLES);
}

/*
 * This is a no-op -- chains are created/deleted from rules during the apply
 * phase
 */
bool osfw_eb_chain_add(int family, enum osfw_table table, const char *chain)
{
    (void)family;
    (void)table;
    (void)chain;

    return true;
}

/*
 * This is a no-op -- chains are created/deleted from rules during the apply
 * phase
 */
bool osfw_eb_chain_del(int family, enum osfw_table table, const char *chain)
{
    (void)family;
    (void)table;
    (void)chain;

    return true;
}

bool osfw_eb_rule_add(int family, enum osfw_table table, const char *chain,
                      int prio, const char *match, const char *target, const char *name)
{
    struct osfw_rule *prule;

    LOG(INFO, "osfw: %d.%s.%s: Adding ebtables rule (priority %d): %s -> %s",
                family,
                osfw_table_str(table),
                chain,
                prio,
                match,
                target);

    if (!chain || !chain[0] || !match || !target || !target[0]) {
        LOGE("Add ebtables rule: invalid parameters");
        return false;
    }

    if (family != AF_BRIDGE)
    {
        LOGE("OSFW ebtables invalid family %d", family);
        return false;
    }

    /*
     * The main reason we're building a list of rules instead of inserting them
     * directly is to sort out the priority. ebtables doesn't have a notion of
     * priority. Instead it only understands rule numbers, which are basically
     * just rule positions. Rule numbers cannot have holes and if a rule is
     * inserted in the middle of the list, it pushes down the list all rules
     * with a lower ranking. This makes it very difficult to translate fixed
     * priorities to rule numbers (positions).
     */
    prule = CALLOC(1, sizeof(struct osfw_rule));
    prule->fr_enabled = true;
    prule->fr_family = family;
    prule->fr_table = table;
    prule->fr_chain = STRDUP(chain);
    prule->fr_priority = prio;
    prule->fr_rule = STRDUP(match);
    prule->fr_target = STRDUP(target);
    prule->fr_name = STRDUP(name);

    ds_tree_insert(&osfw_rule_list, prule, prule);

    struct osfw_table_def *tbl = osfw_table_get(table);
    LOGI("New ebtables rule %s %s %s %s %s", osfw_eb_convert_family(family), tbl->name, chain, (char *)target, prule->fr_rule);

    return true;
}

bool osfw_eb_rule_del(int family, enum osfw_table table, const char *chain,
                       int prio, const char *match, const char *target)
{
    struct osfw_rule *rule;
    struct osfw_rule key;

    LOG(INFO, "osfw: %d.%s.%s: Deleting ebtables rule (priority %d): %s -> %s",
                family,
                osfw_table_str(table),
                chain,
                prio,
                match,
                target);

    if (!chain || !chain[0] || !match || !target || !target[0]) {
        LOGE("Delete ebtables rule: invalid parameters");
        return false;
    }

    if (family != AF_BRIDGE)
    {
        LOGE("OSFW ebtables invalid family %d", family);
        return false;
    }

    key.fr_family = family;
    key.fr_table = table;
    key.fr_chain = (char *)chain;
    key.fr_priority = prio;
    key.fr_rule = (char *)match;
    key.fr_target = (char *)target;

    rule = ds_tree_find(&osfw_rule_list, &key);
    if (rule == NULL)
    {
        LOG(WARN, "osfw: ebtables rule does not exist");
        return true;
    }

    ds_tree_remove(&osfw_rule_list, rule);

    FREE(rule->fr_chain);
    FREE(rule->fr_rule);
    FREE(rule->fr_target);

    FREE(rule);

    return true;
}

/*
 * ===========================================================================
 *  Private implementation
 * ===========================================================================
 */

static bool osfw_ebtables_chain_flush(
        int family,
        enum osfw_table table,
        const char *chain)
{
    char cmd[OSFW_SIZE_CMD*2];


    struct osfw_table_def *tbl = osfw_table_get(table);
    if (tbl == NULL)
    {
        LOG(ERR, "osfw: Unknown ebtables table %d during chain flush", table);
        return false;
    }

    if (!is_input_shell_safe(tbl->name) || !is_input_shell_safe(chain)) return false;

    snprintf(cmd, sizeof(cmd) - 1, "%s -t %s -F %s 2>&1", osfw_eb_convert_cmd(family), tbl->name, (char *)chain);
    cmd[sizeof(cmd) - 1] = '\0';

    if(cmd_log(cmd))
    {
        LOGE("osfw: %s %s %s: Unable to flush ebtables chain", osfw_eb_convert_family(family), tbl->name, chain);
        return false;
    }

    return true;
}

static bool osfw_ebtables_chain_add(
        int family,
        enum osfw_table table,
        const char *chain)
{
    char cmd[OSFW_SIZE_CMD*2];

    struct osfw_table_def *tbl = osfw_table_get(table);
    if (tbl == NULL)
    {
        LOG(ERR, "osfw: Unknown ebtables table %d during chain add.", table);
        return false;
    }

    /*
     * Chain must not share names with built-in targets. Skipping a built-in
     * chain is intentional and not a failure.
     */
    if (osfw_eb_is_builtin_chain(table, chain))
    {
        LOG(INFO, "osfw: %s %s %s, skipping adding ebtables built in chain", osfw_eb_convert_cmd(family), tbl->name, chain);
        return true;
    }

    if (!is_input_shell_safe(tbl->name) || !is_input_shell_safe(chain)) return false;

    LOG(INFO, "osfw: %s.%s.%s: Adding ebtables chain.",
            osfw_eb_convert_family(family), tbl->name, chain);

    snprintf(cmd, sizeof(cmd) - 1, "%s -t %s -N %s 2>&1", osfw_eb_convert_cmd(family), tbl->name, (char *)chain);
    cmd[sizeof(cmd) - 1] = '\0';

    /* Silent error as the chain may already exists */
    cmd_log(cmd);

    LOG(INFO, "osfw: %s.%s.%s: Flushing ebtables chain (post-add).",
            osfw_eb_convert_family(family), tbl->name, chain);

    /* Success is determined by the flush: the chain must exist and be empty. */
    return osfw_ebtables_chain_flush(family, table, chain);
}

/*
 * Flush and delete an ebtables chain
 */
static bool osfw_ebtables_chain_del(
        int family,
        enum osfw_table table,
        const char *chain)
{
    char cmd[OSFW_SIZE_CMD*2];
    struct osfw_table_def *tbl = osfw_table_get(table);

    if (tbl == NULL)
    {
        LOG(ERR, "osfw: Unknown ebtables table %d during chain del.", table);
        return false;
    }

    /*
     * Chain must not share names with built-in targets
     */
    if (osfw_eb_is_builtin_chain(table, chain))
    {
        LOGW("osfw: %s %s %s, skipping deleting ebtables built in chain", osfw_eb_convert_cmd(family), tbl->name, chain);
        return true;
    }

    LOG(INFO, "osfw: %s.%s.%s: Flushing ebtables chain (pre-del).",
            osfw_eb_convert_cmd(family), tbl->name, chain);

    snprintf(cmd, sizeof(cmd) - 1, "%s -t %s -F %s 2>&1", osfw_eb_convert_cmd(family), tbl->name, (char *)chain);
    cmd[sizeof(cmd) - 1] = '\0';

    if (!osfw_ebtables_chain_flush(family, table, chain)) return false;

    if (!is_input_shell_safe(tbl->name) || !is_input_shell_safe(chain)) return false;

    LOG(INFO, "osfw: %s.%s.%s: Deleting ebtables chain.",
            osfw_eb_convert_family(family), tbl->name, chain);

    snprintf(cmd, sizeof(cmd) - 1, "%s -t %s -X %s 2>&1", osfw_eb_convert_cmd(family), tbl->name, (char *)chain);
    cmd[sizeof(cmd) - 1] = '\0';

    if(cmd_log(cmd))
    {
        LOGE("osfw: %s %s %s: Unable to delete ebtables chain", osfw_eb_convert_family(family), tbl->name, chain);
        return false;
    }

    return true;
}

static bool osfw_ebtables_rule_add(
        int family,
        enum osfw_table table,
        const char *chain,
        int priority,
        const char *match,
        const char *target,
        const char *name)
{
    (void)priority;
    int rc = 0;
    bool ok = true;

    char cmd[OSFW_SIZE_CMD*2];

    struct osfw_table_def *tbl = osfw_table_get(table);
    if (tbl == NULL)
    {
        LOG(ERR, "osfw: Unknown ebtables table %d during rule add.", table);
        return false;
    }

    if (!is_input_shell_safe(tbl->name)
        || !is_input_shell_safe(chain)
        || !is_input_shell_safe(match)
        || !is_input_shell_safe(target))
    {
        return false;
    }

    snprintf(cmd, sizeof(cmd) - 1, "%s -t %s -A %s -j %s %s 2>&1", osfw_eb_convert_cmd(family), tbl->name, (char *)chain, (char *)target, (char *)match);
    cmd[sizeof(cmd) - 1] = '\0';

    rc = cmd_log(cmd);
    if (rc != 0)
    {
         LOGE("osfw: %s %s %s %s %s: Unable to add ebtables rule", osfw_eb_convert_family(family), tbl->name, chain, match, target);
         ok = false;
    }

    if (osfw_ebbase.status_fn) osfw_ebbase.status_fn((char *)name, rc);

    return ok;
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

static int osfw_rule_cmp(const void *_a, const void *_b)
{
    const struct osfw_rule *a = _a;
    const struct osfw_rule *b = _b;
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

static int osfw_chain_cmp(const void *_a, const void *_b)
{
    int rc;

    const struct osfw_chain *a = _a;
    const struct osfw_chain *b = _b;

    rc = a->fc_family - b->fc_family;
    if (rc != 0) return rc;

    rc = a->fc_table - b->fc_table;
    if (rc != 0) return rc;

    rc = strcmp(a->fc_chain, b->fc_chain);
    if (rc != 0) return rc;

    return 0;
}
