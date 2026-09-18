# dm_schema — TR181 Schema Utilities

A compile-time-verified, auto-complete-friendly way to construct TR181 data
model paths in C. Replaces ad-hoc string literals and `snprintf` constructions
with type-safe, schema-backed expressions.

---

## Quick start

```c
#include "dm_schema.h"

/* Compile-time verified path → template string */
const char *tmpl = DM_SCHEMA(Device.DeviceInfo.FirmwareImage.i.Name);
/* → "Device.DeviceInfo.FirmwareImage.{i}.Name" */

/* One-shot with instance index */
const char *path = DM_FMT(Device.DeviceInfo.FirmwareImage.i.Name, 2);
/* → "Device.DeviceInfo.FirmwareImage.2.Name" */
```

If `Device.DeviceInfo.FirmwareImage.i.Name` doesn't exist in the schema the
code **will not compile**. For example, misspelling `WiFi` as `Wifi`:

```
test.c:24: error: 'dm_Device_t' has no member named 'Wifi'; did you mean 'WiFi'?
    str = DM_SCHEMA(Device.Wifi.AccessPoint.i.AssociatedDevice.i.Active);
                         ^~~~
```

---

## Building

```sh
# Regenerate schema from XML (after editing vendor_opensync.xml or upgrading TR181 XML)
make TARGET=native src/lib/dm_schema/generate
```

The generator (`gen_dm_tr181_schema.py`) reads:
- `tr181-schema/tr-181-2-20-1-usp-full.xml` — standard TR181 schema
- `tr181-schema/vendor_opensync.xml` — OpenSync vendor extensions

Output goes to `inc/dm_tr181_schema.h` and `src/dm_tr181_schema.c`.

---

## Core concepts

### Node handle types

```c
typedef struct { uint16_t idx; } dm_schema_node_t;  /* any node   */
typedef struct { uint16_t idx; } dm_schema_inst_t;  /* {i} node only */
```

These are distinct types — passing a `dm_schema_node_t` where a
`dm_schema_inst_t` is expected is a **compile error**.

### Navigation macros

```c
DM(Device.DeviceInfo.SoftwareVersion)         /* → dm_schema_node_t */
DMI(Device.DeviceInfo.FirmwareImage.i)        /* → dm_schema_inst_t */
```

`DM()` / `DMI()` dereference the global `Device` struct generated from the
XML. They return node handles by value — no pointers, no allocation.

---

## Formatters — three families

All formatters return `NULL` if any instance slot was not set.

### 1. `DM_FMT_AT` — explicit slots (recommended for multi-instance paths)

Slots are identified by address, not position. Order of arguments doesn't
matter; wrong-subtree detection at runtime.

```c
/* Single instance */
DM_FMT_AT(Device.DeviceInfo.FirmwareImage.i.Status,
           DM_AT(Device.DeviceInfo.FirmwareImage.i, 2));
/* → "Device.DeviceInfo.FirmwareImage.2.Status" */

/* Double instance — any order */
DM_FMT_AT(Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active,
           DM_AT(Device.WiFi.AccessPoint.i, 3),
           DM_AT(Device.WiFi.AccessPoint.i.AssociatedDevice.i, 7));
/* → "Device.WiFi.AccessPoint.3.AssociatedDevice.7.Active" */

/* String instance — alias, wildcard, search expression */
DM_FMT_AT(Device.DeviceInfo.FirmwareImage.i.Status,
           DM_AT_S(Device.DeviceInfo.FirmwareImage.i, "active"));
/* → "Device.DeviceInfo.FirmwareImage.active.Status" */
```

### 2. `DM_FMT` — positional integers (simplest for unambiguous paths)

Instances filled root-to-leaf in argument order.

```c
DM_FMT(Device.DeviceInfo.FirmwareImage.i.Name, 1);
/* → "Device.DeviceInfo.FirmwareImage.1.Name" */

DM_FMT(Device.WiFi.AccessPoint.i.AssociatedDevice.i.Active, 2, 5);
/* → "Device.WiFi.AccessPoint.2.AssociatedDevice.5.Active" */
```

---

## Path object API — for reusable paths

