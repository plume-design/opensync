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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ev.h>

#include "os.h"
#include "log.h"
#include "kconfig.h"
#include "target.h"
#include "memutil.h"
#include "module.h"
#include "util.h"

#include "dm.h"
#include "dm_mod.h"

#define SIPALG_MODULE               "sipalg"

MODULE(dm_mod_sipalg, dm_mod_sipalg_init, dm_mod_sipalg_fini)

struct sipalg
{
    struct dm_mod  sip_mod;

};

static struct sipalg *sipalg;

/* Callback called by DM when there's a Node_Config request to either enable
 * or disable Sip ALG agent */
static bool dm_mod_sipalg_activate(struct dm_mod *mod_nat, bool enable)
{
    char activate_cmd[C_MAXPATH_LEN];

    if (enable)
    {
        snprintf(activate_cmd, sizeof(activate_cmd),
               "modprobe nf_nat_sip && conntrack -F");
    }
    else
    {
        snprintf(activate_cmd, sizeof(activate_cmd),
                 "rmmod nf_nat_sip && rmmod nf_conntrack_sip && conntrack -F");
    }

    LOG(INFO, "%s sipALG", (enable ? "Enabling" : "Disabling"));
    LOG(DEBUG, "Running command %s", activate_cmd);

    target_device_execute(activate_cmd);

    if (target_device_execute("lsmod | grep sip"))
    {
        dm_mod_update_state(&sipalg->sip_mod, true);
        return true;
    }
    else
    {
        dm_mod_update_state(&sipalg->sip_mod, false);
        return false;
    }
}

void dm_mod_sipalg_init(void *data)
{
    LOG(INFO, "dm_mod_sipalg: Initializing.");

    sipalg = CALLOC(1, sizeof(*sipalg));
    sipalg->sip_mod = DM_MOD_INIT(SIPALG_MODULE, dm_mod_sipalg_activate);

    dm_mod_register(&sipalg->sip_mod);
}

void dm_mod_sipalg_fini(void *data)
{
    LOG(INFO, "dm_mod_sipalg: Finishing.");

    dm_mod_unregister(&sipalg->sip_mod);
}
