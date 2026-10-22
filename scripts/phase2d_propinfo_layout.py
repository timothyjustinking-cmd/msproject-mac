#!/usr/bin/env python3
"""Phase 2d: Decode PropInfoData object layout and cache population.

Now that we know there are two classes (PropInfoData and CustomPropInfoData),
we need to understand:
1. The full 0x40-byte object layout (what are +0x20..+0x3F?)
2. The cache population function (how fields get their offsets at runtime)
3. The GUID at 0x140fd9760 used by base class getters
4. The CustomPropInfoData constructor to learn its extended fields

Key addresses:
- PropInfoData vtable: 0x141050670
- CustomPropInfoData vtable: 0x140fb4a80
- CustomPropInfoData constructors: 0x1407ca9bf, 0x1407cabb8
- Base GUID: 0x140fd9760
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


def disasm_bytes(data, sections, va, count=32):
    off = va_to_file(va, sections)
    if off is None:
        return
    raw = data[off:off + count]
    for i in range(0, min(len(raw), count), 1):
        if i % 16 == 0:
            print(f"    0x{va + i:x}: ", end='')
        print(f'{raw[i]:02x} ', end='')
        if (i + 1) % 16 == 0:
            print()
    if count % 16 != 0:
        print()


def main():
    print("Phase 2d: PropInfoData Layout & Cache Population")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)

    # =========================================================================
    # 1. The GUID at 0x140fd9760
    # =========================================================================
    print("\n1. BASE CLASS GUID AT 0x140fd9760")
    print("-" * 60)

    guid_va = 0x140fd9760
    guid_off = va_to_file(guid_va, sections)
    if guid_off:
        guid_data = data[guid_off:guid_off + 16]
        d1 = struct.unpack_from('<I', guid_data, 0)[0]
        d2 = struct.unpack_from('<H', guid_data, 4)[0]
        d3 = struct.unpack_from('<H', guid_data, 6)[0]
        d4 = guid_data[8:16]
        guid_str = f'{{{d1:08x}-{d2:04x}-{d3:04x}-{d4[0]:02x}{d4[1]:02x}-{d4[2]:02x}{d4[3]:02x}{d4[4]:02x}{d4[5]:02x}{d4[6]:02x}{d4[7]:02x}}}'
        print(f"  GUID: {guid_str}")
        is_null = all(b == 0 for b in guid_data)
        print(f"  Is GUID_NULL: {is_null}")

    # =========================================================================
    # 2. Disassemble CustomPropInfoData constructor context
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. CUSTOM PROP INFO DATA CONSTRUCTION SITES")
    print("-" * 60)

    for site_va in [0x1407ca9bf, 0x1407cabb8]:
        print(f"\n  Construction site at 0x{site_va:x}:")
        # Show 16 bytes before and 32 bytes after the LEA
        start_va = site_va - 32
        disasm_bytes(data, sections, start_va, 80)

    # =========================================================================
    # 3. Analyze first few PropInfoData construction sites
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. CORE PropInfoData CONSTRUCTION FUNCTION")
    print("-" * 60)

    # First 5 LEA refs from section 5 of previous analysis
    early_refs = [0x140001967, 0x140001b76, 0x140001d9d, 0x140001f82, 0x140002183]

    for ref_va in early_refs:
        print(f"\n  Ref at 0x{ref_va:x}:")
        start_va = ref_va - 16
        disasm_bytes(data, sections, start_va, 48)

    # =========================================================================
    # 4. Disassemble CustomPropInfoData Is... methods
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. CustomPropInfoData 'Is...' METHOD IMPLEMENTATIONS")
    print("-" * 60)

    custom_vt = 0x140fb4a80
    custom_off = va_to_file(custom_vt, sections)

    method_names = [
        'IsCustomField', 'IsEnterprise', 'IsCalculated',
        'IsFormula', 'IsOutlineCode', 'IsLookupTable', 'IsPersistable'
    ]

    for idx, mname in zip(range(14, 21), method_names):
        func_va = struct.unpack_from('<Q', data, custom_off + idx * 8)[0]
        func_off = va_to_file(func_va, sections)
        raw = data[func_off:func_off + 32]
        hex_first = ' '.join(f'{b:02x}' for b in raw[:16])
        print(f"\n  [{idx}] {mname} at 0x{func_va:x}:")
        print(f"    {hex_first}")

        # Try to decode common patterns
        if raw[0] == 0xB0 and raw[2] == 0xC3:
            print(f"    -> return {raw[1]} (constant)")
        elif raw[0] == 0x0F and raw[1] == 0xB6 and raw[2] == 0x41:
            member_off = raw[3]
            print(f"    -> MOVZX EAX, BYTE [RCX+0x{member_off:x}]; ... (read flag byte)")
        elif raw[0] == 0x8A and raw[1] == 0x41:
            member_off = raw[2]
            print(f"    -> MOV AL, BYTE [RCX+0x{member_off:x}]; ... (read flag byte)")
        elif raw[0:2] == bytes([0xF6, 0x41]):
            member_off = raw[2]
            mask = raw[3]
            print(f"    -> TEST BYTE [RCX+0x{member_off:x}], 0x{mask:02x}; ... (test flag bit)")

    # =========================================================================
    # 5. Deduce full PropInfoData layout from vtable accessors
    # =========================================================================
    print(f"\n{'='*70}")
    print("5. DEDUCED PropInfoData OBJECT LAYOUT")
    print("-" * 60)

    # From vtable analysis:
    # [0] GetTypeCode reads u16 from +0x14
    # [4] GetFieldCount reads u32 from +0x10
    # From cache entry 0:
    # +0x00: vtable ptr (8 bytes)
    # +0x08: field_id (4 bytes)
    # +0x0C: type_code (4 bytes) — but GetTypeCode reads u16 from +0x14!

    # Wait — let me re-check. The vtable method reads from +0x14, but we
    # assumed type_code is at +0x0C. Let me verify by looking at where
    # type codes actually ARE for the known valid entries.

    print("\n  Cross-checking type_code position for Task entry 0:")
    task_cache_off = va_to_file(0x14130be10, sections)
    entry = data[task_cache_off:task_cache_off + 0x40]

    print("  Raw bytes (hex):")
    for row in range(4):
        off = row * 16
        hex_str = ' '.join(f'{entry[off+i]:02x}' for i in range(16))
        ascii_str = ''.join(chr(entry[off+i]) if 32 <= entry[off+i] < 127 else '.' for i in range(16))
        print(f"    +0x{off:02x}: {hex_str}  {ascii_str}")

    # Task entry 0 has type_code = 5 (Work). Let's find where 5 appears.
    print(f"\n  Looking for type_code value 5:")
    for off in range(0, 0x40, 2):
        val16 = struct.unpack_from('<H', entry, off)[0]
        if val16 == 5:
            print(f"    Found 5 at +0x{off:02x} (u16)")
    for off in range(0, 0x40, 4):
        val32 = struct.unpack_from('<I', entry, off)[0]
        if val32 == 5:
            print(f"    Found 5 at +0x{off:02x} (u32)")

    # Now check Assignment entry 0 (type_code = 3, Duration)
    print(f"\n  Cross-checking for Assignment entry 0 (type=3, Duration):")
    assn_cache_off = va_to_file(0x1412c0f30, sections)
    entry2 = data[assn_cache_off:assn_cache_off + 0x40]

    print("  Raw bytes (hex):")
    for row in range(4):
        off = row * 16
        hex_str = ' '.join(f'{entry2[off+i]:02x}' for i in range(16))
        print(f"    +0x{off:02x}: {hex_str}")

    # GetTypeCode reads u16 from +0x14
    tc_task = struct.unpack_from('<H', entry, 0x14)[0]
    tc_assn = struct.unpack_from('<H', entry2, 0x14)[0]
    print(f"\n  GetTypeCode (u16 at +0x14):")
    print(f"    Task entry 0: {tc_task}")
    print(f"    Assn entry 0: {tc_assn}")

    # GetFieldCount reads u32 from +0x10
    fc_task = struct.unpack_from('<I', entry, 0x10)[0]
    fc_assn = struct.unpack_from('<I', entry2, 0x10)[0]
    print(f"\n  GetFieldCount (u32 at +0x10):")
    print(f"    Task entry 0: {fc_task}")
    print(f"    Assn entry 0: {fc_assn}")

    # =========================================================================
    # 6. Correct layout based on vtable accessors
    # =========================================================================
    print(f"\n{'='*70}")
    print("6. REVISED PropInfoData LAYOUT (from vtable accessors)")
    print("-" * 60)

    # Let me dump all interpretations
    print("\n  TASK ENTRY 0 — Full field decode:")
    offsets = [
        (0x00, 'Q', 'vtable'),
        (0x08, 'I', 'field_id'),
        (0x0C, 'I', 'unknown_0C'),
        (0x10, 'I', 'vfunc4_reads_u32 (GetFieldCount?)'),
        (0x14, 'H', 'vfunc0_reads_u16 (GetTypeCode)'),
        (0x16, 'H', 'unknown_16'),
        (0x18, 'I', 'rec_offset'),
        (0x1C, 'I', 'flags'),
        (0x20, 'Q', 'unknown_20'),
        (0x28, 'I', 'unknown_28'),
        (0x2C, 'I', 'unknown_2C'),
        (0x30, 'I', 'size'),
        (0x34, 'I', 'unknown_34'),
        (0x38, 'Q', 'unknown_38'),
    ]

    for off, fmt, name in offsets:
        if fmt == 'Q':
            val = struct.unpack_from('<Q', entry, off)[0]
            sec = section_for_va(val, sections) if is_valid_ptr(val, sections) else ''
            print(f"    +0x{off:02x}: 0x{val:016x} {sec:<8s} {name}")
        elif fmt == 'I':
            val = struct.unpack_from('<I', entry, off)[0]
            print(f"    +0x{off:02x}: 0x{val:08x} ({val:>10d}) {name}")
        elif fmt == 'H':
            val = struct.unpack_from('<H', entry, off)[0]
            print(f"    +0x{off:02x}: 0x{val:04x} ({val:>6d})     {name}")

    print("\n  ASSIGNMENT ENTRY 0 — Full field decode:")
    for off, fmt, name in offsets:
        if fmt == 'Q':
            val = struct.unpack_from('<Q', entry2, off)[0]
            sec = section_for_va(val, sections) if is_valid_ptr(val, sections) else ''
            print(f"    +0x{off:02x}: 0x{val:016x} {sec:<8s} {name}")
        elif fmt == 'I':
            val = struct.unpack_from('<I', entry2, off)[0]
            print(f"    +0x{off:02x}: 0x{val:08x} ({val:>10d}) {name}")
        elif fmt == 'H':
            val = struct.unpack_from('<H', entry2, off)[0]
            print(f"    +0x{off:02x}: 0x{val:04x} ({val:>6d})     {name}")

    # =========================================================================
    # 7. Look at the cache population function for layout clues
    # =========================================================================
    print(f"\n{'='*70}")
    print("7. CACHE POPULATION — FINDING THE CORE LOOP")
    print("-" * 60)

    # The 3410 references are from .text LEAs to the PropInfoData vtable.
    # Let's find the function that contains the MOST such references.
    # First, find a few hundred and group by function start.

    vt_rva = 0x141050670 - IMAGE_BASE
    ref_counts = {}  # func_start -> count

    for s in sections:
        if s['name'] != '.text':
            continue
        pos = s['raw_ptr']
        end = s['raw_ptr'] + s['raw_size'] - 16
        refs = []
        while pos < end:
            if data[pos] == 0x48 and data[pos+1] == 0x8D:
                modrm = data[pos+2]
                if (modrm & 0xC7) == 0x05:
                    disp = struct.unpack_from('<i', data, pos + 3)[0]
                    target_va = file_to_va(pos + 7, sections)
                    if target_va:
                        target = target_va + disp
                        if target == 0x141050670:
                            refs.append(file_to_va(pos, sections))
            pos += 1

    # Group by 0x100-byte windows to find hot functions
    windows = {}
    for ref in refs:
        win = ref & ~0xFF
        windows[win] = windows.get(win, 0) + 1

    top_windows = sorted(windows.items(), key=lambda x: -x[1])[:10]
    print(f"  Top 10 function regions with most PropInfoData constructions:")
    for win, count in top_windows:
        print(f"    0x{win:x}: {count} references")

    # Also check specific functions we know about
    # The field lookup function at 0x140239380 (TTaskTable vfunc[158])
    print(f"\n  Known function sizes:")
    known_funcs = {
        'TTaskTable_vf158_lookup': 0x140239380,
        'TTaskTable_vf87_GetFieldValue': 0x140231f00,
    }
    for name, va in known_funcs.items():
        off = va_to_file(va, sections)
        if off:
            # Count bytes until RET
            size = 0
            while size < 0x2000:
                if data[off + size] == 0xC3:
                    size += 1
                    break
                size += 1
            print(f"    {name}: 0x{va:x}, ~{size} bytes")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
