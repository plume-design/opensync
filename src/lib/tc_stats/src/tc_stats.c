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

#include <stdbool.h>

#include "log.h"
#include "fcm.h"
#include "ct_stats.h"

/**
 * @brief Traffic Class Stats plugin - delegates to CT Stats
 *
 * This plugin acts as a thin wrapper that delegates all functionality
 * to ct_stats with the parent_plugin field set to FCM_PARENT_TC_STATS.
 */

int tc_stats_plugin_init(fcm_collect_plugin_t *collector)
{
    int rc;

    LOGI("%s: Traffic Class Stats plugin initialization", __func__);

    if (collector == NULL) return -1;

    /* Set parent plugin to indicate it's being called from tc_stats */
    collector->parent_plugin = FCM_PARENT_TC_STATS;

    rc = ct_stats_plugin_init(collector);
    if (rc != 0)
    {
        LOGN("%s: ct_stats_plugin_init failed with rc=%d", __func__, rc);
        return rc;
    }

    LOGI("%s: Successfully initialized via ct_stats_plugin_init", __func__);
    return 0;
}
