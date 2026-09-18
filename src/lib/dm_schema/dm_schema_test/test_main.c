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
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "memutil.h"
#include "dm_schema.h"

void test_unit(void)
{
    /* --- basic node access --- */
    dm_schema_node_t fw = DM(Device.DeviceInfo.FirmwareImage);
    dm_schema_node_t fw_i = DM(Device.DeviceInfo.FirmwareImage.i);
    dm_schema_node_t ver = DM(Device.DeviceInfo.SoftwareVersion);

    printf("FirmwareImage : type=%d name=%s idx=%d\n", dm_node(fw)->type, dm_node_name(fw), fw.idx);
    printf("FirmwareImage.i: type=%d name=%s idx=%d\n", dm_node(fw_i)->type, dm_node_name(fw_i), fw_i.idx);
    printf("SoftwareVersion: type=%d name=%s idx=%d\n", dm_node(ver)->type, dm_node_name(ver), ver.idx);

    dm_schema_inst_t fi = DMI(Device.DeviceInfo.FirmwareImage.i);
    printf("inst.idx == i.node.idx: %d\n", fi.idx == fw_i.idx);

    /* --- dm_schema_str --- */
    const char *tmpl;

    tmpl = dm_schema_str(DM(Device.DeviceInfo.FirmwareImage.i.Status));
    printf("template: %s\n", tmpl);
    assert(strcmp(tmpl, "Device.DeviceInfo.FirmwareImage.{i}.Status") == 0);

    tmpl = dm_schema_str(DM(Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active));
    printf("template: %s\n", tmpl);
    assert(strcmp(tmpl, "Device.WiFi.AccessPoint.{i}.AssociatedDevice.{i}.Active") == 0);

    /* --- dm_schema_fmt_at — single instance --- */
    const char *path;

    path = DM_FMT_AT(Device.DeviceInfo.FirmwareImage.i.Status, DM_AT(Device.DeviceInfo.FirmwareImage.i, 2));
    printf("fmt: %s\n", path);
    assert(strcmp(path, "Device.DeviceInfo.FirmwareImage.2.Status") == 0);

    /* --- dm_schema_fmt_at — double instance --- */
    path = DM_FMT_AT(
            Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active,
            DM_AT(Device.WiFi.AccessPoint.i, 3),
            DM_AT(Device.WiFi.AccessPoint.i.AssociatedDevice.i, 7));
    printf("fmt: %s\n", path);
    assert(strcmp(path, "Device.WiFi.AccessPoint.3.AssociatedDevice.7.Active") == 0);

    /* --- dm_schema_new / set_instance / str / delete --- */
    dm_schema_t *p = dm_schema_new(DM(Device.DeviceInfo.FirmwareImage.i.Name));
    assert(p != NULL);

    /* str returns NULL if instance not yet set */
    assert(dm_schema_fmt(p) == NULL);

    dm_schema_set_instance(p, DMI(Device.DeviceInfo.FirmwareImage.i), 1);
    path = dm_schema_fmt(p);
    printf("str: %s\n", path);
    assert(strcmp(path, "Device.DeviceInfo.FirmwareImage.1.Name") == 0);
    dm_schema_delete(p);

    /* --- vendor extension --- */
    tmpl = dm_schema_str(DM(Device.DeviceInfo.FirmwareImage.i.X_OPENSYNC_DecryptionPassword));
    printf("vendor template: %s\n", tmpl);
    assert(strcmp(tmpl, "Device.DeviceInfo.FirmwareImage.{i}.X_OPENSYNC_DecryptionPassword") == 0);

    /* --- dm_schema_set_instance_str — alias --- */
    p = dm_schema_new(DM(Device.DeviceInfo.FirmwareImage.i.Status));
    assert(p != NULL);
    dm_schema_set_instance_str(p, DMI(Device.DeviceInfo.FirmwareImage.i), "inactive");
    path = dm_schema_fmt(p);
    printf("str alias: %s\n", path);
    assert(strcmp(path, "Device.DeviceInfo.FirmwareImage.inactive.Status") == 0);
    dm_schema_delete(p);

    /* --- DM_AT_S — alias via fmt --- */
    path = DM_FMT_AT(Device.DeviceInfo.FirmwareImage.i.Status, DM_AT_S(Device.DeviceInfo.FirmwareImage.i, "active"));
    printf("fmt alias: %s\n", path);
    assert(strcmp(path, "Device.DeviceInfo.FirmwareImage.active.Status") == 0);

    /* --- DM_AT_S — wildcard --- */
    path = DM_FMT_AT(
            Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active,
            DM_AT(Device.WiFi.AccessPoint.i, 3),
            DM_AT_S(Device.WiFi.AccessPoint.i.AssociatedDevice.i, "*"));
    printf("fmt wildcard: %s\n", path);
    assert(strcmp(path, "Device.WiFi.AccessPoint.3.AssociatedDevice.*.Active") == 0);

    /* --- negative: not all instances set → NULL --- */
    p = dm_schema_new(DM(Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active));
    assert(p != NULL);
    dm_schema_set_instance(p, DMI(Device.WiFi.AccessPoint.i), 3);
    /* AssociatedDevice.i not set yet */
    assert(dm_schema_fmt(p) == NULL);
    dm_schema_delete(p);

    /* --- negative: set_instance returns false for non-ancestor --- */
    p = dm_schema_new(DM(Device.DeviceInfo.FirmwareImage.i.Status));
    assert(p != NULL);
    assert(!dm_schema_set_instance(p, DMI(Device.WiFi.AccessPoint.i), 5));
    assert(!dm_schema_set_instance_str(p, DMI(Device.WiFi.AccessPoint.i), "bad"));
    dm_schema_delete(p);

    /* --- edge: leaf with no {i} ancestors renders without any set_instance --- */
    p = dm_schema_new(DM(Device.DeviceInfo.SoftwareVersion));
    assert(p != NULL);
    path = dm_schema_fmt(p);
    assert(strcmp(path, "Device.DeviceInfo.SoftwareVersion") == 0);
    dm_schema_delete(p);

    /* --- override: re-setting a slot replaces previous value --- */
    p = dm_schema_new(DM(Device.DeviceInfo.FirmwareImage.i.Status));
    assert(p != NULL);
    dm_schema_set_instance(p, DMI(Device.DeviceInfo.FirmwareImage.i), 2);
    assert(strcmp(dm_schema_fmt(p), "Device.DeviceInfo.FirmwareImage.2.Status") == 0);
    dm_schema_set_instance_str(p, DMI(Device.DeviceInfo.FirmwareImage.i), "inactive");
    assert(strcmp(dm_schema_fmt(p), "Device.DeviceInfo.FirmwareImage.inactive.Status") == 0);
    dm_schema_set_instance(p, DMI(Device.DeviceInfo.FirmwareImage.i), 1);
    assert(strcmp(dm_schema_fmt(p), "Device.DeviceInfo.FirmwareImage.1.Status") == 0);
    dm_schema_delete(p);

    /* --- DM_SCHEMA / DM_NAME convenience macros --- */
    assert(strcmp(DM_SCHEMA(Device.DeviceInfo.FirmwareImageNumberOfEntries),
                  "Device.DeviceInfo.FirmwareImageNumberOfEntries")
           == 0);
    assert(strcmp(DM_SCHEMA(Device.DeviceInfo.FirmwareImage.i.Name), "Device.DeviceInfo.FirmwareImage.{i}.Name") == 0);
    assert(strcmp(DM_NAME(Device.DeviceInfo.FirmwareImageNumberOfEntries), "FirmwareImageNumberOfEntries") == 0);
    assert(strcmp(DM_NAME(Device.DeviceInfo.FirmwareImage.i.Name), "Name") == 0);

    /* --- DM_FMT — positional integers --- */
    path = DM_FMT(Device.DeviceInfo.FirmwareImage.i.Name, 1);
    assert(strcmp(path, "Device.DeviceInfo.FirmwareImage.1.Name") == 0);

    path = DM_FMT(Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active, 2, 3);
    assert(strcmp(path, "Device.WiFi.AccessPoint.2.AssociatedDevice.3.Active") == 0);

    /* fewer args than instances → unset slot → NULL */
    path = DM_FMT(Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active, 2);
    assert(path == NULL);

    printf("DM_FMT: ok\n");

    /* --- trailing dot by node type (TR181 §3.6.1) --- */

    /* object → trailing dot */
    assert(strcmp(dm_schema_str(DM(Device.DeviceInfo)), "Device.DeviceInfo.") == 0);
    /* table → trailing dot */
    assert(strcmp(dm_schema_str(DM(Device.DeviceInfo.FirmwareImage)), "Device.DeviceInfo.FirmwareImage.") == 0);
    /* instance → trailing dot */
    assert(strcmp(dm_schema_str(DM(Device.DeviceInfo.FirmwareImage.i)), "Device.DeviceInfo.FirmwareImage.{i}.") == 0);
    /* parameter → no trailing dot */
    assert(strcmp(dm_schema_str(DM(Device.DeviceInfo.FirmwareImage.i.Status)),
                  "Device.DeviceInfo.FirmwareImage.{i}.Status")
           == 0);
    /* method → no trailing dot */
    assert(strcmp(dm_schema_str(DM(Device.DeviceInfo.FirmwareImage.i.Download)),
                  "Device.DeviceInfo.FirmwareImage.{i}.Download()")
           == 0);

    printf("trailing dot dm_schema_str: ok\n");

    /* dm_schema_fmt: table leaf (no instances in path) → trailing dot */
    {
        dm_schema_t *td = dm_schema_new(DM(Device.DeviceInfo.FirmwareImage));
        assert(strcmp(dm_schema_fmt(td), "Device.DeviceInfo.FirmwareImage.") == 0);
        dm_schema_delete(td);
    }

    /* dm_schema_fmt: instance leaf → formatted instance with trailing dot */
    path = dm_schema_fmt_at(
            DM(Device.DeviceInfo.FirmwareImage.i),
            DM_AT(Device.DeviceInfo.FirmwareImage.i, 1),
            DM_AT_END);
    assert(strcmp(path, "Device.DeviceInfo.FirmwareImage.1.") == 0);

    /* dm_schema_fmt: parameter leaf → no trailing dot (regression) */
    path = dm_schema_fmt_at(
            DM(Device.DeviceInfo.FirmwareImage.i.Status),
            DM_AT(Device.DeviceInfo.FirmwareImage.i, 2),
            DM_AT_END);
    assert(strcmp(path, "Device.DeviceInfo.FirmwareImage.2.Status") == 0);

    printf("trailing dot dm_schema_fmt: ok\n");

    /* --- no-dot variants — suppress trailing dot --- */

    /* DM_FMT_AT_NO_DOT: instance leaf → no trailing dot */
    path = DM_FMT_AT_NO_DOT(Device.DeviceInfo.FirmwareImage.i, DM_AT(Device.DeviceInfo.FirmwareImage.i, 1));
    assert(strcmp(path, "Device.DeviceInfo.FirmwareImage.1") == 0);

    /* DM_FMT_NO_DOT: instance leaf → no trailing dot */
    path = DM_FMT_NO_DOT(Device.DeviceInfo.FirmwareImage.i, 2);
    assert(strcmp(path, "Device.DeviceInfo.FirmwareImage.2") == 0);

    /* dm_schema_set_no_dot: explicit path object */
    {
        dm_schema_t *nd = dm_schema_new(DM(Device.DeviceInfo.FirmwareImage.i));
        dm_schema_set_instance(nd, DMI(Device.DeviceInfo.FirmwareImage.i), 4);
        dm_schema_set_no_dot(nd);
        assert(strcmp(dm_schema_fmt(nd), "Device.DeviceInfo.FirmwareImage.4") == 0);
        dm_schema_delete(nd);
    }

    /* parameter leaf: no_dot has no effect (parameters never have trailing dot) */
    path = DM_FMT_AT_NO_DOT(Device.DeviceInfo.FirmwareImage.i.Status, DM_AT(Device.DeviceInfo.FirmwareImage.i, 5));
    assert(strcmp(path, "Device.DeviceInfo.FirmwareImage.5.Status") == 0);

    printf("no-dot variants: ok\n");

    /* --- buffer overflow → NULL --- */
    char small[10];
    assert(dm_schema_str_buf(small, sizeof(small), DM(Device.DeviceInfo.SoftwareVersion)) == NULL);
    assert(dm_schema_fmt_pos_buf(small, sizeof(small), DM(Device.DeviceInfo.FirmwareImage.i.Name), 1, DM_FMT_END)
           == NULL);

    printf("overflow → NULL: ok\n");

    /* --- dm_schema_parse_inst_buf --- */
    {
        char ibuf[64];
        const char *multi = "Device.WiFi.AccessPoint.home2G.AssociatedDevice.4";

        /* positive: extract first instance */
        const char *i_ap = dm_schema_parse_inst_buf(ibuf, sizeof(ibuf), DMI(Device.WiFi.AccessPoint.i), multi);
        assert(i_ap != NULL && strcmp(i_ap, "home2G") == 0);

        /* positive: extract second instance */
        char ibuf2[64];
        const char *i_ad;
        i_ad = dm_schema_parse_inst_buf(ibuf2, sizeof(ibuf2), DMI(Device.WiFi.AccessPoint.i.AssociatedDevice.i), multi);
        assert(i_ad != NULL && strcmp(i_ad, "4") == 0);

        /* positive: _dup variant */
        char *dup_ap = dm_schema_parse_inst_dup(DMI(Device.WiFi.AccessPoint.i), multi);
        char *dup_ad = dm_schema_parse_inst_dup(DMI(Device.WiFi.AccessPoint.i.AssociatedDevice.i), multi);
        assert(dup_ap != NULL && strcmp(dup_ap, "home2G") == 0);
        assert(dup_ad != NULL && strcmp(dup_ad, "4") == 0);
        FREE(dup_ap);
        FREE(dup_ad);

        /* negative: schema mismatch — "Radio" != "AccessPoint" */
        const char *bad = "Device.WiFi.Radio.1";
        assert(dm_schema_parse_inst_buf(ibuf, sizeof(ibuf), DMI(Device.WiFi.AccessPoint.i), bad) == NULL);
    }

    printf("dm_schema_parse_inst: ok\n");

    printf("\nAll assertions passed.\n");
}

/* Forward declarations for the other test modules */
void test_dump(void);
void test_count(void);
void test_showcase(void);

int main(int argc, char *argv[])
{
    const char *cmd = (argc > 1) ? argv[1] : "--unit";

    if (strcmp(cmd, "--unit") == 0)
        test_unit();
    else if (strcmp(cmd, "--dump") == 0)
        test_dump();
    else if (strcmp(cmd, "--count") == 0)
        test_count();
    else if (strcmp(cmd, "--showcase") == 0)
        test_showcase();
    else
    {
        fprintf(stderr, "usage: test_main [--unit|--dump|--count|--showcase]\n");
        return 1;
    }
    return 0;
}
