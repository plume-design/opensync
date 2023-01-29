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

#ifndef OSW_MODULE_H_INCLUDED
#define OSW_MODULE_H_INCLUDED

#include <module.h>
#include <ds_tree.h>

typedef void *osw_module_fn_t(void);

struct osw_module {
    struct ds_tree_node node;
    const char *name;
    const char *file;
    void *data;
    bool loaded;
    osw_module_fn_t *fn;
};

void osw_module_register(struct osw_module *m);
void osw_module_load(void);
void *osw_module_load_name(const char *name);

#define OSW_MODULE(mod_name) \
    static void *osw_module_ ## mod_name ## _load_cb(void); \
    static void osw_module_ ## mod_name ## _init_cb(void *arg) { \
        static struct osw_module m = { \
            .name = #mod_name, \
            .file = __FILE__, \
            .fn = osw_module_ ## mod_name ## _load_cb, \
        }; \
        osw_module_register(&m); \
    } \
    static void osw_module_ ## mod_name ## _fini_cb(void *arg) {} \
    MODULE(osw_module_## mod_name, \
           osw_module_## mod_name ## _init_cb, \
           osw_module_## mod_name ## _fini_cb); \
    static void *osw_module_ ## mod_name ## _load_cb(void)

/* This can be called from within OSW_MODULE() function body
 * only. It's intended to allow resolving dependencies
 * regardless of their registering ordering.
 */
#define OSW_MODULE_LOAD(name) \
    osw_module_load_name(#name)

#endif /* OSW_MODULE_H_INCLUDED */
