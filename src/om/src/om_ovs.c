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

/*
 * Openflow Manager - openflow rules processing
 */

#define  _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>

#include "schema.h"
#include "os.h"
#include "log.h"
#include "target.h"
#include "om.h"
#include "util.h"

/*****************************************************************************/
#define MODULE_ID LOG_MODULE_ID_MAIN
/*****************************************************************************/

bool om_add_flow(const char *token, const struct schema_Openflow_Config *ofconf)
{
    const char *ovs_ofctl_prog = "ovs-ofctl";
    const char *ovs_ofctl_cmd  = "add-flow";
    char *flow_entry = strfmta("table=%d,priority=%d%s%s,actions=%s",
                               ofconf->table,
                               ofconf->priority,
                               strlen(ofconf->rule) > 0 ? "," : "",
                               ofconf->rule,
                               ofconf->action);

    LOGI("Flow entry add: %s %s %s %s", ovs_ofctl_prog, ovs_ofctl_cmd, ofconf->bridge, flow_entry);
    if (strexa(ovs_ofctl_prog, ovs_ofctl_cmd, ofconf->bridge, flow_entry) == NULL) {
        LOGE("Flow entry add failed: %s %s %s %s", ovs_ofctl_prog, ovs_ofctl_cmd, ofconf->bridge, flow_entry);
        return false;
    }

    target_om_hook(TARGET_OM_POST_ADD, ofconf->rule); 

    return true;
}

bool om_del_flow(const char *token, const struct schema_Openflow_Config *ofconf)
{
    const char *ovs_ofctl_prog = "ovs-ofctl";
    const char *ovs_ofctl_cmd  = "del-flows";
    char *flow_entry = strfmta("table=%d,priority=%d%s%s",
                               ofconf->table,
                               ofconf->priority,
                               strlen(ofconf->rule) > 0 ? "," : "",
                               ofconf->rule);
    char *strict_param = "--strict";

    LOGI("Flow entry del: %s %s %s %s %s", ovs_ofctl_prog, ovs_ofctl_cmd, ofconf->bridge, flow_entry, strict_param);
    if (strexa(ovs_ofctl_prog, ovs_ofctl_cmd, ofconf->bridge, flow_entry, strict_param) == NULL) {
        LOGE("Flow entry del failed: %s %s %s %s %s", ovs_ofctl_prog, ovs_ofctl_cmd, ofconf->bridge, flow_entry, strict_param);
        return false;
    }

    target_om_hook(TARGET_OM_POST_DEL, ofconf->rule);

    return true;
}
