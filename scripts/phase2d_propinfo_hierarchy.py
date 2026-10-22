#!/usr/bin/env python3
"""Phase 2d: Map the full PropInfoData class hierarchy.

Discovered classes:
  - PropInfoData           (base, vtable 0x141050670)
  - PropInfoDataPersist    (type_info at 0x1414a8050)
  - CustomPropInfoData     (type_info at 0x1413e37b0)
  - CustomPropInfoDataPersist (type_info at 0x1413e37e0)

Goals:
1. Find vtables for all 4 classes
2. Compare vtable entries to understand which methods are overridden
3. Determine which cache entries use which vtable at runtime
4. Disassemble key method differences
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


def resolve_rtti_name(data, sections, vtable_va):
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
        name_bytes.append(chr(data[name_off]) if 32 <= data[name_off] < 127 else '?')
        name_off += 1
    return ''.join(name_bytes)


def count_vtable_entries(data, sections, vtable_off, max_entries=100):
    count = 0
    for i in range(max_entries):
        v = struct.unpack_from('<Q', data, vtable_off + i * 8)[0]
        if not is_valid_ptr(v, sections):
            break
        if section_for_va(v, sections) != '.text':
            break
        count += 1
    return count


def describe_func(data, sections, func_va):
    off = va_to_file(func_va, sections)
    if off is None:
        return '?', ''
    raw = data[off:off + 16]
    hex_str = ' '.join(f'{b:02x}' for b in raw[:8])

    if raw[0] == 0x33 and raw[1] == 0xC0 and raw[2] == 0xC3:
        return hex_str, 'return 0 (int)'
    if raw[0] == 0x32 and raw[1] == 0xC0 and raw[2] == 0xC3:
        return hex_str, 'return false (bool)'
    if raw[0] == 0xB0 and raw[1] == 0x01 and raw[2] == 0xC3:
        return hex_str, 'return true (bool)'
    if raw[0] == 0xC2 and raw[1] == 0x00 and raw[2] == 0x00:
        return hex_str, 'RET (no-op setter)'
    if raw[0] == 0xC3:
        return hex_str, 'RET (void no-op)'
    if raw[0:3] == bytes([0x0F, 0xB7, 0x41]):
        member_off = raw[3]
        return hex_str, f'MOVZX EAX, WORD [RCX+0x{member_off:x}] (read u16)'
    if raw[0:2] == bytes([0x8B, 0x41]):
        member_off = raw[2]
        return hex_str, f'MOV EAX, [RCX+0x{member_off:x}] (read u32)'
    if raw[0:2] == bytes([0x48, 0x8B]) and raw[2] == 0x41:
        member_off = raw[3]
        return hex_str, f'MOV RAX, [RCX+0x{member_off:x}] (read u64/ptr)'
    if raw[0:3] == bytes([0x0F, 0x10, 0x05]):
        rip_off = struct.unpack_from('<i', raw, 3)[0]
        target = func_va + 7 + rip_off
        return hex_str, f'MOVUPS XMM0, [0x{target:x}] (load 128-bit from static)'
    if raw[0] == 0xB8 and raw[5] == 0xC3:
        imm = struct.unpack_from('<I', raw, 1)[0]
        return hex_str, f'return 0x{imm:x} ({imm})'
    if raw[0] == 0x48 and raw[1] == 0x8D and raw[2] == 0x05:
        rip_off = struct.unpack_from('<i', raw, 3)[0]
        target = func_va + 7 + rip_off
        sec = section_for_va(target, sections)
        return hex_str, f'LEA RAX, [0x{target:x}] ({sec})'

    return hex_str, '(complex)'


def main():
    print("Phase 2d: PropInfoData Class Hierarchy")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)

    # =========================================================================
    # 1. Find vtables for all PropInfoData-related classes
    # =========================================================================
    print("\n1. FINDING ALL PropInfoData-FAMILY VTABLES")
    print("-" * 60)

    target_names = {
        '.?AUPropInfoData@@',
        '.?AUPropInfoDataPersist@@',
        '.?AUCustomPropInfoData@@',
        '.?AUCustomPropInfoDataPersist@@',
    }

    # Scan entire .rdata for vtables with these RTTI names
    found_vtables = {}
    for s in sections:
        if s['name'] != '.rdata':
            continue
        start_off = s['raw_ptr']
        end_off = s['raw_ptr'] + s['raw_size']
        pos = start_off + 8  # need room for COL pointer at vtable[-1]
        while pos < end_off - 8:
            # Check if this looks like a vtable: COL at [-1], .text ptr at [0]
            col_ptr = struct.unpack_from('<Q', data, pos - 8)[0]
            first = struct.unpack_from('<Q', data, pos)[0]

            if (is_valid_ptr(col_ptr, sections)
                    and section_for_va(col_ptr, sections) == '.rdata'
                    and is_valid_ptr(first, sections)
                    and section_for_va(first, sections) == '.text'):
                col_off = va_to_file(col_ptr, sections)
                if col_off and struct.unpack_from('<I', data, col_off)[0] == 1:
                    name = resolve_rtti_name(data, sections, file_to_va(pos, sections))
                    if name and name in target_names:
                        vt_va = file_to_va(pos, sections)
                        n_entries = count_vtable_entries(data, sections, pos)
                        if name not in found_vtables:
                            found_vtables[name] = []
                        found_vtables[name].append((vt_va, n_entries))
                        print(f"  {name}: VA 0x{vt_va:x}, {n_entries} entries")
            pos += 8

    # =========================================================================
    # 2. Compare vtable entries between base and derived classes
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. VTABLE COMPARISON")
    print("-" * 60)

    base_vt = 0x141050670
    base_off = va_to_file(base_vt, sections)
    base_count = count_vtable_entries(data, sections, base_off)
    print(f"\n  Base PropInfoData: {base_count} entries")

    base_funcs = []
    for i in range(base_count):
        func_va = struct.unpack_from('<Q', data, base_off + i * 8)[0]
        base_funcs.append(func_va)

    # Compare each derived vtable
    for cls_name in sorted(found_vtables.keys()):
        if cls_name == '.?AUPropInfoData@@':
            continue
        for vt_va, n_entries in found_vtables[cls_name]:
            print(f"\n  {cls_name}: {n_entries} entries at 0x{vt_va:x}")
            vt_off = va_to_file(vt_va, sections)

            # Compare with base
            max_compare = min(base_count, n_entries)
            diffs = []
            for i in range(max_compare):
                func_va = struct.unpack_from('<Q', data, vt_off + i * 8)[0]
                if func_va != base_funcs[i]:
                    diffs.append(i)

            # New entries
            new_entries = list(range(base_count, n_entries))

            print(f"    Overridden slots: {diffs}")
            if new_entries:
                print(f"    New slots: {new_entries[:10]}{'...' if len(new_entries) > 10 else ''}")

            # Detail each override
            for i in diffs:
                base_func = base_funcs[i]
                derived_func = struct.unpack_from('<Q', data, vt_off + i * 8)[0]
                _, base_desc = describe_func(data, sections, base_func)
                _, derived_desc = describe_func(data, sections, derived_func)
                print(f"    [{i:2d}] base: {base_desc}")
                print(f"         derived: 0x{derived_func:x} — {derived_desc}")

            # Detail new entries
            for i in new_entries:
                func_va = struct.unpack_from('<Q', data, vt_off + i * 8)[0]
                _, desc = describe_func(data, sections, func_va)
                print(f"    [{i:2d}] 0x{func_va:x}: {desc}")

    # =========================================================================
    # 3. Annotate PropInfoData vtable methods
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. PropInfoData VTABLE METHOD ANNOTATIONS")
    print("-" * 60)

    # Based on the getter/setter pair pattern and the disassembly
    method_names = [
        'GetTypeCode',           # [0]: MOVZX EAX, WORD [RCX+0x14] — reads type code from +0x14
        'SetTypeCode',           # [1]: RET (no-op in base)
        'GetSubTypeCode',        # [2]: return 0
        'SetSubTypeCode',        # [3]: RET (no-op)
        'GetFieldCount',         # [4]: MOV EAX, [RCX+0x10] — reads from +0x10
        'SetFieldCount',         # [5]: RET (no-op)
        'GetGUID_1',             # [6]: MOVUPS from static — loads 128-bit GUID
        'SetGUID_1',             # [7]: RET (no-op)
        'GetGUID_2',             # [8]: same as [6]
        'SetGUID_2',             # [9]: RET (no-op)
        'GetGUID_3',             # [10]: same as [6]
        'SetGUID_3',             # [11]: RET (no-op)
        'GetGUID_4',             # [12]: same as [6]
        'SetGUID_4',             # [13]: RET (no-op)
        'IsCustomField',         # [14]: return false
        'IsEnterprise',          # [15]: return false
        'IsCalculated',          # [16]: return false
        'IsFormula',             # [17]: return false
        'IsOutlineCode',         # [18]: return false
        'IsLookupTable',         # [19]: return false
        'IsPersistable',         # [20]: return false
    ]

    print(f"\n  {'Slot':<6s} {'Method':<25s} {'Address':>12s} Description")
    for i in range(min(base_count, len(method_names))):
        func_va = base_funcs[i]
        _, desc = describe_func(data, sections, func_va)
        print(f"  [{i:2d}]  {method_names[i]:<25s} 0x{func_va:x}  {desc}")

    # =========================================================================
    # 4. Check what methods CustomPropInfoData overrides to return true
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. WHICH 'Is...' METHODS EACH DERIVED CLASS OVERRIDES")
    print("-" * 60)

    for cls_name in sorted(found_vtables.keys()):
        if cls_name == '.?AUPropInfoData@@':
            continue
        for vt_va, n_entries in found_vtables[cls_name]:
            print(f"\n  {cls_name} (0x{vt_va:x}):")
            vt_off = va_to_file(vt_va, sections)

            for i in range(14, min(21, n_entries)):
                func_va = struct.unpack_from('<Q', data, vt_off + i * 8)[0]
                hex_str, desc = describe_func(data, sections, func_va)
                is_overridden = func_va != base_funcs[i] if i < base_count else True
                marker = ' ** OVERRIDDEN **' if is_overridden else ''
                mname = method_names[i] if i < len(method_names) else f'vfunc_{i}'
                print(f"    [{i:2d}] {mname:<25s} {desc}{marker}")

    # =========================================================================
    # 5. Scan for references to derived vtables (where are they constructed?)
    # =========================================================================
    print(f"\n{'='*70}")
    print("5. CROSS-REFERENCE: WHICH FUNCTIONS CONSTRUCT DERIVED PropInfoData")
    print("-" * 60)

    for cls_name in sorted(found_vtables.keys()):
        for vt_va, n_entries in found_vtables[cls_name]:
            # Search .text for LEA instructions that reference this vtable
            vt_rva = vt_va - IMAGE_BASE
            # RIP-relative LEA: 48 8D 05 XX XX XX XX
            for s in sections:
                if s['name'] != '.text':
                    continue
                refs = 0
                pos = s['raw_ptr']
                end = s['raw_ptr'] + s['raw_size'] - 16
                while pos < end:
                    # LEA reg, [rip+disp32]
                    if data[pos] == 0x48 and data[pos+1] == 0x8D:
                        modrm = data[pos+2]
                        if (modrm & 0xC7) == 0x05:  # [rip+disp32]
                            disp = struct.unpack_from('<i', data, pos + 3)[0]
                            target_va = file_to_va(pos + 7, sections)
                            if target_va:
                                target = target_va + disp
                                if target == vt_va:
                                    caller_va = file_to_va(pos, sections)
                                    if refs < 5:
                                        print(f"  {cls_name} vtable 0x{vt_va:x} ref from 0x{caller_va:x}")
                                    refs += 1
                    pos += 1
                if refs > 5:
                    print(f"    ... ({refs} total references)")
                elif refs == 0:
                    print(f"  {cls_name} vtable 0x{vt_va:x}: no LEA references from .text")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
