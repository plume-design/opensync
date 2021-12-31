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

#include "log.h"
#include "lan_stats.h"

#define OVS_DPCTL_DUMP_FLOWS "ovs-dpctl dump-flows -m"
static char *collect_cmd = OVS_DPCTL_DUMP_FLOWS;

void
lan_stats_collect_flows(lan_stats_instance_t *lan_stats_instance)
{
    char line_buf[LINE_BUFF_LEN] = {0,};
    fcm_collect_plugin_t *collector;
    dp_ctl_stats_t *stats;
    FILE *fp = NULL;

    if (lan_stats_instance == NULL) return;
    stats = &lan_stats_instance->stats;

    collector = lan_stats_instance->collector;
    if (collector == NULL) return;

    collect_cmd  = collector->fcm_plugin_ctx;
    if (collect_cmd == NULL)
        collect_cmd = OVS_DPCTL_DUMP_FLOWS;

    if ((fp = popen(collect_cmd, "r")) == NULL)
    {
        LOGE("popen error");
        return;
    }

    while (fgets(line_buf, LINE_BUFF_LEN, fp) != NULL)
    {
        memset(stats, 0, sizeof(*stats));
        LOGD("ovs-dpctl dump line %s", line_buf);
        lan_stats_parse_flows(lan_stats_instance, line_buf);
        lan_stats_add_uplink_info(lan_stats_instance);
        lan_stats_flows_filter(lan_stats_instance);
        memset(line_buf, 0, sizeof(line_buf));
    }

    pclose(fp);
    fp = NULL;
}

