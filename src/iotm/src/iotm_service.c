/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "iotm_service.h"
#include "iotm_ovsdb.h"
#include <dlfcn.h>

bool iotm_parse_dso(struct iotm_session *session)
{
    char *plugin;

    plugin = session->conf->plugin;
    if (plugin != NULL)
    {
        LOGI("%s: plugin: %s", __func__, plugin);
        session->dso = strdup(plugin);
    }
    else
    {
        char *dir = "/usr/plume/lib";
        char dso[256];

        memset(dso, 0, sizeof(dso));
        snprintf(dso, sizeof(dso), "%s/libiotm_%s.so", dir, session->name);
        session->dso = strdup(dso);
    }

    LOGT("%s: session %s set dso path to %s", __func__,
            session->name, session->dso != NULL ? session->dso : "None");

    return (session->dso != NULL ? true : false);
}

bool iotm_init_plugin(struct iotm_session *session)
{
    void (*init)(struct iotm_session *session);
    char *dso_init;
    char init_fn[256];
    char *error;

    dlerror();
    session->handle = dlopen(session->dso, RTLD_NOW);
    if (session->handle == NULL)
    {
        LOGE("%s: dlopen %s failed: %s", __func__, session->dso, dlerror());
        return false;
    }
    dlerror();

    LOGI("%s: session name: %s, dso %s", __func__, session->name, session->dso);
    dso_init = iotm_get_other_config_val(session, "dso_init");
    if (dso_init == NULL)
    {
        memset(init_fn, 0, sizeof(init_fn));
        snprintf(init_fn, sizeof(init_fn), "%s_plugin_init", session->name);
        dso_init = init_fn;
    }

    if (dso_init == NULL)
    {
        LOGE("%s: DSO_INIT was null after memset.\n", __func__);
        return false;
    };

    *(void **)(&init) = dlsym(session->handle, dso_init);
    LOGI("%s: Attempting to run init: [%s]\n", __func__, dso_init);
    error = dlerror();
    if (error != NULL)
    {
        LOGE("%s: could not get init symbol %s: %s",
                __func__, dso_init, error);
        dlclose(session->handle);
        return false;
    }

    init(session);
    return true;
}
