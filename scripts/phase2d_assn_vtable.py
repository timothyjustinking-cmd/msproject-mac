#!/usr/bin/env python3
"""Phase 2d: Find TAssnTable vtable and understand its field access.

Key findings so far:
- TAssnTable RTTI exists at VA 0x1414ab9b0 (.data)
- TBkndAssnElem does NOT exist (unlike TBkndTaskElem for Task)
- IBkndAssnDispatch exists
- Assignments accessed through TTaskTable::EnumAssignments() etc.
- TTaskAssnContainer and TRscAssnContainer hold assignments
- ASSIGNNODE struct is the record type

Strategy:
1. Find TAssnTable vtable from its RTTI type_info pointer
2. Look for TAssnTable's complete_object_locator (COL) referencing the RTTI
3. Find vtable entries pointing to field accessors
4. Check if TAssnTable has its own descriptor-like structure
"""

import struct
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


def is_code_ptr(val, sections):
    rva = val - IMAGE_BASE
    for s in sections:
        if s['name'] == '.text' and s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return True
    return False


def is_valid_ptr(val, sections):
    if val < IMAGE_BASE or val > IMAGE_BASE + 0x20000000:
        return False
    rva = val - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return True
    return False


def find_rtti_vtables(data, sections, rtti_name_va):
    """Find vtables that reference a given RTTI type_info VA.

    MSVC RTTI chain: vtable[-1] -> COL -> type_descriptor (type_info)
    COL is _RTL_Complete_Object_Locator with RVA-based references (PE32+).
    """
    rtti_rva = rtti_name_va - IMAGE_BASE

    # COL format for PE32+: uses RVAs relative to image base
    # Offset 12 (0x0C) = RVA of type_descriptor
    # We need to find COLs that point to a type_descriptor whose name matches

    # The type_descriptor (type_info) has the name at offset 16+
    # The RTTI name VA we have points to the .?AV... string
    # type_descriptor starts 16 bytes before the name string (2 qwords: vtable + spare)
    type_desc_va = rtti_name_va - 16
    type_desc_rva = type_desc_va - IMAGE_BASE

    # Now find COLs that reference this type_descriptor RVA at offset +0x0C
    # COL is 24 bytes: [sig(4), offset(4), cdOffset(4), typeDesc_rva(4), classHier_rva(4), selfRef_rva(4)]
    td_bytes = struct.pack('<I', type_desc_rva & 0xFFFFFFFF)

    col_locations = []
    pos = 0
    while True:
        idx = data.find(td_bytes, pos)
        if idx == -1:
            break
        # Check if this is at offset +0x0C of a COL
        col_start = idx - 0x0C
        if col_start >= 0:
            sig = struct.unpack_from('<I', data, col_start)[0]
            if sig == 1:  # COL signature for PE32+
                col_va = file_to_va(col_start, sections)
                if col_va and section_for_va(col_va, sections) == '.rdata':
                    col_locations.append((col_start, col_va))
        pos = idx + 1

    # Now find vtable entries: vtable[-1] (the qword before the vtable) points to COL
    vtables = []
    for col_off, col_va in col_locations:
        col_bytes = struct.pack('<Q', col_va)
        pos = 0
        while True:
            idx = data.find(col_bytes, pos)
            if idx == -1:
                break
            # vtable starts at idx + 8 (the COL pointer is at vtable[-1])
            vtable_off = idx + 8
            vtable_va = file_to_va(vtable_off, sections)
            if vtable_va:
                sec = section_for_va(vtable_va, sections)
                if sec == '.rdata':
                    vtables.append((vtable_off, vtable_va, col_va))
            pos = idx + 1

    return vtables


