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

#ifndef MODULE_H_INCLUDED
#define MODULE_H_INCLUDED

#include "ds_dlist.h"

/**
 * Initializer for the module_t structure
 */
#define MODULE_INIT(name, start, stop)  \
{                                       \
    .m_name = (name),                   \
    .m_start_fn = (start),              \
    .m_stop_fn = (stop)                 \
}

/*
 * Modules are implemented by using "constructor" functions. The main property of
 * these functions is that they are called automatically before main() and
 * automatically after loading a shared library. These functions are used to
 * build a global list of modules, which can be later on initialized using the
 * module_init() function (typically after basic process setup, for example,
 * after logging initialization).
 */

/* __GNUC__ is true for both GCC and CLANG, both support constructors */
#if defined(__GNUC__)
#define MOD_CTOR        __attribute__((constructor))
#define MOD_DTOR        __attribute__((destructor))
#else
/*
 * It should be possible to implement this on compilers that don't support
 * constructors by generating a register function in MODULE() macro, which
 * would be called when loading a shared object. For linked-in modules, the
 * build process could be changed so that it generates a list of modules during
 * build time. Other approaches might be possible as well.
 */
#error CONSTRUCTOR/DESTRUCTOR attributes not available, unsupported compiler.
#endif

/**
 * Macro for registering a module
 */

#define MODULE(name, start, stop)                                               \
                                                                                \
module_fn_t start;                                                              \
module_fn_t stop;                                                               \
                                                                                \
static struct module module_##name = MODULE_INIT(#name, start, stop);           \
                                                                                \
/*                                                                              \
 * Constructor function, this will be called during object initialization       \
 */                                                                             \
static void MOD_CTOR module_ctor_##name(void)                                   \
{                                                                               \
    module_register(&module_##name);                                            \
}                                                                               \
                                                                                \
/*                                                                              \
 * Destructor function, this will be called during object initialization        \
 */                                                                             \
static void MOD_DTOR module_dtor_##name(void)                                   \
{                                                                               \
    __module_dtor(&module_##name);                                              \
}

/**
 * Prototype for module start/stop functions
 */
typedef void module_fn_t(void);

/*
 * Basic module structure
 */
struct module
{
    const char         *m_name;         /* Module name */
    module_fn_t        *m_start_fn;     /* Start function pointer */
    module_fn_t        *m_stop_fn;      /* Stop function pointer */
    bool                m_started;      /* True if module has been started */
    ds_dlist_node_t     m_dnode;
};

typedef struct module module_t;

/**
 * Register module @p mod
 *
 * Note: This function only adds the module to a global list.
 * The mod->m_start_fn function is executed when @ref module_init() is called.
 *
 * Note: Module registration should be normally performed via the @ref MODULE()
 * macro
 */
void module_register(module_t *mod);

/**
 * Unregister module @p mod
 *
 * This function also executes the module stop function
 */
void module_unregister(module_t *mod);

/**
 * Start a single module - this calls the module start function
 */
void module_start(module_t *mod);

/**
 * Stop a single module - this calls the module stop function
 */
void module_stop(module_t *mod);

/**
 * Start all currently registered modules; this function can be called more than
 * once. For example, after a successful call to @ref module_load() or @ref
 * module_load_all()
 */
void module_init(void);

/**
 * Stop all currently registered and started modules. This function stops all
 * currently started modules.
 *
 * @note
 * Dynamic modules are automatically stopped when they are unloaded. The current
 * implementation stops and unregisters all built-in modules on process exit,
 * however this is NOT GUARANTEED. It is good practice to call this function
 * on process exit to gracefully terminate any active modules.
 */
void module_fini(void);

/**
 * Dynamically load a single module as a shared library from @p path.
 * If successful, the module will be registered in the global list of modules.
 * To start it, a call to @ref module_init() is required.
 *
 * @return
 * This function returns true if the module was successfully loaded, false
 * otherwise.
 *
 * @note
 * Once the module has been successfully loaded, it must be started by
 * calling module_init().
 */
bool module_load(const char *path);

/**
 * Unload a single module. If started, the module is stopped first.
 * Started modules will be automatically stopped before unloading.
 *
 * @note
 * @p path must be exactly the same as was given to module_load().
 */
bool module_unload(const char *path);

/**
 * Dynamically load multiple modules by expanding the @p pattern. @p pattern
 * is expanded using the glob() function.
 *
 * If successful, modules will be registered. To start a module, a call to
 * @ref module_init() is required.
 */
bool module_load_all(const char *pattern);

/**
 * Unload modules that were previously loaded with @ref module_load_all(). This
 * function will automatically stop any started modules.
 *
 * @note
 * The exact same pattern must be specified that was used to load modules
 * using @ref module_load_all().
 */
bool module_unload_all(const char *pattern);

/*
 * Internal module destructor handler
 */
void __module_dtor(module_t *mod);

#endif /* MODULE_H_INCLUDED */
