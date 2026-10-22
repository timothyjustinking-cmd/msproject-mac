#!/usr/bin/env python3
"""Phase 2d: Identify the field type vtables used in cache entries.

From the Task cache entry 0: vtable = 0x141050670. This is a polymorphic object.
The RTTI chain from the vtable gives us the class name.

MSVC RTTI chain: vtable[-1] -> Complete Object Locator -> type_info -> name

Also: examine the Task-34 descriptor (0x140f864d0) to understand its 34 fields.
"""

import struct
from pathlib import Path

IMAGE_BASE = 0x140000000
WINPROJ = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
BASE = Path(r"C:\Users\timot\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile")


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


def resolve_rtti(data, sections, vtable_va):
    """Resolve MSVC RTTI from vtable pointer.
    vtable[-1] -> Complete Object Locator (COL)
    COL+0x10 -> RVA to type_info
    type_info+0x10 -> decorated name string
    """
    vt_off = va_to_file(vtable_va, sections)
    if vt_off is None or vt_off < 8:
        return None, None

    # vtable[-1] = pointer to COL
    col_ptr = struct.unpack_from('<Q', data, vt_off - 8)[0]
    if not is_valid_ptr(col_ptr, sections):
        return None, None

    col_off = va_to_file(col_ptr, sections)
    if col_off is None:
        return None, None

    # COL layout (MSVC x64):
    # +0x00: signature (u32) = 1
    # +0x04: offset (u32)
    # +0x08: cdOffset (u32)
    # +0x0C: type_desc_rva (u32) -> RVA to type_info
    # +0x10: class_desc_rva (u32) -> RVA to class hierarchy descriptor
    # +0x14: self_rva (u32) -> RVA of this COL

    sig = struct.unpack_from('<I', data, col_off)[0]
    if sig != 1:
        return None, None

    td_rva = struct.unpack_from('<I', data, col_off + 0x0C)[0]
    type_info_va = IMAGE_BASE + td_rva
    ti_off = va_to_file(type_info_va, sections)
    if ti_off is None:
        return None, None

    # type_info layout:
    # +0x00: vtable ptr (8 bytes)
    # +0x08: spare (8 bytes)
    # +0x10: decorated name (null-terminated ASCII)
    name_off = ti_off + 0x10
    name_bytes = []
    while name_off < len(data) and data[name_off] != 0 and len(name_bytes) < 256:
        b = data[name_off]
        name_bytes.append(chr(b) if 32 <= b < 127 else '?')
        name_off += 1

    return type_info_va, ''.join(name_bytes)


