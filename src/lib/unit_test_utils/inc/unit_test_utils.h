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

#ifndef UNIT_TEST_UTILS_H_INCLUDED
#define UNIT_TEST_UTILS_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "net_header_parse.h"

/*
 * @brief Creates the folder that will be used to store
 *        the pcap file
 *
 * @remark Ideally this should be called from within a setUp()
 *         function
 */
void ut_prepare_pcap(const char *test_name);

/*
 * @brief Cleans up latest created test environment.
 *
 * This will attempt to delete pcap files and containing folder
 * (unless disa led with ut_keep_temp_folder())
 */
void ut_cleanup_pcap(void);

/*
 * @brief MACRO to hide some of the parameter details
 *        to ut_create_pcap_payload()
 * The macro will pick up the name of the variable as a string
 * and pass it to the function.
 */
#define UT_CREATE_PCAP_PAYLOAD(pcap, parser) \
    ut_create_pcap_payload(#pcap, pcap, sizeof(pcap), parser);

/*
 * @brief Populates the parser and dump the array into a file
 *
 * @remark User should use the macro UT_CREATE_PCAP_PAYLOAD
 */
void ut_create_pcap_payload(const char *pkt_name,
                            const uint8_t pkt[], size_t len,
                            struct net_header_parser *parser);

/*
 * @brief Enable/Disable temp folder cleanup
 *
 * By default, all the temporary files and folders get deleted from
 * storage. This function will allow the keeping of temporarily
 * created files.
 *
 * @remark It should mainly only be used during development/coding)
 */
void ut_keep_temp_folder(bool f);

/*
 * @brief Configure setUp and tearDown for unity framework
 *
 * If this is not called, the UT will use a default pair of setUp()/tearDown()
 * that do not perform anything (NO-OP)
 */
void ut_setUp_tearDown(const char* test_name, void (*setup)(void), void (*teardown)(void));

/*
 * @brief Generic initialization of a unit-test executable
 */
void ut_init(const char *ut_name, void (*global_ut_init)(void), void (*global_ut_exit)(void));

/*
 * @brief Generic cleanup before terminating a UT
 *
 * @remark this is going to be added to at_exit() so it will be called
 *         even in the case of a failing test.
 */
int ut_fini(void);

#endif /* UNIT_TEST_UTILS_H_INCLUDED */
