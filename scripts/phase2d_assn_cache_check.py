#!/usr/bin/env python3
"""Phase 2d: Verify the Assignment cache and extract pre-populated entries.

BREAKTHROUGH: Assignment descriptor found at VA 0x140f86410 (.rdata)!
- Cache at VA 0x1412c0f30 (.data), 777 entries × 0x40 bytes
- vfunc[152] is the descriptor accessor (returns descriptor pointer)
- Marker at +0x24 = 0x0f4040c8 (slightly different from 0xXX404000 pattern)

Also examine the unknown Task-34 descriptor at 0x140f864d0.
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


def file_to_va(foff, sections):
    for s in sections:
        if s['raw_ptr'] <= foff < s['raw_ptr'] + s['raw_size']:
            rva = foff - s['raw_ptr'] + s['virt_addr']
            return IMAGE_BASE + rva
    return None


def section_for_va(va, sections):
    rva = va - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return s['name']
    return '?'


def is_valid_ptr(val, sections):
    if val < IMAGE_BASE or val > IMAGE_BASE + 0x20000000:
        return False
    rva = val - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return True
    return False


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
    print("Phase 2d: Assignment Cache Verification")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # =========================================================================
    # 1. Verify the Assignment cache at 0x1412c0f30
    # =========================================================================
    print("\n1. ASSIGNMENT CACHE AT VA 0x1412c0f30")
    print("-" * 60)

    cache_va = 0x1412c0f30
    cache_off = va_to_file(cache_va, sections)
    count = 777

    # Check first few entries for pre-populated data
    non_zero_entries = 0
    first_populated = None
    vtable_ptrs = set()

    for i in range(count):
        entry_off = cache_off + i * 0x40
        vtable = struct.unpack_from('<Q', data, entry_off)[0]
        field_id = struct.unpack_from('<I', data, entry_off + 0x08)[0]
        type_code = struct.unpack_from('<I', data, entry_off + 0x0C)[0]
        rec_offset = struct.unpack_from('<I', data, entry_off + 0x18)[0]
        flags = struct.unpack_from('<I', data, entry_off + 0x1C)[0]
        size = struct.unpack_from('<I', data, entry_off + 0x30)[0]

        # Check if entry is non-zero (populated)
        entry_bytes = data[entry_off:entry_off + 0x40]
        if any(b != 0 for b in entry_bytes):
            non_zero_entries += 1
            if vtable != 0:
                vtable_ptrs.add(vtable)
            if first_populated is None:
                first_populated = i

            if non_zero_entries <= 5:
                name = field_map.get(field_id, '')
                vt_sec = section_for_va(vtable, sections) if is_valid_ptr(vtable, sections) else 'zero'
                print(f"  Entry [{i:3d}]: vtable=0x{vtable:x}({vt_sec}) field_id=0x{field_id:08x}({name}) "
                      f"type={type_code} offset=0x{rec_offset:x} flags=0x{flags:x} size={size}")
                # Hex dump
                hex_str = ' '.join(f'{b:02x}' for b in entry_bytes[:32])
                print(f"             {hex_str}")

    print(f"\n  Total entries: {count}")
    print(f"  Non-zero entries: {non_zero_entries}")
    print(f"  Distinct vtable pointers: {len(vtable_ptrs)}")
    for vt in sorted(vtable_ptrs):
        sec = section_for_va(vt, sections) if is_valid_ptr(vt, sections) else '?'
        print(f"    0x{vt:x} ({sec})")

    # =========================================================================
    # 2. Compare with Task cache (entry 0 is known pre-populated)
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. TASK CACHE ENTRY 0 (for comparison)")
    print("-" * 60)

    task_cache_va = 0x14130be10
    task_off = va_to_file(task_cache_va, sections)
    entry_bytes = data[task_off:task_off + 0x40]
    hex_str = ' '.join(f'{b:02x}' for b in entry_bytes)
    print(f"  Raw: {hex_str}")

    vtable = struct.unpack_from('<Q', data, task_off)[0]
    field_id = struct.unpack_from('<I', data, task_off + 0x08)[0]
    type_code = struct.unpack_from('<I', data, task_off + 0x0C)[0]
    rec_offset = struct.unpack_from('<I', data, task_off + 0x18)[0]
    size = struct.unpack_from('<I', data, task_off + 0x30)[0]
    name = field_map.get(field_id, '')
    print(f"  vtable=0x{vtable:x} field_id=0x{field_id:08x}({name}) type={type_code} "
          f"offset=0x{rec_offset:x} size={size}")

    # =========================================================================
    # 3. Examine the Task-34 descriptor at 0x140f864d0
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. UNKNOWN TASK-34 DESCRIPTOR AT VA 0x140f864d0")
    print("-" * 60)

    desc_va = 0x140f864d0
    desc_off = va_to_file(desc_va, sections)
    print(f"  Structure at VA 0x{desc_va:x}:")
    for i in range(0, 0x70, 8):
        val = struct.unpack_from('<Q', data, desc_off + i)[0]
        lo = val & 0xFFFFFFFF
        hi = (val >> 32) & 0xFFFFFFFF
        sec = section_for_va(val, sections) if is_valid_ptr(val, sections) else ''
        print(f"    +0x{i:02x}: 0x{val:016x}  (lo=0x{lo:08x} hi=0x{hi:08x}) {sec}")

    # =========================================================================
    # 4. Decode Assignment descriptor fields (parallel to Task)
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. DECODED ASSIGNMENT DESCRIPTOR (0x140f86410)")
    print("-" * 60)

    desc_off = va_to_file(0x140f86410, sections)
    d = lambda off, fmt: struct.unpack_from(fmt, data, desc_off + off)[0]

    print(f"  +0x00: type_info ptr   = 0x{d(0, '<Q'):x}")
    print(f"  +0x08: cache ptr       = 0x{d(8, '<Q'):x}")
    print(f"  +0x10: field count     = {d(0x10, '<H')}, {d(0x12, '<H')}")
    print(f"  +0x14: categories      = {d(0x14, '<H')}, {d(0x16, '<H')}, {d(0x18, '<H')}, {d(0x1a, '<H')}")
    print(f"  +0x1c: categories      = {d(0x1c, '<H')}, {d(0x1e, '<H')}, {d(0x20, '<H')}, {d(0x22, '<H')}")
    marker = d(0x24, '<I')
    print(f"  +0x24: field marker    = 0x{marker:08x}")
    print(f"  +0x28: flags           = {d(0x28, '<I')}, {d(0x2c, '<I')}")
    print(f"  +0x30: unknown         = {d(0x30, '<I')}, {d(0x34, '<I')}")
    print(f"  +0x38: column count?   = {d(0x38, '<I')}, {d(0x3c, '<I')}")
    idx_table = d(0x40, '<Q')
    print(f"  +0x40: index table     = 0x{idx_table:x}")
    print(f"  +0x48: packed          = 0x{d(0x48, '<Q'):016x}")
    print(f"  +0x50: near-last fid   = 0x{d(0x50, '<I'):08x}")
    sec_desc = d(0x58, '<Q')
    print(f"  +0x58: secondary desc  = 0x{sec_desc:016x}")
    disp = d(0x60, '<Q')
    accessor = d(0x68, '<Q')
    disp_sec = section_for_va(disp, sections) if is_valid_ptr(disp, sections) else ''
    acc_sec = section_for_va(accessor, sections) if is_valid_ptr(accessor, sections) else ''
    print(f"  +0x60: dispatcher      = 0x{disp:x} ({disp_sec})")
    print(f"  +0x68: accessor        = 0x{accessor:x} ({acc_sec})")

    # Compare with Task descriptor
    print(f"\n  COMPARISON WITH TASK DESCRIPTOR (0x140f8bda0):")
    task_off = va_to_file(0x140f8bda0, sections)
    td = lambda off, fmt: struct.unpack_from(fmt, data, task_off + off)[0]

    print(f"  {'Field':<20s} {'Assignment':>16s} {'Task':>16s}")
    print(f"  {'-----':<20s} {'----------':>16s} {'----':>16s}")
    print(f"  {'field count':<20s} {d(0x10, '<H'):>16d} {td(0x10, '<H'):>16d}")
    print(f"  {'marker':<20s} {d(0x24, '<I'):>16x} {td(0x24, '<I'):>16x}")
    print(f"  {'column count?':<20s} {d(0x38, '<I'):>16d} {td(0x38, '<I'):>16d}")

    # =========================================================================
    # 5. Find the sorted array for Assignment binary search
    # =========================================================================
    print(f"\n{'='*70}")
    print("5. FINDING ASSIGNMENT SORTED ARRAY (for binary search)")
    print("-" * 60)

    # The Task descriptor doesn't have an explicit sorted array pointer;
    # vfunc[158] accesses it via instance data. But the sorted arrays are
    # in .data at known addresses. For Assignment, the sorted arrays were:
    # 156 at 0x1412aa6f0, 191 at 0x1412ac58c
    # But 777 fields need a larger array. It might be 777 entries.

    # Scan .data near the cache for a sorted 0x0f4 array of 777 entries
    # Cache is at 0x1412c0f30, spanning to 0x1412c0f30 + 777*0x40 = 0x1412CD170

    # Check before the cache
    print("  Scanning .data before cache (0x1412b0000-0x1412c0f30):")
    scan_va = 0x1412b0000
    scan_off = va_to_file(scan_va, sections)
    cache_off = va_to_file(cache_va, sections)

    pos = scan_off
    while pos < cache_off - 16:
        vals = []
        check = pos
        while check < cache_off - 4:
            v = struct.unpack_from('<I', data, check)[0]
            if (v >> 20) == 0x0f4 and v < 0x0f410000:
                if not vals or v >= vals[-1]:
                    vals.append(v)
                    check += 4
                else:
                    break
            else:
                break
        if len(vals) >= 10:
            va = file_to_va(pos, sections)
            print(f"    VA 0x{va:x}: {len(vals)} sorted 0x0f4 IDs "
                  f"(0x{min(vals):08x}-0x{max(vals):08x})")
            pos = check
        else:
            pos += 4

    # Also check after the cache
    cache_end_off = cache_off + 777 * 0x40
    cache_end_va = file_to_va(cache_end_off, sections) if cache_end_off else 0
    print(f"\n  Cache ends at VA 0x{cache_end_va:x}")
    print(f"  Scanning .data after cache (0x{cache_end_va:x}-0x1412e0d30):")

    pos = cache_end_off
    rsc_cache_off = va_to_file(0x1412e0d30, sections)
    while pos < rsc_cache_off - 16:
        vals = []
        check = pos
        while check < rsc_cache_off - 4:
            v = struct.unpack_from('<I', data, check)[0]
            if (v >> 20) == 0x0f4 and v < 0x0f410000:
                if not vals or v >= vals[-1]:
                    vals.append(v)
                    check += 4
                else:
                    break
            else:
                break
        if len(vals) >= 10:
            va = file_to_va(pos, sections)
            print(f"    VA 0x{va:x}: {len(vals)} sorted 0x0f4 IDs "
                  f"(0x{min(vals):08x}-0x{max(vals):08x})")
            pos = check
        else:
            pos += 4

    # =========================================================================
    # 6. Summary of ALL descriptors including Assignment
    # =========================================================================
    print(f"\n{'='*70}")
    print("6. COMPLETE DESCRIPTOR TABLE (ALL 7+ RECORD TYPES)")
    print("-" * 60)

    all_descs = [
        (0x140f85ff0, 'Project'),
        (0x140f86350, 'OutlineCode'),
        (0x140f863b0, 'Calendar'),
        (0x140f86410, 'Assignment'),
        (0x140f864d0, 'Task-34'),
        (0x140f8bda0, 'Task'),
        (0x140f8bf60, 'Resource'),
        (0x140f8c080, 'Links'),
    ]

    print(f"  {'Name':<15s} {'VA':>16s} {'Fields':>8s} {'Cache VA':>16s} {'Marker':>12s}")
    print(f"  {'----':<15s} {'--':>16s} {'------':>8s} {'--------':>16s} {'------':>12s}")

    for desc_va, name in all_descs:
        doff = va_to_file(desc_va, sections)
        if doff is None:
            continue
        cache_ptr = struct.unpack_from('<Q', data, doff + 0x08)[0]
        fcount = struct.unpack_from('<H', data, doff + 0x10)[0]
        marker = struct.unpack_from('<I', data, doff + 0x24)[0]
        print(f"  {name:<15s} 0x{desc_va:x} {fcount:>8d} 0x{cache_ptr:x} 0x{marker:08x}")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
