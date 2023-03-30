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

/* This file groups rfkill related helpers */

#define RFKILL_STATE_BLOCKED_SOFT 0
#define RFKILL_STATE_UNBLOCKED 1
#define RFKILL_STATE_BLOCKED_HARD 2

static bool
rfkill_get(const char *phy_name, const char *type)
{
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "/sys/class/ieee80211/%s/rfkill*/%s", phy_name, type);

    glob_t g = {0};
    glob(pattern, 0, NULL, &g);
    LOGT("%s: %s+%s: glob: '%s'", __func__, phy_name, type, pattern);

    bool blocked = false;
    size_t i;
    for (i = 0; i < g.gl_pathc; i++) {
        const char *path = g.gl_pathv[i];
        const char *data = file_geta(path);
        LOGT("%s: %s: read: '%s'", __func__, path, data);
        if (WARN_ON(data == NULL)) continue;
        if (atoi(data) == 1) blocked = true;
    }

    globfree(&g);
    return blocked;
}

static bool
rfkill_get_phy_enabled(const char *phy_name)
{
    const bool soft_blocked = rfkill_get(phy_name, "soft");
    const bool hard_blocked = rfkill_get(phy_name, "hard");
    const bool enabled = (soft_blocked == false)
                      && (hard_blocked == false);
    LOGT("%s: %s: soft=%d hard=%d enabled=%d",
         __func__, phy_name, soft_blocked, hard_blocked, enabled);
    return enabled;
}
