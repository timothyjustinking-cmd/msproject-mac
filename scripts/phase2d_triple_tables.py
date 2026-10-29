#!/usr/bin/env python3
"""Phase 2d: Deep-dive into the (field_id, type, offset) triple tables
and extended index arrays.

Found a 14-entry triple table at VA 0x14111a854. Need to find MORE such
tables — there should be data for all 1444 fields somewhere.

Also found a second index array at VA 0x1410fb418 with non-Standard indices.
"""

import struct
import csv
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


def main():
    print("Phase 2d: Triple Tables & Extended Index Arrays")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # Load task field IDs
    fid_off = va_to_file(0x1410e77a0, sections)
    task_fids = [struct.unpack_from('<I', data, fid_off + i*4)[0] for i in range(1444)]

    # ===== 1. Detailed analysis of triple table at 0x14111a854 =====
    print("\n1. TRIPLE TABLE AT VA 0x14111a854")
    print("-" * 50)

    table_va = 0x14111a854
    table_off = va_to_file(table_va, sections)

    # Read entries with relaxed criteria — allow type up to 100, offset up to 0x10000
    triples = []
    for i in range(200):
        fid = struct.unpack_from('<I', data, table_off + i*12)[0]
        tc = struct.unpack_from('<I', data, table_off + i*12 + 4)[0]
        off_val = struct.unpack_from('<I', data, table_off + i*12 + 8)[0]

        if 0x0b400000 <= fid <= 0x0f4fffff and tc <= 100:
            triples.append((fid, tc, off_val))
        else:
            print(f"  End at entry {i}: fid=0x{fid:08x} tc={tc} off=0x{off_val:08x}")
            break

    print(f"  Total entries: {len(triples)}")
    print(f"\n  {'FieldID':>12} {'Type':>5} {'Offset':>8} Name")
    print(f"  {'-'*12} {'-'*5} {'-'*8} {'-'*30}")
    for fid, tc, off_val in triples:
        name = field_map.get(fid, '')
        print(f"  0x{fid:08x} {tc:5d} 0x{off_val:04x}  {name}")

    # What's BEFORE the triple table?
    print(f"\n  Data BEFORE triple table:")
    for i in range(10, 0, -1):
        off = table_off - i * 4
        val = struct.unpack_from('<I', data, off)[0]
        va = table_va - i * 4
        print(f"    VA 0x{va:x}: 0x{val:08x} ({val})")

    # ===== 2. Scan wider around 0x14111a854 for MORE triple tables =====
    print(f"\n{'='*70}")
    print("2. SCANNING AROUND 0x14111a854 FOR MORE TABLES")
    print("-" * 50)

    # Scan +/- 0x10000 bytes for stride-12 runs
    scan_center = va_to_file(0x14111a854, sections)
    scan_start = max(0, scan_center - 0x10000)
    scan_end = min(len(data) - 12, scan_center + 0x10000)
    scan_va_base = 0x14111a854 - (scan_center - scan_start)

    found_tables = []
    pos = scan_start
    while pos < scan_end:
        fid = struct.unpack_from('<I', data, pos)[0]
        tc = struct.unpack_from('<I', data, pos + 4)[0]
        off_val = struct.unpack_from('<I', data, pos + 8)[0]

        if 0x0b400000 <= fid <= 0x0f4fffff and tc <= 30 and off_val <= 0x8000:
            run = 1
            for j in range(1, 500):
                if pos + (j+1)*12 > scan_end:
                    break
                f2 = struct.unpack_from('<I', data, pos + j*12)[0]
                t2 = struct.unpack_from('<I', data, pos + j*12 + 4)[0]
                o2 = struct.unpack_from('<I', data, pos + j*12 + 8)[0]
                if 0x0b400000 <= f2 <= 0x0f4fffff and t2 <= 30 and o2 <= 0x8000:
                    run += 1
                else:
                    break

            if run >= 5:
                va = scan_va_base + (pos - scan_start)
                found_tables.append((va, pos, run))
                print(f"\n  Table at VA 0x{va:x}: {run} triples")
                for j in range(min(run, 10)):
                    f = struct.unpack_from('<I', data, pos + j*12)[0]
                    t = struct.unpack_from('<I', data, pos + j*12 + 4)[0]
                    o = struct.unpack_from('<I', data, pos + j*12 + 8)[0]
                    name = field_map.get(f, '')
                    print(f"    0x{f:08x} type={t:2d} off=0x{o:04x} {name}")
                if run > 10:
                    print(f"    ... ({run - 10} more)")
                pos += run * 12
                continue

        pos += 4

    print(f"\n  Total tables found: {len(found_tables)}")

    # ===== 3. Full .rdata scan for ALL triple tables =====
    print(f"\n{'='*70}")
    print("3. FULL .RDATA SCAN FOR TRIPLE TABLES")
    print("-" * 50)

    rdata = None
    for s in sections:
        if s['name'] == '.rdata':
            rdata = s
            break

    all_triple_tables = []
    if rdata:
        rdata_off = rdata['raw_ptr']
        rdata_size = rdata['raw_size']
        rdata_va = IMAGE_BASE + rdata['virt_addr']

        pos = 0
        while pos < rdata_size - 12:
            fid = struct.unpack_from('<I', data, rdata_off + pos)[0]
            tc = struct.unpack_from('<I', data, rdata_off + pos + 4)[0]
            off_val = struct.unpack_from('<I', data, rdata_off + pos + 8)[0]

            if 0x0b400000 <= fid <= 0x0f4fffff and tc <= 50 and off_val <= 0x8000:
                run = 1
                entries = [(fid, tc, off_val)]
                for j in range(1, 1500):
                    if pos + (j+1)*12 > rdata_size:
                        break
                    f2 = struct.unpack_from('<I', data, rdata_off + pos + j*12)[0]
                    t2 = struct.unpack_from('<I', data, rdata_off + pos + j*12 + 4)[0]
                    o2 = struct.unpack_from('<I', data, rdata_off + pos + j*12 + 8)[0]
                    if 0x0b400000 <= f2 <= 0x0f4fffff and t2 <= 50 and o2 <= 0x8000:
                        run += 1
                        entries.append((f2, t2, o2))
                    else:
                        break

                if run >= 5:
                    va = rdata_va + pos
                    all_triple_tables.append((va, run, entries))
                    pos += run * 12
                    continue

            pos += 4

    print(f"  Total triple tables in .rdata: {len(all_triple_tables)}")
    total_entries = sum(run for _, run, _ in all_triple_tables)
    print(f"  Total entries across all tables: {total_entries}")

    for i, (va, run, entries) in enumerate(all_triple_tables):
        # Determine field type prefix
        prefixes = Counter((fid >> 20) & 0xFFF for fid, _, _ in entries)
        prefix_str = ', '.join(f"0x{p:03x}:{c}" for p, c in prefixes.most_common(3))
        type_counts = Counter(tc for _, tc, _ in entries)
        type_str = ', '.join(f"t{t}:{c}" for t, c in type_counts.most_common(5))
        offset_range = (min(o for _, _, o in entries), max(o for _, _, o in entries))

        print(f"\n  Table {i}: VA 0x{va:x}, {run} entries")
        print(f"    Prefixes: {prefix_str}")
        print(f"    Types: {type_str}")
        print(f"    Offset range: 0x{offset_range[0]:04x} - 0x{offset_range[1]:04x}")

        # Show first 5 entries
        for fid, tc, off_val in entries[:5]:
            name = field_map.get(fid, '')
            print(f"    0x{fid:08x} t={tc:2d} off=0x{off_val:04x} {name}")
        if run > 5:
            print(f"    ... ({run - 5} more)")

    # ===== 4. Collect ALL entries and write to file =====
    if all_triple_tables:
        print(f"\n{'='*70}")
        print("4. COMBINED TRIPLE TABLE OUTPUT")
        print("-" * 50)

        all_entries = []
        for va, run, entries in all_triple_tables:
            for fid, tc, off_val in entries:
                all_entries.append({
                    'table_va': va,
                    'field_id': fid,
                    'type_code': tc,
                    'offset': off_val,
                    'name': field_map.get(fid, ''),
                })

        out_path = ANALYSIS / "static_field_triples.tsv"
        with open(out_path, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f, delimiter='\t')
            w.writerow(['table_va_hex', 'field_id_hex', 'type_code', 'offset_hex',
                         'offset_dec', 'property_name'])
            for e in all_entries:
                w.writerow([f"0x{e['table_va']:x}", f"0x{e['field_id']:08x}",
                            e['type_code'], f"0x{e['offset']:04x}",
                            e['offset'], e['name']])
        print(f"  Wrote {len(all_entries)} total entries to {out_path}")

        # Stats
        unique_fids = set(e['field_id'] for e in all_entries)
        print(f"  Unique field IDs: {len(unique_fids)}")
        prefix_counts = Counter((e['field_id'] >> 20) & 0xFFF for e in all_entries)
        for p, c in prefix_counts.most_common():
            pname = {0x0b4:'Task', 0x0c4:'Resource', 0x0f4:'Assignment'}.get(p, f'0x{p:03x}')
            print(f"    {pname}: {c} entries")

    # ===== 5. Extended index arrays after the 115-entry array =====
    print(f"\n{'='*70}")
    print("5. EXTENDED INDEX ARRAYS")
    print("-" * 50)

    # The 115-entry array at 0x1410fb248 + header(4) + 115*4 = 0x1410fb418
    # What follows are more indices (1021, 1022, 1033, ...)
    ext_va = 0x1410fb418
    ext_off = va_to_file(ext_va, sections)

    # Read until we hit non-index data (value > 1444 or 0)
    ext_indices = []
    for i in range(500):
        val = struct.unpack_from('<I', data, ext_off + i*4)[0]
        if val > 1444:
            print(f"  Extended array ends at entry {i}: value={val}")
            break
        ext_indices.append(val)

    print(f"  Read {len(ext_indices)} extended indices")
    if ext_indices:
        print(f"  Range: {min(ext_indices)} - {max(ext_indices)}")
        print(f"  Sorted: {sorted(ext_indices) == ext_indices}")

        # Show which categories these fall into
        cat_dist = Counter()
        for idx in ext_indices:
            if idx < 1021:
                cat_dist['Standard'] += 1
            elif idx < 1043:
                cat_dist['Estimated'] += 1
            elif idx < 1046:
                cat_dist['OutlineCodes'] += 1
            elif idx < 1069:
                cat_dist['EnterpriseOC_1'] += 1
            elif idx < 1082:
                cat_dist['EnterpriseOC_2'] += 1
            elif idx < 1084:
                cat_dist['BaselineEst_1'] += 1
            elif idx < 1134:
                cat_dist['BaselineEst_2'] += 1
            elif idx < 1156:
                cat_dist['Variance'] += 1
            else:
                cat_dist['Extended'] += 1
        print(f"  Category distribution: {dict(cat_dist)}")

        # Show named fields
        print(f"\n  Named fields in extended array:")
        named_count = 0
        for i, idx in enumerate(ext_indices):
            if idx < 1444:
                fid = task_fids[idx]
                name = field_map.get(fid, '')
                if name:
                    named_count += 1
                    if named_count <= 30:
                        print(f"    [{i:3d}] idx={idx:4d} fid=0x{fid:08x} {name}")
        if named_count > 30:
            print(f"    ... ({named_count - 30} more named)")
        print(f"  Total named: {named_count}/{len(ext_indices)}")

    # Check what follows the extended array
    after_ext_off = ext_off + len(ext_indices) * 4
    print(f"\n  Data after extended array:")
    for i in range(15):
        val = struct.unpack_from('<I', data, after_ext_off + i*4)[0]
        va = ext_va + len(ext_indices)*4 + i*4
        label = ''
        if 0x0b400000 <= val <= 0x0f4fffff:
            label = f'FIELD_ID {field_map.get(val, "")}'
        print(f"    VA 0x{va:x}: 0x{val:08x} ({val})  {label}")

    print(f"\n{'='*70}")
    print("DONE")
    print("=" * 70)


if __name__ == '__main__':
    main()
