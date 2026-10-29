#!/usr/bin/env python3
"""Phase 2d: Trace the metadata structure that wraps the field ID arrays.

The pointer to Table 2 (field IDs at 0x1410e77a0) is found at VA 0x1414c2a78
in .data, embedded in a structure with counts and many .rdata pointers.
This script follows all pointers to map out the complete field descriptor.
"""

import struct
import csv
from collections import Counter
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


def section_for_va(va, sections):
    rva = va - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return s['name']
    return '?'


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


def is_ptr(val):
    return 0x140000000 <= val <= 0x145000000


def main():
    print("Phase 2d: Metadata Structure Trace")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # The containing structure starts around VA 0x1414c2a58 (or earlier)
    # Let me read a bigger chunk starting earlier
    struct_va = 0x1414c2a00  # start reading from here
    struct_off = va_to_file(struct_va, sections)

    print(f"Reading metadata structure from VA 0x{struct_va:x}")
    print(f"Section: {section_for_va(struct_va, sections)}")

    # Dump 0x200 bytes to see the full structure
    print(f"\n--- Structure dump (0x200 bytes) ---")
    for i in range(0, 0x200, 8):
        qval = struct.unpack_from('<Q', data, struct_off + i)[0]
        u32a = struct.unpack_from('<I', data, struct_off + i)[0]
        u32b = struct.unpack_from('<I', data, struct_off + i + 4)[0]
        notes = []
        if is_ptr(qval):
            notes.append(f"-> {section_for_va(qval, sections)}")
        if u32a == 1444 or u32b == 1444:
            notes.append("COUNT=1444")
        if u32a == 562 or u32b == 562:
            notes.append("COUNT=562")
        if u32a == 1007 or u32b == 1007:
            notes.append("COUNT=1007")
        note_str = f"  {'  '.join(notes)}" if notes else ""
        print(f"  +0x{i:03x} (VA 0x{struct_va+i:x}): 0x{qval:016x}  ({u32a:>10d} | {u32b:>10d}){note_str}")

    # ===== Follow the key pointers from the structure at 0x1414c2a78 =====
    # Known structure layout (relative to 0x1414c2a58):
    #   +0x10: type/flags = 7
    #   +0x18: count A = 0x3ef (1007)
    #   +0x20: ptr to field ID array (0x1410e77a0, 1444 task field IDs)
    #   +0x28: count B = 0x232 (562)
    #   +0x30: ptr to array B (0x1410e8f50)
    #   ...more ptrs...

    print("\n" + "=" * 70)
    print("Following pointers from metadata structure")
    print("=" * 70)

    # Define the pointers we found
    pointers = [
        (0x1414c2a78, 0x1410e77a0, "field_ids"),
        (0x1414c2a88, 0x1410e8f50, "array_B"),
        (0x1414c2aa8, 0x1410e6b70, "array_C"),
        (0x1414c2ab0, 0x1410ea4d0, "array_D"),
        (0x1414c2ab8, 0x1410ea450, "array_E"),
        (0x1414c2ac0, 0x1410ea2e0, "array_F"),
        (0x1414c2ac8, 0x1410ea298, "array_G"),
        (0x1414c2ad0, 0x1410ea418, "array_H"),
        (0x1414c2ad8, 0x1410ea3b0, "array_I"),
        (0x1414c2ae0, 0x1410ea930, "array_J"),
        (0x1414c2ae8, 0x1410ea8a0, "array_K"),
        (0x1414c2af0, 0x1410e9f58, "array_L"),
    ]

    for ptr_loc, target_va, name in pointers:
        foff = va_to_file(target_va, sections)
        if foff is None:
            print(f"\n{name} at VA 0x{target_va:x}: NOT IN FILE")
            continue

        sec = section_for_va(target_va, sections)
        print(f"\n--- {name} at VA 0x{target_va:x} ({sec}) ---")

        # Read first 200 u32 values
        sample_u32 = [struct.unpack_from('<I', data, foff + j*4)[0] for j in range(200)]
        sample_u16 = [struct.unpack_from('<H', data, foff + j*2)[0] for j in range(200)]
        sample_u8 = [data[foff + j] for j in range(200)]

        # Classify the data
        # Check for field IDs
        task_ids = sum(1 for v in sample_u32 if 0x0b400000 <= v <= 0x0b4fffff)
        rsc_ids = sum(1 for v in sample_u32 if 0x0c400000 <= v <= 0x0c4fffff)
        assn_ids = sum(1 for v in sample_u32 if 0x0f400000 <= v <= 0x0f4fffff)
        ptrs = sum(1 for v in sample_u32 if 0x14000000 <= v <= 0x14500000)
        small = sum(1 for v in sample_u32 if v < 0x10000)
        zeros = sum(1 for v in sample_u32 if v == 0)

        print(f"  First 200 u32: task_ids={task_ids}, rsc_ids={rsc_ids}, assn_ids={assn_ids}, "
              f"ptrs={ptrs}, small={small}, zeros={zeros}")

        # Show first 40 values
        print(f"  As u32 (first 40):")
        for j in range(0, 40, 8):
            vals = sample_u32[j:j+8]
            print(f"    [{j:3d}] {' '.join(f'{v:08x}' for v in vals)}")

        # For small-value arrays, check if they look like offsets
        if small > 150:
            # Look for increasing runs
            increasing = 0
            for j in range(1, 200):
                if sample_u32[j] >= sample_u32[j-1] and sample_u32[j] < 0x10000:
                    increasing += 1
            print(f"  Increasing pairs: {increasing}/199")
            print(f"  Max value: 0x{max(sample_u32):x}")
            print(f"  Unique: {len(set(sample_u32))}")

        # For u8 data, check value distribution
        u8_unique = len(set(sample_u8))
        if u8_unique < 30:
            u8_dist = Counter(sample_u8)
            print(f"  As u8: unique={u8_unique}, dist={dict(u8_dist.most_common(15))}")

        # For u16 data
        u16_unique = len(set(sample_u16))
        u16_small = sum(1 for v in sample_u16 if v < 256)
        if u16_unique < 50 or u16_small > 150:
            print(f"  As u16: unique={u16_unique}, small(<256)={u16_small}")
            if u16_unique < 30:
                u16_dist = Counter(sample_u16)
                print(f"  u16 dist: {dict(u16_dist.most_common(15))}")

    # ===== Now investigate the region between field IDs end and array_B =====
    print("\n" + "=" * 70)
    print("Gap analysis: field_ids end (0x1410e8e30) to array_B (0x1410e8f50)")
    print("=" * 70)

    gap_start_va = 0x1410e8e30
    gap_end_va = 0x1410e8f50
    gap_size = gap_end_va - gap_start_va
    print(f"Gap: 0x{gap_size:x} bytes = {gap_size} bytes = {gap_size // 4} u32 values")

    gap_off = va_to_file(gap_start_va, sections)
    gap_u32 = [struct.unpack_from('<I', data, gap_off + j*4)[0] for j in range(gap_size // 4)]
    print(f"Gap u32 values:")
    for j in range(0, len(gap_u32), 8):
        vals = gap_u32[j:j+8]
        print(f"  [{j:3d}] {' '.join(f'{v:08x}' for v in vals)}")

    # ===== Look at what array_B contains (count=562 from structure) =====
    print("\n" + "=" * 70)
    print("Array B deep dive (count=562 from metadata)")
    print("=" * 70)

    arrB_va = 0x1410e8f50
    arrB_off = va_to_file(arrB_va, sections)
    arrB_u32 = [struct.unpack_from('<I', data, arrB_off + j*4)[0] for j in range(600)]

    # These first 562 values might be the record offsets
    offsets_562 = arrB_u32[:562]
    small_count = sum(1 for v in offsets_562 if v < 0x10000)
    print(f"First 562 u32 values: {small_count} are < 0x10000")
    print(f"  Max: 0x{max(offsets_562):x}")
    print(f"  Min: 0x{min(offsets_562):x}")
    print(f"  Unique: {len(set(offsets_562))}")
    print(f"  Zeros: {offsets_562.count(0)}")

    # Check if sorted/increasing
    increasing = sum(1 for j in range(1, 562) if offsets_562[j] >= offsets_562[j-1])
    print(f"  Monotonically increasing pairs: {increasing}/561")

    print(f"\n  First 50 values:")
    for j in range(0, 50, 10):
        vals = offsets_562[j:j+10]
        print(f"    [{j:3d}] {' '.join(f'{v:5d}' for v in vals)}")

    print(f"\n  Last 20 values:")
    for j in range(542, 562, 10):
        vals = offsets_562[j:j+10]
        print(f"    [{j:3d}] {' '.join(f'{v:5d}' for v in vals)}")

    # ===== Check array_C (at 0x1410e6b70, count might be 0x234=564) =====
    print("\n" + "=" * 70)
    print("Array C deep dive (0x1410e6b70)")
    print("=" * 70)

    arrC_va = 0x1410e6b70
    arrC_off = va_to_file(arrC_va, sections)
    arrC_u32 = [struct.unpack_from('<I', data, arrC_off + j*4)[0] for j in range(600)]
    task_ids_c = sum(1 for v in arrC_u32[:564] if 0x0b400000 <= v <= 0x0b4fffff)
    small_c = sum(1 for v in arrC_u32[:564] if v < 0x10000)
    print(f"First 564 u32: task_ids={task_ids_c}, small(<0x10000)={small_c}")
    print(f"  First 20: {[f'0x{v:08x}' for v in arrC_u32[:20]]}")

    # ===== Build the mapping if array_B has valid offsets =====
    if small_count >= 500:
        print("\n" + "=" * 70)
        print("BUILDING FIELD ID -> OFFSET MAPPING")
        print("=" * 70)

        # The field IDs are at 0x1410e77a0 (1444 entries)
        # The offsets are at 0x1410e8f50 (562 entries per count in metadata)
        # The gap between field IDs and offsets has 72 u32 values
        # We need to figure out the INDEX mapping

        # Option 1: field_ids[i] -> offsets[i] for i in range(min(1444, 562))
        # Option 2: there's an index array that maps field_id indices to offset indices

        fid_off = va_to_file(0x1410e77a0, sections)
        field_ids = [struct.unpack_from('<I', data, fid_off + j*4)[0] for j in range(1444)]

        # Since we have 1444 field IDs but only 562 offsets, there must be
        # an index/mapping between them. Perhaps the first 562 field IDs
        # correspond to the 562 offsets?

        # Or perhaps the gap data (72 u32 values) contains a mapping

        # Let me check: do the 72 gap values look like indices into the offset array?
        print(f"\nGap values (potential index mapping, {len(gap_u32)} values):")
        gap_in_range = sum(1 for v in gap_u32 if v < 562)
        print(f"  Values < 562: {gap_in_range}/{len(gap_u32)}")
        gap_in_range2 = sum(1 for v in gap_u32 if v < 1444)
        print(f"  Values < 1444: {gap_in_range2}/{len(gap_u32)}")

        # Check if offsets contain increasing values suggesting byte offsets into a record
        print(f"\nRecord structure analysis from {len(offsets_562)} offset values:")

        # Gap analysis (difference between consecutive sorted unique offsets)
        unique_offs = sorted(set(offsets_562))
        gaps = [unique_offs[j+1] - unique_offs[j] for j in range(len(unique_offs)-1)]
        gap_dist = Counter(gaps)
        print(f"  Unique offsets: {len(unique_offs)}")
        print(f"  Offset range: {unique_offs[0]}-{unique_offs[-1]} (0x{unique_offs[-1]:x})")
        print(f"  Common gaps: {dict(gap_dist.most_common(10))}")

        # Write provisional mapping (first 562 field_ids -> 562 offsets)
        out_path = ANALYSIS / "task_field_offsets_provisional.tsv"
        with open(out_path, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f, delimiter='\t')
            w.writerow(['index', 'field_id_hex', 'record_offset_hex', 'record_offset_dec',
                         'property_name'])
            for i in range(min(1444, len(offsets_562))):
                fid = field_ids[i]
                off = offsets_562[i] if i < len(offsets_562) else -1
                w.writerow([
                    i, f'0x{fid:08x}', f'0x{off:04x}' if off >= 0 else '',
                    off if off >= 0 else '',
                    field_map.get(fid, ''),
                ])
        print(f"\nWrote provisional mapping to {out_path}")

    print("\nDone.")


if __name__ == '__main__':
    main()