def main():
    print("Phase 2d: TAssnTable VTable and Field Access Analysis")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)

    # =========================================================================
    # 1. Find TAssnTable vtable
    # =========================================================================
    print("\n1. FINDING TAssnTable VTABLE")
    print("-" * 50)

    # TAssnTable RTTI name at VA 0x1414ab9b0 (points to ".?AVTAssnTable@@")
    # Actually, the RTTI name string starts at +16 of the type_descriptor
    # Let me find the exact string position
    target = b'.?AVTAssnTable@@'
    idx = data.find(target)
    if idx == -1:
        print("  ERROR: TAssnTable RTTI string not found")
        return

    rtti_name_va = file_to_va(idx, sections)
    print(f"  RTTI name string at VA 0x{rtti_name_va:x}")

    vtables = find_rtti_vtables(data, sections, rtti_name_va)
    print(f"  Found {len(vtables)} vtable(s)")

    for vtable_off, vtable_va, col_va in vtables:
        print(f"\n  VTABLE at VA 0x{vtable_va:x} (COL at 0x{col_va:x})")

        # Dump vtable entries
        max_entries = 200  # reasonable max
        entries = []
        for i in range(max_entries):
            off = vtable_off + i * 8
            if off + 8 > len(data):
                break
            val = struct.unpack_from('<Q', data, off)[0]
            if not is_code_ptr(val, sections):
                break
            entries.append(val)

        print(f"  {len(entries)} vtable entries:")
        for i, val in enumerate(entries[:30]):
            print(f"    [{i:3d}] 0x{val:x}")
        if len(entries) > 30:
            print(f"    ... ({len(entries) - 30} more, total {len(entries)})")
            # Show last 5
            for i in range(max(30, len(entries)-5), len(entries)):
                print(f"    [{i:3d}] 0x{val:x}")

    # =========================================================================
    # 2. Also find TTaskTable vtable for comparison
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. TTaskTable VTABLE FOR COMPARISON")
    print("-" * 50)

    target = b'.?AVTTaskTable@@'
    idx = data.find(target)
    if idx != -1:
        rtti_name_va = file_to_va(idx, sections)
        print(f"  RTTI name string at VA 0x{rtti_name_va:x}")

        vtables = find_rtti_vtables(data, sections, rtti_name_va)
        print(f"  Found {len(vtables)} vtable(s)")

        for vtable_off, vtable_va, col_va in vtables:
            # Count entries
            count = 0
            for i in range(500):
                off = vtable_off + i * 8
                if off + 8 > len(data):
                    break
                val = struct.unpack_from('<Q', data, off)[0]
                if not is_code_ptr(val, sections):
                    break
                count += 1
            print(f"  VTABLE at VA 0x{vtable_va:x}: {count} entries")

    # =========================================================================
    # 3. Find IBkndAssnDispatch vtable
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. IBkndAssnDispatch VTABLE")
    print("-" * 50)

    target = b'.?AVIBkndAssnDispatch@@'
    idx = data.find(target)
    if idx != -1:
        rtti_name_va = file_to_va(idx, sections)
        print(f"  RTTI name string at VA 0x{rtti_name_va:x}")

        vtables = find_rtti_vtables(data, sections, rtti_name_va)
        print(f"  Found {len(vtables)} vtable(s)")

        for vtable_off, vtable_va, col_va in vtables:
            entries = []
            for i in range(200):
                off = vtable_off + i * 8
                if off + 8 > len(data):
                    break
                val = struct.unpack_from('<Q', data, off)[0]
                if not is_code_ptr(val, sections):
                    break
                entries.append(val)
            print(f"  VTABLE at VA 0x{vtable_va:x}: {len(entries)} entries")
            for i, val in enumerate(entries[:20]):
                print(f"    [{i:3d}] 0x{val:x}")
            if len(entries) > 20:
                print(f"    ... ({len(entries) - 20} more)")

    # =========================================================================
    # 4. Find TTaskAssnContainer and TRscAssnContainer vtables
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. ASSIGNMENT CONTAINER VTABLES")
    print("-" * 50)

    for cls_name in ['TTaskAssnContainer', 'TRscAssnContainer', 'IAssignmentContainer']:
        target = f'.?AV{cls_name}@@'.encode()
        idx = data.find(target)
        if idx != -1:
            rtti_name_va = file_to_va(idx, sections)
            print(f"\n  {cls_name}: RTTI at VA 0x{rtti_name_va:x}")

            vtables = find_rtti_vtables(data, sections, rtti_name_va)
            print(f"  Found {len(vtables)} vtable(s)")

            for vtable_off, vtable_va, col_va in vtables:
                entries = []
                for i in range(200):
                    off = vtable_off + i * 8
                    if off + 8 > len(data):
                        break
                    val = struct.unpack_from('<Q', data, off)[0]
                    if not is_code_ptr(val, sections):
                        break
                    entries.append(val)
                print(f"  VTABLE at VA 0x{vtable_va:x}: {len(entries)} entries")
                for i, val in enumerate(entries[:15]):
                    print(f"    [{i:3d}] 0x{val:x}")
                if len(entries) > 15:
                    print(f"    ... ({len(entries) - 15} more)")
        else:
            print(f"\n  {cls_name}: NOT FOUND")

    # =========================================================================
    # 5. Check code around known TAssnTable functions
    # =========================================================================
    print(f"\n{'='*70}")
    print("5. LOOKING FOR LEA INSTRUCTIONS LOADING DESCRIPTOR-LIKE ADDRESSES")
    print("-" * 50)
    print("Searching .text for LEA RXX, [rip+disp] that loads known descriptor VAs...")

    # Known descriptor RVAs
    known_descs = {
        0xf8bda0: 'Task',
        0xf8bf60: 'Resource',
        0xf8c080: 'Links',
        0xf863b0: 'Calendar',
        0xf85ff0: 'Project',
        0xf86350: 'OutlineCode',
    }

    text_sec = None
    for s in sections:
        if s['name'] == '.text':
            text_sec = s
            break

    if text_sec:
        # LEA reg, [rip+disp32] is encoded as:
        # 48/4C 8D 05/0D/15/1D/25/2D/35/3D disp32
        # Or REX.W + 8D ModRM with ModRM.mod=00, r/m=101 (RIP-relative)
        for desc_rva, name in known_descs.items():
            desc_va = IMAGE_BASE + desc_rva
            found_count = 0
            for pos in range(text_sec['raw_ptr'], text_sec['raw_ptr'] + text_sec['raw_size'] - 7):
                # Check for LEA with RIP-relative addressing
                b0 = data[pos]
                b1 = data[pos+1]
                b2 = data[pos+2]
                if b0 not in (0x48, 0x4C):
                    continue
                if b1 != 0x8D:
                    continue
                modrm = b2
                if (modrm & 0xC7) != 0x05:  # mod=00, r/m=101 (RIP-relative)
                    continue
                disp = struct.unpack_from('<i', data, pos + 3)[0]
                # RIP at time of execution = VA of next instruction = VA of (pos+7)
                instr_va = file_to_va(pos, sections)
                if instr_va is None:
                    continue
                target_va = instr_va + 7 + disp
                if target_va == desc_va:
                    found_count += 1
                    if found_count <= 5:
                        print(f"  {name} descriptor loaded at VA 0x{instr_va:x}")
            if found_count > 5:
                print(f"  ... {found_count} total references to {name} descriptor")
            elif found_count == 0:
                print(f"  {name} descriptor: no LEA references found")

    # =========================================================================
    # 6. Search for any descriptor-like structure for Assignment (0x0f4)
    # =========================================================================
    print(f"\n{'='*70}")
    print("6. SEARCHING FOR ASSIGNMENT DESCRIPTOR-LIKE STRUCTURES")
    print("-" * 50)
    print("Looking for structures with 0x0f4xxxxx field IDs near cache-like pointers...")

    # Look in .rdata and .data for structures that:
    # - Have a pointer to .data (cache)
    # - Have count-like values (200-600)
    # - Have 0x0f4 prefix field IDs nearby
    for s in sections:
        if s['name'] not in ('.rdata', '.data'):
            continue
        for pos in range(0, s['raw_size'] - 0x70, 8):
            foff = s['raw_ptr'] + pos
            # Check for the marker pattern: a u32 with 0x0f4 prefix at what would be the marker offset (+0x24)
            marker_off = foff + 0x24
            if marker_off + 4 > len(data):
                continue
            marker = struct.unpack_from('<I', data, marker_off)[0]
            if (marker >> 20) != 0x0f4:
                continue
            va = IMAGE_BASE + s['virt_addr'] + pos
            print(f"\n  Potential descriptor at VA 0x{va:x} ({s['name']}):")
            for i in range(14):
                eoff = foff + i * 8
                if eoff + 8 > len(data):
                    break
                ev = struct.unpack_from('<Q', data, eoff)[0]
                v32a = struct.unpack_from('<I', data, eoff)[0]
                v32b = struct.unpack_from('<I', data, eoff + 4)[0]
                sec2 = section_for_va(ev, sections) if is_valid_ptr(ev, sections) else ''
                print(f"    +0x{i*8:02x}: 0x{ev:016x}  (u32: 0x{v32a:08x}, 0x{v32b:08x}) {sec2}")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
