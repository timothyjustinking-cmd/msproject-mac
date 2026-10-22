#!/usr/bin/env python3
"""Phase 2d: Examine key structures referenced by TAssnTable-specific functions.

Key addresses from vtable analysis:
- 0x140f86410 (.rdata) — returned by vfunc[152], LEA'd by vfunc[100]
- 0x1414af680 (.data) — global pointer used by 12+ TAssnTable functions
- 0x141099868 (.rdata) — ubiquitous CALL_IND target (likely __guard_dispatch_icall_fptr)
- 0x1413dd3e0 (.data) — referenced by vfunc[101]
- 0x1415dd9c8 (.data) — returned by vfunc[145]

Also compare vfunc[152] between TAssnTable (returns 0x140f86410) and TTaskTable (0x140224180).
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


def read_rtti_string(data, sections, type_info_va):
    """Read the class name from RTTI type_info structure."""
    off = va_to_file(type_info_va, sections)
    if off is None:
        return None
    # type_info layout: vtable(8), spare(8), name(variable at +16)
    name_off = off + 16
    raw = []
    while name_off < len(data) and data[name_off] != 0:
        b = data[name_off]
        raw.append(chr(b) if 32 <= b < 127 else '?')
        name_off += 1
        if len(raw) > 200:
            break
    return ''.join(raw)


known_descs = {
    0x140f8bda0: 'Task',
    0x140f8bf60: 'Resource',
    0x140f8c080: 'Links',
    0x140f863b0: 'Calendar',
    0x140f85ff0: 'Project',
    0x140f86350: 'OutlineCode',
}


def dump_structure(data, sections, va, size=0x70, label=''):
    """Dump a structure at VA with pointer annotation."""
    off = va_to_file(va, sections)
    if off is None:
        print(f"  Cannot read VA 0x{va:x}")
        return
    print(f"  Structure at VA 0x{va:x} ({label}):")
    for i in range(0, size, 8):
        if off + i + 8 > len(data):
            break
        val = struct.unpack_from('<Q', data, off + i)[0]
        lo = val & 0xFFFFFFFF
        hi = (val >> 32) & 0xFFFFFFFF
        ann = ''
        if is_valid_ptr(val, sections):
            sec = section_for_va(val, sections)
            ann = f'  [{sec}]'
            if val in known_descs:
                ann += f' = {known_descs[val]} descriptor'
            # Check if it's a known vtable
            if sec == '.rdata' and val > 0x141030000 and val < 0x141060000:
                ann += ' (vtable range)'
        elif lo == 0x0b404000:
            ann = '  ** Task field marker **'
        elif lo == 0x0c404000:
            ann = '  ** Resource field marker **'
        elif lo == 0x0f404000:
            ann = '  ** Assignment field marker **'
        elif lo == 0x0e404000:
            ann = '  ** Links field marker **'
        elif lo == 0x3c404000:
            ann = '  ** Calendar field marker **'
        elif lo == 0x07404000:
            ann = '  ** Project field marker **'
        elif lo == 0x3e404000:
            ann = '  ** OutlineCode field marker **'
        # Show as both u64 and two u32s
        print(f"    +0x{i:02x}: 0x{val:016x}  (lo=0x{lo:08x} hi=0x{hi:08x}){ann}")


def main():
    print("Phase 2d: Assignment-Specific Structure Analysis")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)

    # =========================================================================
    # 1. Examine 0x140f86410 — returned by TAssnTable vfunc[152]
    # =========================================================================
    print("\n1. STRUCTURE AT 0x140f86410 (TAssnTable vfunc[152] return)")
    print("-" * 60)
    dump_structure(data, sections, 0x140f86410, 0x80, 'vfunc[152] return value')

    # For comparison, what does TTaskTable vfunc[152] do?
    # TTaskTable vfunc[152] is at 0x140224180
    print("\n  TTaskTable vfunc[152] at 0x140224180:")
    func_off = va_to_file(0x140224180, sections)
    if func_off:
        raw = data[func_off:func_off + 32]
        hex_str = ' '.join(f'{b:02x}' for b in raw)
        print(f"    Raw bytes: {hex_str}")
        # Check for LEA rax, [rip+disp]; RET pattern
        if raw[0] in (0x48, 0x4C) and raw[1] == 0x8D:
            modrm = raw[2]
            if (modrm & 0xC7) == 0x05:
                disp = struct.unpack_from('<i', raw, 3)[0]
                target = 0x140224180 + 7 + disp
                sec = section_for_va(target, sections) if is_valid_ptr(target, sections) else '?'
                print(f"    -> LEA rax, 0x{target:x} ({sec})")
                if is_valid_ptr(target, sections):
                    print(f"\n  TTaskTable vfunc[152] return structure:")
                    dump_structure(data, sections, target, 0x80, 'TTaskTable vfunc[152] return')

    # =========================================================================
    # 2. Compare known descriptors with 0x140f86410
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. ALL KNOWN DESCRIPTOR STRUCTURES (for comparison)")
    print("-" * 60)

    for desc_va, name in sorted(known_descs.items()):
        print()
        dump_structure(data, sections, desc_va, 0x70, name)

    # =========================================================================
    # 3. Global pointer at 0x1414af680
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. GLOBAL POINTER AT 0x1414af680")
    print("-" * 60)
    off = va_to_file(0x1414af680, sections)
    if off:
        val = struct.unpack_from('<Q', data, off)[0]
        sec = section_for_va(val, sections) if is_valid_ptr(val, sections) else 'NULL/runtime'
        print(f"  Value: 0x{val:016x} ({sec})")
        print("  (This is likely a global project state pointer, NULL in static binary)")

        # Show surrounding context
        print("\n  Context around 0x1414af680:")
        for i in range(-4, 8):
            addr = 0x1414af680 + i * 8
            a_off = va_to_file(addr, sections)
            if a_off:
                v = struct.unpack_from('<Q', data, a_off)[0]
                s = section_for_va(v, sections) if is_valid_ptr(v, sections) else ''
                print(f"    VA 0x{addr:x}: 0x{v:016x} {s}")

    # =========================================================================
    # 4. Check RTTI for type_info pointers in the descriptor area
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. RTTI TYPE_INFO STRINGS FOR DESCRIPTORS")
    print("-" * 60)

    for desc_va, name in sorted(known_descs.items()):
        desc_off = va_to_file(desc_va, sections)
        if desc_off is None:
            continue
        ti_ptr = struct.unpack_from('<Q', data, desc_off)[0]
        if is_valid_ptr(ti_ptr, sections):
            rtti = read_rtti_string(data, sections, ti_ptr)
            print(f"  {name} (0x{desc_va:x}): type_info at 0x{ti_ptr:x} = {rtti}")

    # Check 0x140f86410 type_info
    off_86410 = va_to_file(0x140f86410, sections)
    if off_86410:
        ti_ptr = struct.unpack_from('<Q', data, off_86410)[0]
        if is_valid_ptr(ti_ptr, sections):
            rtti = read_rtti_string(data, sections, ti_ptr)
            print(f"  vfunc[152] (0x140f86410): type_info at 0x{ti_ptr:x} = {rtti}")
        else:
            print(f"  vfunc[152] (0x140f86410): +0x00 = 0x{ti_ptr:016x} (not a pointer)")

    # =========================================================================
    # 5. Scan .rdata near descriptors for all descriptor-like structures
    # =========================================================================
    print(f"\n{'='*70}")
    print("5. ALL DESCRIPTOR-LIKE STRUCTURES IN .RDATA (0x140f85000-0x140f90000)")
    print("-" * 60)

    # A descriptor has:
    # +0x00: valid pointer (type_info in .data)
    # +0x08: valid pointer (cache in .data)
    # +0x10: field count (small number < 10000)
    # +0x24: field_id marker (0xXX404000)

    scan_start = 0x140f85000
    scan_end = 0x140f90000

    start_off = va_to_file(scan_start, sections)
    end_off = va_to_file(scan_end, sections)

    if start_off and end_off:
        for pos in range(start_off, end_off - 0x30, 8):
            va = file_to_va(pos, sections)
            # Check +0x24 for known marker
            marker = struct.unpack_from('<I', data, pos + 0x24)[0]
            marker_prefix = marker >> 20
            if marker & 0xFFF != 0:
                continue
            if marker_prefix not in (0x0b4, 0x0c4, 0x0f4, 0x0e4, 0x3c4, 0x074, 0x3e4):
                continue

            # Check +0x00 for valid pointer
            p0 = struct.unpack_from('<Q', data, pos)[0]
            if not is_valid_ptr(p0, sections):
                continue

            type_names = {0x0b4: 'Task', 0x0c4: 'Resource', 0x0f4: 'Assignment',
                          0x0e4: 'Links', 0x3c4: 'Calendar', 0x074: 'Project', 0x3e4: 'OutlineCode'}
            tname = type_names.get(marker_prefix, '?')
            count_lo = struct.unpack_from('<H', data, pos + 0x10)[0]
            count_hi = struct.unpack_from('<H', data, pos + 0x12)[0]

            known = known_descs.get(va, '')
            if known:
                known = f'  ** KNOWN: {known} **'

            print(f"  VA 0x{va:x}: marker=0x{marker:08x} ({tname}), "
                  f"count=({count_lo},{count_hi}){known}")

    # =========================================================================
    # 6. Look at other data addresses referenced
    # =========================================================================
    print(f"\n{'='*70}")
    print("6. OTHER KEY DATA STRUCTURES")
    print("-" * 60)

    targets = [
        (0x1413dd3e0, 'vfunc[101] LEA r8 target'),
        (0x1415dd9c8, 'vfunc[145] LEA rax target'),
        (0x140fb2f10, 'vfunc[119] LEA rax target'),
        (0x1410d6b50, 'vfunc[126] LEA r8 target (also TTaskTable vfunc[66])'),
        (0x14112fe28, 'vfunc[126] LEA r8 target 2'),
        (0x14112fe50, 'vfunc[126] LEA r8 target 3'),
        (0x1411302f0, 'vfunc[135] LEA r8 target'),
        (0x140fa51f0, 'vfunc[98] LEA r8 target (also TTaskTable vfunc[72])'),
    ]

    for tva, desc in targets:
        toff = va_to_file(tva, sections)
        sec = section_for_va(tva, sections) if is_valid_ptr(tva, sections) else '?'
        print(f"\n  0x{tva:x} ({sec}) — {desc}:")
        if toff:
            # Try to read as string
            try:
                raw = data[toff:toff+64]
                # Check if it looks like a string
                is_ascii = all(32 <= b < 127 or b == 0 for b in raw[:32])
                if is_ascii and raw[0] != 0:
                    s = raw.split(b'\x00')[0].decode('ascii')
                    print(f"    ASCII string: \"{s}\"")
                else:
                    # Try as wide string
                    is_wide = all(raw[i+1] == 0 for i in range(0, min(20, len(raw)-1), 2) if raw[i] != 0)
                    if is_wide and raw[0] != 0:
                        ws = raw.decode('utf-16-le').split('\x00')[0]
                        print(f"    Wide string: \"{ws}\"")
                    else:
                        # Dump first 32 bytes
                        hex_str = ' '.join(f'{b:02x}' for b in raw[:32])
                        print(f"    Raw: {hex_str}")
                        # Also as pointers
                        for i in range(0, 32, 8):
                            v = struct.unpack_from('<Q', data, toff + i)[0]
                            s2 = section_for_va(v, sections) if is_valid_ptr(v, sections) else ''
                            print(f"    +{i:02x}: 0x{v:016x} {s2}")
            except Exception:
                pass

    # =========================================================================
    # 7. Find what vfunc[152] looks like across ALL record types
    # =========================================================================
    print(f"\n{'='*70}")
    print("7. vfunc[152] ACROSS ALL TABLE VTABLES")
    print("-" * 60)

    # Check vtable[152] for other known table vtables
    table_vtables = {
        'TAssnTable': 0x141038b08,
        'TTaskTable': 0x141048648,
        'TTaskAssnContainer': 0x14102c148,
        'TRscAssnContainer': 0x14102cc60,
    }

    for name, vt_va in sorted(table_vtables.items()):
        vt_off = va_to_file(vt_va, sections)
        if vt_off is None:
            continue
        # Read slot 152
        if vt_off + 152 * 8 + 8 <= len(data):
            func_va = struct.unpack_from('<Q', data, vt_off + 152 * 8)[0]
            func_off = va_to_file(func_va, sections)
            if func_off:
                raw = data[func_off:func_off + 16]
                hex_str = ' '.join(f'{b:02x}' for b in raw)
                # Check for LEA rax, [rip+disp]; RET
                target = None
                if raw[0] in (0x48, 0x4C) and raw[1] == 0x8D and (raw[2] & 0xC7) == 0x05:
                    disp = struct.unpack_from('<i', raw, 3)[0]
                    target = func_va + 7 + disp
                print(f"  {name:25s} vfunc[152] = 0x{func_va:x}: {hex_str}")
                if target:
                    sec = section_for_va(target, sections) if is_valid_ptr(target, sections) else '?'
                    print(f"    -> returns pointer to 0x{target:x} ({sec})")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
