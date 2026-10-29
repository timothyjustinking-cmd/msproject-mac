#!/usr/bin/env python3
"""Phase 2d: Trace how Assignment fields are accessed without a descriptor.

Known: Assignment descriptor (marker 0x0f404000) is absent from the binary.
But 564 Assignment field IDs exist in the OLE DB layer at VA 0x1410e6b70.

Strategy:
1. Find RTTI strings containing "Assn" or "Assignment" in the binary
2. Find pointer tables/arrays that contain known descriptor VAs
   (Task=0x140f8bda0, Resource=0x140f8bf60) and check what's at index 2/3
3. Look for cross-references to the Assignment field ID array (0x1410e6b70)
4. Search for the pattern: load descriptor then route by type index
"""

import struct
from pathlib import Path
from collections import defaultdict

IMAGE_BASE = 0x140000000
WINPROJ = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
BASE = Path(__file__).resolve().parents[1]
ANALYSIS = BASE / "analysis"

KNOWN_DESCRIPTORS = {
    'Task':        0x140f8bda0,
    'Resource':    0x140f8bf60,
    'Links':       0x140f8c080,
    'Calendar':    0x140f863b0,
    'Project':     0x140f85ff0,
    'OutlineCode': 0x140f86350,
}


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


def is_pointer(val, sections):
    if val < IMAGE_BASE or val > IMAGE_BASE + 0x20000000:
        return False
    rva = val - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return True
    return False


