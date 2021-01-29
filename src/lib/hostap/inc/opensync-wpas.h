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

#ifndef OPENSYNC_WPAS_H_INCLUDED
#define OPENSYNC_WPAS_H_INCLUDED

#include "schema.h"

struct wpas {
    char phy[IFNAMSIZ];
    char driver[32]; /* eg. nl80211, wext, ... */
    char confpath[PATH_MAX];
    char conf[4096];
    int freqlist[64];
    int respect_multi_ap;
    void (*connected)(struct wpas *wpas, const char *bssid, int id, const char *id_str);
    void (*disconnected)(struct wpas *wpas, const char *bssid, int reason, int local);
    void (*scan_results)(struct wpas *wpas);
    void (*scan_failed)(struct wpas *wpas, int status);
    struct ctrl ctrl;
};

struct wpas *wpas_lookup(const char *bss);
struct wpas *wpas_new(const char *phy, const char *bss);
void wpas_destroy(struct wpas *wpas);
int wpas_conf_gen(struct wpas *wpas,
                  const struct schema_Wifi_Radio_Config *rconf,
                  const struct schema_Wifi_VIF_Config *vconf,
                  const struct schema_Wifi_Credential_Config *cconfs,
                  size_t n_cconfs);
int wpas_conf_apply(struct wpas *wpas);
int wpas_bss_get(struct wpas *wpas,
                 struct schema_Wifi_VIF_State *vstate);

#endif /* OPENSYNC_WPAS_H_INCLUDED */
