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

#ifndef OSW_CONFSYNC_H_INCLUDED
#define OSW_CONFSYNC_H_INCLUDED

/**
 * This modules provides logic that pits intended config
 * (from osw_conf) against state (from osw_state) and
 * generates (re)configuration commands to the underlying
 * OSW driver(s) through osw_mux.
 *
 * It is possible to observe this module's progression.
 */

struct osw_confsync;
struct osw_confsync_changed;

typedef void osw_confsync_changed_fn_t(struct osw_confsync *cs, void *priv);

enum osw_confsync_state {
    OSW_CONFSYNC_IDLE,
    OSW_CONFSYNC_REQUESTING,
    OSW_CONFSYNC_WAITING,
    OSW_CONFSYNC_VERIFYING,
};

/**
 * Fetch global singleton instance of osw_confsync.
 *
 * The returned object is guaranteed to be initialized and
 * ready for use.
 */
struct osw_confsync *
osw_confsync_get(void);

/**
 * Fetch the progression state.
 *
 * Expected to be called from within
 * osw_confsync_changed_fn_t callbacks.
 */
enum osw_confsync_state
osw_confsync_get_state(struct osw_confsync *cs);

/**
 * Convert the enum into a string.
 *
 * Intended for logging purposes.
 */
const char *
osw_confsync_state_to_str(enum osw_confsync_state s);

/**
 * Register an observer callback against changes.
 *
 * Whenever confsync's state changes the provided callback
 * (fn) will be called. Before this function exists (fn)
 * will be called too.
 *
 * @param name Used for logging. Must not be NULL.
 *             Recommended: __FILE__.
 * @param fn The callback.
 * @param fn_priv This pointer will be provided back to the
 *                callback upon notification.
 * @return Handle that can be used to unregister through
 *         osw_confsync_unregister_changed.
 */
struct osw_confsync_changed *
osw_confsync_register_changed_fn(struct osw_confsync *cs,
                                 const char *name,
                                 osw_confsync_changed_fn_t *fn,
                                 void *fn_priv);

/**
 * Unregister an observer callback. After calling this the
 * osw_confsync_changed handle becomes invalid and the
 * pointer must be discarded.
 */
void
osw_confsync_unregister_changed(struct osw_confsync_changed *c);

/* FIXME. osw_confsync_set_retry_seconds() */
/* FIXME. osw_confsync_set_enabled() */

#endif /* OSW_CONFSYNC_H_INCLUDED */
