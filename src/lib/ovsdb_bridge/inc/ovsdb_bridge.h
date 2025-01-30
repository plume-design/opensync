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

#ifndef OVSDB_BRIDGE_H_INCLUDED
#define OVSDB_BRIDGE_H_INCLUDED

#include <stdbool.h>

/**
 * @brief Creates default Interface, Port and Bridge tables.
 *
 * @param br_name   Name of bridge we are trying to create
 *
 * @return true on success or if bridge already exists, false on failure
 */
bool ovsdb_bridge_create(const char *br_name);

/**
 * @brief Manages updates to OVSDB tables whenever adding a port to a bridge
 *        or removing a port from a bridge. Interacts with Interface, Port and
 *        Bridge tables.
 *
 * @param port_name  Name of port we are trying to manage
 * @param br_name    Name of bridge we are managing
 * @param add        Flag to indicate that we are adding the port to a bridge if
 *                   set to true or removing it if set to false
 *
 * @return true on success or if desired state is already achieved, false on failure
 */
bool ovsdb_bridge_manage_port(const char *port_name, const char *br_name, bool add);

/**
 * @brief Interrogates OVSDB whether given port is in a bridge, and if
 *        it is, returns the bridge name.
 *
 * @param port_name  Name of port we are trying to manage
 *
 * @return NULL when port_name is not in any bridge. non-NULL of the
 *         bridge name otherwise. Remember to FREE(). This is
 *         heap-allocated.
 */
char *ovsdb_bridge_port_get_bridge(const char *port_name);

#endif /* OVSDB_BRIDGE_H_INCLUDED */