Use when a path is set once and rendered multiple times, or when instance
values come from variables rather than literals.

### Heap-allocated

```c
dm_schema_t *p = dm_schema_new(DM(Device.DeviceInfo.FirmwareImage.i.Name));
dm_schema_set_instance(p, DMI(Device.DeviceInfo.FirmwareImage.i), idx);
const char *str = dm_schema_fmt(p);   /* NULL if any slot unset */
/* use str … */
dm_schema_delete(p);
```

### String instance values

```c
dm_schema_set_instance_str(p, DMI(Device.DeviceInfo.FirmwareImage.i), "active");
/* The string pointer is borrowed — keep it alive until dm_schema_fmt() */
```

---

## Template strings

```c
DM_SCHEMA(Device.DeviceInfo.FirmwareImage.i.Name)
/* → "Device.DeviceInfo.FirmwareImage.{i}.Name" */

/* With caller buffer (thread-safe, no static storage): */
char buf[DM_SCHEMA_BUF_SIZE];
dm_schema_str_buf(buf, sizeof(buf), DM(Device.DeviceInfo.FirmwareImage.i.Name));
```

---

## Buffer variants (thread-safe / no static storage)

Every formatter has `_buf` and `_buf_ap` variants:

```c
char buf[DM_SCHEMA_BUF_SIZE];

dm_schema_fmt_at_buf(buf, sizeof(buf), DM(leaf), DM_AT(...), DM_AT_END);
dm_schema_fmt_pos_buf(buf, sizeof(buf), DM(leaf), 2, 5, DM_FMT_END);
dm_schema_str_buf(buf, sizeof(buf), DM(leaf));
```

The bare (non-`_buf`) versions use a function-local `static` buffer and are
**not thread-safe** but convenient for single-threaded / logging use.

---

## `_dup` variants — heap-allocated return value

Every one-shot formatter has a `_dup` counterpart that returns a
heap-allocated copy. The caller is responsible for freeing it.
Use when thread-safety is required or when multiple results must be
live simultaneously.

```c
char *path = DM_FMT_DUP(Device.DeviceInfo.FirmwareImage.i.Name, 2);
/* → heap copy of "Device.DeviceInfo.FirmwareImage.2.Name" */
free(path);

char *tmpl = DM_SCHEMA_DUP(Device.WiFi.AccessPoint.i.Status);
/* → heap copy of "Device.WiFi.AccessPoint.{i}.Status" */
free(tmpl);
```

| Static-buffer macro | `_dup` equivalent |
|---|---|
| `DM_SCHEMA(n)` | `DM_SCHEMA_DUP(n)` |
| `DM_FMT_AT(leaf, ...)` | `DM_FMT_AT_DUP(leaf, ...)` |
| `DM_FMT(leaf, ...)` | `DM_FMT_DUP(leaf, ...)` |

---

## Instance extraction — `dm_schema_parse_inst`

Extracts a single instance identifier from a runtime path string,
validated against a compile-time schema slot.

```c
const char *path = "Device.WiFi.AccessPoint.home2G.AssociatedDevice.4";

char *ap = dm_schema_parse_inst_dup(DMI(Device.WiFi.AccessPoint.i), path);
/* → "home2G" */

char *ad = dm_schema_parse_inst_dup(DMI(Device.WiFi.AccessPoint.i.AssociatedDevice.i), path);
/* → "4" */

free(ap);
free(ad);
```

Returns `NULL` if any non-instance segment of the path does not match the
corresponding schema node name:

```c
dm_schema_parse_inst_dup(DMI(Device.WiFi.AccessPoint.i),
                         "Device.WiFi.Radio.1"); /* → NULL */
```

Use `dm_schema_parse_inst_buf` when a pre-allocated buffer is preferred
over heap allocation.

---

## Node introspection

```c
dm_node_name(DM(Device.DeviceInfo.SoftwareVersion))   /* → "SoftwareVersion" */
dm_node_type_str(dm_node(DM(node))->type)              /* → "parameter" */
DM_NAME(Device.DeviceInfo.SoftwareVersion)             /* shorthand for dm_node_name */
```

`dm_node_type_str` returns one of: `"object"`, `"table"`, `"instance"`,
`"parameter"`, `"method"`, `"event"`, `"stub"`, or `""`.

