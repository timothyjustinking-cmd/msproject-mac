#!/usr/bin/env python3
"""Phase 2d: Analyze the PropInfoData class and verify cache entry validity.

PropInfoData (`.?AUPropInfoData@@`) is the 0x40-byte field metadata class.
Vtable at 0x141050670 with 21 entries.

Goals:
1. Verify which cache entries are truly valid (vtable == 0x141050670)
2. Dump PropInfoData vtable entries and resolve their RTTI
3. Look for other PropInfoData-derived vtables
4. Map the 21 vfuncs to understand the field metadata interface
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
    vt_off = va_to_file(vtable_va, sections)
    if vt_off is None or vt_off < 8:
        return None
    col_ptr = struct.unpack_from('<Q', data, vt_off - 8)[0]
    if not is_valid_ptr(col_ptr, sections):
        return None
    col_off = va_to_file(col_ptr, sections)
    if col_off is None:
        return None
    sig = struct.unpack_from('<I', data, col_off)[0]
    if sig != 1:
        return None
    td_rva = struct.unpack_from('<I', data, col_off + 0x0C)[0]
    ti_va = IMAGE_BASE + td_rva
    ti_off = va_to_file(ti_va, sections)
    if ti_off is None:
        return None
    name_off = ti_off + 0x10
    name_bytes = []
    while name_off < len(data) and data[name_off] != 0 and len(name_bytes) < 256:
        b = data[name_off]
        name_bytes.append(chr(b) if 32 <= b < 127 else '?')
        name_off += 1
    return ''.join(name_bytes)


PROPINFO_VT = 0x141050670


def main():
    print("Phase 2d: PropInfoData Analysis")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)

    # =========================================================================
    # 1. Verify valid entries across all caches
    # =========================================================================
    print("\n1. VALID CACHE ENTRIES (vtable == 0x141050670)")
    print("-" * 60)

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

    for name, cache_va, count in caches:
        cache_off = va_to_file(cache_va, sections)
        if cache_off is None:
            continue

        valid = 0
        any_vtable = set()
        for i in range(count):
            entry_off = cache_off + i * 0x40
            vt = struct.unpack_from('<Q', data, entry_off)[0]
            if vt == PROPINFO_VT:
                valid += 1
            elif vt != 0:
                any_vtable.add(vt)

        other_vt_str = ''
        if any_vtable:
            vt_list = sorted(any_vtable)[:5]
            other_vt_str = f", other vtables: {', '.join(f'0x{v:x}' for v in vt_list)}"
        print(f"  {name:<15s}: {valid:4d}/{count:4d} valid PropInfoData entries{other_vt_str}")

    # =========================================================================
    # 2. PropInfoData vtable entries
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. PropInfoData VTABLE (21 entries at 0x141050670)")
    print("-" * 60)

    vt_off = va_to_file(PROPINFO_VT, sections)
    for i in range(21):
        func_va = struct.unpack_from('<Q', data, vt_off + i * 8)[0]
        func_off = va_to_file(func_va, sections)

        # Disassemble first 16 bytes to identify trivial functions
        desc = ''
        if func_off:
            raw = data[func_off:func_off + 16]
            # Common patterns:
            # 33 c0 c3 = XOR EAX, EAX; RET (return 0)
            # b0 01 c3 = MOV AL, 1; RET (return true)
            # b8 XX XX XX XX c3 = MOV EAX, imm32; RET
            # 48 8b .. c3 = MOV RAX, [member]; RET
            if raw[0] == 0x33 and raw[1] == 0xC0 and raw[2] == 0xC3:
                desc = 'return 0'
            elif raw[0] == 0xB0 and raw[2] == 0xC3:
                desc = f'return {raw[1]}'
            elif raw[0] == 0xB8 and raw[5] == 0xC3:
                imm = struct.unpack_from('<I', raw, 1)[0]
                desc = f'return 0x{imm:x} ({imm})'
            elif raw[0] == 0xC3:
                desc = 'return void'
            elif raw[0] == 0x48 and raw[1] == 0x83 and raw[2] == 0xC1:
                # ADD RCX, imm8 — thunk
                desc = f'ADD RCX, {raw[3]}; (thunk)'
            elif raw[0:2] == bytes([0x0F, 0xB6]):
                # MOVZX
                desc = 'MOVZX ... (read byte field)'
            elif raw[0] == 0x48 and raw[1] == 0x89:
                desc = 'MOV [stack], reg (complex function)'

            hex_str = ' '.join(f'{b:02x}' for b in raw[:8])
            print(f"  [{i:2d}] 0x{func_va:x}: {hex_str}  {desc}")

    # =========================================================================
    # 3. Look for PropInfoData-derived classes (different vtables)
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. SEARCHING FOR PropInfoData-DERIVED VTABLES")
    print("-" * 60)

    # The COL for PropInfoData should have a class hierarchy that we can trace.
    # Also scan for vtables whose RTTI includes "PropInfo" in the name.

    # Get all RTTI strings near the PropInfoData area
    rtti_name = resolve_rtti(data, sections, PROPINFO_VT)
    print(f"  PropInfoData RTTI: {rtti_name}")

    # Search for RTTI strings containing "PropInfo" or "Prop"
    print(f"\n  Scanning RTTI for 'PropInfo' classes:")

    # Scan .data for type_info strings
    for s in sections:
        if s['name'] != '.data':
            continue
        search = b'PropInfo'
        pos = 0
        while True:
            idx = data.find(search, s['raw_ptr'] + pos, s['raw_ptr'] + s['raw_size'])
            if idx == -1:
                break
            # Read surrounding string
            start = idx
            while start > s['raw_ptr'] and data[start - 1] != 0:
                start -= 1
            end = idx
            while end < s['raw_ptr'] + s['raw_size'] and data[end] != 0:
                end += 1
            raw_str = ''.join(chr(b) if 32 <= b < 127 else '?' for b in data[start:end])
            va = file_to_va(start, sections)
            # Check if this is at +0x10 from a type_info (vtable at start-0x10)
            ti_off = start - 0x10
            if ti_off >= s['raw_ptr']:
                ti_va = file_to_va(ti_off, sections)
                print(f"    VA 0x{va:x}: {raw_str} (type_info at 0x{ti_va:x})")
            pos = idx - s['raw_ptr'] + 1

    # =========================================================================
    # 4. Entry 0 comparison across all cache types
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. ENTRY 0 COMPARISON ACROSS ALL CACHE TYPES")
    print("-" * 60)

    print(f"  {'Type':<12s} {'FieldID':>12s} {'TypeCode':>10s} {'Offset':>8s} {'Size':>6s} {'Flags':>10s}")
    for name, cache_va, count in caches:
        cache_off = va_to_file(cache_va, sections)
        if cache_off is None:
            continue
        vt = struct.unpack_from('<Q', data, cache_off)[0]
        if vt != PROPINFO_VT:
            continue
        fid = struct.unpack_from('<I', data, cache_off + 8)[0]
        tc = struct.unpack_from('<I', data, cache_off + 12)[0]
        off = struct.unpack_from('<I', data, cache_off + 0x18)[0]
        sz = struct.unpack_from('<I', data, cache_off + 0x30)[0]
        fl = struct.unpack_from('<I', data, cache_off + 0x1C)[0]
        print(f"  {name:<12s} 0x{fid:08x} {tc:10d} 0x{off:04x} {sz:6d} 0x{fl:08x}")

    # =========================================================================
    # 5. Check if Task34 has valid entries after entry 0
    # =========================================================================
    print(f"\n{'='*70}")
    print("5. TASK34 CACHE DETAILED ANALYSIS")
    print("-" * 60)

    cache_va = 0x14137e9a0
    cache_off = va_to_file(cache_va, sections)

    for i in range(34):
        entry_off = cache_off + i * 0x40
        vt = struct.unpack_from('<Q', data, entry_off)[0]
        fid = struct.unpack_from('<I', data, entry_off + 8)[0]
        tc = struct.unpack_from('<I', data, entry_off + 12)[0]
        off = struct.unpack_from('<I', data, entry_off + 0x18)[0]
        sz = struct.unpack_from('<I', data, entry_off + 0x30)[0]
        fl = struct.unpack_from('<I', data, entry_off + 0x1C)[0]

        valid = 'VALID' if vt == PROPINFO_VT else f'vt=0x{vt:x}'
        print(f"  [{i:2d}] {valid:20s} fid=0x{fid:08x} type={tc:4d} off=0x{off:04x} sz={sz:6d} fl=0x{fl:08x}")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