def main():
    print("Phase 2d: Tracing Assignment Field Access Architecture")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)

    # =========================================================================
    # 1. Find RTTI strings mentioning "Assn" or "Assignment"
    # =========================================================================
    print("\n1. RTTI STRINGS CONTAINING 'Assn' or 'Assignment'")
    print("-" * 50)

    rtti_hits = []
    for pattern in [b'Assn', b'ASSN', b'Assignment', b'ASSIGNMENT', b'TAssnTable',
                    b'TBkndAssn', b'IBkndAssn', b'AssnTable', b'AssignmentTable']:
        pos = 0
        while True:
            idx = data.find(pattern, pos)
            if idx == -1:
                break
            va = file_to_va(idx, sections)
            sec = section_for_va(va, sections) if va else '?'
            # Extract surrounding string
            start = idx
            while start > 0 and data[start-1:start] not in (b'\x00', b'\n'):
                start -= 1
            end = idx + len(pattern)
            while end < len(data) and data[end:end+1] not in (b'\x00', b'\n'):
                end += 1
            raw = data[start:min(end, start+120)]
            context = ''.join(chr(b) if 32 <= b < 127 else '?' for b in raw)
            if va:
                rtti_hits.append((va, sec, context))
                print(f"  VA 0x{va:x} ({sec}): {context}")
            pos = idx + 1

    if not rtti_hits:
        print("  No RTTI strings found")

    # =========================================================================
    # 2. Find pointer tables containing known descriptor VAs
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. POINTER TABLES CONTAINING KNOWN DESCRIPTOR VAs")
    print("-" * 50)
    print("Looking for arrays of pointers where Task and Resource descriptors appear...")

    task_desc = KNOWN_DESCRIPTORS['Task']
    rsc_desc = KNOWN_DESCRIPTORS['Resource']
    task_bytes = struct.pack('<Q', task_desc)
    rsc_bytes = struct.pack('<Q', rsc_desc)

    # Find all occurrences of Task descriptor VA as a 64-bit pointer
    task_locs = []
    pos = 0
    while True:
        idx = data.find(task_bytes, pos)
        if idx == -1:
            break
        va = file_to_va(idx, sections)
        if va:
            sec = section_for_va(va, sections)
            task_locs.append((idx, va, sec))
        pos = idx + 1

    print(f"\n  Task descriptor VA (0x{task_desc:x}) found as pointer at {len(task_locs)} locations:")
    for foff, va, sec in task_locs:
        print(f"    VA 0x{va:x} ({sec})")

    # For each Task descriptor pointer, check if Resource descriptor is nearby
    print(f"\n  Checking for descriptor pointer arrays...")
    for foff, va, sec in task_locs:
        # Check +8, +16, -8, -16 for Resource descriptor
        for delta in [8, 16, -8, -16, 24, 32, -24, -32]:
            check_off = foff + delta
            if check_off < 0 or check_off + 8 > len(data):
                continue
            check_val = struct.unpack_from('<Q', data, check_off)[0]
            if check_val == rsc_desc:
                print(f"\n  ARRAY FOUND at base VA ~0x{va:x} ({sec}):")
                # Dump the array
                base_off = min(foff, check_off) - 32
                end_off = max(foff, check_off) + 40
                for p in range(base_off, end_off, 8):
                    pval = struct.unpack_from('<Q', data, p)[0]
                    pva = file_to_va(p, sections)
                    psec = section_for_va(pval, sections) if is_pointer(pval, sections) else ''
                    label = ''
                    for name, desc_va in KNOWN_DESCRIPTORS.items():
                        if pval == desc_va:
                            label = f' <-- {name} descriptor'
                    print(f"    VA 0x{pva:x}: 0x{pval:016x} {psec}{label}")

    # =========================================================================
    # 3. Cross-references to Assignment field ID array (VA 0x1410e6b70)
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. CROSS-REFERENCES TO ASSIGNMENT FIELD ID ARRAY (0x1410e6b70)")
    print("-" * 50)

    assn_array_va = 0x1410e6b70
    assn_bytes = struct.pack('<Q', assn_array_va)
    pos = 0
    xrefs = []
    while True:
        idx = data.find(assn_bytes, pos)
        if idx == -1:
            break
        va = file_to_va(idx, sections)
        if va:
            sec = section_for_va(va, sections)
            xrefs.append((idx, va, sec))
            print(f"  VA 0x{va:x} ({sec})")
            # Show context
            for di in range(-4, 8):
                coff = idx + di * 8
                if coff < 0 or coff + 8 > len(data):
                    continue
                cv = struct.unpack_from('<Q', data, coff)[0]
                cva = file_to_va(coff, sections)
                csec = section_for_va(cv, sections) if is_pointer(cv, sections) else ''
                marker = ' <-- TARGET' if di == 0 else ''
                for name, desc_va in KNOWN_DESCRIPTORS.items():
                    if cv == desc_va:
                        marker += f' ({name} desc!)'
                print(f"    0x{cva:x}: 0x{cv:016x} {csec}{marker}")
        pos = idx + 1

    # =========================================================================
    # 4. Search for type dispatch tables (arrays of 3+ descriptor pointers)
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. SEARCHING FOR TYPE DISPATCH TABLES")
    print("-" * 50)
    print("Looking for any array in .rdata/.data with 3+ known descriptor pointers...")

    desc_set = set(KNOWN_DESCRIPTORS.values())

    for s in sections:
        if s['name'] not in ('.rdata', '.data'):
            continue
        for pos in range(0, s['raw_size'] - 8 * 6, 8):
            # Check 8 consecutive qwords
            foff = s['raw_ptr'] + pos
            vals = [struct.unpack_from('<Q', data, foff + i*8)[0] for i in range(8)]
            matches = sum(1 for v in vals if v in desc_set)
            if matches >= 3:
                va = IMAGE_BASE + s['virt_addr'] + pos
                print(f"\n  {matches} known descriptors found in array at VA 0x{va:x} ({s['name']}):")
                for i, v in enumerate(vals):
                    label = ''
                    for name, dv in KNOWN_DESCRIPTORS.items():
                        if v == dv:
                            label = f' <-- {name}'
                    sec2 = section_for_va(v, sections) if is_pointer(v, sections) else ''
                    print(f"    [{i}] 0x{v:016x} {sec2}{label}")
                # Also check before and after
                print("    --- extended context ---")
                for i in range(-4, 16):
                    eoff = foff + i*8
                    if eoff < s['raw_ptr'] or eoff + 8 > s['raw_ptr'] + s['raw_size']:
                        continue
                    ev = struct.unpack_from('<Q', data, eoff)[0]
                    eva = IMAGE_BASE + s['virt_addr'] + pos + i*8
                    label = ''
                    for name, dv in KNOWN_DESCRIPTORS.items():
                        if ev == dv:
                            label = f' <-- {name}'
                    sec2 = section_for_va(ev, sections) if is_pointer(ev, sections) else ''
                    print(f"    [{i:+d}] VA 0x{eva:x}: 0x{ev:016x} {sec2}{label}")

    # =========================================================================
    # 5. Look for the Links descriptor at offset patterns
    # =========================================================================
    print(f"\n{'='*70}")
    print("5. LINKS DESCRIPTOR (0x140f8c080) CROSS-REFERENCES")
    print("-" * 50)

    links_desc = KNOWN_DESCRIPTORS['Links']
    links_bytes = struct.pack('<Q', links_desc)
    pos = 0
    while True:
        idx = data.find(links_bytes, pos)
        if idx == -1:
            break
        va = file_to_va(idx, sections)
        if va:
            sec = section_for_va(va, sections)
            print(f"  VA 0x{va:x} ({sec})")
        pos = idx + 1

    # =========================================================================
    # 6. Check if Assignment uses IBkndAssnDispatch differently
    # =========================================================================
    print(f"\n{'='*70}")
    print("6. LOOKING FOR IBkndAssnDispatch / TBkndAssnElem RTTI")
    print("-" * 50)

    for pattern in [b'.?AVTBkndAssn', b'.?AVIBkndAssn', b'.?AVTAssn',
                    b'TBkndAssnElem', b'IBkndAssnDispatch',
                    b'.?AVTAssnTable', b'AssnTable',
                    b'BkndAssn', b'bkndassn']:
        pos = 0
        found = False
        while True:
            idx = data.find(pattern, pos)
            if idx == -1:
                break
            va = file_to_va(idx, sections)
            sec = section_for_va(va, sections) if va else '?'
            start = idx
            while start > 0 and data[start-1:start] not in (b'\x00',):
                start -= 1
            end = idx + len(pattern)
            while end < len(data) and data[end:end+1] != b'\x00':
                end += 1
            context = data[start:min(end, start+120)].decode('ascii', errors='replace')
            if va:
                print(f"  VA 0x{va:x} ({sec}): {context}")
                found = True
            pos = idx + 1
        if not found:
            print(f"  '{pattern.decode()}' -- not found")

    # =========================================================================
    # 7. Look at the OLE DB schema structure to find Assignment references
    # =========================================================================
    print(f"\n{'='*70}")
    print("7. OLE DB SCHEMA STRUCTURE (VA 0x1414c2a68) - ASSIGNMENT POINTERS")
    print("-" * 50)

    schema_va = 0x1414c2a68
    schema_off = va_to_file(schema_va, sections)
    if schema_off:
        # Dump 128 qwords from the schema structure
        for i in range(128):
            off = schema_off + i * 8
            if off + 8 > len(data):
                break
            v = struct.unpack_from('<Q', data, off)[0]
            va = schema_va + i * 8
            sec = section_for_va(v, sections) if is_pointer(v, sections) else ''
            label = ''
            # Check if it's a known descriptor
            for name, dv in KNOWN_DESCRIPTORS.items():
                if v == dv:
                    label = f' <-- {name} desc'
            # Check if it's the assn field array
            if v == assn_array_va:
                label = ' <-- ASSIGNMENT field ID array!'
            # Check for field ID array pointers
            known_arrays = {
                0x1410e77a0: 'Task field IDs',
                0x1410e8f50: 'Resource field IDs',
                0x1410e6b70: 'Assignment field IDs',
            }
            if v in known_arrays:
                label = f' <-- {known_arrays[v]}'
            if label or (i < 64):
                print(f"  [{i:3d}] VA 0x{va:x}: 0x{v:016x} {sec}{label}")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
