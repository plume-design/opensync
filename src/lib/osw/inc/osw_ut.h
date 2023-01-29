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

#ifndef OSW_UT_H_INCLUDED
#define OSW_UT_H_INCLUDED

#include <stdbool.h>
#include <module.h>

#define OSW_UT_EVAL(cond) assert(cond)

typedef void (*osw_ut_module_run_f) (void *data);

#define OSW_UT(name) \
    static void osw_ut_func_ ## name(void *arg); \
    static void osw_ut_init_ ## name(void *arg) { osw_ut_register(#name, osw_ut_func_ ## name, NULL); } \
    static void osw_ut_fini_ ## name(void *arg) {} \
    MODULE(osw_ut_ ## name, osw_ut_init_ ## name, osw_ut_fini_ ## name); \
    static void osw_ut_func_ ## name(void *arg)

#define osw_ut_register(name, fun, data) \
    osw_ut_register_raw(name, __FILE__, #fun, fun, data)

void
osw_ut_register_raw(const char *name,
                    const char *file_name,
                    const char *fun_name,
                    osw_ut_module_run_f fun,
                    void *data);

bool
osw_ut_run_by_prefix(const char *prefix,
                     bool dont_fork,
                     bool verbose);

bool
osw_ut_run_all(bool dont_fork,
               bool verbose);

void
osw_ut_print_test_names(void);

void
osw_ut_time_init(void);

void
osw_ut_time_advance(uint64_t delta_nsec);

#if 0
// FIXME: Make kconfig configurable?
static inline void
osw_ut_register_raw(const char *name,
                    const char *file_name,
                    const char *fun_name,
                    osw_ut_module_run_f fun,
                    void *data)
{
    /* nop */
}

static inline void
osw_ut_run_all(bool verbose)
{
    fprintf(stderr, "Binary compiled without unit tests, exiting\n");
}
#endif

#endif /* OSW_UT_H_INCLUDED */
