#!/usr/bin/env python3
"""Phase 2d: Full extraction of field_id -> record_offset mapping.

Key discovery: The .rdata array at VA 0x1410e77a0 contains 1444 Task field IDs
(packed u32, stride 4), immediately followed by a parallel u32 array of offsets
starting at VA 0x1410e8e30 (= 0x1410e77a0 + 1444*4).

This script:
1. Extracts the field_id + offset parallel arrays from .rdata Table 2
2. Looks for additional parallel arrays (type codes, sizes, flags)
3. Cross-references with field_id_map.tsv for property names
4. Produces the definitive field_id -> (offset, type, name) mapping
"""

import struct
import csv
from collections import Counter
from pathlib import Path

IMAGE_BASE = 0x140000000
WINPROJ = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
BASE = Path(r"C:\Users\timot\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile")
ANALYSIS = BASE / "analysis"

FIELD_IDS_VA = 0x1410e77a0
FIELD_COUNT = 1444


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


def read_u32_array(data, file_offset, count):
    return [struct.unpack_from('<I', data, file_offset + i * 4)[0] for i in range(count)]


def read_u16_array(data, file_offset, count):
    return [struct.unpack_from('<H', data, file_offset + i * 2)[0] for i in range(count)]


def read_u8_array(data, file_offset, count):
    return [data[file_offset + i] for i in range(count)]


def analyze_parallel_array(values, name, expected_count):
    """Analyze a candidate parallel array to determine if it's meaningful."""
    unique = len(set(values[:expected_count]))
    vals = values[:expected_count]
    min_v = min(vals)
    max_v = max(vals)
    zeros = vals.count(0)
    nonzero = [v for v in vals if v > 0]
    return {
        'name': name,
        'unique': unique,
        'min': min_v,
        'max': max_v,
        'zeros': zeros,
        'nonzero_count': len(nonzero),
        'nonzero_min': min(nonzero) if nonzero else 0,
        'nonzero_max': max(nonzero) if nonzero else 0,
    }


