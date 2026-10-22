"""
Phase 2c: Cross-reference field IDs with struct offsets.

Parses the big switch functions in TTaskTable, TAssnTable, TRscTable that
dispatch on PjField/PjAssignmentField enum values.  For each case block,
extracts the struct offsets accessed, producing a mapping:
    field_id → property_name → struct_offset(s)

Strategy:
  1. Find all functions that have field-ID switch statements (from enum_constants.tsv)
  2. Parse the decompiled C for those functions, extract case blocks
  3. Within each case block, find offset accesses (+ 0xNN) on the record pointer
  4. Join field_id → typelib property name
  5. Produce named struct layouts

Reads:
  analysis/typelib.tsv
  analysis/field_id_map.tsv
  analysis/enum_constants.tsv
  analysis/named_functions_p1.tsv
  decompiled/part_NNNN.c

Produces:
  analysis/field_offsets.tsv        (class, field_id, property_name, record_offset, vt_type)
  analysis/named_struct_layouts.tsv (class, offset_hex, field_name, field_type, access_count)
  analysis/phase2c_summary.txt
"""

import csv
import os
import re
import glob
import collections

BASE = "C:/Users/timot/SFM Staff Dropbox/Tim King/Claude/Projects/MS Project Decompile"

# Field ID ranges for each object type
FIELD_RANGES = {
    "Task": (0x0b400000, 0x0b4fffff),
    "Resource": (0x0c400000, 0x0c4fffff),
    "Assignment": (0x0f400000, 0x0f4fffff),
    # Also calendar/outline/etc.
    "Calendar": (0x0d400000, 0x0d4fffff),
}


