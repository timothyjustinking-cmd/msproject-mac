"""
Phase 2a: Struct/class field layout recovery.

Two extraction strategies:
  1. COM dispatch field-ID extraction — IBknd*Dispatch getters/setters call a
     central dispatcher with field IDs that match typelib memids.  We walk the
     vtable, decompile each slot function, extract the field ID, and join
     with the typelib to get property names.
  2. Direct offset scanning — scan decompiled C for (param_1 + 0xNN) access
     patterns, group by named class (from named_functions_p1.tsv), and build
     raw struct layouts.

Reads:
  analysis/typelib.tsv              (COM type library dump)
  analysis/vtable_matches.tsv       (interface → vtable class mapping)
  analysis/meta/vtables.tsv         (vtable slot → function address)
  analysis/named_functions_p1.tsv   (function names from Phase 1)
  decompiled/part_NNNN.c            (Ghidra decompiled output)

Produces:
  analysis/field_id_map.tsv         (vtable_class, slot, field_id, vt_type, property, interface)
  analysis/struct_offsets.tsv        (class, offset, access_size, read/write, func_addr, func_name)
  analysis/struct_layouts.tsv        (class, offset, field_name, field_type, source, confidence)
  analysis/phase2a_summary.txt       (human-readable summary)
"""

import csv
import os
import re
import glob
import collections

BASE = "C:/Users/timot/SFM Staff Dropbox/Tim King/Claude/Projects/MS Project Decompile"

GETTER_DISPATCHER = "FUN_1403cff50"
SETTER_DISPATCHER = "FUN_1403b6810"

VT_TYPES = {
    0x2: "VT_I2",
    0x3: "VT_I4",
    0x4: "VT_R4",
    0x5: "VT_R8",
    0x6: "VT_CY",
    0x7: "VT_DATE",
    0x8: "VT_BSTR",
    0x9: "VT_DISPATCH",
    0xb: "VT_BOOL",
    0xc: "VT_VARIANT",
    0xd: "VT_UNKNOWN",
    0x15: "VT_UI4",
    0x16: "VT_I8",
    0x17: "VT_UI8",
    0x1d: "VT_USERDEFINED",
}


