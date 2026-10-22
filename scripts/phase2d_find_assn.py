#!/usr/bin/env python3
"""Find the missing Assignment descriptor.

Known descriptors in .rdata:
  Task:        0x140f8bda0 (marker 0x0b404000)
  Resource:    0x140f8bf60 (marker 0x0c404000)
  Calendar:    0x140f863b0 (marker 0x3c404000)
  Project:     0x140f85ff0 (marker 0x07404000)
  OutlineCode: 0x140f86350 (marker 0x3e404000)

Assignment should have marker 0x0f404000 but wasn't found.
Search strategies:
1. Look near known descriptors
2. Search all sections for 0x0f404000 at any alignment
3. Look for 0x0f4 field_id prefix patterns near descriptor-like structures
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


def section_for_va(va, sections):
    rva = va - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return s['name']
    return '?'


def is_pointer(val, sections):
    if val < IMAGE_BASE or val > IMAGE_BASE + 0x20000000:
        return False
    rva = val - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return True
    return False


def main():
    print("Phase 2d: Finding Assignment Descriptor")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)

    # 1. Search ALL sections for 0x0f404000
    print("\n1. SEARCHING ALL SECTIONS FOR 0x0f404000")
    print("-" * 50)
    target = 0x0f404000
    for s in sections:
        count = 0
        for pos in range(0, s['raw_size'] - 4, 4):
            val = struct.unpack_from('<I', data, s['raw_ptr'] + pos)[0]
            if val == target:
                va = IMAGE_BASE + s['virt_addr'] + pos
                count += 1
                print(f"  Found at VA 0x{va:x} ({s['name']})")
                # Show context
                for di in range(-4, 8):
                    coff = s['raw_ptr'] + pos + di*4
                    if coff < s['raw_ptr'] or coff + 4 > s['raw_ptr'] + s['raw_size']:
                        continue
                    cv = struct.unpack_from('<I', data, coff)[0]
                    cva = va + di*4
                    marker = ' <-- TARGET' if di == 0 else ''
                    print(f"    VA 0x{cva:x}: 0x{cv:08x} ({cv}){marker}")
        if count == 0:
            pass  # print(f"  Not found in {s['name']}")

    # 2. Search for 0x0f40 as u16 in .rdata near other descriptors (0x140f8b000 - 0x140f9c000)
    print(f"\n{'='*70}")
    print("2. SEARCHING .RDATA NEAR OTHER DESCRIPTORS FOR 0x0f4 PATTERN")
    print("-" * 50)
    scan_start_va = 0x140f8b000
    scan_end_va = 0x140f9c000
    scan_start_off = va_to_file(scan_start_va, sections)
    scan_end_off = va_to_file(scan_end_va, sections)

    if scan_start_off and scan_end_off:
        for pos in range(scan_start_off, scan_end_off - 4, 4):
            val = struct.unpack_from('<I', data, pos)[0]
            # Look for any 0x0f4xxxxx value that could be a field_id or marker
            if (val >> 20) == 0x0f4:
                va = scan_start_va + (pos - scan_start_off)
                print(f"  VA 0x{va:x}: 0x{val:08x}")

    # 3. Look at the region between Task (0x140f8bda0) and Resource (0x140f8bf60)
    print(f"\n{'='*70}")
    print("3. REGION BETWEEN TASK AND RESOURCE DESCRIPTORS")
    print("-" * 50)
    task_va = 0x140f8bda0
    rsc_va = 0x140f8bf60
    gap = rsc_va - task_va
    print(f"  Gap: {gap} bytes (0x{gap:x})")

    task_off = va_to_file(task_va, sections)
    for i in range(gap // 8 + 8):
        off = task_off + i * 8
        v = struct.unpack_from('<Q', data, off)[0]
        va = task_va + i * 8
        sec = section_for_va(v, sections) if is_pointer(v, sections) else ''
        v32a = struct.unpack_from('<I', data, off)[0]
        v32b = struct.unpack_from('<I', data, off + 4)[0]

        marker = ''
        if va == task_va:
            marker = ' <-- TASK DESC START'
        elif va == rsc_va:
            marker = ' <-- RESOURCE DESC START'
        elif va == task_va + 0x24:
            marker = ' <-- Task marker field'
        elif va == rsc_va + 0x24:
            marker = ' <-- Resource marker field'

        print(f"  0x{va:x}: 0x{v:016x}  (u32: 0x{v32a:08x}, 0x{v32b:08x}) {sec}{marker}")

    # 4. Look at what's AFTER the Resource descriptor (+0x80)
    print(f"\n{'='*70}")
    print("4. AFTER RESOURCE DESCRIPTOR (0x140f8bf60 + 0x80)")
    print("-" * 50)
    rsc_off = va_to_file(rsc_va, sections)
    for i in range(32):
        off = rsc_off + 0x80 + i * 8
        v = struct.unpack_from('<Q', data, off)[0]
        va = rsc_va + 0x80 + i * 8
        sec = section_for_va(v, sections) if is_pointer(v, sections) else ''
        v32a = struct.unpack_from('<I', data, off)[0]
        v32b = struct.unpack_from('<I', data, off + 4)[0]

        marker = ''
        if (v32a >> 20) == 0x0f4 or (v32b >> 20) == 0x0f4:
            marker = ' <-- 0x0f4 prefix!'

        print(f"  0x{va:x}: 0x{v:016x}  (u32: 0x{v32a:08x}, 0x{v32b:08x}) {sec}{marker}")

    # 5. Check if Resource desc at +0x70 continues into Assignment desc
    # Task desc is 0x70 bytes (0x140f8bda0 to 0x140f8be10), then secondary chain
    # Resource desc would be from 0x140f8bf60 to 0x140f8bfd0 (0x70 bytes)
    # Then at 0x140f8bfd0+ could be the Assignment descriptor
    print(f"\n{'='*70}")
    print("5. POTENTIAL ASSIGNMENT DESCRIPTOR AT 0x140f8c0e0+")
    print("-" * 50)

    # Resource desc +0x58 points to 0x140f8bff8 (secondary desc for Resource)
    # Let's check if there's another descriptor after that
    rsc_sec_va = 0x140f8bff8
    rsc_sec_off = va_to_file(rsc_sec_va, sections)

    # Walk forward from Resource secondary descriptor
    for i in range(64):
        off = rsc_sec_off + i * 8
        v = struct.unpack_from('<Q', data, off)[0]
        va = rsc_sec_va + i * 8
        sec = section_for_va(v, sections) if is_pointer(v, sections) else ''
        v32a = struct.unpack_from('<I', data, off)[0]
        v32b = struct.unpack_from('<I', data, off + 4)[0]

        marker = ''
        if v32a == 0x0f404000 or v32b == 0x0f404000:
            marker = ' <-- ASSIGNMENT MARKER!'
        elif (v32a >> 20) == 0x0f4 or (v32b >> 20) == 0x0f4:
            marker = f' <-- 0x0f4 prefix!'
        elif is_pointer(v, sections) and section_for_va(v, sections) == '.data':
            # Could be a cache pointer - check if reasonable cache address
            if 0x1412c0000 <= v <= 0x14140000:
                marker = ' <-- possible cache ptr'

        if i < 30 or marker:
            print(f"  0x{va:x}: 0x{v:016x}  (u32: 0x{v32a:08x}, 0x{v32b:08x}) {sec}{marker}")

    # 6. Try marker variations
    print(f"\n{'='*70}")
    print("6. SEARCHING FOR ALTERNATIVE ASSIGNMENT MARKERS")
    print("-" * 50)
    alt_markers = [0x0f400000, 0x0f404000, 0x0f440000, 0x0f400400, 0x0f404400,
                   0x17404000, 0x17400000]  # 0x17 = Assignment entity type
    for marker in alt_markers:
        found = False
        for s in sections:
            if s['name'] not in ('.rdata', '.data'):
                continue
            for pos in range(0, s['raw_size'] - 4, 4):
                val = struct.unpack_from('<I', data, s['raw_ptr'] + pos)[0]
                if val == marker:
                    va = IMAGE_BASE + s['virt_addr'] + pos
                    found = True
                    print(f"  0x{marker:08x} found at VA 0x{va:x} ({s['name']})")
                    break
            if found:
                break
        if not found:
            print(f"  0x{marker:08x} NOT FOUND")

    print(f"\n{'='*70}")
    print("DONE")
    print("=" * 70)


if __name__ == '__main__':
    main()
