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

#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "network_zone.h"
#include "network_zone_internals.h"
#include "log.h"
#include "memutil.h"
#include "target.h"
#include "unity.h"
#include "os_nif.h"
#include "policy_tags.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "test_network_zone.h"
#include "unit_test_utils.h"

static void
test_network_zone_get_mac_type(void)
{
    struct test_network_zone_mac
    {
        struct nz_cache_entry nz;
        struct network_zone_mac mac_in;
        char *expected;
    } in[] =
    {
        {
            .nz =
            {
                .name = "nz_1",
                .priority = 1,
            },
            .mac_in =
            {
                .type = NZ_TAG,
                .val = "${tag_1}",
            },
            .expected = "tag_1",
        },
        {
            .nz =
            {
                .name = "nz_1",
                .priority = 1,
            },
            .mac_in =
            {
                .type = NZ_TAG,
                .val = "${@tag_2}",
            },
            .expected = "tag_2",
        },
        {
            .nz =
            {
                .name = "nz_1",
                .priority = 1,
            },
            .mac_in =
            {
                .type = NZ_TAG,
                .val = "${*tag_3}",
                .nz_name = "nz_1",
                .priority = 1,
            },
            .expected = "tag_3",
        },
        {
            .nz =
            {
                .name = "nz_1",
                .priority = 1,
            },
            .mac_in =
            {
                .type = NZ_TAG,
                .val = "${#tag_4}",
            },
            .expected = "tag_4",
        },
        {
            .nz =
            {
                .name = "nz_2",
                .priority = 2,
            },
            .mac_in =
            {
                .type = NZ_GTAG,
                .val = "$[gtag_1]",
            },
            .expected = "gtag_1",
        },
        {
            .nz =
            {
                .name = "nz_2",
                .priority = 2,
            },
            .mac_in =
            {
                .type = NZ_GTAG,
                .val = "$[@gtag_2]",
            },
            .expected = "gtag_2",
        },
        {
            .nz =
            {
                .name = "nz_2",
                .priority = 2,
            },
            .mac_in =
            {
                .type = NZ_GTAG,
                .val = "$[*gtag_3]",
            },
            .expected = "gtag_3",
        },
        {
            .nz =
            {
                .name = "nz_2",
                .priority = 2,
            },
            .mac_in =
            {
                .type = NZ_GTAG,
                .val = "$[#gtag_4]",
            },
            .expected = "gtag_4",
        },
        {
            .nz =
            {
                .name = "nz_3",
                .priority = 3,
            },
            .mac_in =
            {
                .type = NZ_MAC,
                .val = "22:33:44:55:66:77",
            },
            .expected = "22:33:44:55:66:77",
        },
    };

    struct network_zone_mac out;
    size_t nelems;
    size_t i;

    nelems = ARRAY_SIZE(in);
    for (i = 0; i < nelems; i++)
    {
        network_zone_get_mac(&in[i].nz, in[i].mac_in.val, &out);
        TEST_ASSERT_EQUAL(in[i].mac_in.type, out.type);
        TEST_ASSERT_EQUAL_STRING(in[i].expected, out.val);
        TEST_ASSERT_EQUAL(in[i].nz.priority, out.priority);
        TEST_ASSERT_EQUAL_STRING(in[i].nz.name, out.nz_name);
    }
}

/**
 * @brief Test the addition and removal zones for a given device
 *
 * Add 3 zone entries for a device
 * Validate that the higher priority zone is returned when requested
 * Delete each zone, validate the overall zone priority
 */
void
test_network_add_remove_macs(void)
{
    struct nz_device_entry *entry;
    struct network_zone_mgr *mgr;
    struct network_zone_mac *mac;
    os_macaddr_t lookup_mac;
    char *effective_zone;
    size_t nelems;
    size_t i;
    bool ret;

    struct network_zone_mac macs_array[] =
    {
        { /* 0 */
            .nz_name = "ut_test_add_mac_1",
            .priority = 1,
            .type = NZ_MAC,
            .val = "11:22:33:44:55:66",
        },
        { /* 1 */
            .nz_name = "ut_test_add_mac_3",
            .priority = 3,
            .type = NZ_MAC,
            .val = "11:22:33:44:55:66",
        },
        { /* 2 */
            .nz_name = "ut_test_add_mac_2",
            .priority = 2,
            .type = NZ_MAC,
            .val = "11:22:33:44:55:66",
        },
    };

    /* Convert the mac string to a byte array for lookup purposes */
    mac = &macs_array[0];
    ret = os_nif_macaddr_from_str(&lookup_mac, mac->val);
    TEST_ASSERT_TRUE(ret);

    nelems = ARRAY_SIZE(macs_array);
    for (i = 0; i < nelems; i++)
    {
        mac = &macs_array[i];
        ret = network_zone_add_mac(mac);
        TEST_ASSERT_TRUE(ret);
    }

    /* Validate the entry effective zone (zone 3) */
    effective_zone = network_zone_get_zone(&lookup_mac);
    mac = &macs_array[1]; /* zone 3 */
    TEST_ASSERT_EQUAL_STRING(mac->nz_name, effective_zone);

    /* Remove zone 3 entry */
    mac = &macs_array[1]; /* zone 3 */
    network_zone_delete_mac(mac);

    /* Validate the new zone */
    effective_zone = network_zone_get_zone(&lookup_mac);
    mac = &macs_array[2]; /* zone 2 */
    TEST_ASSERT_EQUAL_STRING(mac->nz_name, effective_zone);

    /* Remove zone 1 entry */
    mac = &macs_array[0]; /* zone 1 */
    network_zone_delete_mac(mac);

    /* Validate the zone, it still should be zone 2 */
    effective_zone = network_zone_get_zone(&lookup_mac);
    mac = &macs_array[2]; /* zone 2 */
    TEST_ASSERT_EQUAL_STRING(mac->nz_name, effective_zone);

    /* Remove zone 2 entry */
    mac = &macs_array[2]; /* zone 2 */
    network_zone_delete_mac(mac);

    /* Validate that no zone is found */
    effective_zone = network_zone_get_zone(&lookup_mac);
    TEST_ASSERT_NULL(effective_zone);

    /* Validate that the device cache entry is gone */
    mgr = network_zone_get_mgr();
    entry = ds_tree_find(&mgr->device_cache, &lookup_mac);
    TEST_ASSERT_NULL(entry);
}


void
run_network_zone_routines(void)
{
    RUN_TEST(test_network_zone_get_mac_type);
    RUN_TEST(test_network_add_remove_macs);
}
