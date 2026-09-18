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
 * test_showcase.c — dm_schema API usage showcase
 *
 * Demonstrates each API using a single path throughout:
 *   Device.WiFi.AccessPoint.{i}.AssociatedDevice.{i}.Active
 */

#include <stdio.h>
#include "dm_schema.h"

#define TITLE(title) printf("\n--- " title " ---\n")

static void print(const char *name, const char *val)
{
    printf("%-17s: %s\n", name, val ? val : "(null)");
}

void test_showcase(void)
{
    const char *str;
    /* ------------------------------------------------------------------ */
    TITLE("Convenience macros");

    str = DM_SCHEMA(Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active);
    print("DM_SCHEMA", str);

    str = DM_NAME(Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active);
    print("DM_NAME", str);

    str = DM_FMT_AT(
            Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active,
            DM_AT(Device.WiFi.AccessPoint.i, 2),
            DM_AT(Device.WiFi.AccessPoint.i.AssociatedDevice.i, 5));
    print("DM_FMT_AT", str);

    str = DM_FMT(Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active, 2, 5);
    print("DM_FMT", str);

    /* ------------------------------------------------------------------ */
    /* API usage */
    dm_schema_node_t path = DM(Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active);
    dm_schema_inst_t ap = DMI(Device.WiFi.AccessPoint.i);
    dm_schema_inst_t ad = DMI(Device.WiFi.AccessPoint.i.AssociatedDevice.i);

    char buf[DM_SCHEMA_BUF_SIZE];

    /* ------------------------------------------------------------------ */
    TITLE("Template and node name");

    str = dm_schema_str(path);
    print("path_str", str);

    str = dm_schema_str_buf(buf, sizeof(buf), path);
    print("path_str_buf", str);

    str = dm_node_name(path);
    print("node_name", str);

    /* ------------------------------------------------------------------ */
    TITLE("Heap path — dm_schema_new / set_instance / str / delete");

    dm_schema_t *p = dm_schema_new(path);
    dm_schema_set_instance(p, ap, 2);
    dm_schema_set_instance(p, ad, 5);
    str = dm_schema_fmt(p);
    print("fmt", str);
    dm_schema_delete(p);

    /* ------------------------------------------------------------------ */
    TITLE("String instance — dm_schema_set_instance_str");

    p = dm_schema_new(path);
    dm_schema_set_instance_str(p, ap, "home");
    dm_schema_set_instance(p, ad, 1);
    str = dm_schema_fmt(p);
    print("fmt (alias+int)", str);
    dm_schema_delete(p);

    /* ------------------------------------------------------------------ */
    TITLE("Explicit-slot formatter — dm_schema_fmt_at / dm_schema_fmt_at_buf");

    str = dm_schema_fmt_at(path, dm_at(ap, 2), dm_at(ad, 5), DM_AT_END);
    print("fmt_at", str);

    str = dm_schema_fmt_at_buf(buf, sizeof(buf), path, dm_at(ap, 2), dm_at(ad, 5), DM_AT_END);
    print("fmt_at_buf", str);

    /* ------------------------------------------------------------------ */
    TITLE("Positional formatter — dm_schema_fmt_pos / dm_schema_fmt_pos_buf");

    str = dm_schema_fmt_pos(path, 2, 5, DM_FMT_END);
    print("fmt_pos", str);

    str = dm_schema_fmt_pos_buf(buf, sizeof(buf), path, 2, 5, DM_FMT_END);
    print("fmt_pos_buf", str);

    printf("\n");
}
