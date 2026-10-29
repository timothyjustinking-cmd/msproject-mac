#!/usr/bin/env python3
"""Phase 2d: Extract field descriptor table directly from WINPROJ.EXE binary.

Reads the Task type descriptor data table at VA 0x140f8bda0, which contains
1444 entries of 0x40 bytes each starting at VA 0x14130be10. Each entry
encodes field_id, type code, record offset, and size information.

The decompiled accessor FUN_140239380 (TTaskTable::vfunc_158) reads:
  entry+0x00: uint   (field_id or internal index)
  entry+0x08: short  (type indicator)
  entry+0x0C: uint   (offset/flags)
  entry+0x10: uint   (offset/size)
"""

import struct
import csv
import os
from pathlib import Path

IMAGE_BASE = 0x140000000
WINPROJ = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"

BASE = Path(__file__).resolve().parents[1]
ANALYSIS = BASE / "analysis"

DESCRIPTOR_VA = 0x140f8bda0
FIELD_ARRAY_VA = 0x14130be10
FIELD_COUNT = 1444
ENTRY_SIZE = 0x40


def parse_pe_sections(data):
    """Parse PE32+ section headers to build VA-to-file-offset mapping."""
    # DOS header: e_lfanew at offset 0x3C
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]

    # PE signature at e_lfanew
    sig = data[e_lfanew:e_lfanew+4]
    assert sig == b'PE\x00\x00', f"Bad PE signature: {sig}"

    # COFF header at e_lfanew + 4
    coff_offset = e_lfanew + 4
    num_sections = struct.unpack_from('<H', data, coff_offset + 2)[0]
    opt_header_size = struct.unpack_from('<H', data, coff_offset + 16)[0]

    # Optional header starts at coff_offset + 20
    opt_offset = coff_offset + 20
    magic = struct.unpack_from('<H', data, opt_offset)[0]
    assert magic == 0x20b, f"Not PE32+: magic=0x{magic:x}"

    # Section headers start after optional header
    section_offset = opt_offset + opt_header_size
    sections = []
    for i in range(num_sections):
        off = section_offset + i * 40
        name = data[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
        virt_size = struct.unpack_from('<I', data, off + 8)[0]
        virt_addr = struct.unpack_from('<I', data, off + 12)[0]
        raw_size = struct.unpack_from('<I', data, off + 16)[0]
        raw_ptr = struct.unpack_from('<I', data, off + 20)[0]
        sections.append({
            'name': name,
            'virt_addr': virt_addr,
            'virt_size': virt_size,
            'raw_ptr': raw_ptr,
            'raw_size': raw_size,
        })
    return sections


def va_to_file_offset(va, sections):
    """Convert a virtual address to file offset using PE sections."""
    rva = va - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return rva - s['virt_addr'] + s['raw_ptr']
    raise ValueError(f"VA 0x{va:x} (RVA 0x{rva:x}) not in any section")


def load_field_id_map():
    """Load field_id -> property_name from field_id_map.tsv."""
    fmap = {}
    path = ANALYSIS / "field_id_map.tsv"
    if not path.exists():
        return fmap
    with open(path, 'r') as f:
        reader = csv.DictReader(f, delimiter='\t')
        for row in reader:
            fid_str = row['field_id']
            fid = int(fid_str, 16) if fid_str.startswith('0x') else int(fid_str)
            prop = row['property_name']
            disp = row.get('dispatcher', '')
            if disp == 'get' or fid not in fmap:
                fmap[fid] = prop
    return fmap


def classify_field_id(fid):
    """Classify a field ID by its prefix."""
    prefix = fid & 0x3f000000
    ranges = {
        0x0b000000: 'Task',
        0x0c000000: 'Resource',
        0x0d000000: 'Calendar',
        0x0f000000: 'Assignment',
    }
    return ranges.get(prefix, f'0x{prefix:08x}')


def main():
    print("Phase 2d: Direct Binary Descriptor Table Extraction")
    print("=" * 70)

    print(f"\nReading {WINPROJ}...")
    with open(WINPROJ, 'rb') as f:
        data = f.read()
    print(f"  File size: {len(data):,} bytes ({len(data)/1024/1024:.1f} MB)")

    sections = parse_pe_sections(data)
    print(f"\nPE sections:")
    for s in sections:
        print(f"  {s['name']:8s}  VA=0x{s['virt_addr']+IMAGE_BASE:012x}  "
              f"VirtSize=0x{s['virt_size']:08x}  "
              f"RawPtr=0x{s['raw_ptr']:08x}  RawSize=0x{s['raw_size']:08x}")

    # Read the descriptor header at 0x140f8bda0
    desc_file_off = va_to_file_offset(DESCRIPTOR_VA, sections)
    print(f"\nDescriptor at VA 0x{DESCRIPTOR_VA:x} -> file offset 0x{desc_file_off:x}")

    # Read 0x20 bytes of the descriptor
    desc_data = data[desc_file_off:desc_file_off+0x20]
    rtti_ptr = struct.unpack_from('<Q', desc_data, 0)[0]
    array_ptr = struct.unpack_from('<Q', desc_data, 8)[0]
    count1 = struct.unpack_from('<H', desc_data, 0x10)[0]
    count2 = struct.unpack_from('<H', desc_data, 0x12)[0]

    print(f"  RTTI ptr:    0x{rtti_ptr:x}")
    print(f"  Array ptr:   0x{array_ptr:x}")
    print(f"  Count 1:     {count1}")
    print(f"  Count 2:     {count2}")

    assert array_ptr == FIELD_ARRAY_VA, f"Array ptr mismatch: 0x{array_ptr:x} != 0x{FIELD_ARRAY_VA:x}"
    assert count1 == FIELD_COUNT, f"Count mismatch: {count1} != {FIELD_COUNT}"

    # Read the field entry array
    array_file_off = va_to_file_offset(FIELD_ARRAY_VA, sections)
    total_size = FIELD_COUNT * ENTRY_SIZE
    print(f"\nField array at VA 0x{FIELD_ARRAY_VA:x} -> file offset 0x{array_file_off:x}")
    print(f"  {FIELD_COUNT} entries x 0x{ENTRY_SIZE:x} bytes = {total_size:,} bytes")

    array_data = data[array_file_off:array_file_off + total_size]

    # Load property names for cross-reference
    field_map = load_field_id_map()
    print(f"  Loaded {len(field_map)} field ID -> property name mappings")

    # Parse each entry — dump the raw structure first to understand the layout
    entries = []
    print(f"\nParsing {FIELD_COUNT} entries...")

    # First pass: dump a few entries to understand the structure
    print("\n--- Sample entries (first 10) ---")
    print(f"{'Idx':>5}  {'Bytes 0-7':>18}  {'Bytes 8-F':>18}  "
          f"{'Bytes 10-17':>18}  {'Bytes 18-1F':>18}")
    for i in range(min(10, FIELD_COUNT)):
        off = i * ENTRY_SIZE
        entry_bytes = array_data[off:off + ENTRY_SIZE]
        q0 = struct.unpack_from('<Q', entry_bytes, 0x00)[0]
        q1 = struct.unpack_from('<Q', entry_bytes, 0x08)[0]
        q2 = struct.unpack_from('<Q', entry_bytes, 0x10)[0]
        q3 = struct.unpack_from('<Q', entry_bytes, 0x18)[0]
        print(f"  {i:4d}  {q0:018x}  {q1:018x}  {q2:018x}  {q3:018x}")

    # Detailed breakdown of first 10 entries using known field offsets
    print("\n--- Detailed breakdown (first 20) ---")
    print(f"{'Idx':>5}  {'@0x00(u32)':>12}  {'@0x04(u32)':>12}  "
          f"{'@0x08(i16)':>10}  {'@0x0A(i16)':>10}  "
          f"{'@0x0C(u32)':>12}  {'@0x10(u32)':>12}  "
          f"{'@0x14(u32)':>12}  {'@0x18(u32)':>12}  "
          f"{'@0x1C(u32)':>12}  {'@0x20':>12}")
    for i in range(min(20, FIELD_COUNT)):
        off = i * ENTRY_SIZE
        eb = array_data[off:off + ENTRY_SIZE]
        u32_00 = struct.unpack_from('<I', eb, 0x00)[0]
        u32_04 = struct.unpack_from('<I', eb, 0x04)[0]
        i16_08 = struct.unpack_from('<h', eb, 0x08)[0]
        i16_0a = struct.unpack_from('<h', eb, 0x0A)[0]
        u32_0c = struct.unpack_from('<I', eb, 0x0C)[0]
        u32_10 = struct.unpack_from('<I', eb, 0x10)[0]
        u32_14 = struct.unpack_from('<I', eb, 0x14)[0]
        u32_18 = struct.unpack_from('<I', eb, 0x18)[0]
        u32_1c = struct.unpack_from('<I', eb, 0x1C)[0]
        u32_20 = struct.unpack_from('<I', eb, 0x20)[0]
        print(f"  {i:4d}  {u32_00:>12x}  {u32_04:>12x}  "
              f"{i16_08:>10d}  {i16_0a:>10d}  "
              f"{u32_0c:>12x}  {u32_10:>12x}  "
              f"{u32_14:>12x}  {u32_18:>12x}  "
              f"{u32_1c:>12x}  {u32_20:>12x}")

    # Full parse — extract all fields at the known offsets
    print("\n--- Full extraction ---")
    for i in range(FIELD_COUNT):
        off = i * ENTRY_SIZE
        eb = array_data[off:off + ENTRY_SIZE]

        entry = {
            'index': i,
            'u32_00': struct.unpack_from('<I', eb, 0x00)[0],
            'u32_04': struct.unpack_from('<I', eb, 0x04)[0],
            'i16_08': struct.unpack_from('<h', eb, 0x08)[0],
            'i16_0a': struct.unpack_from('<h', eb, 0x0A)[0],
            'u32_0c': struct.unpack_from('<I', eb, 0x0C)[0],
            'u32_10': struct.unpack_from('<I', eb, 0x10)[0],
            'u32_14': struct.unpack_from('<I', eb, 0x14)[0],
            'u32_18': struct.unpack_from('<I', eb, 0x18)[0],
            'u32_1c': struct.unpack_from('<I', eb, 0x1C)[0],
            'u32_20': struct.unpack_from('<I', eb, 0x20)[0],
            'u32_24': struct.unpack_from('<I', eb, 0x24)[0],
            'u32_28': struct.unpack_from('<I', eb, 0x28)[0],
            'u32_2c': struct.unpack_from('<I', eb, 0x2C)[0],
            'u32_30': struct.unpack_from('<I', eb, 0x30)[0],
            'u32_34': struct.unpack_from('<I', eb, 0x34)[0],
            'u32_38': struct.unpack_from('<I', eb, 0x38)[0],
            'u32_3c': struct.unpack_from('<I', eb, 0x3C)[0],
        }

        # Also store raw qwords for pointer detection
        entry['q00'] = struct.unpack_from('<Q', eb, 0x00)[0]
        entry['q08'] = struct.unpack_from('<Q', eb, 0x08)[0]
        entry['q10'] = struct.unpack_from('<Q', eb, 0x10)[0]
        entry['q18'] = struct.unpack_from('<Q', eb, 0x18)[0]
        entry['q20'] = struct.unpack_from('<Q', eb, 0x20)[0]
        entry['q28'] = struct.unpack_from('<Q', eb, 0x28)[0]
        entry['q30'] = struct.unpack_from('<Q', eb, 0x30)[0]
        entry['q38'] = struct.unpack_from('<Q', eb, 0x38)[0]

        entries.append(entry)

    # Analyze the structure: look for field_id patterns
    # Field IDs for tasks are 0x0b4xxxxx — check which offset contains them
    print("\nSearching for PjField ID patterns in entries...")
    for field_name in ['u32_00', 'u32_04', 'u32_0c', 'u32_10', 'u32_14',
                       'u32_18', 'u32_1c', 'u32_20', 'u32_24', 'u32_28',
                       'u32_2c', 'u32_30', 'u32_34', 'u32_38', 'u32_3c']:
        task_count = sum(1 for e in entries
                         if 0x0b400000 <= e[field_name] <= 0x0b4fffff)
        if task_count > 10:
            print(f"  {field_name}: {task_count} values in Task field ID range")

    # Check qword fields for pointers (0x140xxxxxx range)
    print("\nChecking for pointer-like values in qword fields...")
    for field_name in ['q00', 'q08', 'q10', 'q18', 'q20', 'q28', 'q30', 'q38']:
        ptr_count = sum(1 for e in entries
                        if 0x140000000 <= e[field_name] <= 0x145000000)
        if ptr_count > 10:
            print(f"  {field_name}: {ptr_count} pointer-like values")

    # Statistical analysis of each u32 field
    print("\nValue distribution per u32 field:")
    for field_name in ['u32_00', 'u32_04', 'u32_0c', 'u32_10', 'u32_14',
                       'u32_18', 'u32_1c', 'u32_20', 'u32_24', 'u32_28',
                       'u32_2c', 'u32_30', 'u32_34', 'u32_38', 'u32_3c']:
        vals = [e[field_name] for e in entries]
        unique = len(set(vals))
        min_v = min(vals)
        max_v = max(vals)
        zeros = vals.count(0)
        print(f"  {field_name}: unique={unique:5d}  min=0x{min_v:08x}  "
              f"max=0x{max_v:08x}  zeros={zeros}")

    # Similarly for i16 fields
    for field_name in ['i16_08', 'i16_0a']:
        vals = [e[field_name] for e in entries]
        unique = len(set(vals))
        min_v = min(vals)
        max_v = max(vals)
        print(f"  {field_name}: unique={unique:5d}  min={min_v:6d}  max={max_v:6d}")

    # Write raw dump
    raw_path = ANALYSIS / "descriptor_table_raw.tsv"
    with open(raw_path, 'w', newline='') as f:
        writer = csv.writer(f, delimiter='\t')
        writer.writerow(['index',
                         'u32_00', 'u32_04', 'i16_08', 'i16_0a',
                         'u32_0c', 'u32_10', 'u32_14', 'u32_18',
                         'u32_1c', 'u32_20', 'u32_24', 'u32_28',
                         'u32_2c', 'u32_30', 'u32_34', 'u32_38', 'u32_3c',
                         'q00', 'q08', 'q10', 'q18', 'q20', 'q28', 'q30', 'q38'])
        for e in entries:
            writer.writerow([
                e['index'],
                f"0x{e['u32_00']:08x}", f"0x{e['u32_04']:08x}",
                e['i16_08'], e['i16_0a'],
                f"0x{e['u32_0c']:08x}", f"0x{e['u32_10']:08x}",
                f"0x{e['u32_14']:08x}", f"0x{e['u32_18']:08x}",
                f"0x{e['u32_1c']:08x}", f"0x{e['u32_20']:08x}",
                f"0x{e['u32_24']:08x}", f"0x{e['u32_28']:08x}",
                f"0x{e['u32_2c']:08x}", f"0x{e['u32_30']:08x}",
                f"0x{e['u32_34']:08x}", f"0x{e['u32_38']:08x}",
                f"0x{e['u32_3c']:08x}",
                f"0x{e['q00']:016x}", f"0x{e['q08']:016x}",
                f"0x{e['q10']:016x}", f"0x{e['q18']:016x}",
                f"0x{e['q20']:016x}", f"0x{e['q28']:016x}",
                f"0x{e['q30']:016x}", f"0x{e['q38']:016x}",
            ])
    print(f"\nRaw dump written to {raw_path}")

    # Now try to identify field_id position by cross-referencing with known IDs
    print("\n--- Cross-referencing with field_id_map.tsv ---")
    known_task_ids = {fid for fid in field_map if 0x0b400000 <= fid <= 0x0b4fffff}
    print(f"Known Task field IDs from COM dispatch: {len(known_task_ids)}")

    # Check each u32 field for overlap with known IDs
    for field_name in ['u32_00', 'u32_04', 'u32_0c', 'u32_10', 'u32_14',
                       'u32_18', 'u32_1c', 'u32_20', 'u32_24', 'u32_28',
                       'u32_2c', 'u32_30', 'u32_34', 'u32_38', 'u32_3c']:
        table_vals = {e[field_name] for e in entries}
        overlap = known_task_ids & table_vals
        if overlap:
            print(f"  {field_name}: {len(overlap)} known Task field IDs found in table")
            # Show first few matches
            for fid in sorted(overlap)[:5]:
                name = field_map.get(fid, '?')
                idx = next(i for i, e in enumerate(entries) if e[field_name] == fid)
                print(f"    0x{fid:08x} ({name}) at entry {idx}")

    # Also check if field IDs might be stored as the low 16 bits only
    known_task_low = {fid & 0xffff: fid for fid in known_task_ids}
    for field_name in ['u32_00', 'u32_04', 'u32_0c', 'u32_10']:
        table_low = {e[field_name] & 0xffff: e[field_name] for e in entries
                     if e[field_name] != 0}
        overlap = set(known_task_low.keys()) & set(table_low.keys())
        if len(overlap) > 50:
            print(f"  {field_name} (low 16): {len(overlap)} potential matches")

    print("\nDone. Review descriptor_table_raw.tsv and the analysis above to determine")
    print("the exact field layout before proceeding to the mapping step.")


if __name__ == '__main__':
    main()
