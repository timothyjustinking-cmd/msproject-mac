#!/usr/bin/env python3
"""Phase 2d: Generate the definitive field catalog for all record types.

Cross-references:
1. Engine field IDs from descriptors (sorted arrays in .data)
2. COM property names from field_id_map.tsv
3. Static pre-populated cache entries (entry 0 for Task/Assignment)
4. SQL/OLE DB field arrays
5. Category boundaries from descriptors

Outputs per-type TSV with: index, field_id, name, category, static_type, static_offset, static_size
"""

import struct
import csv
from pathlib import Path

IMAGE_BASE = 0x140000000
WINPROJ = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
BASE = Path(r"C:\Users\timot\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile")
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


def file_to_va(foff, sections):
    for s in sections:
        if s['raw_ptr'] <= foff < s['raw_ptr'] + s['raw_size']:
            rva = foff - s['raw_ptr'] + s['virt_addr']
            return IMAGE_BASE + rva
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


def extract_sorted_array(data, sections, va, count):
    """Extract sorted field ID array from .data."""
    off = va_to_file(va, sections)
    if off is None:
        return []
    ids = []
    for i in range(count):
        v = struct.unpack_from('<I', data, off + i * 4)[0]
        ids.append(v)
    return ids


def extract_cache_entry(data, sections, cache_va, index):
    """Extract a single cache entry."""
    off = va_to_file(cache_va, sections)
    if off is None:
        return None
    entry_off = off + index * 0x40
    entry_data = data[entry_off:entry_off + 0x40]
    if all(b == 0 for b in entry_data):
        return None
    return {
        'vtable': struct.unpack_from('<Q', entry_data, 0)[0],
        'field_id': struct.unpack_from('<I', entry_data, 8)[0],
        'type_code': struct.unpack_from('<I', entry_data, 12)[0],
        'rec_offset': struct.unpack_from('<I', entry_data, 0x18)[0],
        'flags': struct.unpack_from('<I', entry_data, 0x1C)[0],
        'size': struct.unpack_from('<I', entry_data, 0x30)[0],
    }


def get_category(index, boundaries, type_name):
    """Determine category from index and boundary list."""
    if type_name == 'Task':
        cats = [
            (0, 1020, 'Standard'),
            (1021, 1042, 'Estimated'),
            (1043, 1045, 'OutlineCodes'),
            (1046, 1068, 'EnterpriseOC_1'),
            (1069, 1081, 'EnterpriseOC_2'),
            (1082, 1083, 'BaselineEst_1'),
            (1084, 1133, 'BaselineEst_2'),
            (1134, 1155, 'Variance'),
            (1156, 1443, 'Extended'),
        ]
        for start, end, name in cats:
            if start <= index <= end:
                return name
    elif type_name == 'Assignment':
        # Boundaries: 1023, 1044, 1048, 1071, 1136, 0, 0, 1158
        cats = [
            (0, 1022, 'Standard'),
            (1023, 1043, 'Category_2'),
            (1044, 1047, 'Category_3'),
            (1048, 1070, 'Category_4'),
            (1071, 1135, 'Category_5'),
            (1136, 1157, 'Category_6'),
            (1158, 776, 'Extended'),
        ]
        for start, end, name in cats:
            if start <= index <= end or (name == 'Extended' and index >= start):
                return name
    elif type_name == 'Resource':
        cats = [
            (0, 1021, 'Standard'),
            (1022, 1043, 'Category_2'),
            (1044, 1046, 'Category_3'),
            (1047, 1069, 'Category_4'),
            (1070, 1133, 'Category_5'),
            (1134, 1135, 'Category_6'),
            (1136, 1157, 'Category_7'),
            (1158, 882, 'Extended'),
        ]
        for start, end, name in cats:
            if start <= index <= end or (name == 'Extended' and index >= start):
                return name
    return 'Unknown'


