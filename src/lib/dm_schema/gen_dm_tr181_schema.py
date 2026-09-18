#!/usr/bin/env python3
"""
gen_dm_tr181_schema.py — TR181 schema code generator (index-based)

Parses a TR-181 XML file and emits:
  dm_tr181_schema.h  — dm_Device_t typedef + extern const dm_Device_t Device
  dm_tr181_schema.c  — dm_strings[], dm_nodes[], and Device static initializer

Each node is identified by a uint16_t index into dm_nodes[].
Index 0 is the sentinel (like NULL). Device root is index 1.

Usage:
  python3 gen_dm_tr181_schema.py <input.xml> [-o <output-dir>]
"""

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from typing import Optional


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------

class NT:
    OBJECT   = "DM_NODE_OBJECT"
    TABLE    = "DM_NODE_TABLE"
    INSTANCE = "DM_NODE_INSTANCE"
    PARAM    = "DM_NODE_PARAMETER"
    METHOD   = "DM_NODE_METHOD"
    EVENT    = "DM_NODE_EVENT"
    STUB     = "DM_NODE_STUB"


@dataclass
class Node:
    name: str
    node_type: str
    parent: Optional["Node"] = None
    children: list = field(default_factory=list)
    depth: int = 0
    idx: int = 0          # assigned in assign_indices()
    name_offset: int = 0  # byte offset into dm_strings[]

    def c_name(self) -> str:
        if self.node_type == NT.INSTANCE:
            return "i"
        s = self.name.rstrip(".()")
        s = re.sub(r"[^A-Za-z0-9_]", "_", s)
        if s and s[0].isdigit():
            s = "_" + s
        return s

    def c_path(self) -> str:
        parts = []
        n = self
        while n is not None:
            parts.append(n.c_name())
            n = n.parent
        parts.reverse()
        return ".".join(parts)


# ---------------------------------------------------------------------------
# XML namespace helpers
# ---------------------------------------------------------------------------

NS_DM = "urn:broadband-forum-org:cwmp:datamodel-1-15"

def strip_ns(tag: str) -> str:
    return re.sub(r"\{[^}]*\}", "", tag)


# ---------------------------------------------------------------------------
# Path decomposition and tree builder  (identical to dm_schema)
# ---------------------------------------------------------------------------

def path_segments(full_path: str) -> list:
    return [s for s in full_path.rstrip(".").split(".") if s]


def infer_type(seg: str, next_seg) -> str:
    if seg == "{i}":
        return NT.INSTANCE
    if next_seg == "{i}":
        return NT.TABLE
    return NT.OBJECT


def seg_key(segments: list, up_to: int) -> str:
    return ".".join(segments[:up_to + 1]) + "."


class TreeBuilder:
    def __init__(self):
        self.root = Node(name="Device", node_type=NT.OBJECT, depth=0)
        self.map = {"Device.": self.root}

    def ensure_path(self, full_path: str, leaf_type=None) -> "Node":
        segs = path_segments(full_path)
        is_leaf = not full_path.endswith(".")
        node = self.root

        for i, seg in enumerate(segs):
            if i == 0:
                continue
            key = seg_key(segs, i) if (not is_leaf or i < len(segs) - 1) else \
                  ".".join(segs[:i + 1])
            if key in self.map:
                node = self.map[key]
                next_seg = segs[i + 1] if i + 1 < len(segs) else None
                if next_seg == "{i}" and node.node_type == NT.OBJECT:
                    node.node_type = NT.TABLE
                continue
            next_seg = segs[i + 1] if i + 1 < len(segs) else None
            ntype = (leaf_type or NT.PARAM) if (is_leaf and i == len(segs) - 1) \
                    else infer_type(seg, next_seg)
            new_node = Node(name=seg, node_type=ntype, parent=node)
            cname = new_node.c_name()
            existing = {c.c_name() for c in node.children}
            if cname in existing:
                suffix = 2
                while f"{cname}_{suffix}" in existing:
                    suffix += 1
                new_node.name = f"{seg}_{suffix}"
            node.children.append(new_node)
            self.map[key] = new_node
            node = new_node
        return node

    def assign_depths(self):
        def recurse(n, d):
            n.depth = d
            for c in n.children:
                recurse(c, d + 1)
        recurse(self.root, 0)

    def count(self) -> int:
        def recurse(n):
            return 1 + sum(recurse(c) for c in n.children)
        return recurse(self.root)


# ---------------------------------------------------------------------------
# Include-list filter
# ---------------------------------------------------------------------------

LEAF_TYPES = {NT.PARAM, NT.METHOD, NT.EVENT}

def apply_filter(root: Node, include: set):
    """
    Stub out any direct Object/Table children of Device not in include set.
    Params/methods/events directly under Device are always kept.
    """
    if not include:
        return
    for child in root.children:
        if child.node_type in LEAF_TYPES:
            continue  # always keep
        if child.name not in include:
            child.node_type = NT.STUB
            child.children = []


# ---------------------------------------------------------------------------
# XML parsing  (identical to dm_schema)
# ---------------------------------------------------------------------------

