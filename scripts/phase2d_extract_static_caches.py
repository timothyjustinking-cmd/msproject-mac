#!/usr/bin/env python3
"""Phase 2d: Extract ALL pre-populated cache entries from the static binary.

BREAKTHROUGH: Links (30), Calendar (14), Project (9), OutlineCode (16) are
FULLY pre-populated in the binary! Also Task entry 0, Resource entry 0,
Assignment entry 0.

This script extracts every non-zero cache entry with full PropInfoData fields.
"""

import struct
import csv
from pathlib import Path

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


# PjFieldType names (from COM documentation)
TYPE_NAMES = {
    0: 'Unknown',
    1: 'Currency',
    2: 'Date',
    3: 'Duration',
    4: 'Finish',
    5: 'Work',
    6: 'Number',
    7: 'Text',
    8: 'Boolean',
    9: 'Start',
    10: 'Resource',
    11: 'Cost',
    12: 'Percent',
    13: 'Enumerated',
    14: 'Mask',
    15: 'Indicator',
    16: 'IndicatorCriteria',
    17: 'Formula',
    18: 'Guid',
    19: 'Image',
    20: 'CompoundDuration',
    21: 'Duration2',
}


def main():
    print("Phase 2d: Static Cache Extraction")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # All caches
    caches = [
        ('Task',        0x14130be10, 1444),
        ('Resource',    0x1412e0d30,  883),
        ('Assignment',  0x1412c0f30,  777),
        ('Links',       0x141378be0,   30),
        ('Calendar',    0x141381ae0,   14),
        ('Project',     0x141382aa0,    9),
        ('OutlineCode', 0x141384990,   16),
        ('Task34',      0x14137e9a0,   34),
    ]

    all_static = {}

    for cache_name, cache_va, count in caches:
        cache_off = va_to_file(cache_va, sections)
        if cache_off is None:
            print(f"\n  ERROR: Cannot read {cache_name} cache at VA 0x{cache_va:x}")
            continue

        entries = []
        for i in range(count):
            entry_off = cache_off + i * 0x40
            entry_data = data[entry_off:entry_off + 0x40]

            if all(b == 0 for b in entry_data):
                continue

            vtable = struct.unpack_from('<Q', entry_data, 0)[0]
            field_id = struct.unpack_from('<I', entry_data, 8)[0]
            type_code = struct.unpack_from('<I', entry_data, 12)[0]
            padding1 = struct.unpack_from('<Q', entry_data, 0x10)[0]
            rec_offset = struct.unpack_from('<I', entry_data, 0x18)[0]
            flags = struct.unpack_from('<I', entry_data, 0x1C)[0]
            hash_key = struct.unpack_from('<Q', entry_data, 0x20)[0]
            extra1 = struct.unpack_from('<I', entry_data, 0x28)[0]
            extra2 = struct.unpack_from('<I', entry_data, 0x2C)[0]
            size = struct.unpack_from('<I', entry_data, 0x30)[0]

            name = field_map.get(field_id, '')
            type_name = TYPE_NAMES.get(type_code, f'type_{type_code}')

            entries.append({
                'index': i,
                'field_id': field_id,
                'field_id_hex': f'0x{field_id:08x}',
                'name': name,
                'type_code': type_code,
                'type_name': type_name,
                'rec_offset': rec_offset,
                'size': size,
                'flags': flags,
                'vtable': vtable,
                'hash_key': hash_key,
                'extra1': extra1,
                'extra2': extra2,
            })

        if not entries:
            continue

        all_static[cache_name] = entries

        print(f"\n{'='*70}")
        print(f"{cache_name}: {len(entries)}/{count} pre-populated entries")
        print("-" * 70)

        # Print all entries for small caches
        if len(entries) <= 40:
            print(f"  {'Idx':>4} {'FieldID':>12} {'Type':>15} {'Offset':>8} {'Size':>6} {'Flags':>10} Name")
            for e in entries:
                print(f"  {e['index']:4d} {e['field_id_hex']:>12s} {e['type_name']:>15s} "
                      f"0x{e['rec_offset']:04x} {e['size']:6d} 0x{e['flags']:08x}  {e['name']}")
        else:
            print(f"  First 5:")
            for e in entries[:5]:
                print(f"  {e['index']:4d} {e['field_id_hex']:>12s} {e['type_name']:>15s} "
                      f"0x{e['rec_offset']:04x} {e['size']:6d}  {e['name']}")

        # Stats
        offsets = [e['rec_offset'] for e in entries if e['rec_offset'] > 0]
        types = {}
        for e in entries:
            t = e['type_name']
            types[t] = types.get(t, 0) + 1
        sizes = {}
        for e in entries:
            s = e['size']
            sizes[s] = sizes.get(s, 0) + 1

        if offsets:
            max_off = max(offsets)
            max_entry = max(e['rec_offset'] + e['size'] for e in entries if e['rec_offset'] > 0)
            print(f"\n  Offset range: 0x{min(offsets):x} - 0x{max_off:x}")
            print(f"  Max record extent: 0x{max_entry:x} ({max_entry} bytes)")
        print(f"  Type distribution: {types}")
        print(f"  Size distribution: {sizes}")

        # Write TSV
        out_path = ANALYSIS / f"static_cache_{cache_name.lower()}.tsv"
        with open(out_path, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f, delimiter='\t')
            w.writerow(['index', 'field_id_hex', 'field_id_dec', 'property_name',
                         'type_code', 'type_name', 'record_offset_hex', 'record_offset_dec',
                         'size', 'flags_hex', 'vtable_hex', 'hash_key_hex',
                         'extra1_hex', 'extra2_hex'])
            for e in entries:
                w.writerow([
                    e['index'], e['field_id_hex'], e['field_id'],
                    e['name'], e['type_code'], e['type_name'],
                    f"0x{e['rec_offset']:04x}", e['rec_offset'],
                    e['size'], f"0x{e['flags']:08x}",
                    f"0x{e['vtable']:016x}", f"0x{e['hash_key']:016x}",
                    f"0x{e['extra1']:08x}", f"0x{e['extra2']:08x}",
                ])
        print(f"  -> {out_path.name}")

    # =========================================================================
    # Summary
    # =========================================================================
    print(f"\n{'='*70}")
    print("SUMMARY OF STATIC EXTRACTIONS")
    print("=" * 70)

    total_static = sum(len(v) for v in all_static.values())
    total_fields = sum(c for _, _, c in caches)
    print(f"\n  Total static entries: {total_static} / {total_fields}")
    print(f"\n  {'Type':<15s} {'Extracted':>10s} {'Total':>8s} {'%':>6s}")
    for cache_name, cache_va, count in caches:
        if cache_name in all_static:
            n = len(all_static[cache_name])
            pct = f"{100*n/count:.0f}%"
        else:
            n = 0
            pct = "0%"
        print(f"  {cache_name:<15s} {n:>10d} {count:>8d} {pct:>6s}")

    print(f"\n  STILL NEED RUNTIME DUMP FOR:")
    for cache_name, cache_va, count in caches:
        n = len(all_static.get(cache_name, []))
        if n < count:
            print(f"    {cache_name}: {count - n} entries (have {n}/{count})")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