---

## Trailing dots and NO_DOT variants

TR-106 §3.1 mandates a trailing dot on all object, table, and instance paths:

```
Device.DeviceInfo.                          ← object
Device.DeviceInfo.FirmwareImage.            ← table
Device.DeviceInfo.FirmwareImage.1.          ← instance
Device.DeviceInfo.FirmwareImage.{i}.        ← instance template
Device.DeviceInfo.FirmwareImage.{i}.Status  ← parameter (no dot)
Device.DeviceInfo.FirmwareImage.{i}.Download() ← method (no dot)
```

All formatters apply this rule automatically. No action needed for the
normal case.

**Exception — TR-106 §3.2.3:** Path names stored *as values* inside
parameters (references) must **not** have a trailing dot:

```c
/* ActiveFirmwareImage returns a reference — no trailing dot */
return os_val_set_str_ref(value,
    DM_FMT_NO_DOT(Device.DeviceInfo.FirmwareImage.i, FW_INST_ACTIVE));
/* → "Device.DeviceInfo.FirmwareImage.1" */
```

### NO_DOT macro variants

Each formatter family has a `_NO_DOT` counterpart:

| Normal | No-dot |
|---|---|
| `DM_FMT_AT(leaf, ...)` | `DM_FMT_AT_NO_DOT(leaf, ...)` |
| `DM_FMT(leaf, ...)` | `DM_FMT_NO_DOT(leaf, ...)` |

For the path object API:

```c
dm_schema_t *p = dm_schema_new(DM(Device.DeviceInfo.FirmwareImage.i));
dm_schema_set_instance(p, DMI(Device.DeviceInfo.FirmwareImage.i), 1);
dm_schema_set_no_dot(p);   /* suppress trailing dot */
const char *ref = dm_schema_fmt(p);
/* → "Device.DeviceInfo.FirmwareImage.1" */
dm_schema_delete(p);
```

### NO_DOT sentinels (raw function use only)

The one-shot macros inject sentinels automatically. When calling the raw
functions directly, use the no-dot sentinels as the terminal argument:

```c
/* _at family */
dm_schema_fmt_at(DM(leaf), DM_AT(node, 1), DM_AT_END_NO_DOT);

/* _pos family */
dm_schema_fmt_pos_buf(buf, sizeof(buf), DM(leaf), 1, DM_FMT_END_NO_DOT);
```

| Sentinel | Used with |
|---|---|
| `DM_AT_END` | `dm_schema_fmt_at` / `dm_schema_fmt_at_buf` (normal) |
| `DM_AT_END_NO_DOT` | `dm_schema_fmt_at` / `dm_schema_fmt_at_buf` (no dot) |
| `DM_FMT_END` | `dm_schema_fmt_pos` / `dm_schema_fmt_pos_buf` (normal) |
| `DM_FMT_END_NO_DOT` | `dm_schema_fmt_pos` / `dm_schema_fmt_pos_buf` (no dot) |

---

## File layout

| File | Description |
|---|---|
| `inc/dm_schema.h` | Public API — include this and nothing else |
| `inc/dm_schema_types.h` | Core types only (included by generated header) |
| `inc/dm_tr181_schema.h` | Generated — do not edit |
| `src/dm_schema.c` | Runtime implementation |
| `src/dm_tr181_schema.c` | Generated — do not edit |
| `gen_dm_tr181_schema.py` | Code generator |
| `tr181-schema/tr-181-2-20-1-usp-full.xml` | TR181 standard schema (input to generator) |
| `tr181-schema/vendor_opensync.xml` | OpenSync vendor extension nodes |
| `dm_schema_test/test_main.c` | Entry point; `--unit` / `--dump` / `--count` / `--showcase` |
| `dm_schema_test/test_dump.c` | Schema tree dump and node count |
| `dm_schema_test/test_showcase.c` | API usage showcase |

---

## Limits

| Constant | Value | Notes |
|---|---|---|
| `DM_SCHEMA_BUF_SIZE` | 1024 | Max rendered path string length |
| `DM_IDX_NONE` | 0 | Sentinel index — "no node" |
