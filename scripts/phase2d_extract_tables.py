#!/usr/bin/env python3
"""Phase 2d part 3: Deep extraction of field descriptor tables.

Key targets:
1. .rdata 1444-field-ID array at VA 0x140fb7e80 + surrounding parallel arrays
2. .rdata 1444-field-ID array at VA 0x1410e77a0 + surrounding parallel arrays
3. .data structured table near VA 0x1412a9024 (1445 field IDs, first stride 0x3c)
4. Index mapping table at VA 0x1413b0440 (descriptor+0x40)
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


def is_task_field_id(val):
    return 0x0b400000 <= val <= 0x0b4fffff


def main():
    print("Phase 2d part 3: Deep Field Descriptor Extraction")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()
    print(f"Loaded {len(field_map)} field ID -> property name mappings")

    # ===== TABLE 1: .rdata array at VA 0x140fb7e80 (1444 field IDs) =====
    print("\n" + "=" * 70)
    print("TABLE 1: .rdata field ID array at VA 0x140fb7e80")
    print("=" * 70)

    array1_va = 0x140fb7e80
    array1_off = va_to_file(array1_va, sections)
    array1_size = 1444 * 4

    # Read the 1444 field IDs
    field_ids_1 = []
    for i in range(1444):
        foff = array1_off + i * 4
        val = struct.unpack_from('<I', data, foff)[0]
        field_ids_1.append(val)

    print(f"Read {len(field_ids_1)} field IDs")
    task_count = sum(1 for v in field_ids_1 if is_task_field_id(v))
    print(f"  Task field IDs: {task_count}")
    print(f"  First 10: {[f'0x{v:08x}' for v in field_ids_1[:10]]}")
    unique_ids = len(set(field_ids_1))
    print(f"  Unique: {unique_ids}")

    # Check what's before and after the array
    print(f"\n  Data BEFORE array (64 bytes):")
    before_off = array1_off - 64
    for i in range(0, 64, 8):
        qval = struct.unpack_from('<Q', data, before_off + i)[0]
        u32a = struct.unpack_from('<I', data, before_off + i)[0]
        u32b = struct.unpack_from('<I', data, before_off + i + 4)[0]
        print(f"    VA 0x{array1_va - 64 + i:x}: 0x{qval:016x}  ({u32a:#010x} | {u32b:#010x})")

    print(f"\n  Data AFTER array (256 bytes, starting at VA 0x{array1_va + array1_size:x}):")
    after_off = array1_off + array1_size
    after_va = array1_va + array1_size
    # Check if there are parallel arrays
    # Dump as u16 values to check for type codes
    for i in range(0, 256, 16):
        vals_u16 = [struct.unpack_from('<H', data, after_off + i + j)[0] for j in range(0, 16, 2)]
        vals_u32 = [struct.unpack_from('<I', data, after_off + i + j)[0] for j in range(0, 16, 4)]
        print(f"    +0x{i:04x}: u16=[{', '.join(f'{v:#06x}' for v in vals_u16)}]  "
              f"u32=[{', '.join(f'{v:#010x}' for v in vals_u32)}]")

    # Look for parallel arrays: check if there's a 1444*2 block of u16 type codes after
    print(f"\n  Checking for parallel u16 array after field IDs:")
    u16_after = [struct.unpack_from('<H', data, after_off + i * 2)[0] for i in range(min(1444, 2000))]
    u16_unique = len(set(u16_after[:1444]))
    u16_max = max(u16_after[:1444])
    u16_zeros = u16_after[:1444].count(0)
    print(f"    Unique u16 values: {u16_unique}, max: {u16_max} (0x{u16_max:x}), zeros: {u16_zeros}")
    if u16_unique < 50:
        from collections import Counter
        u16_dist = Counter(u16_after[:1444])
        print(f"    Distribution: {dict(u16_dist.most_common(20))}")

    # ===== TABLE 2: .rdata array at VA 0x1410e77a0 (1444 field IDs) =====
    print("\n" + "=" * 70)
    print("TABLE 2: .rdata field ID array at VA 0x1410e77a0")
    print("=" * 70)

    array2_va = 0x1410e77a0
    array2_off = va_to_file(array2_va, sections)

    field_ids_2 = []
    for i in range(1444):
        foff = array2_off + i * 4
        val = struct.unpack_from('<I', data, foff)[0]
        field_ids_2.append(val)

    print(f"Read {len(field_ids_2)} field IDs")
    same_as_1 = field_ids_1 == field_ids_2
    print(f"  Same as Table 1: {same_as_1}")
    if not same_as_1:
        diffs = sum(1 for a, b in zip(field_ids_1, field_ids_2) if a != b)
        print(f"  Differences: {diffs}")

    # Check what surrounds this array — especially what comes after
    print(f"\n  Data BEFORE array (128 bytes):")
    before_off2 = array2_off - 128
    for i in range(0, 128, 16):
        vals_u32 = [struct.unpack_from('<I', data, before_off2 + i + j)[0] for j in range(0, 16, 4)]
        ptr_notes = []
        for j in range(0, 16, 8):
            qv = struct.unpack_from('<Q', data, before_off2 + i + j)[0]
            if 0x140000000 <= qv <= 0x145000000:
                ptr_notes.append(f"ptr@+{i+j:#x}")
        print(f"    VA 0x{array2_va - 128 + i:x}: [{', '.join(f'{v:#010x}' for v in vals_u32)}] {' '.join(ptr_notes)}")

    print(f"\n  Data AFTER array (512 bytes, starting at VA 0x{array2_va + array1_size:x}):")
    after_off2 = array2_off + array1_size
    # Look for u16 type array, u32 offset array, etc.
    for i in range(0, 512, 16):
        vals_u16 = [struct.unpack_from('<H', data, after_off2 + i + j)[0] for j in range(0, 16, 2)]
        vals_u32 = [struct.unpack_from('<I', data, after_off2 + i + j)[0] for j in range(0, 16, 4)]
        print(f"    +0x{i:04x}: u16=[{', '.join(f'{v:#06x}' for v in vals_u16)}]  "
              f"u32=[{', '.join(f'{v:#010x}' for v in vals_u32)}]")

    # Check for u16 parallel arrays
    print(f"\n  Checking parallel u16 array after Table 2 field IDs:")
    u16_after2 = [struct.unpack_from('<H', data, after_off2 + i * 2)[0] for i in range(1444)]
    u16_unique2 = len(set(u16_after2))
    u16_max2 = max(u16_after2)
    print(f"    Unique u16 values: {u16_unique2}, max: {u16_max2} (0x{u16_max2:x})")
    if u16_unique2 < 50:
        from collections import Counter
        print(f"    Distribution: {dict(Counter(u16_after2).most_common(20))}")

    # Check for u32 parallel array (offsets?)
    print(f"\n  Checking parallel u32 array after Table 2 field IDs:")
    u32_after2 = [struct.unpack_from('<I', data, after_off2 + i * 4)[0] for i in range(1444)]
    u32_unique2 = len(set(u32_after2))
    u32_max2 = max(u32_after2)
    u32_min2 = min(v for v in u32_after2 if v > 0) if any(v > 0 for v in u32_after2) else 0
    print(f"    Unique u32 values: {u32_unique2}, min(nonzero): 0x{u32_min2:x}, max: 0x{u32_max2:x}")
    in_offset_range = sum(1 for v in u32_after2 if 0 < v < 0x2000)
    print(f"    Values in offset range (1-0x2000): {in_offset_range}")

    # ===== TABLE 3: .data structured table near VA 0x1412a9000 =====
    print("\n" + "=" * 70)
    print("TABLE 3: .data structured table near VA 0x1412a9000")
    print("=" * 70)

    # First field ID at 0x1412a9024, then packed array at 0x1412a9060
    struct_va = 0x1412a9000
    struct_off = va_to_file(struct_va, sections)

    # Dump the header area (first 0x80 bytes)
    print(f"\n  Header (0x80 bytes from VA 0x{struct_va:x}):")
    for i in range(0, 0x80, 8):
        qval = struct.unpack_from('<Q', data, struct_off + i)[0]
        u32a = struct.unpack_from('<I', data, struct_off + i)[0]
        u32b = struct.unpack_from('<I', data, struct_off + i + 4)[0]
        ptr_note = ""
        if 0x140000000 <= qval <= 0x145000000:
            for s in sections:
                if s['virt_addr'] <= (qval - IMAGE_BASE) < s['virt_addr'] + s['virt_size']:
                    ptr_note = f"  -> {s['name']}"
        fid_note = ""
        if is_task_field_id(u32a):
            name = field_map.get(u32a, '?')
            fid_note += f"  [low={name}]"
        if is_task_field_id(u32b):
            name = field_map.get(u32b, '?')
            fid_note += f"  [high={name}]"
        print(f"    +0x{i:02x}: 0x{qval:016x}  ({u32a:#010x} | {u32b:#010x}){ptr_note}{fid_note}")

    # Read the packed array starting at 0x1412a9060
    packed_va = 0x1412a9060
    packed_off = va_to_file(packed_va, sections)
    print(f"\n  Packed array at VA 0x{packed_va:x} (1444 entries at stride 4):")
    packed_ids = []
    for i in range(1444):
        val = struct.unpack_from('<I', data, packed_off + i * 4)[0]
        packed_ids.append(val)
    task_count = sum(1 for v in packed_ids if is_task_field_id(v))
    print(f"    Task field IDs: {task_count}/{len(packed_ids)}")
    print(f"    First 20: {[f'0x{v:08x}' for v in packed_ids[:20]]}")

    # Check if this is a sorted field ID list (0x0b400000, 0x0b400001, ...)
    sequential = all(packed_ids[i] == 0x0b400000 + i for i in range(min(len(packed_ids), 20)))
    print(f"    Sequential from 0x0b400000: {sequential}")
    if sequential:
        last_sequential = 0
        for i in range(len(packed_ids)):
            if packed_ids[i] == 0x0b400000 + i:
                last_sequential = i
            else:
                break
        print(f"    Sequential up to index {last_sequential} (0x{0x0b400000 + last_sequential:08x})")

    # What comes AFTER this packed array?
    after_packed_off = packed_off + 1444 * 4
    after_packed_va = packed_va + 1444 * 4
    print(f"\n  Data AFTER packed array (VA 0x{after_packed_va:x}, 256 bytes):")
    for i in range(0, 256, 16):
        vals_u16 = [struct.unpack_from('<H', data, after_packed_off + i + j)[0] for j in range(0, 16, 2)]
        vals_u32 = [struct.unpack_from('<I', data, after_packed_off + i + j)[0] for j in range(0, 16, 4)]
        print(f"    +0x{i:04x}: u16=[{', '.join(f'{v:#06x}' for v in vals_u16)}]  "
              f"u32=[{', '.join(f'{v:#010x}' for v in vals_u32)}]")

    # ===== TABLE 4: Index mapping at VA 0x1413b0440 (descriptor+0x40) =====
    print("\n" + "=" * 70)
    print("TABLE 4: Index mapping at VA 0x1413b0440")
    print("=" * 70)

    idx_va = 0x1413b0440
    idx_off = va_to_file(idx_va, sections)

    # Read pairs of (index, field_id) — first 8 bytes may be header
    print("  First 8 bytes (header?):")
    h0 = struct.unpack_from('<I', data, idx_off)[0]
    h1 = struct.unpack_from('<I', data, idx_off + 4)[0]
    print(f"    0x{h0:08x} ({field_map.get(h0, '?')})")
    print(f"    0x{h1:08x} ({field_map.get(h1, '?')})")

    # Read pairs starting at +8
    print(f"\n  Index mapping pairs (from +0x08):")
    pair_count = 0
    idx_map = {}
    for i in range(2000):
        poff = idx_off + 8 + i * 8
        if poff + 8 > len(data):
            break
        idx_val = struct.unpack_from('<I', data, poff)[0]
        fid_val = struct.unpack_from('<I', data, poff + 4)[0]

        # Check if still in the 0x1f4xxxxx pattern
        if (idx_val & 0xfff00000) != 0x1f400000:
            print(f"\n  Pattern breaks at pair {i}: idx=0x{idx_val:08x} fid=0x{fid_val:08x}")
            pair_count = i
            break

        if is_task_field_id(fid_val):
            name = field_map.get(fid_val, '?')
            idx_map[idx_val & 0xffff] = fid_val
            if i < 20:
                print(f"    [{i:4d}] 0x{idx_val:08x} -> 0x{fid_val:08x} ({name})")
    else:
        pair_count = 2000

    print(f"\n  Total pairs: {pair_count}")
    print(f"  Task field IDs in mapping: {sum(1 for v in idx_map.values() if is_task_field_id(v))}")

    # ===== Cross-reference all tables =====
    print("\n" + "=" * 70)
    print("CROSS-REFERENCE")
    print("=" * 70)

    set1 = set(field_ids_1)
    set2 = set(field_ids_2)
    set_packed = set(packed_ids)
    set_idx = set(idx_map.values())

    print(f"Table 1 (.rdata 0x140fb7e80): {len(set1)} unique IDs")
    print(f"Table 2 (.rdata 0x1410e77a0): {len(set2)} unique IDs")
    print(f"Table 3 (.data packed):        {len(set_packed)} unique IDs")
    print(f"Table 4 (index mapping):       {len(set_idx)} unique IDs")

    print(f"\nOverlap:")
    print(f"  T1 ∩ T2: {len(set1 & set2)}")
    print(f"  T1 ∩ T3: {len(set1 & set_packed)}")
    print(f"  T1 ∩ T4: {len(set1 & set_idx)}")
    print(f"  T3 ∩ T4: {len(set_packed & set_idx)}")

    # IDs in T1 but not in COM field_id_map
    known_ids = set(field_map.keys())
    in_t1_not_known = set1 - known_ids
    print(f"\n  In T1 but NOT in field_id_map.tsv: {len(in_t1_not_known)}")
    if in_t1_not_known:
        sample = sorted(in_t1_not_known)[:10]
        print(f"    Sample: {[f'0x{v:08x}' for v in sample]}")

    in_known_not_t1 = known_ids - set1
    task_in_known_not_t1 = {v for v in in_known_not_t1 if is_task_field_id(v)}
    print(f"  Task IDs in field_id_map but NOT in T1: {len(task_in_known_not_t1)}")
    if task_in_known_not_t1:
        sample = sorted(task_in_known_not_t1)[:10]
        print(f"    Sample: {[f'0x{v:08x} ({field_map[v]})' for v in sample]}")

    # Write the field ID list with names
    out_path = ANALYSIS / "descriptor_field_ids.tsv"
    with open(out_path, 'w', newline='') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['table_index', 'field_id_hex', 'field_id_dec', 'property_name',
                     'in_rdata_t2', 'in_data_packed', 'in_idx_map'])
        for i, fid in enumerate(field_ids_1):
            w.writerow([
                i, f'0x{fid:08x}', fid,
                field_map.get(fid, ''),
                'Y' if fid in set2 else '',
                'Y' if fid in set_packed else '',
                'Y' if fid in set_idx else '',
            ])
    print(f"\nWrote field ID listing to {out_path}")

    print("\nDone.")


if __name__ == '__main__':
    main()