def parse_xml(paths: list) -> Node:
    builder = TreeBuilder()
    for path in paths:
        tree = ET.parse(path)
        xml_root = tree.getroot()
        model = xml_root.find(f"{{{NS_DM}}}model")
        if model is None:
            model = xml_root
        _walk(model, builder, parent_obj_path=None)
    builder.assign_depths()
    return builder.root


def _walk(xml_node, builder, parent_obj_path):
    for child in xml_node:
        tag = strip_ns(child.tag)
        if tag == "model":
            _walk(child, builder, parent_obj_path)
        elif tag == "object":
            obj_path = child.get("name", "")
            if not obj_path:
                continue
            if not obj_path.endswith("."):
                obj_path += "."
            if not obj_path.startswith("Device.") and parent_obj_path:
                obj_path = parent_obj_path + obj_path
            builder.ensure_path(obj_path)
            _walk(child, builder, parent_obj_path=obj_path)
        elif tag == "parameter":
            pname = child.get("name", "")
            if pname and parent_obj_path:
                builder.ensure_path(parent_obj_path + pname, NT.PARAM)
        elif tag == "command":
            cname = child.get("name", "")
            if cname and parent_obj_path:
                builder.ensure_path(parent_obj_path + cname, NT.METHOD)
        elif tag == "event":
            ename = child.get("name", "")
            if ename and parent_obj_path:
                builder.ensure_path(parent_obj_path + ename, NT.EVENT)
        elif tag in ("input", "output", "uniqueKey", "description",
                     "syntax", "string", "int", "unsignedInt", "boolean",
                     "dateTime", "base64", "hexBinary", "list",
                     "dataType", "component", "profile"):
            pass
        else:
            _walk(child, builder, parent_obj_path)


# ---------------------------------------------------------------------------
# Index assignment and string table
# ---------------------------------------------------------------------------

def assign_indices(root: Node):
    """DFS pre-order index assignment starting from 1 (0 is sentinel)."""
    counter = [1]
    def recurse(n):
        n.idx = counter[0]
        counter[0] += 1
        for c in n.children:
            recurse(c)
    recurse(root)


def collect_nodes_dfs(root: Node) -> list:
    """Return all nodes in DFS pre-order (result[i].idx == i+1)."""
    result = []
    def recurse(n):
        result.append(n)
        for c in n.children:
            recurse(c)
    recurse(root)
    return result


class StringTable:
    """
    Builds a deduplicated table of segment name strings.

    Emits as a flat const char dm_strings[] NUL-terminated string pool.
    dm_schema_node_data_t.name is a byte offset into this array.
    Eliminates the pointer array and its relocations vs. const char *dm_strings[].
    """
    def __init__(self):
        self.names = []    # in insertion order
        self.map = {}      # name → byte offset
        self.offset = 0    # current byte offset

    def intern(self, name: str) -> int:
        if name in self.map:
            return self.map[name]
        off = self.offset
        self.map[name] = off
        self.names.append(name)
        self.offset += len(name.encode("ascii")) + 1  # +1 for NUL
        return off

    def emit_c(self) -> str:
        lines = ["const char dm_strings[] ="]
        for name in self.names:
            escaped = name.replace("\\", "\\\\").replace('"', '\\"')
            off = self.map[name]
            lines.append(f'    "{escaped}\\0"  /* {off:6d} */')
        lines[-1] = lines[-1].rstrip()  # no trailing space on last entry
        lines.append(";")
        return "\n".join(lines)

    def total_bytes(self) -> int:
        return self.offset


# ---------------------------------------------------------------------------
# Header (.h) generator
# ---------------------------------------------------------------------------

SCHEMA_H_PREAMBLE = """\
/* dm_tr181_schema.h — auto-generated by gen_dm_tr181_schema.py — do not edit */
#ifndef DM_TR181_SCHEMA_H
#define DM_TR181_SCHEMA_H

#include "dm_schema_types.h"

/* Root typedef — capital D mirrors the Device global */
typedef struct dm_Device_s {
"""

SCHEMA_H_EPILOGUE = """\
} dm_Device_t;

extern const dm_Device_t Device;

#endif /* DM_TR181_SCHEMA_H */
"""


def gen_h(root: Node) -> str:
    lines = [SCHEMA_H_PREAMBLE]
    _h_node_body(root, lines, indent=1)
    lines.append(SCHEMA_H_EPILOGUE)
    return "\n".join(lines)


def _h_node_body(node: Node, lines: list, indent: int):
    pad = "    " * indent
    if node.node_type == NT.INSTANCE:
        lines.append(f"{pad}union {{ dm_schema_node_t node; dm_schema_inst_t inst; }};")
    else:
        lines.append(f"{pad}dm_schema_node_t  node;")
    for child in node.children:
        cname = child.c_name()
        if child.node_type == NT.INSTANCE:
            lines.append(f"{pad}struct {{")
            _h_node_body(child, lines, indent + 1)
            lines.append(f"{pad}}} i;")
        elif child.node_type == NT.STUB:
            lines.append(f"{pad}struct {{ dm_schema_node_t node; struct {{}} stub; }} {cname};")
        elif child.children:
            lines.append(f"{pad}struct {{")
            _h_node_body(child, lines, indent + 1)
            lines.append(f"{pad}}} {cname};")
        else:
            lines.append(f"{pad}struct {{ dm_schema_node_t node; }} {cname};")


