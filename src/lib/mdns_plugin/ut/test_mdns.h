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

#ifndef TEST_MDNS_H_INCLUDED
#define TEST_MDNS_H_INCLUDED

#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "qm_conn.h"

#include "mdns_plugin.h"
#include "mdns_records.h"

typedef struct
{
    bool                        initialized;
    bool                        has_qm;
    char                       *f_name;
    mdns_records_report_data_t  report;
} mdns_records_test_mgr;

/***********************************************************************************************************/

extern void     emit_report(packed_buffer_t *pb);
extern void     setup_mdns_records_report(void);
extern void     teardown_mdns_records_report(void);

extern void     test_serialize_node_info(void);
extern void     test_serialize_node_info_no_field_set(void);
extern void     test_serialize_node_info_null_ptr(void);

extern void     test_serialize_observation_window(void);
extern void     test_serialize_observation_window_null_ptr(void);
extern void     test_serialize_observation_window_no_field_set(void);

extern void     test_serialize_record(void);
extern void     test_set_records(void);

extern void     test_serialize_client(void);
extern void     test_set_serialization_clients(void);

extern void     test_serialize_report(void);
extern void     test_Mdns_Records_Report(void);

extern void     test_mdns_records_send_records(void);

/***********************************************************************************************************/

#endif /* TEST_MDNS_H_INCLUDED */
