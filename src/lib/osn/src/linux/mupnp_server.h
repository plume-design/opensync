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

#ifndef MUPNP_SERVER_H_INCLUDED
#define MUPNP_SERVER_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>

/**
 * Command used to execute the miniupnpd firewall script (iptscript.sh)
 *
 * - $1 is the action to perform, see iptscript.sh for details
 */
#define MUPNP_IPTSCRIPT_CMD _S(/etc/miniupnpd/iptscript.sh $1)

/**
 * @brief Interface of miniupnpd daemon service which
 * complies with generic upnp server interface defined in
 * upnp_server.h
 */

/**
 * @brief Declaration of miniupnpd server object type
 */
typedef struct upnp_server mupnp_server_t;

/**
 * @brief Definition of miniupnpd server configuration structure
 */
typedef struct mupnp_config
{
    const char *name; //< upnp server name
    const char *config_dir_path; //< config file directory path
    bool enable_natpmp; //< enable NAT-PMP support
    bool secure_mode; //< allow clients to add mappings only to their own IP
    bool system_uptime; //< report system uptime instead of service uptime
    const char **perm_rules; //< NULL terminated array of permission rules
    unsigned http_port; //< HTTP port for UPnP device description and SOAP (0 for autoselect)
    const char *upnp_forward_chain; //< UPnP forward chain name or NULL when not provided
    const char *upnp_nat_chain; //< UPnP nat chain name or NULL when not provided

} mupnp_config_t;

mupnp_server_t *mupnp_server_new(const mupnp_config_t *cfg);
void mupnp_server_del(mupnp_server_t *self);

bool mupnp_server_attach_external(mupnp_server_t *self, const char *ifname);
bool mupnp_server_attach_external6(mupnp_server_t *self, const char *ifname);
bool mupnp_server_attach_internal(mupnp_server_t *self, const char *ifname);
bool mupnp_server_detach(mupnp_server_t *self, const char *ifname);

bool mupnp_server_start(mupnp_server_t *self);
bool mupnp_server_stop(mupnp_server_t *self);
bool mupnp_server_started(const mupnp_server_t *self);

#endif /* MUPNP_SERVER_H_INCLUDED */