# ---------------------------------------------------------------------------
# Source (.c) generator
# ---------------------------------------------------------------------------

SCHEMA_C_PREAMBLE = """\
/* dm_tr181_schema.c — auto-generated by gen_dm_tr181_schema.py — do not edit */
#include "dm_tr181_schema.h"

"""

SCHEMA_C_EPILOGUE = """\
};
"""


def gen_c(root: Node) -> str:
    assign_indices(root)
    nodes_dfs = collect_nodes_dfs(root)

    strtab = StringTable()
    for n in nodes_dfs:
        n.name_offset = strtab.intern(n.name)

    lines = [SCHEMA_C_PREAMBLE]

    # dm_strings[]
    lines.append(strtab.emit_c())
    lines.append("")

    # dm_nodes[] — index 0 is sentinel (all zeros)
    lines.append(f"/* {len(nodes_dfs)} nodes; dm_nodes[0] unused (sentinel) */")
    lines.append("const dm_schema_node_data_t dm_nodes[] = {")
    lines.append("    { 0, 0, 0, 0, 0 },  /* [0] sentinel */")
    for n in nodes_dfs:
        parent_idx = n.parent.idx if n.parent else 0
        child_idx  = n.children[0].idx if n.children else 0
        next_idx   = 0
        if n.parent:
            siblings = n.parent.children
            i = siblings.index(n)
            if i + 1 < len(siblings):
                next_idx = siblings[i + 1].idx
        lines.append(
            f"    {{ {n.node_type}, {n.name_offset:6d},"
            f" {parent_idx:5d}, {child_idx:5d}, {next_idx:5d} }},"
            f"  /* [{n.idx:5d}] {n.name} */"
        )
    lines.append("};")
    lines.append("const size_t dm_nodes_count = sizeof(dm_nodes) / sizeof(*dm_nodes);")
    lines.append("")

    # Device struct initializer
    lines.append("const dm_Device_t Device = {")
    _c_node_init(root, lines, indent=1)
    lines.append(SCHEMA_C_EPILOGUE)
    return "\n".join(lines)


def _c_node_init(node: Node, lines: list, indent: int):
    pad = "    " * indent
    lines.append(f"{pad}.node = {{ .idx = {node.idx} }},")
    for child in node.children:
        cname = child.c_name()
        if child.node_type == NT.INSTANCE:
            lines.append(f"{pad}.i = {{")
            _c_node_init(child, lines, indent + 1)
            lines.append(f"{pad}}},")
        elif child.node_type == NT.STUB:
            lines.append(f"{pad}.{cname} = {{ .node = {{ .idx = {child.idx} }} }},")
        elif child.children:
            lines.append(f"{pad}.{cname} = {{")
            _c_node_init(child, lines, indent + 1)
            lines.append(f"{pad}}},")
        else:
            lines.append(f"{pad}.{cname} = {{ .node = {{ .idx = {child.idx} }} }},")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="Generate dm_tr181_schema.h/.c (index-based)")
    ap.add_argument("inputs", nargs="+", help="TR181 XML input file(s)")
    ap.add_argument("-o", "--output-dir", default=None,
                    help="Output directory for both .h and .c (overridden by --out-h/--out-c)")
    ap.add_argument("--out-h", default=None, help="Output directory for dm_tr181_schema.h")
    ap.add_argument("--out-c", default=None, help="Output directory for dm_tr181_schema.c")
    ap.add_argument("-i", "--include", default=None,
                    help="Space-separated list of top-level objects to include "
                         "(e.g. 'WiFi DeviceInfo Routing'); omit to include everything")
    args = ap.parse_args()

    include = set(args.include.split()) if args.include else set()

    import os
    base = args.output_dir or "."
    out_h = args.out_h or base
    out_c = args.out_c or base
    os.makedirs(out_h, exist_ok=True)
    os.makedirs(out_c, exist_ok=True)

    print(f"Parsing {len(args.inputs)} XML file(s)...", file=sys.stderr)
    root = parse_xml(args.inputs)
    if include:
        apply_filter(root, include)
        print(f"Include filter: {sorted(include)}", file=sys.stderr)
    n = _count(root)
    print(f"Parsed tree: {n} nodes", file=sys.stderr)

    _write(os.path.join(out_h, "dm_tr181_schema.h"), gen_h(root))
    _write(os.path.join(out_c, "dm_tr181_schema.c"), gen_c(root))


def _write(path: str, content: str):
    with open(path, "w") as f:
        f.write(content)
    print(f"Wrote {path}", file=sys.stderr)


def _count(node: Node) -> int:
    return 1 + sum(_count(c) for c in node.children)


if __name__ == "__main__":
    main()
