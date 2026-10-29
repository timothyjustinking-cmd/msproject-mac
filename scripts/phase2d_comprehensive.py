#!/usr/bin/env python3
"""Phase 2d: Comprehensive field extraction — combine all discovered data.

Produces the definitive output files:
1. All 1444 Task field IDs with property names and categories
2. All 562 Resource field IDs with property names
3. All 564 Assignment field IDs with property names
4. The 67 core SQL-layer offsets
5. Category classification for each field
6. Decode the SQL DDL strings to extract column definitions
"""

import struct
import csv
import re
from pathlib import Path
from collections import Counter

IMAGE_BASE = 0x140000000
WINPROJ = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
BASE = Path(__file__).resolve().parents[1]
ANALYSIS = BASE / "analysis"


def parse_pe_sections(data):
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    coff_offset = e_lfanew + 4
    num_sections = struct.unpack_from('<H', data, coff_offset + 2)[0]
    opt_header_size = struct.unpack_from('<H', data, coff_offset + 16)[0]
    section_offset = coff_offset + 20 + opt_header_size
    sections = []
    for i in range(num_sections):
        off = section_offset + i * 40
        name = data[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
        sections.append({
            'name': name,
            'virt_addr': struct.unpack_from('<I', data, off + 12)[0],
            'virt_size': struct.unpack_from('<I', data, off + 8)[0],
            'raw_ptr': struct.unpack_from('<I', data, off + 20)[0],
            'raw_size': struct.unpack_from('<I', data, off + 16)[0],
        })
    return sections


def va_to_file(va, sections):
    rva = va - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            off = rva - s['virt_addr'] + s['raw_ptr']
            if off < s['raw_ptr'] + s['raw_size']:
                return off
    return None


def load_field_id_map():
    fmap = {}
    path = ANALYSIS / "field_id_map.tsv"
    if not path.exists():
        return fmap
    with open(path, 'r') as f:
        for row in csv.DictReader(f, delimiter='\t'):
            fid_str = row['field_id']
            fid = int(fid_str, 16) if fid_str.startswith('0x') else int(fid_str)
            prop = row['property_name']
            disp = row.get('dispatcher', '')
            if disp == 'get' or fid not in fmap:
                fmap[fid] = prop
    return fmap


def get_category(index):
    """Classify field by its index in the 1444-entry array."""
    boundaries = [
        (0, 1021, 'Standard'),
        (1021, 1043, 'CategoryA'),
        (1043, 1046, 'CategoryB'),
        (1046, 1069, 'CategoryC'),
        (1069, 1082, 'CategoryD'),
        (1082, 1084, 'CategoryE'),
        (1084, 1134, 'CategoryF'),
        (1134, 1156, 'CategoryG'),
        (1156, 1444, 'Extended'),
    ]
    for lo, hi, name in boundaries:
        if lo <= index < hi:
            return name
    return 'Unknown'


def main():
    print("Phase 2d: Comprehensive Field Extraction")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # ===== Read all field ID arrays =====
    fid_off = va_to_file(0x1410e77a0, sections)
    task_fids = [struct.unpack_from('<I', data, fid_off + i*4)[0] for i in range(1444)]

    rsc_off = va_to_file(0x1410e8f50, sections)
    rsc_fids = [struct.unpack_from('<I', data, rsc_off + i*4)[0] for i in range(562)]

    assn_off = va_to_file(0x1410e6b70, sections)
    assn_fids = [struct.unpack_from('<I', data, assn_off + i*4)[0] for i in range(564)]

    # Calendar, Project, OutlineCode
    cal_off = va_to_file(0x140fb9520, sections)
    cal_fids = [struct.unpack_from('<I', data, cal_off + i*4)[0] for i in range(9)]

    proj_off = va_to_file(0x140fb9568, sections)
    proj_fids = [struct.unpack_from('<I', data, proj_off + i*4)[0] for i in range(9)]

    oc_off = va_to_file(0x140fb95a0, sections)
    oc_fids = [struct.unpack_from('<I', data, oc_off + i*4)[0] for i in range(10)]

    # ===== Read core SQL offsets from gap =====
    gap_off = va_to_file(0x1410e8e30, sections)
    gap = [struct.unpack_from('<I', data, gap_off + i*4)[0] for i in range(72)]
    task_sql_offsets = gap[0:24]
    rsc_sql_offsets = gap[28:48]
    assn_sql_offsets = gap[48:71]

    # ===== Write Task field catalog =====
    out_path = ANALYSIS / "task_field_catalog.tsv"
    with open(out_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['index', 'field_id_hex', 'field_id_dec', 'property_name',
                     'category', 'sql_offset_hex', 'sql_offset_dec'])
        for i, fid in enumerate(task_fids):
            cat = get_category(i)
            sql_off = task_sql_offsets[i] if i < 24 else ''
            sql_hex = f'0x{sql_off:04x}' if isinstance(sql_off, int) else ''
            w.writerow([
                i, f'0x{fid:08x}', fid,
                field_map.get(fid, ''),
                cat, sql_hex, sql_off,
            ])
    print(f"Wrote {len(task_fids)} Task fields to {out_path}")

    # Category distribution
    cat_counts = Counter(get_category(i) for i in range(1444))
    print(f"  Category distribution: {dict(cat_counts)}")
    named = sum(1 for fid in task_fids if fid in field_map)
    print(f"  Named fields: {named}/{len(task_fids)}")

    # ===== Write Resource field catalog =====
    out_path = ANALYSIS / "resource_field_catalog.tsv"
    with open(out_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['index', 'field_id_hex', 'field_id_dec', 'property_name',
                     'sql_offset_hex', 'sql_offset_dec'])
        for i, fid in enumerate(rsc_fids):
            sql_off = rsc_sql_offsets[i] if i < 20 else ''
            sql_hex = f'0x{sql_off:04x}' if isinstance(sql_off, int) else ''
            w.writerow([
                i, f'0x{fid:08x}', fid,
                field_map.get(fid, ''),
                sql_hex, sql_off,
            ])
    print(f"Wrote {len(rsc_fids)} Resource fields to {out_path}")
    named_r = sum(1 for fid in rsc_fids if fid in field_map)
    print(f"  Named fields: {named_r}/{len(rsc_fids)}")

    # ===== Write Assignment field catalog =====
    out_path = ANALYSIS / "assignment_field_catalog.tsv"
    with open(out_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['index', 'field_id_hex', 'field_id_dec', 'property_name',
                     'sql_offset_hex', 'sql_offset_dec'])
        for i, fid in enumerate(assn_fids):
            sql_off = assn_sql_offsets[i] if i < 23 else ''
            sql_hex = f'0x{sql_off:04x}' if isinstance(sql_off, int) else ''
            w.writerow([
                i, f'0x{fid:08x}', fid,
                field_map.get(fid, ''),
                sql_hex, sql_off,
            ])
    print(f"Wrote {len(assn_fids)} Assignment fields to {out_path}")
    named_a = sum(1 for fid in assn_fids if fid in field_map)
    print(f"  Named fields: {named_a}/{len(assn_fids)}")

    # ===== Write all field types =====
    out_path = ANALYSIS / "all_field_types.tsv"
    with open(out_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['record_type', 'count', 'id_prefix', 'array_va'])
        w.writerow(['Task', 1444, '0x0b4', '0x1410e77a0'])
        w.writerow(['Resource', 562, '0x0c4', '0x1410e8f50'])
        w.writerow(['Assignment', 564, '0x0f4', '0x1410e6b70'])
        w.writerow(['Calendar', 9, '0x3c4', '0x140fb9520'])
        w.writerow(['Project', 9, '0x074', '0x140fb9568'])
        w.writerow(['OutlineCode', 10, '0x3e4', '0x140fb95a0'])
    print(f"Wrote field type summary to {out_path}")

    # ===== Decode SQL DDL strings =====
    print(f"\n{'='*70}")
    print("SQL DDL STRING DECODE")
    print("=" * 70)

    ddl_ptrs = [
        (0x1410ea4d0, "DDL_A"),
        (0x1410ea450, "DDL_B"),
        (0x1410ea2e0, "DDL_C"),
        (0x1410ea298, "DDL_D"),
        (0x1410ea418, "DDL_E"),
        (0x1410ea3b0, "DDL_F"),
        (0x1410ea930, "DDL_G"),
        (0x1410ea8a0, "DDL_H"),
        (0x1410e9f58, "DDL_I"),
    ]

    all_ddl = []
    for va, label in ddl_ptrs:
        foff = va_to_file(va, sections)
        # Read up to 8KB of ASCII text
        raw = data[foff:foff + 8192]
        # Find null terminator or end of ASCII
        end = 0
        for j in range(len(raw)):
            if raw[j] == 0:
                end = j
                break
            if raw[j] > 127:
                end = j
                break
        else:
            end = len(raw)

        text = raw[:end].decode('ascii', errors='replace').strip()
        if text:
            all_ddl.append((label, va, text))
            # Show first 200 chars
            preview = text[:200] + ('...' if len(text) > 200 else '')
            print(f"\n{label} at VA 0x{va:x} ({len(text)} chars):")
            print(f"  {preview}")

    # Write DDL to file
    ddl_path = ANALYSIS / "sql_ddl_strings.txt"
    with open(ddl_path, 'w', encoding='utf-8') as f:
        for label, va, text in all_ddl:
            f.write(f"=== {label} at VA 0x{va:x} ===\n")
            f.write(text + '\n\n')
    print(f"\nWrote SQL DDL to {ddl_path}")

    # Parse column definitions from DDL
    print(f"\n{'='*70}")
    print("SQL COLUMN DEFINITIONS EXTRACTED")
    print("=" * 70)

    columns = []
    for label, va, text in all_ddl:
        # Find table names
        for match in re.finditer(r'(?:alter table|CREATE TABLE)\s+(MSP_\w+)', text, re.IGNORECASE):
            table = match.group(1)
        # Find column definitions
        for match in re.finditer(r'(\w+)\s+(int|smallint|decimal\([^)]+\)|text|bit|datetime|image|varchar\(\d+\)|NUMBER\([^)]+\))', text, re.IGNORECASE):
            col_name = match.group(1)
            col_type = match.group(2)
            columns.append((col_name, col_type, label))

    # Deduplicate
    unique_cols = {}
    for col_name, col_type, src in columns:
        if col_name not in unique_cols:
            unique_cols[col_name] = (col_type, src)

    col_path = ANALYSIS / "sql_columns.tsv"
    with open(col_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['column_name', 'sql_type', 'source'])
        for col_name in sorted(unique_cols):
            col_type, src = unique_cols[col_name]
            w.writerow([col_name, col_type, src])
    print(f"Extracted {len(unique_cols)} unique SQL column definitions to {col_path}")

    # ===== Summary statistics =====
    print(f"\n{'='*70}")
    print("SUMMARY")
    print("=" * 70)
    print(f"Total field IDs discovered:")
    print(f"  Task:       1444 ({named} named)")
    print(f"  Resource:    562 ({named_r} named)")
    print(f"  Assignment:  564 ({named_a} named)")
    print(f"  Calendar:      9")
    print(f"  Project:       9")
    print(f"  OutlineCode:  10")
    print(f"  TOTAL:      {1444+562+564+9+9+10}")
    print(f"")
    print(f"SQL-layer core field offsets: 24 Task + 20 Resource + 23 Assignment = 67")
    print(f"SQL column definitions: {len(unique_cols)}")
    print(f"")
    print(f"Runtime cache entry structure:")
    print(f"  Size: 0x40 bytes per field")
    print(f"  Key fields: +0x08=field_id, +0x0C=type, +0x18=offset, +0x30=size")
    print(f"  Pre-populated: 1/1444 (entry 0 = Work)")
    print(f"  Remaining 1443 entries: populated at runtime")
    print(f"")
    print(f"To extract ALL scheduling engine offsets:")
    print(f"  1. Start MS Project with a .mpp file open")
    print(f"  2. Run: python scripts/phase2d_dump_cache.py")
    print(f"  3. This reads the runtime cache from the live process")

    print("\nDone.")


if __name__ == '__main__':
    main()