def load_typelib_field_names():
    """Build field_id → (interface, property_name, invkind) from typelib."""
    field_names = {}
    with open(os.path.join(BASE, "analysis/typelib.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            if row["typekind"] == "ENUM":
                continue
            if row["invkind"] not in ("INVOKE_PROPERTYGET", "INVOKE_PROPERTYPUT",
                                       "INVOKE_PROPERTYPUTREF", "INVOKE_FUNC"):
                continue
            memid = int(row["memid"])
            if memid not in field_names:
                field_names[memid] = (row["typeinfo"], row["name"])
            elif row["invkind"] == "INVOKE_PROPERTYGET":
                field_names[memid] = (row["typeinfo"], row["name"])
    return field_names


def load_pjfield_enum():
    """Load PjField, PjAssignmentField, PjResourceField enum values."""
    enums = {}
    with open(os.path.join(BASE, "analysis/typelib.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            if row["typekind"] != "ENUM":
                continue
            if row["typeinfo"] in ("PjField", "PjAssignmentField"):
                enums[int(row["memid"])] = (row["typeinfo"], row["name"])
    return enums


def load_field_id_map():
    """Load Phase 2a field_id_map for VT types."""
    vt_by_field = {}
    path = os.path.join(BASE, "analysis/field_id_map.tsv")
    if not os.path.exists(path):
        return vt_by_field
    with open(path, encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            fid = int(row["field_id_dec"])
            if fid not in vt_by_field:
                vt_by_field[fid] = row["vt_type"]
    return vt_by_field


def find_switch_functions():
    """Find functions with large PjField switches from enum_constants."""
    func_field_counts = collections.Counter()
    func_classes = {}
    with open(os.path.join(BASE, "analysis/enum_constants.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            if row["enum_name"] not in ("PjField", "PjAssignmentField"):
                continue
            addr = row["func_addr"]
            func_field_counts[addr] += 1
            func_classes[addr] = row["func_name"]

    # Return functions with at least 10 field ID cases (big switches)
    big_funcs = [(addr, func_classes[addr], count)
                 for addr, count in func_field_counts.items() if count >= 10]
    return sorted(big_funcs, key=lambda x: -x[2])


def build_func_index():
    """Build func_addr → (file, line) index."""
    idx = {}
    pattern = re.compile(r"^// ==== (FUN_[0-9a-f]+) @ ([0-9a-f]+) ====$")
    for fpath in sorted(glob.glob(os.path.join(BASE, "decompiled/part_*.c"))):
        with open(fpath, encoding="utf-8", errors="replace") as f:
            for lineno, line in enumerate(f, 1):
                m = pattern.match(line)
                if m:
                    idx[m.group(2)] = (fpath, lineno)
    return idx


def read_function_body(fpath, start_line, max_lines=15000):
    """Read a function body from decompiled output."""
    lines = []
    brace_depth = 0
    started = False
    with open(fpath, encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f, 1):
            if i < start_line:
                continue
            if i > start_line + max_lines:
                break
            lines.append(line)
            if "{" in line:
                brace_depth += line.count("{") - line.count("}")
                started = True
            elif "}" in line:
                brace_depth += line.count("{") - line.count("}")
                if started and brace_depth <= 0:
                    break
            elif started:
                brace_depth += line.count("{") - line.count("}")
    return lines


def parse_switch_cases(func_lines):
    """Parse a function's lines to extract switch case blocks.

    Returns a list of (case_value, case_lines) tuples.
    Each case_value is the hex integer from 'case 0xNNN:'.
    case_lines are the lines between this case and the next case/break/default.
    """
    # First pass: find all case markers and their line indices
    case_pattern = re.compile(r"case\s+(0x[0-9a-fA-F]+)\s*:")
    case_positions = []
    for i, line in enumerate(func_lines):
        for m in case_pattern.finditer(line):
            val = int(m.group(1), 16)
            # Only interested in field IDs
            if (0x0b400000 <= val <= 0x0b4fffff or
                0x0c400000 <= val <= 0x0c4fffff or
                0x0d400000 <= val <= 0x0d4fffff or
                0x0f400000 <= val <= 0x0f4fffff):
                case_positions.append((i, val))

    if not case_positions:
        return []

    # Second pass: for each case, collect lines until next case/break/default/}
    results = []
    break_pattern = re.compile(r"^\s*(break\s*;|goto\s|return\s|case\s|default\s*:)")

    for idx, (line_idx, case_val) in enumerate(case_positions):
        # Find the code block for this case
        # Skip consecutive case labels (fallthrough groups)
        code_start = line_idx + 1
        while code_start < len(func_lines):
            stripped = func_lines[code_start].strip()
            if stripped.startswith("case ") or stripped == "":
                code_start += 1
            else:
                break

        # Collect until break/next case/return/goto
        code_end = code_start
        while code_end < len(func_lines):
            stripped = func_lines[code_end].strip()
            if break_pattern.match(stripped):
                break
            code_end += 1

        case_lines = func_lines[code_start:code_end + 1]
        if case_lines:
            results.append((case_val, case_lines))

    return results


def extract_offsets_from_case(case_lines):
    """Extract struct offset accesses from a case block.

    Looks for patterns like:
      (longlong)(var) + 0xNN)
      (var + 0xNN)
      *(type *)((longlong)var + 0xNN)

    Returns set of integer offsets.
    """
    offset_pattern = re.compile(
        r'\+\s*(0x[0-9a-fA-F]+)\s*\)'
    )
    offsets = set()
    for line in case_lines:
        for m in offset_pattern.finditer(line):
            off = int(m.group(1), 16)
            # Filter: only interested in reasonable struct offsets (< 0x1000)
            # and exclude very common vtable/dispatcher offsets
            if 0x8 <= off <= 0xfff:
                # Exclude vtable call offsets (multiples of 8 above 0x100 that are common)
                # and known dispatcher infrastructure offsets
                if off not in (0x168, 0x238, 0x248, 0x258, 0x268, 0x2a8, 0x2c0, 0x2d0,
                               0x378, 0x398, 0x3b8, 0x418, 0x438, 0x4f0, 0x820):
                    offsets.add(off)
    return offsets


def extract_offsets_from_case_strict(case_lines):
    """Stricter offset extraction — looks for the record-pointer access pattern.

    Matches:  *(type*)((longlong)local_NNN + 0xNN)
              *(type*)((longlong)p_Var + 0xNN)
              *(type*)(lVarN + 0xNN)
    Where the variable is likely the record pointer (not param_1 which is the table).
    """
    # Pattern: access on record-like variable + offset
    record_pattern = re.compile(
        r'\*\s*\(\s*\w+\s*\*\s*\)\s*\(\s*(?:\(\s*longlong\s*\))?\s*'
        r'(local_\w+|p_Var\d+|lVar\d+)\s*\+\s*(0x[0-9a-fA-F]+)\s*\)'
    )
    offsets = collections.defaultdict(set)
    for line in case_lines:
        for m in record_pattern.finditer(line):
            var = m.group(1)
            off = int(m.group(2), 16)
            if 0x8 <= off <= 0xfff:
                offsets[var].add(off)

    # If multiple variables found, prefer the one that's most likely the record
    # (local_398, p_Var26, etc. — not param_1)
    all_offsets = set()
    for var, offs in offsets.items():
        all_offsets.update(offs)
    return all_offsets


def main():
    print("Phase 2c: Cross-reference field IDs with struct offsets")
    print("=" * 60)

    field_names = load_typelib_field_names()
    pjfield_enum = load_pjfield_enum()
    vt_by_field = load_field_id_map()
    big_funcs = find_switch_functions()
    func_index = build_func_index()

    print(f"  {len(field_names)} field names in typelib")
    print(f"  {len(pjfield_enum)} PjField/PjAssignmentField enum values")
    print(f"  {len(big_funcs)} functions with large field-ID switches")
    print(f"  {len(func_index)} function locations indexed")

    print("\nTop switch functions:")
    for addr, name, count in big_funcs[:15]:
        print(f"  {name} @ {addr}: {count} field ID cases")

    # Parse each big switch function
    field_offset_map = {}  # field_id → set of offsets
    func_contributions = collections.Counter()

    for addr, name, count in big_funcs:
        if addr not in func_index:
            continue
        fpath, lineno = func_index[addr]
        func_lines = read_function_body(fpath, lineno)

        cases = parse_switch_cases(func_lines)
        if not cases:
            continue

        for case_val, case_lines in cases:
            offsets = extract_offsets_from_case_strict(case_lines)
            if not offsets:
                offsets = extract_offsets_from_case(case_lines)

            if offsets:
                if case_val not in field_offset_map:
                    field_offset_map[case_val] = set()
                field_offset_map[case_val].update(offsets)
                func_contributions[name] += 1

    print(f"\nExtracted offset mappings for {len(field_offset_map)} field IDs")
    print(f"Contributions by function:")
    for name, count in func_contributions.most_common(10):
        print(f"  {name}: {count} cases with offsets")

    # Build the output — join field_id → (property_name, offsets)
    field_offset_rows = []
    for field_id, offsets in sorted(field_offset_map.items()):
        # Get property name
        prop_iface, prop_name = "", ""
        if field_id in field_names:
            prop_iface, prop_name = field_names[field_id]
        elif field_id in pjfield_enum:
            _, enum_member = pjfield_enum[field_id]
            prop_name = enum_member
        vt = vt_by_field.get(field_id, "")

        # Determine which class this belongs to
        if 0x0b400000 <= field_id <= 0x0b4fffff:
            cls = "Task"
        elif 0x0c400000 <= field_id <= 0x0c4fffff:
            cls = "Resource"
        elif 0x0f400000 <= field_id <= 0x0f4fffff:
            cls = "Assignment"
        elif 0x0d400000 <= field_id <= 0x0d4fffff:
            cls = "Calendar"
        else:
            cls = "Unknown"

        for off in sorted(offsets):
            field_offset_rows.append({
                "class": cls,
                "field_id": f"0x{field_id:08x}",
                "field_id_dec": field_id,
                "property_name": prop_name,
                "interface": prop_iface,
                "record_offset": f"0x{off:x}",
                "record_offset_dec": off,
                "vt_type": vt,
            })

    # Write field_offsets.tsv
    outpath = os.path.join(BASE, "analysis/field_offsets.tsv")
    with open(outpath, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["class", "field_id", "field_id_dec", "property_name", "interface",
                     "record_offset", "record_offset_dec", "vt_type"])
        for row in field_offset_rows:
            w.writerow([row["class"], row["field_id"], row["field_id_dec"],
                         row["property_name"], row["interface"],
                         row["record_offset"], row["record_offset_dec"], row["vt_type"]])
    print(f"\nWrote {len(field_offset_rows)} entries to {outpath}")

    # Build named struct layouts — aggregate by (class, offset)
    build_named_layouts(field_offset_rows)

    # Write summary
    write_summary(field_offset_rows, big_funcs, func_contributions)


def build_named_layouts(field_offset_rows):
    """Build named struct layouts from field_id → offset mappings.

    For each (class, offset), pick the best property name.
    """
    # Group by (class, offset)
    by_offset = collections.defaultdict(list)
    for row in field_offset_rows:
        key = (row["class"], row["record_offset_dec"])
        by_offset[key].append(row)

    layouts = []
    for (cls, offset), rows in sorted(by_offset.items()):
        # Pick the simplest/most common property name
        names = [r["property_name"] for r in rows if r["property_name"]]
        vt_types = [r["vt_type"] for r in rows if r["vt_type"]]

        if names:
            name_counts = collections.Counter(names)
            best_name = name_counts.most_common(1)[0][0]
            all_names = sorted(set(names))
        else:
            best_name = ""
            all_names = []

        best_vt = vt_types[0] if vt_types else ""

        layouts.append({
            "class": cls,
            "offset_hex": f"0x{offset:x}",
            "offset": offset,
            "field_name": best_name,
            "field_type": best_vt,
            "n_field_ids": len(set(r["field_id"] for r in rows)),
            "all_names": "; ".join(all_names[:5]),
        })

    # Also load struct_layouts.tsv to merge with unnamed offsets
    existing = collections.defaultdict(dict)
    existing_path = os.path.join(BASE, "analysis/struct_layouts.tsv")
    if os.path.exists(existing_path):
        with open(existing_path, encoding="utf-8") as f:
            for row in csv.DictReader(f, delimiter="\t"):
                cls = row["class"]
                off = int(row["offset"])
                existing[cls][off] = row

    # Merge: for TTaskTable/TAssnTable/TRscTable, add named fields
    table_class_map = {
        "Task": "TTaskTable",
        "Resource": "TRscTable",
        "Assignment": "TAssnTable",
    }

    merged_layouts = []
    for entry in layouts:
        cls = entry["class"]
        table_cls = table_class_map.get(cls, "")
        # Check if this offset exists in the struct_layouts
        existing_info = existing.get(table_cls, {}).get(entry["offset"], {})
        merged_layouts.append({
            "class": cls,
            "table_class": table_cls,
            "offset_hex": entry["offset_hex"],
            "offset": entry["offset"],
            "field_name": entry["field_name"],
            "field_type": entry["field_type"],
            "cast_type": existing_info.get("field_type", ""),
            "access_count": existing_info.get("access_count", 0),
            "n_field_ids": entry["n_field_ids"],
            "all_names": entry["all_names"],
        })

    outpath = os.path.join(BASE, "analysis/named_struct_layouts.tsv")
    with open(outpath, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["class", "table_class", "offset_hex", "offset", "field_name",
                     "vt_type", "cast_type", "access_count", "n_field_ids", "all_names"])
        for row in merged_layouts:
            w.writerow([
                row["class"], row["table_class"], row["offset_hex"], row["offset"],
                row["field_name"], row["field_type"], row["cast_type"],
                row["access_count"], row["n_field_ids"], row["all_names"],
            ])
    print(f"Wrote {len(merged_layouts)} named field entries to {outpath}")
    return merged_layouts


def write_summary(field_offset_rows, big_funcs, func_contributions):
    # Stats by class
    by_class = collections.defaultdict(list)
    for row in field_offset_rows:
        by_class[row["class"]].append(row)

    named_count = sum(1 for r in field_offset_rows if r["property_name"])
    unnamed_count = len(field_offset_rows) - named_count

    outpath = os.path.join(BASE, "analysis/phase2c_summary.txt")
    with open(outpath, "w", encoding="utf-8") as f:
        f.write("=" * 70 + "\n")
        f.write("Phase 2c: Field ID → Struct Offset Cross-Reference\n")
        f.write("=" * 70 + "\n\n")

        f.write(f"Total field_id → offset mappings: {len(field_offset_rows)}\n")
        f.write(f"  Named (with typelib property): {named_count}\n")
        f.write(f"  Unnamed: {unnamed_count}\n\n")

        for cls in sorted(by_class.keys()):
            rows = by_class[cls]
            named = sum(1 for r in rows if r["property_name"])
            unique_offsets = len(set(r["record_offset_dec"] for r in rows))
            unique_fields = len(set(r["field_id_dec"] for r in rows))
            f.write(f"\n--- {cls} ---\n")
            f.write(f"  Field IDs mapped: {unique_fields}\n")
            f.write(f"  Distinct struct offsets: {unique_offsets}\n")
            f.write(f"  Named mappings: {named}\n\n")

            # Show first 50 named field→offset mappings
            seen = set()
            count = 0
            for r in sorted(rows, key=lambda x: x["record_offset_dec"]):
                key = (r["record_offset_dec"], r["property_name"])
                if key in seen or not r["property_name"]:
                    continue
                seen.add(key)
                f.write(f"  {r['record_offset']:>6}  {r['property_name']:<40} "
                        f"{r['vt_type']:<12} ({r['field_id']})\n")
                count += 1
                if count >= 50:
                    remaining = len(seen) - count
                    if remaining > 0:
                        f.write(f"  ... and more\n")
                    break

    print(f"Wrote summary to {outpath}")


if __name__ == "__main__":
    main()
