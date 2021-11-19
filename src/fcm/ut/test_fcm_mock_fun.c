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

#define _GNU_SOURCE /* Needed for RTLD_* */
#include <dlfcn.h>
#include "log.h"
#include "fcm.h"
#include "const.h"

/*
 * Since we are wrapping some functions, the following
 * statements will be required in the unit.mk.
 * Note: augment accordingly if other __wrap_'ed functions
 *       are needed.
 *
UNIT_LDFLAGS += -Wl,--wrap=dlopen
UNIT_LDFLAGS += -Wl,--wrap=dlsym
UNIT_LDFLAGS += -Wl,--wrap=dlclose
*/

static int dl_test_handle = 1234;
fcm_collect_plugin_t *test_plugin = NULL;
char *lib_so[] = {"/usr/opensync/lib/libfcm_test_FCM_collector.so", "/usr/opensync/lib/libfcm_test_FCM_collector_1.so"};
char *fct_ptr_name[] = {"test_FCM_collector_plugin_init", "test_FCM_collector_1_plugin_init"};

int test_plugin_init(fcm_collect_plugin_t *collector)
{
    test_plugin = collector;
    LOGD("test plugin initialized");
    return 0;
}

extern void *__real_dlopen(const char *file, int mode);
void *__wrap_dlopen(const char *file, int mode)
{
    /*
     * Ensure we are "blocking" the loading of the actual shared object so
     * we can return 'success'. We'll use the 'mock' handle to dlclose()
     * properly.
     */
    size_t it;
    int rc;

    for (it = 0; it < ARRAY_SIZE(lib_so); it++)
    {
       rc = strcmp(file, lib_so[it]);
       if (!rc) return &dl_test_handle;
    }

    return __real_dlopen(file, mode);
}

extern int __real_dlclose(void *handle);
int __wrap_dlclose(void *handle)
{
    bool cond;

    /* Nothing to be closed if we are using our local mocked functions */
    cond = (handle != RTLD_DEFAULT);
    if (cond) cond &= (handle != RTLD_NEXT);
    if (cond) cond &= (*(int *)handle == dl_test_handle);
    if (cond) return 0;

    return __real_dlclose(handle);
}

extern void* __real_dlsym(void * , const char *);
void *__wrap_dlsym(void *handle, const char *name)
{
    size_t it;
    bool cond;
    int rc;

    /* Find our specific mock function */
    cond = (handle != RTLD_DEFAULT);
    if (cond) cond &= (handle != RTLD_NEXT);
    if (cond) cond &= (*(int *)handle == dl_test_handle);
    if (cond)
    {
        for (it = 0; it < ARRAY_SIZE(fct_ptr_name); it++)
        {
           rc = strcmp(name, fct_ptr_name[it]);
           if (!rc) return &test_plugin_init;
        }

    }

    /* If things were not mocked, then use the regular call */
    return __real_dlsym(handle, name);
}