def main():
    print("Phase 2d: Definitive Field Catalog Generation")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # Descriptor info
    descs = {
        'Task': {
            'desc_va': 0x140f8bda0, 'cache_va': 0x14130be10, 'count': 1444,
            'sorted_va': 0x1412a9060, 'prefix': 0x0b4,
        },
        'Resource': {
            'desc_va': 0x140f8bf60, 'cache_va': 0x1412e0d30, 'count': 883,
            'sorted_va': None, 'prefix': 0x0c4,
        },
        'Assignment': {
            'desc_va': 0x140f86410, 'cache_va': 0x1412c0f30, 'count': 777,
            'sorted_va': None, 'prefix': 0x0f4,
        },
        'Links': {
            'desc_va': 0x140f8c080, 'cache_va': 0x141378be0, 'count': 30,
            'sorted_va': None, 'prefix': 0x0e4,
        },
        'Calendar': {
            'desc_va': 0x140f863b0, 'cache_va': 0x141381ae0, 'count': 14,
            'sorted_va': None, 'prefix': 0x3c4,
        },
        'Project': {
            'desc_va': 0x140f85ff0, 'cache_va': 0x141382aa0, 'count': 9,
            'sorted_va': None, 'prefix': 0x074,
        },
        'OutlineCode': {
            'desc_va': 0x140f86350, 'cache_va': 0x141384990, 'count': 16,
            'sorted_va': None, 'prefix': 0x3e4,
        },
    }

    # Scan .data for sorted arrays for each type
    print("\n1. FINDING SORTED ARRAYS FOR ALL RECORD TYPES")
    print("-" * 60)

    # Scan the known .data region (0x1412a0000-0x1412e0000) for sorted arrays
    for tname, info in descs.items():
        if info['sorted_va'] is not None:
            continue
        prefix = info['prefix']
        target_count = info['count']

        # Scan for sorted arrays with this prefix
        scan_start = 0x1412a0000
        scan_end = 0x1412e0000
        start_off = va_to_file(scan_start, sections)
        end_off = va_to_file(scan_end, sections)

        best_va = None
        best_count = 0

        if start_off and end_off:
            pos = start_off
            while pos < end_off - 16:
                vals = []
                check = pos
                while check < end_off - 4:
                    v = struct.unpack_from('<I', data, check)[0]
                    if (v >> 20) == prefix and v < (prefix << 20) + 0x10000:
                        if not vals or v >= vals[-1]:
                            vals.append(v)
                            check += 4
                        else:
                            break
                    else:
                        break

                if len(vals) >= 5 and len(vals) > best_count:
                    va = file_to_va(pos, sections)
                    best_va = va
                    best_count = len(vals)
                    if len(vals) == target_count:
                        break

                pos = check if check > pos else pos + 4

        if best_va:
            info['sorted_va'] = best_va
            info['sorted_count'] = best_count
            match = "EXACT" if best_count == target_count else f"PARTIAL ({best_count}/{target_count})"
            print(f"  {tname}: VA 0x{best_va:x}, {best_count} entries ({match})")
        else:
            print(f"  {tname}: No sorted array found (runtime-constructed)")

    # =========================================================================
    # 2. Generate per-type field catalogs
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. GENERATING FIELD CATALOGS")
    print("-" * 60)

    for tname in ['Task', 'Resource', 'Assignment', 'Links', 'Calendar', 'Project', 'OutlineCode']:
        info = descs[tname]
        count = info['count']
        cache_va = info['cache_va']

        # Read sorted array if available
        sorted_ids = []
        if info.get('sorted_va'):
            sorted_count = info.get('sorted_count', count)
            sorted_ids = extract_sorted_array(data, sections, info['sorted_va'], sorted_count)

        # Read any pre-populated cache entries
        cache_entries = {}
        cache_off = va_to_file(cache_va, sections)
        if cache_off:
            for i in range(count):
                entry = extract_cache_entry(data, sections, cache_va, i)
                if entry:
                    cache_entries[i] = entry

        # Build catalog
        rows = []
        for i in range(count):
            fid = sorted_ids[i] if i < len(sorted_ids) else None
            entry = cache_entries.get(i)

            # If no sorted array, try the cache entry's field_id
            if fid is None and entry:
                fid = entry.get('field_id', 0)
            if fid is None:
                fid = 0

            name = field_map.get(fid, '')
            cat = get_category(i, [], tname) if tname in ('Task', 'Resource', 'Assignment') else ''

            row = {
                'index': i,
                'field_id': fid,
                'field_id_hex': f'0x{fid:08x}' if fid else '',
                'name': name,
                'category': cat,
                'static_type': entry['type_code'] if entry else '',
                'static_offset': f"0x{entry['rec_offset']:x}" if entry else '',
                'static_size': entry['size'] if entry else '',
                'static_flags': f"0x{entry['flags']:x}" if entry else '',
            }
            rows.append(row)

        # Write TSV
        out_path = ANALYSIS / f"field_catalog_{tname.lower()}.tsv"
        with open(out_path, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f, delimiter='\t')
            w.writerow(['index', 'field_id_hex', 'field_id_dec', 'property_name', 'category',
                         'static_type', 'static_offset', 'static_size', 'static_flags'])
            for r in rows:
                w.writerow([
                    r['index'], r['field_id_hex'], r['field_id'],
                    r['name'], r['category'],
                    r['static_type'], r['static_offset'],
                    r['static_size'], r['static_flags'],
                ])

        named = sum(1 for r in rows if r['name'])
        with_entry = len(cache_entries)
        print(f"  {tname}: {count} fields, {named} named, {with_entry} pre-populated "
              f"-> {out_path.name}")

    # =========================================================================
    # 3. Summary statistics
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. SUMMARY STATISTICS")
    print("-" * 60)

    # Count total unique field IDs across all types
    all_field_ids = set()
    all_named = 0
    for tname in descs:
        info = descs[tname]
        if info.get('sorted_va'):
            ids = extract_sorted_array(data, sections, info['sorted_va'],
                                       info.get('sorted_count', info['count']))
            all_field_ids.update(ids)
            all_named += sum(1 for fid in ids if fid in field_map)

    print(f"  Total field IDs in sorted arrays: {len(all_field_ids)}")
    print(f"  Named (in COM dispatch map): {all_named}")
    print(f"  Total property names in map: {len(field_map)}")
    print(f"  Unnamed engine fields: {len(all_field_ids) - all_named}")

    # Per-type breakdown
    print(f"\n  {'Type':<15s} {'Engine':>8s} {'Named':>8s} {'Unnamed':>8s} {'%Named':>8s}")
    for tname in ['Task', 'Resource', 'Assignment', 'Links', 'Calendar', 'Project', 'OutlineCode']:
        info = descs[tname]
        count = info['count']
        if info.get('sorted_va'):
            ids = extract_sorted_array(data, sections, info['sorted_va'],
                                       info.get('sorted_count', info['count']))
            named = sum(1 for fid in ids if fid in field_map)
        else:
            ids = []
            named = 0
        unnamed = len(ids) - named if ids else count
        pct = f"{100*named/count:.0f}%" if count > 0 else "n/a"
        print(f"  {tname:<15s} {count:>8d} {named:>8d} {unnamed:>8d} {pct:>8s}")

    # =========================================================================
    # 4. Cross-reference with OLE DB arrays
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. ENGINE vs OLE DB FIELD ID COMPARISON")
    print("-" * 60)

    ole_arrays = {
        'Task': (0x1410e77a0, 1444),
        'Resource': (0x1410e8f50, 562),
        'Assignment': (0x1410e6b70, 564),
    }

    for tname, (ole_va, ole_count) in ole_arrays.items():
        info = descs[tname]
        if not info.get('sorted_va'):
            continue

        engine_ids = set(extract_sorted_array(data, sections, info['sorted_va'],
                                              info.get('sorted_count', info['count'])))
        ole_ids = set(extract_sorted_array(data, sections, ole_va, ole_count))

        both = engine_ids & ole_ids
        engine_only = engine_ids - ole_ids
        ole_only = ole_ids - engine_ids

        print(f"  {tname}:")
        print(f"    Engine fields: {len(engine_ids)}")
        print(f"    OLE DB fields: {len(ole_ids)}")
        print(f"    In both: {len(both)}")
        print(f"    Engine-only: {len(engine_only)}")
        print(f"    OLE DB-only: {len(ole_only)}")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
