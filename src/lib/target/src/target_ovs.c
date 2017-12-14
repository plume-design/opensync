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

#include <stdio.h>
#include <stdbool.h>

#include "schema.h"
#include "os.h"
#include "log.h"

bool target_om_add_flow(char *token, struct schema_Openflow_Config *ofconf)
{
    char    flow_entry[512];
    bool    ret = false;

    snprintf( flow_entry, sizeof( flow_entry ),
              "ovs-ofctl add-flow %s \"table=%d,priority=%d%s%s,actions=%s\"",
              ofconf->bridge, ofconf->table, ofconf->priority,
              strlen(ofconf->rule) > 0 ? "," : "",
              ofconf->rule, ofconf->action );

    // Execute ovs-ofctl to add the flow
    // cmd_log returns 0 on success
    ret = !cmd_log( flow_entry );

    if( !ret ) {
        LOGE( "Flow entry add failed: %s", flow_entry );
    }

    return ret;
}

bool target_om_del_flow(char *token, struct schema_Openflow_Config *ofconf)
{
    char    flow_entry[512];
    bool    ret = false;

    snprintf( flow_entry, sizeof( flow_entry ),
              "ovs-ofctl del-flows %s \"table=%d,priority=%d%s%s\" --strict",
              ofconf->bridge, ofconf->table, ofconf->priority,
              strlen( ofconf->rule ) > 0 ? "," : "",
              ofconf->rule );

    // Execute ovs-ofctl to del the flow
    // cmd_log returns 0 on success
    ret = !cmd_log( flow_entry );

    if( !ret ) {
        LOGE( "Flow entry del failed: %s", flow_entry );
    }

    return ret;
}

