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

#ifndef TPSM_MOD_H_INCLUDED
#define TPSM_MOD_H_INCLUDED

#include <stdbool.h>

#include "ds_tree.h"

#define TPSM_MOD_INIT(name, activate_fn) \
    (struct tpsm_mod) \
    { \
        .mod_name = name, .mod_activate_fn = activate_fn \
    }

struct tpsm_mod;

/* Module enable/disable function */
typedef bool tpsm_mod_enable_fn_t(struct tpsm_mod *p, bool enable);

struct tpsm_mod
{
    const char *mod_name;                  /* module name */
    tpsm_mod_enable_fn_t *mod_activate_fn; /* function called to enable/disable */

    ds_tree_node_t _mod_node; /* internal */
};

/**
 * Register a module to TPSM
 *
 * @param[in]   p  Pointer to a module definition structure.
 */
void tpsm_mod_register(struct tpsm_mod *p);

/**
 * Unregister a module from TPSM
 *
 * @param[in]   p  Pointer to a module definition structure.
 */
void tpsm_mod_unregister(struct tpsm_mod *p);

/**
 * Update Node_State for this module accordingly.
 *
 * Expected to be called by the registered module at the point when the
 * enable/disable state becomes known.
 *
 * @param[in]    p         Pointer to a module definition structure
 * @param[in]    enabled   true if module enabled, false otherwise
 *
 * @return True on success (Node_State successfully updated).
 */
bool tpsm_mod_update_state(struct tpsm_mod *p, bool enabled);

void tpsm_mod_init(void);

#endif /* TPSM_MOD_H_INCLUDED */
