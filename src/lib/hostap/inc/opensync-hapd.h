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

#ifndef OPENSYNC_HAPD_H_INCLUDED
#define OPENSYNC_HAPD_H_INCLUDED

#include "schema.h"

struct hapd {
    char phy[IFNAMSIZ];
    char driver[32]; /* eg. nl80211, wext, ... */
    char pskspath[PATH_MAX];
    char confpath[PATH_MAX];
    char conf[4096];
    char psks[4096];
    char country[3];
    int respect_multi_ap;
    int skip_probe_response;
    void (*sta_connected)(struct hapd *hapd, const char *mac, const char *keyid);
    void (*sta_disconnected)(struct hapd *hapd, const char *mac);
    void (*ap_enabled)(struct hapd *hapd);
    void (*ap_disabled)(struct hapd *hapd);
    void (*wps_active)(struct hapd *hapd);
    void (*wps_success)(struct hapd *hapd);
    void (*wps_timeout)(struct hapd *hapd);
    void (*wps_disable)(struct hapd *hapd);
    struct ctrl ctrl;
};

struct hapd *hapd_lookup(const char *bss);
struct hapd *hapd_new(const char *phy, const char *bss);
void hapd_destroy(struct hapd *hapd);
int hapd_conf_gen(struct hapd *hapd,
                  const struct schema_Wifi_Radio_Config *rconf,
                  const struct schema_Wifi_VIF_Config *vconf);
int hapd_conf_apply(struct hapd *hapd);
int hapd_bss_get(struct hapd *hapd,
                 struct schema_Wifi_VIF_State *vstate);
int hapd_sta_get(struct hapd *hapd,
                 const char *mac,
                 struct schema_Wifi_Associated_Clients *client);
int hapd_sta_deauth(struct hapd *hapd, const char *mac);
void hapd_sta_iter(struct hapd *hapd,
                   void (*cb)(struct hapd *hapd, const char *mac, void *data),
                   void *data);
int hapd_wps_activate(struct hapd *hapd);
int hapd_wps_cancel(struct hapd *hapd);
#endif /* OPENSYNC_WPAS_H_INCLUDED */
