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

#ifndef DATA_REPORT_TAGS_H_INCLUDED
#define DATA_REPORT_TAGS_H_INCLUDED

#include "os_types.h"
#include "ds_tree.h"
#include "policy_tags.h"
#include "ovsdb_utils.h"

/**
 * @brief releases all resources the library may hold
 */
void
data_report_tags_exit(void);


/**
 * @brief initialize the library
 */
void
data_report_tags_init(void);


/**
 * @brief Openflow tag updates notification callback
 *
 * To be passed to om_tag_init() or called within the routine passed to om_tag_init()
 */
bool
data_report_tags_update_cb(om_tag_t *tag,
                           struct ds_tree *removed,
                           struct ds_tree *added,
                           struct ds_tree *updated);


/**
 * @brief returns the report tags assigned to a device
 *
 * @param mac the device id
 *
 * Returns the report tags assigned to a device,
 * or NULL if none is assigned.
 */
struct str_set *
data_report_tags_get_tags(os_macaddr_t *mac);

#endif /* DATA_REPORT_TAGS_H_INCLUDED */