def main():
    print("Phase 2d: Cache Entry Vtable Identification")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)

    # =========================================================================
    # 1. Resolve RTTI for the known cache entry vtable
    # =========================================================================
    print("\n1. RTTI FOR CACHE ENTRY VTABLE 0x141050670")
    print("-" * 60)

    ti_va, name = resolve_rtti(data, sections, 0x141050670)
    if name:
        print(f"  type_info at 0x{ti_va:x}: {name}")
    else:
        print("  Could not resolve RTTI")

    # =========================================================================
    # 2. Find ALL vtables near 0x141050670 (likely related field type classes)
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. VTABLE NEIGHBORHOOD (0x141050000-0x141052000)")
    print("-" * 60)

    # Scan .rdata for vtable-like sequences (pointers to .text)
    scan_va = 0x141050000
    scan_end = 0x141052000
    scan_off = va_to_file(scan_va, sections)
    end_off = va_to_file(scan_end, sections)

    if scan_off and end_off:
        pos = scan_off
        while pos < end_off - 16:
            # Check for RTTI COL pointer at pos-8
            if pos >= scan_off + 8:
                col_ptr = struct.unpack_from('<Q', data, pos - 8)[0]
                if is_valid_ptr(col_ptr, sections):
                    col_sec = section_for_va(col_ptr, sections)
                    if col_sec == '.rdata':
                        # Verify it's a valid COL (signature = 1)
                        col_off2 = va_to_file(col_ptr, sections)
                        if col_off2 and struct.unpack_from('<I', data, col_off2)[0] == 1:
                            vt_va = file_to_va(pos, sections)
                            ti_va, cls_name = resolve_rtti(data, sections, vt_va)
                            if cls_name:
                                # Count vtable entries
                                vt_count = 0
                                check = pos
                                while check < end_off:
                                    v = struct.unpack_from('<Q', data, check)[0]
                                    if is_valid_ptr(v, sections):
                                        sec = section_for_va(v, sections)
                                        if sec == '.text':
                                            vt_count += 1
                                            check += 8
                                            continue
                                    break
                                print(f"  VA 0x{vt_va:x} ({vt_count} entries): {cls_name}")
            pos += 8

    # =========================================================================
    # 3. Look for all cache entry vtables across the full .rdata range
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. ALL FIELD TYPE VTABLES (searching near 0x141050670)")
    print("-" * 60)

    # The known vtable 0x141050670 resolves to some class.
    # Look for related vtables by scanning ±4096 bytes
    center = 0x141050670
    for offset in range(-0x1000, 0x1000, 8):
        vt_va = center + offset
        vt_off = va_to_file(vt_va, sections)
        if vt_off is None or vt_off < 8:
            continue

        # Quick check: is this a vtable start? (COL pointer before it, .text pointer at it)
        col_ptr = struct.unpack_from('<Q', data, vt_off - 8)[0]
        first_entry = struct.unpack_from('<Q', data, vt_off)[0]

        if not is_valid_ptr(col_ptr, sections):
            continue
        if section_for_va(col_ptr, sections) != '.rdata':
            continue
        if not is_valid_ptr(first_entry, sections):
            continue
        if section_for_va(first_entry, sections) != '.text':
            continue

        col_off2 = va_to_file(col_ptr, sections)
        if col_off2 is None or struct.unpack_from('<I', data, col_off2)[0] != 1:
            continue

        ti_va, cls_name = resolve_rtti(data, sections, vt_va)
        if cls_name:
            marker = " <<< KNOWN CACHE VTABLE" if vt_va == 0x141050670 else ""
            print(f"  0x{vt_va:x}: {cls_name}{marker}")

    # =========================================================================
    # 4. Examine the Task cache pre-populated entry more deeply
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. TASK CACHE ENTRY 0 — FULL 0x40 BYTE ANALYSIS")
    print("-" * 60)

    task_cache = 0x14130be10
    tc_off = va_to_file(task_cache, sections)
    entry = data[tc_off:tc_off + 0x40]

    for off in range(0, 0x40, 4):
        val32 = struct.unpack_from('<I', entry, off)[0]
        if off % 8 == 0:
            val64 = struct.unpack_from('<Q', entry, off)[0]
            sec = section_for_va(val64, sections) if is_valid_ptr(val64, sections) else ''
            print(f"  +0x{off:02x}: 0x{val64:016x} {sec}  (u32: 0x{val32:08x})")
        else:
            print(f"  +0x{off:02x}: (u32: 0x{val32:08x} = {val32})")

    # Same for Assignment cache entry 0
    print(f"\n  ASSIGNMENT CACHE ENTRY 0:")
    assn_cache = 0x1412c0f30
    ac_off = va_to_file(assn_cache, sections)
    entry = data[ac_off:ac_off + 0x40]

    for off in range(0, 0x40, 4):
        val32 = struct.unpack_from('<I', entry, off)[0]
        if off % 8 == 0:
            val64 = struct.unpack_from('<Q', entry, off)[0]
            sec = section_for_va(val64, sections) if is_valid_ptr(val64, sections) else ''
            print(f"  +0x{off:02x}: 0x{val64:016x} {sec}  (u32: 0x{val32:08x})")
        else:
            print(f"  +0x{off:02x}: (u32: 0x{val32:08x} = {val32})")

    # =========================================================================
    # 5. Examine the Task-34 descriptor's fields
    # =========================================================================
    print(f"\n{'='*70}")
    print("5. TASK-34 DESCRIPTOR ANALYSIS")
    print("-" * 60)

    task34_desc = 0x140f864d0
    td_off = va_to_file(task34_desc, sections)

    # Decode
    d = lambda o, f: struct.unpack_from(f, data, td_off + o)[0]
    ti_ptr = d(0, '<Q')
    cache_ptr = d(8, '<Q')
    count = d(0x10, '<H')
    marker = d(0x24, '<I')

    print(f"  type_info:     0x{ti_ptr:x}")
    print(f"  cache:         0x{cache_ptr:x}")
    print(f"  field count:   {count}")
    print(f"  marker:        0x{marker:08x}")

    # Resolve RTTI for type_info
    # type_info is at ti_ptr, name at ti_ptr+0x10
    ti_off = va_to_file(ti_ptr, sections)
    if ti_off:
        name_off = ti_off + 0x10
        name_bytes = []
        while name_off < len(data) and data[name_off] != 0 and len(name_bytes) < 256:
            b = data[name_off]
            name_bytes.append(chr(b) if 32 <= b < 127 else '?')
            name_off += 1
        print(f"  type_info name: {''.join(name_bytes)}")

    # Check the field IDs from +0x48 (packed)
    packed48 = d(0x48, '<Q')
    lo48 = packed48 & 0xFFFFFFFF
    hi48 = (packed48 >> 32) & 0xFFFFFFFF
    packed50 = d(0x50, '<I')
    print(f"  +0x48 packed:  lo=0x{lo48:08x}, hi=0x{hi48:08x}")
    print(f"  +0x50 last_id: 0x{packed50:08x}")

    # secondary descriptor
    sec_desc = d(0x58, '<Q')
    if is_valid_ptr(sec_desc, sections):
        sec = section_for_va(sec_desc, sections)
        print(f"  secondary:     0x{sec_desc:x} ({sec})")

        # If it's a valid descriptor, dump its +0x24 marker
        sd_off = va_to_file(sec_desc, sections)
        if sd_off:
            sd_marker = struct.unpack_from('<I', data, sd_off + 0x24)[0]
            sd_count = struct.unpack_from('<H', data, sd_off + 0x10)[0]
            print(f"    -> count={sd_count}, marker=0x{sd_marker:08x}")

    # Check the cache for pre-populated entries
    cache_off = va_to_file(cache_ptr, sections)
    if cache_off:
        populated = 0
        for i in range(count):
            e_off = cache_off + i * 0x40
            e_data = data[e_off:e_off + 0x40]
            if any(b != 0 for b in e_data):
                populated += 1
                if populated <= 3:
                    vt = struct.unpack_from('<Q', e_data, 0)[0]
                    fid = struct.unpack_from('<I', e_data, 8)[0]
                    tc = struct.unpack_from('<I', e_data, 12)[0]
                    ro = struct.unpack_from('<I', e_data, 0x18)[0]
                    sz = struct.unpack_from('<I', e_data, 0x30)[0]
                    print(f"  Cache[{i}]: vtable=0x{vt:x} field_id=0x{fid:08x} type={tc} offset=0x{ro:x} size={sz}")
        print(f"  Pre-populated: {populated}/{count}")

    # =========================================================================
    # 6. Index mapping table for Assignment descriptor
    # =========================================================================
    print(f"\n{'='*70}")
    print("6. ASSIGNMENT INDEX MAPPING TABLE AT 0x141391f50")
    print("-" * 60)

    idx_table = 0x141391f50
    idx_off = va_to_file(idx_table, sections)
    if idx_off:
        print("  First 30 pairs (column_ordinal, field_id):")
        for i in range(30):
            pair_off = idx_off + i * 8
            ordinal = struct.unpack_from('<I', data, pair_off)[0]
            field_id = struct.unpack_from('<I', data, pair_off + 4)[0]
            if ordinal == 0 and field_id == 0 and i > 0:
                print(f"    [{i}] (end of table)")
                break
            prefix = (field_id >> 20) if field_id > 0 else 0
            if prefix not in (0x0f4, 0x0b4, 0x0c4, 0) and ordinal > 0x10000:
                print(f"    [{i}] 0x{ordinal:08x}, 0x{field_id:08x} (looks like end)")
                break
            print(f"    [{i:2d}] ordinal={ordinal}, field_id=0x{field_id:08x}")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