def main():
    print("Phase 2d: Full Field Descriptor Extraction")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()
    print(f"Loaded {len(field_map)} field ID -> property name mappings")

    # Read field ID array
    fid_offset = va_to_file(FIELD_IDS_VA, sections)
    field_ids = read_u32_array(data, fid_offset, FIELD_COUNT)
    print(f"\nField ID array at VA 0x{FIELD_IDS_VA:x}: {len(field_ids)} entries")
    print(f"  All Task IDs: {all(0x0b400000 <= v <= 0x0b4fffff for v in field_ids)}")
    print(f"  All unique: {len(set(field_ids)) == len(field_ids)}")

    # The parallel offset array starts right after: VA + 1444*4
    offsets_va = FIELD_IDS_VA + FIELD_COUNT * 4
    offsets_file = va_to_file(offsets_va, sections)
    offsets = read_u32_array(data, offsets_file, FIELD_COUNT)
    print(f"\nParallel u32 array at VA 0x{offsets_va:x}:")
    stats = analyze_parallel_array(offsets, 'offsets_u32', FIELD_COUNT)
    print(f"  Unique: {stats['unique']}, Range: 0x{stats['min']:x}-0x{stats['max']:x}, "
          f"Zeros: {stats['zeros']}")

    # Check for MORE parallel arrays after the offset array
    # Array 3: at VA + 1444*4 + 1444*4 = VA + 1444*8
    arr3_va = FIELD_IDS_VA + FIELD_COUNT * 8
    arr3_file = va_to_file(arr3_va, sections)

    # Try as u32
    arr3_u32 = read_u32_array(data, arr3_file, FIELD_COUNT)
    s3 = analyze_parallel_array(arr3_u32, 'arr3_u32', FIELD_COUNT)
    print(f"\nCandidate parallel array 3 (u32 at VA 0x{arr3_va:x}):")
    print(f"  Unique: {s3['unique']}, Range: 0x{s3['min']:x}-0x{s3['max']:x}, Zeros: {s3['zeros']}")

    # Try as u16
    arr3_u16 = read_u16_array(data, arr3_file, FIELD_COUNT)
    s3_16 = analyze_parallel_array(arr3_u16, 'arr3_u16', FIELD_COUNT)
    print(f"  As u16: Unique: {s3_16['unique']}, Range: {s3_16['min']}-{s3_16['max']}, Zeros: {s3_16['zeros']}")
    if s3_16['unique'] < 30:
        dist = Counter(arr3_u16[:FIELD_COUNT])
        print(f"  u16 distribution: {dict(dist.most_common(20))}")

    # Array 4: after array 3
    arr4_va = arr3_va + FIELD_COUNT * 4
    arr4_file = va_to_file(arr4_va, sections)
    arr4_u32 = read_u32_array(data, arr4_file, FIELD_COUNT)
    s4 = analyze_parallel_array(arr4_u32, 'arr4_u32', FIELD_COUNT)
    print(f"\nCandidate parallel array 4 (u32 at VA 0x{arr4_va:x}):")
    print(f"  Unique: {s4['unique']}, Range: 0x{s4['min']:x}-0x{s4['max']:x}, Zeros: {s4['zeros']}")

    # Try u16 for array 3 data (in case arrays switch to u16 after offsets)
    # Check if field_ids, offsets, then u16 type codes
    type_codes_va = offsets_va + FIELD_COUNT * 4
    type_file = va_to_file(type_codes_va, sections)
    type_u16 = read_u16_array(data, type_file, FIELD_COUNT)
    type_u8 = read_u8_array(data, type_file, FIELD_COUNT)

    s_t16 = analyze_parallel_array(type_u16, 'types_u16', FIELD_COUNT)
    s_t8 = analyze_parallel_array(type_u8, 'types_u8', FIELD_COUNT)
    print(f"\nAfter offsets as u16 (VA 0x{type_codes_va:x}):")
    print(f"  Unique: {s_t16['unique']}, Range: {s_t16['min']}-{s_t16['max']}, Zeros: {s_t16['zeros']}")
    print(f"After offsets as u8:")
    print(f"  Unique: {s_t8['unique']}, Range: {s_t8['min']}-{s_t8['max']}, Zeros: {s_t8['zeros']}")
    if s_t8['unique'] < 30:
        dist = Counter(type_u8[:FIELD_COUNT])
        print(f"  u8 distribution: {dict(dist.most_common(20))}")

    # Also check what's BEFORE the field ID array
    # There might be a header/count
    before_va = FIELD_IDS_VA - 16
    before_file = va_to_file(before_va, sections)
    print(f"\n16 bytes before field ID array (VA 0x{before_va:x}):")
    for i in range(0, 16, 4):
        val = struct.unpack_from('<I', data, before_file + i)[0]
        print(f"  VA 0x{before_va + i:x}: 0x{val:08x} ({val})")

    # ===== ANALYSIS: Understand the offset values =====
    print("\n" + "=" * 70)
    print("OFFSET ANALYSIS")
    print("=" * 70)

    # Sort by offset to see the record layout
    pairs = list(zip(field_ids, offsets))
    pairs_sorted = sorted(pairs, key=lambda p: p[1])

    print("\nFirst 50 entries sorted by offset:")
    print(f"{'Offset':>8s}  {'FieldID':>12s}  {'Name':<40s}")
    for fid, off in pairs_sorted[:50]:
        name = field_map.get(fid, '')
        print(f"  0x{off:04x}    0x{fid:08x}  {name}")

    # Check if offsets are monotonically increasing (sparse)
    print(f"\nOffset statistics:")
    print(f"  Min: 0x{min(offsets):x}")
    print(f"  Max: 0x{max(offsets):x}")
    print(f"  Unique: {len(set(offsets))}")
    print(f"  Duplicates: {FIELD_COUNT - len(set(offsets))}")

    # Find duplicate offsets (multiple field IDs -> same offset)
    off_to_fids = {}
    for fid, off in pairs:
        off_to_fids.setdefault(off, []).append(fid)
    dups = {off: fids for off, fids in off_to_fids.items() if len(fids) > 1}
    print(f"  Offsets with multiple field IDs: {len(dups)}")
    if dups:
        for off in sorted(dups.keys())[:10]:
            fids = dups[off]
            names = [field_map.get(f, f'0x{f:08x}') for f in fids]
            print(f"    0x{off:04x}: {names}")

    # Gap analysis — look at consecutive offsets to infer field sizes
    unique_offsets = sorted(set(offsets))
    gaps = [unique_offsets[i+1] - unique_offsets[i] for i in range(len(unique_offsets)-1)]
    gap_dist = Counter(gaps)
    print(f"\n  Gap distribution (between consecutive unique offsets):")
    for gap, count in sorted(gap_dist.items()):
        if count > 5:
            print(f"    gap={gap} (0x{gap:x}): {count} occurrences")

    # ===== WRITE RESULTS =====
    print("\n" + "=" * 70)
    print("WRITING RESULTS")
    print("=" * 70)

    # Main output: field_id -> offset with property names
    out_path = ANALYSIS / "task_field_offsets.tsv"
    with open(out_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['table_index', 'field_id_hex', 'field_id_dec',
                     'record_offset_hex', 'record_offset_dec',
                     'property_name'])
        for i, (fid, off) in enumerate(zip(field_ids, offsets)):
            w.writerow([
                i, f'0x{fid:08x}', fid,
                f'0x{off:04x}', off,
                field_map.get(fid, ''),
            ])
    print(f"Wrote {FIELD_COUNT} entries to {out_path}")

    # Sorted by offset (record layout view)
    layout_path = ANALYSIS / "task_record_layout.tsv"
    with open(layout_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['record_offset_hex', 'record_offset_dec', 'field_id_hex',
                     'property_name', 'next_offset_hex', 'field_size_guess'])
        for i, (fid, off) in enumerate(pairs_sorted):
            # Find next different offset for size estimation
            next_off = None
            for j in range(i + 1, len(pairs_sorted)):
                if pairs_sorted[j][1] != off:
                    next_off = pairs_sorted[j][1]
                    break
            size_guess = next_off - off if next_off is not None else ''
            w.writerow([
                f'0x{off:04x}', off,
                f'0x{fid:08x}',
                field_map.get(fid, ''),
                f'0x{next_off:04x}' if next_off is not None else '',
                size_guess,
            ])
    print(f"Wrote record layout to {layout_path}")

    # Named fields only (for quick reference)
    named_path = ANALYSIS / "task_named_fields.tsv"
    named_count = 0
    with open(named_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['property_name', 'field_id_hex', 'record_offset_hex', 'record_offset_dec'])
        for fid, off in sorted(pairs, key=lambda p: field_map.get(p[0], '')):
            name = field_map.get(fid, '')
            if name:
                w.writerow([name, f'0x{fid:08x}', f'0x{off:04x}', off])
                named_count += 1
    print(f"Wrote {named_count} named fields to {named_path}")

    # Summary statistics
    print(f"\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)
    print(f"Total field entries: {FIELD_COUNT}")
    print(f"Named (matched to COM property): {named_count}")
    print(f"Unnamed: {FIELD_COUNT - named_count}")
    print(f"Unique offsets: {len(set(offsets))}")
    print(f"Record size estimate: 0x{max(offsets):x} ({max(offsets)} bytes)")

    # Show some well-known fields
    print(f"\nWell-known Task fields:")
    well_known = [
        ('Name', 0x0b40000e), ('Duration', 0x0b400017), ('Start', 0x0b400018),
        ('Finish', 0x0b400013), ('Work', 0x0b400000), ('PercentComplete', 0x0b400002),
        ('ID', 0x0b400056), ('UniqueID', 0x0b400047), ('Summary', 0x0b40005c),
        ('OutlineLevel', 0x0b400055), ('Notes', 0x0b40000f),
        ('ActualStart', 0x0b400019), ('ActualFinish', 0x0b40001a),
        ('ActualDuration', 0x0b400001), ('ActualWork', 0x0b400004),
        ('Cost', 0x0b40000b), ('ActualCost', 0x0b400003),
        ('BaselineStart', 0x0b40001c), ('BaselineFinish', 0x0b40001d),
        ('BaselineDuration', 0x0b400014), ('BaselineWork', 0x0b40001f),
        ('BaselineCost', 0x0b400023),
        ('Priority', 0x0b400022), ('Milestone', 0x0b40000c),
        ('Critical', 0x0b400009),
    ]
    for name, fid in well_known:
        idx = None
        for i, f in enumerate(field_ids):
            if f == fid:
                idx = i
                break
        if idx is not None:
            print(f"  {name:<25s}  0x{fid:08x}  -> offset 0x{offsets[idx]:04x} ({offsets[idx]})")
        else:
            print(f"  {name:<25s}  0x{fid:08x}  NOT IN TABLE")

    print("\nDone.")


if __name__ == '__main__':
    main()
