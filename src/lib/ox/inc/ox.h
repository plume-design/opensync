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

#ifndef OX_H_INCLUDED
#define OX_H_INCLUDED

/**
 * OX - OVSDB Translator engine
 *
 * Purpose:
 *
 * Facilitate mostly declarative way of mapping between OVSDB and TR-181.
 *
 * Observe OVSDB insertions/deletions of configured tables can be make to yield
 * automatic spawning/destruction of TR-181 instances.
 *
 * The library is intended to not be all-encompassing, but rather to be a
 * building block for users to build on top of. The os_tr181_handle is
 * intentionally exposed to users, so they can make custom calls against it,
 * eg. to add methods, or more complex parameters.
 *
 * The DM routes are made public so that it is possible for easy migration of
 * behavior to different program(s). For example Device.WiFi is planned to
 * eventually live in a separate process where extra logic can be added, but
 * the basic mapping between OVSDB and TR-181 can be reused with minimal
 * changes.
 *
 * The library avoids using void casting as much as possible in favor of
 * explicitly listed variants, even if they are mostly boilerplate, or can
 * "overlap". Tagged enums aren't enforcable unfortunately, so care needs to be
 * taken when constructing data.
 *
 */

///
/// Major unresolved problems
///
/// # Bridging.Bridge.*.Port.*
///
/// The Port.*.Enable=0 cannot be represented in OVSDB. There's no "enabled"
/// column, and Port entry _cannot_ exist without being referenced in Bridge
/// table (strong ref). Insert works, but Port pops in, and out of, existence
/// immediatelly.
///
/// This means ox_table_instance would need to outlive OVSDB row lifetime. That
/// isn't possible _now_.
///
/// Moreover, implementing Bridging requires mapping DM paths (eg.
/// Device.WiFi.AccessPoint.2) to actual netdevs that OVSDB (Port, Bridge,
/// Interface, Wifi_VIF_Config) undertstands.
///
/// Naive mapping to <Foo>.Name may not work, and will not work (well) if we
/// try to abstract Device.WiFi.EndPoint.1 to support uniformly both MultiAP
/// (4addr) mode and GRE.
///
///
/// # Instance numbering persistence
///
/// TR-369 requires all instances to maintain their identity, and therefore
/// path, including indice numbers.
///
/// OVSDB does not have any concept of instance numbering, and rows are
/// identified by UUID. Moreover, we don't even treat OVSDB conf.db as
/// persistent. There's 0 persistency in Opensync OVSDB.
///
/// To solve this persistence would need to be introduced. Either by extending
/// OVSDB tables with instance numbers, and persisting conf.db itself, or by
/// storing the instance numbers and _some_ arbitrary, per-table identifiers
/// out-of-band and then use it as reference when processing OVSDB update
/// events.
///

#include <ox_dm_deviceinfo.h>
#include <ox_dm_wifi.h>
#include <ox_ovsdb_monitor.h>
#include <ox_ovsdb_row.h>
#include <ox_ovsdb_type.h>
#include <ox_route.h>
#include <ox_router.h>
#include <ox_table_instance.h>
#include <ox_table_singleton.h>
#include <ox_types.h>
#include <ox_util.h>

#endif /* OX_H_INCLUDED */