def load_typelib():
    """Load typelib entries — returns dict of (typeinfo, memid) → list of entries."""
    entries_by_memid = collections.defaultdict(list)
    all_entries = []
    with open(os.path.join(BASE, "analysis/typelib.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            all_entries.append(row)
            if row["invkind"] in ("INVOKE_PROPERTYGET", "INVOKE_PROPERTYPUT", "INVOKE_PROPERTYPUTREF"):
                key = (row["typeinfo"], int(row["memid"]))
                entries_by_memid[key].append(row)
    return entries_by_memid, all_entries


def load_typelib_slot_map():
    """Build (interface, slot) → (name, invkind, memid) from typelib."""
    slot_map = {}
    with open(os.path.join(BASE, "analysis/typelib.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            if row["typekind"] == "ENUM":
                continue
            iface = row["typeinfo"]
            slot = row.get("slot", "")
            if slot:
                slot_map[(iface, int(slot))] = {
                    "name": row["name"],
                    "invkind": row["invkind"],
                    "memid": int(row["memid"]),
                }
    return slot_map


def load_vtable_matches():
    """Load interface → vtable_class mappings."""
    matches = {}
    with open(os.path.join(BASE, "analysis/vtable_matches.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            matches[row["interface"]] = row
    return matches


def load_vtable_slots():
    """Load vtable: (vtable_addr, slot) → func_addr, grouped by class."""
    by_class = collections.defaultdict(dict)
    with open(os.path.join(BASE, "analysis/meta/vtables.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            cls = row["class"]
            slot = int(row["slot"])
            func = row["func"]
            vtaddr = row["vtable_addr"]
            by_class[(cls, vtaddr)][slot] = func
    return by_class


def load_named_functions():
    """Load named functions from Phase 1."""
    names = {}
    with open(os.path.join(BASE, "analysis/named_functions_p1.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            names[row["func_addr"]] = row["assigned_name"]
    return names


def build_func_index():
    """Build func_addr → (file, line) index from decompiled files."""
    idx = {}
    pattern = re.compile(r"^// ==== (FUN_[0-9a-f]+) @ ([0-9a-f]+) ====$")
    for fpath in sorted(glob.glob(os.path.join(BASE, "decompiled/part_*.c"))):
        fname = os.path.basename(fpath)
        with open(fpath, encoding="utf-8", errors="replace") as f:
            for lineno, line in enumerate(f, 1):
                m = pattern.match(line)
                if m:
                    addr = m.group(2)
                    idx[addr] = (fpath, lineno)
    return idx


def extract_field_id_from_func(fpath, start_line, max_lines=60):
    """Read a decompiled function and extract the field ID from
    a call to the getter/setter dispatcher.

    Returns (field_id_hex, vt_type, dispatcher_type) or None.
    """
    results = []
    with open(fpath, encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f, 1):
            if i < start_line:
                continue
            if i > start_line + max_lines:
                break
            # getter: FUN_1403cff50(param_1,0xb400000,param_2,0xc)
            m = re.search(
                rf"{GETTER_DISPATCHER}\s*\([^,]+,\s*(0x[0-9a-fA-F]+)\s*,[^,]+,\s*(0x[0-9a-fA-F]+|\d+)\s*\)",
                line,
            )
            if m:
                fid = int(m.group(1), 16)
                vt = int(m.group(2), 16) if m.group(2).startswith("0x") else int(m.group(2))
                results.append((fid, vt, "get"))
            # setter: FUN_1403b6810(param_1,0xb400000,param_2,0xc)
            m = re.search(
                rf"{SETTER_DISPATCHER}\s*\([^,]+,\s*(0x[0-9a-fA-F]+)\s*,[^,]+,\s*(0x[0-9a-fA-F]+|\d+)\s*\)",
                line,
            )
            if m:
                fid = int(m.group(1), 16)
                vt = int(m.group(2), 16) if m.group(2).startswith("0x") else int(m.group(2))
                results.append((fid, vt, "put"))
    return results[0] if results else None


def phase2a_field_id_extraction():
    """Extract field IDs from all IBknd*Dispatch vtables and map to typelib properties."""
    print("Loading data...")
    typelib_by_memid, _ = load_typelib()
    slot_map = load_typelib_slot_map()
    vtable_matches = load_vtable_matches()
    vtable_slots = load_vtable_slots()
    func_index = build_func_index()

    print(f"  {len(typelib_by_memid)} property memids in typelib")
    print(f"  {len(slot_map)} typelib slot entries")
    print(f"  {len(vtable_matches)} interface→vtable matches")
    print(f"  {len(vtable_slots)} vtable groups")
    print(f"  {len(func_index)} function locations indexed")

    field_id_rows = []
    stats = {"extracted": 0, "matched": 0, "no_match": 0, "no_func": 0}

    for iface, match in sorted(vtable_matches.items()):
        vtable_class = match["vtable_class"]
        match_type = match["match_type"]

        # Find the vtable for this class — pick the one with the most slots
        candidates = [
            (key, slots) for key, slots in vtable_slots.items() if key[0] == vtable_class
        ]
        if not candidates:
            continue
        best_key, best_slots = max(candidates, key=lambda x: len(x[1]))

        n_typelib = int(match.get("n_typelib_methods", 0))
        # IBkndDispatch pattern: first 7 slots are IUnknown+IDispatch
        dispatch_offset = 7

        for slot_idx in sorted(best_slots.keys()):
            if slot_idx < dispatch_offset:
                continue
            typelib_slot = slot_idx
            func_addr = best_slots[slot_idx]

            # Look up this function in the decompiled code
            if func_addr not in func_index:
                stats["no_func"] += 1
                continue

            fpath, lineno = func_index[func_addr]
            result = extract_field_id_from_func(fpath, lineno)
            if result is None:
                continue

            field_id, vt_type, disp_type = result
            stats["extracted"] += 1

            # Try to match the field_id to a typelib property
            prop_name = ""
            prop_invkind = ""
            matched_iface = iface

            # Try the interface directly
            key = (iface, field_id)
            if key in typelib_by_memid:
                entries = typelib_by_memid[key]
                for e in entries:
                    if disp_type == "get" and e["invkind"] == "INVOKE_PROPERTYGET":
                        prop_name = e["name"]
                        prop_invkind = "get"
                        break
                    elif disp_type == "put" and e["invkind"] in ("INVOKE_PROPERTYPUT", "INVOKE_PROPERTYPUTREF"):
                        prop_name = e["name"]
                        prop_invkind = "put"
                        break
                if not prop_name and entries:
                    prop_name = entries[0]["name"]
                    prop_invkind = entries[0]["invkind"]

            # Also search all interfaces for this memid
            if not prop_name:
                for (ti, mid), entries in typelib_by_memid.items():
                    if mid == field_id:
                        prop_name = entries[0]["name"]
                        prop_invkind = entries[0]["invkind"]
                        matched_iface = ti
                        break

            if prop_name:
                stats["matched"] += 1
            else:
                stats["no_match"] += 1

            field_id_rows.append({
                "vtable_class": vtable_class,
                "interface": matched_iface,
                "slot": slot_idx,
                "func_addr": func_addr,
                "field_id": f"0x{field_id:08x}",
                "field_id_dec": field_id,
                "vt_type": VT_TYPES.get(vt_type, f"0x{vt_type:x}"),
                "vt_type_raw": vt_type,
                "dispatcher": disp_type,
                "property_name": prop_name,
                "property_invkind": prop_invkind,
            })

    # Write field_id_map.tsv
    outpath = os.path.join(BASE, "analysis/field_id_map.tsv")
    with open(outpath, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["vtable_class", "interface", "slot", "func_addr", "field_id",
                     "field_id_dec", "vt_type", "dispatcher", "property_name"])
        for row in sorted(field_id_rows, key=lambda r: (r["interface"], r["slot"])):
            w.writerow([
                row["vtable_class"], row["interface"], row["slot"], row["func_addr"],
                row["field_id"], row["field_id_dec"], row["vt_type"],
                row["dispatcher"], row["property_name"],
            ])

    print(f"\nField ID extraction: {stats}")
    print(f"Wrote {len(field_id_rows)} entries to {outpath}")
    return field_id_rows, stats


def phase2a_offset_scanning():
    """Scan decompiled code for (param_N + 0xNN) access patterns
    and group by function/class."""
    print("\nScanning decompiled code for struct offset patterns...")

    named_funcs = load_named_functions()
    # Build addr → class mapping
    func_to_class = {}
    for addr, name in named_funcs.items():
        if "::" in name:
            cls = name.split("::")[0]
            func_to_class[addr] = cls

    # Regex patterns for field access:
    # *(type *)(param_1 + 0xNN)            — read
    # *(type *)(param_1 + 0xNN) = ...      — write
    # (param_1 + 0xNN)                     — address-of or nested deref
    offset_pattern = re.compile(
        r'\*\s*\(\s*(\w+)\s*\*\s*\)\s*\(\s*(\w+)\s*\+\s*(0x[0-9a-fA-F]+)\s*\)'
    )
    # Also match: *(longlong **)(param_1 + 0x20)
    ptr_deref_pattern = re.compile(
        r'\*\s*\(\s*(\w+)\s*\*{1,2}\s*\)\s*\(\s*(\w+)\s*\+\s*(0x[0-9a-fA-F]+)\s*\)'
    )

    func_header_pattern = re.compile(r"^// ==== (FUN_[0-9a-f]+) @ ([0-9a-f]+) ====$")

    offsets_by_class = collections.defaultdict(lambda: collections.defaultdict(list))
    all_offset_rows = []
    current_func_addr = None
    current_func_name = None

    total_files = sorted(glob.glob(os.path.join(BASE, "decompiled/part_*.c")))
    for fi, fpath in enumerate(total_files):
        if fi % 5 == 0:
            print(f"  Scanning {os.path.basename(fpath)} ({fi+1}/{len(total_files)})...")
        with open(fpath, encoding="utf-8", errors="replace") as f:
            for line in f:
                # Track current function
                m = func_header_pattern.match(line)
                if m:
                    current_func_addr = m.group(2)
                    current_func_name = named_funcs.get(current_func_addr, m.group(1))
                    continue

                if current_func_addr is None:
                    continue

                # Find offset accesses
                for m in ptr_deref_pattern.finditer(line):
                    cast_type = m.group(1)
                    base_var = m.group(2)
                    offset_hex = m.group(3)
                    offset = int(offset_hex, 16)

                    # Only interested in this/param_1 accesses or known var patterns
                    if base_var not in ("param_1", "param_2", "param_3",
                                        "lVar1", "lVar2", "lVar3", "lVar4",
                                        "lVar5", "lVar6"):
                        continue

                    # Determine access size from cast type
                    size = _type_to_size(cast_type)
                    is_write = " = " in line.split(m.group(0), 1)[-1][:20] if m.group(0) in line else False

                    cls = func_to_class.get(current_func_addr, "")
                    if base_var == "param_1":
                        deref_level = 0
                    else:
                        deref_level = 1  # accessed via pointer loaded from this

                    row = {
                        "class": cls,
                        "offset": offset,
                        "offset_hex": offset_hex,
                        "cast_type": cast_type,
                        "access_size": size,
                        "rw": "write" if is_write else "read",
                        "base_var": base_var,
                        "deref_level": deref_level,
                        "func_addr": current_func_addr,
                        "func_name": current_func_name,
                    }
                    all_offset_rows.append(row)
                    if cls:
                        offsets_by_class[cls][offset].append(row)

    # Write struct_offsets.tsv
    outpath = os.path.join(BASE, "analysis/struct_offsets.tsv")
    with open(outpath, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["class", "offset_hex", "offset", "cast_type", "access_size",
                     "rw", "base_var", "deref_level", "func_addr", "func_name"])
        for row in sorted(all_offset_rows, key=lambda r: (r["class"], r["offset"], r["func_addr"])):
            w.writerow([
                row["class"], row["offset_hex"], row["offset"], row["cast_type"],
                row["access_size"], row["rw"], row["base_var"], row["deref_level"],
                row["func_addr"], row["func_name"],
            ])

    print(f"\nOffset scanning: {len(all_offset_rows)} total accesses, {len(offsets_by_class)} classes")
    print(f"Wrote {outpath}")

    # Build consolidated struct layouts
    struct_layouts = build_struct_layouts(offsets_by_class)
    return all_offset_rows, offsets_by_class, struct_layouts


def _type_to_size(cast_type):
    sizes = {
        "char": 1, "uchar": 1, "undefined1": 1, "byte": 1, "bool": 1,
        "short": 2, "ushort": 2, "undefined2": 2, "wchar_t": 2,
        "int": 4, "uint": 4, "undefined4": 4, "float": 4, "DWORD": 4,
        "longlong": 8, "ulonglong": 8, "undefined8": 8, "double": 8,
        "long": 4, "ulong": 4,
    }
    return sizes.get(cast_type, 0)


def build_struct_layouts(offsets_by_class):
    """Consolidate offset accesses into struct layouts."""
    layouts = {}
    for cls, offset_map in offsets_by_class.items():
        fields = []
        for offset in sorted(offset_map.keys()):
            accesses = offset_map[offset]
            # Determine most common type/size
            type_counts = collections.Counter()
            sizes = set()
            rw = set()
            funcs = set()
            for a in accesses:
                type_counts[a["cast_type"]] += 1
                if a["access_size"]:
                    sizes.add(a["access_size"])
                rw.add(a["rw"])
                funcs.add(a["func_addr"])

            best_type = type_counts.most_common(1)[0][0]
            best_size = max(sizes) if sizes else 0

            fields.append({
                "class": cls,
                "offset": offset,
                "offset_hex": f"0x{offset:x}",
                "field_type": best_type,
                "field_size": best_size,
                "access_count": len(accesses),
                "read": "read" in rw,
                "write": "write" in rw,
                "n_funcs": len(funcs),
            })
        layouts[cls] = fields

    # Write struct_layouts.tsv
    outpath = os.path.join(BASE, "analysis/struct_layouts.tsv")
    with open(outpath, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["class", "offset_hex", "offset", "field_type", "field_size",
                     "access_count", "read", "write", "n_funcs"])
        for cls in sorted(layouts.keys()):
            for field in layouts[cls]:
                w.writerow([
                    field["class"], field["offset_hex"], field["offset"],
                    field["field_type"], field["field_size"], field["access_count"],
                    field["read"], field["write"], field["n_funcs"],
                ])

    print(f"Built struct layouts for {len(layouts)} classes")
    print(f"Wrote {outpath}")
    return layouts


def write_summary(field_id_rows, field_stats, offset_rows, layouts):
    outpath = os.path.join(BASE, "analysis/phase2a_summary.txt")
    with open(outpath, "w", encoding="utf-8") as f:
        f.write("=" * 70 + "\n")
        f.write("Phase 2a: Struct Recovery Summary\n")
        f.write("=" * 70 + "\n\n")

        f.write("--- Field ID Extraction (COM Dispatch) ---\n")
        f.write(f"Total field ID entries extracted: {len(field_id_rows)}\n")
        f.write(f"  Successfully matched to typelib: {field_stats['matched']}\n")
        f.write(f"  No typelib match: {field_stats['no_match']}\n")
        f.write(f"  Function not found in decompiled: {field_stats['no_func']}\n\n")

        # Count by interface
        iface_counts = collections.Counter(r["interface"] for r in field_id_rows)
        f.write("Top interfaces by extracted fields:\n")
        for iface, count in iface_counts.most_common(20):
            f.write(f"  {iface}: {count}\n")

        f.write(f"\n--- Direct Offset Scanning ---\n")
        f.write(f"Total offset accesses found: {len(offset_rows)}\n")
        f.write(f"Classes with offsets: {len(layouts)}\n\n")

        # Top classes by offset count
        class_counts = collections.Counter(r["class"] for r in offset_rows if r["class"])
        f.write("Top classes by offset access count:\n")
        for cls, count in class_counts.most_common(30):
            n_offsets = len(layouts.get(cls, []))
            f.write(f"  {cls}: {count} accesses, {n_offsets} distinct offsets\n")

        # Show sample struct layout for key classes
        for key_cls in ["TTaskTable", "TaskElement", "TRscTable", "TAssnTable"]:
            if key_cls in layouts:
                fields = layouts[key_cls]
                f.write(f"\n--- {key_cls} struct layout ({len(fields)} fields) ---\n")
                for field in fields[:40]:
                    rw = "RW" if field["read"] and field["write"] else ("R" if field["read"] else "W")
                    f.write(f"  {field['offset_hex']:>8}  {field['field_type']:<15} "
                            f"(size={field['field_size']})  {rw}  "
                            f"({field['access_count']} accesses in {field['n_funcs']} funcs)\n")
                if len(fields) > 40:
                    f.write(f"  ... and {len(fields) - 40} more fields\n")

    print(f"\nWrote summary to {outpath}")


def main():
    print("Phase 2a: Struct/Class Field Layout Recovery")
    print("=" * 50)

    field_id_rows, field_stats = phase2a_field_id_extraction()
    offset_rows, offsets_by_class, layouts = phase2a_offset_scanning()
    write_summary(field_id_rows, field_stats, offset_rows, layouts)

    print("\nDone!")


if __name__ == "__main__":
    main()
