#!/usr/bin/env python3
"""Phase 2d: Analyze sorted assignment field ID arrays in .data and find cache references.

Key discovery: TAssnTable shares vfunc[87] and vfunc[158] with TTaskTable.
Since vfunc[158] at 0x140239380 does the SHL RDX, 6 cache lookup, the descriptor
and cache must be passed as parameters (likely from `this` pointer).

Sorted .data arrays found:
- 0x1412aa6f0: 156 sorted IDs (0x0f400001-0x0f400308)
- 0x1412ac58c: 191 sorted IDs (0x0f400001-0x0f400308)

For reference, Task sorted array at 0x1412a9060 has 1444 entries.
All three are in the .data section near each other.

Goals:
1. Extract and compare the sorted arrays
2. Look at what's near them (descriptor-like headers)
3. Find LEA references to these arrays
4. Check if the IBkndAssnDispatch functions reference a descriptor
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


def find_lea_refs(data, sections, target_va):
    """Find LEA reg, [rip+disp] instructions that load target_va."""
    refs = []
    text_sec = None
    for s in sections:
        if s['name'] == '.text':
            text_sec = s
            break
    if not text_sec:
        return refs

    for pos in range(text_sec['raw_ptr'], text_sec['raw_ptr'] + text_sec['raw_size'] - 7):
        b0 = data[pos]
        b1 = data[pos+1]
        b2 = data[pos+2]
        if b0 not in (0x48, 0x4C):
            continue
        if b1 != 0x8D:
            continue
        modrm = b2
        if (modrm & 0xC7) != 0x05:
            continue
        disp = struct.unpack_from('<i', data, pos + 3)[0]
        instr_va = file_to_va(pos, sections)
        if instr_va is None:
            continue
        result_va = instr_va + 7 + disp
        if result_va == target_va:
            refs.append(instr_va)
    return refs


def main():
    print("Phase 2d: Sorted Assignment Array Analysis")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # =========================================================================
    # 1. Compare the area around the Task sorted array with Assignment arrays
    # =========================================================================
    print("\n1. CONTEXT AROUND TASK SORTED ARRAY (0x1412a9060)")
    print("-" * 50)

    task_arr_va = 0x1412a9060
    task_arr_off = va_to_file(task_arr_va, sections)

    # Show 32 bytes before
    print("  Before Task array:")
    for i in range(-8, 0):
        off = task_arr_off + i * 4
        v = struct.unpack_from('<I', data, off)[0]
        va = task_arr_va + i * 4
        print(f"    VA 0x{va:x}: 0x{v:08x} ({v})")

    # Show first 5 entries
    print("  First 5 Task array entries:")
    for i in range(5):
        v = struct.unpack_from('<I', data, task_arr_off + i * 4)[0]
        name = field_map.get(v, '')
        print(f"    [{i}] 0x{v:08x}  {name}")

    # =========================================================================
    # 2. Context around sorted Assignment arrays in .data
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. SORTED ASSIGNMENT ARRAYS IN .DATA")
    print("-" * 50)

    for arr_va, expected_count in [(0x1412aa6f0, 156), (0x1412ac58c, 191)]:
        arr_off = va_to_file(arr_va, sections)
        print(f"\n  Array at VA 0x{arr_va:x} ({expected_count} entries):")

        # Show 16 bytes before
        print("  Before array:")
        for i in range(-8, 0):
            off = arr_off + i * 4
            v = struct.unpack_from('<I', data, off)[0]
            va = arr_va + i * 4
            name = field_map.get(v, '') if (v >> 20) in (0x0f4, 0x0b4, 0x0c4) else ''
            print(f"    VA 0x{va:x}: 0x{v:08x} ({v}) {name}")

        # Show first 10
        print("  First 10 entries:")
        ids = []
        for i in range(expected_count):
            v = struct.unpack_from('<I', data, arr_off + i * 4)[0]
            ids.append(v)
            if i < 10:
                name = field_map.get(v, '')
                print(f"    [{i:3d}] 0x{v:08x}  {name}")

        # Check what's after
        print("  After array:")
        for i in range(5):
            off = arr_off + expected_count * 4 + i * 4
            v = struct.unpack_from('<I', data, off)[0]
            va = arr_va + expected_count * 4 + i * 4
            print(f"    VA 0x{va:x}: 0x{v:08x} ({v})")

        # Find LEA references
        refs = find_lea_refs(data, sections, arr_va)
        print(f"  LEA references from .text: {len(refs)}")
        for r in refs:
            print(f"    VA 0x{r:x}")

    # =========================================================================
    # 3. Look at the area between Task and Assignment sorted arrays
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. SCANNING .DATA FOR ALL SORTED 0x0f4 ARRAYS NEAR TASK ARRAY")
    print("-" * 50)

    # Task array at 0x1412a9060, scan from there to 0x1412b0000
    scan_start = 0x1412a9060
    scan_end = 0x1412b0000

    start_off = va_to_file(scan_start, sections)
    end_off = va_to_file(scan_end, sections)

    if start_off and end_off:
        pos = start_off
        while pos < end_off - 16:
            # Look for runs of sorted 0x0f4 values
            vals = []
            check_pos = pos
            while check_pos < end_off - 4:
                v = struct.unpack_from('<I', data, check_pos)[0]
                if (v >> 20) == 0x0f4 and v < 0x0f410000:
                    if not vals or v >= vals[-1]:
                        vals.append(v)
                        check_pos += 4
                    else:
                        break
                else:
                    break

            if len(vals) >= 10:
                va = file_to_va(pos, sections)
                print(f"  VA 0x{va:x}: {len(vals)} sorted 0x0f4 IDs, "
                      f"range 0x{min(vals):08x}-0x{max(vals):08x}")
                pos = check_pos
            else:
                pos += 4

    # =========================================================================
    # 4. Look for Resource sorted arrays too (for comparison)
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. ALL SORTED ARRAYS IN .DATA SECTION (NEAR 0x1412a0000-0x1412b0000)")
    print("-" * 50)

    scan_start = 0x1412a0000
    scan_end = 0x1412d0000
    start_off = va_to_file(scan_start, sections)
    end_off = va_to_file(scan_end, sections)

    if start_off and end_off:
        pos = start_off
        while pos < end_off - 16:
            vals = []
            check_pos = pos
            while check_pos < end_off - 4:
                v = struct.unpack_from('<I', data, check_pos)[0]
                prefix = v >> 20
                if prefix in (0x0b4, 0x0c4, 0x0f4, 0x0e4, 0x3c4, 0x074, 0x3e4) and v < (prefix << 20) + 0x10000:
                    if not vals or v >= vals[-1]:
                        vals.append(v)
                        check_pos += 4
                    else:
                        break
                else:
                    break

            if len(vals) >= 10:
                va = file_to_va(pos, sections)
                prefix = vals[0] >> 20
                type_names = {0x0b4: 'Task', 0x0c4: 'Resource', 0x0f4: 'Assignment',
                              0x0e4: 'Links', 0x3c4: 'Calendar', 0x074: 'Project', 0x3e4: 'OutlineCode'}
                tname = type_names.get(prefix, '?')
                # Check header (4 bytes before)
                hdr_off = pos - 4
                if hdr_off >= start_off:
                    hdr = struct.unpack_from('<I', data, hdr_off)[0]
                else:
                    hdr = 0
                print(f"  VA 0x{va:x}: {len(vals)} sorted {tname} IDs "
                      f"(0x{min(vals):08x}-0x{max(vals):08x}), hdr=0x{hdr:08x}({hdr})")
                pos = check_pos
            else:
                pos += 4

    # =========================================================================
    # 5. Check the area around TAssnTable descriptor search
    # =========================================================================
    print(f"\n{'='*70}")
    print("5. LOOKING FOR DESCRIPTOR REFERENCES IN TAssnTable CONSTRUCTOR")
    print("-" * 50)

    # TAssnTable vtable[3] is the first differing function (0x1403fc120 vs Task 0x1403fbfe0)
    # This is likely the constructor or a type-specific method
    assn_ctor = 0x1403fc120
    task_ctor = 0x1403fbfe0

    # Check what each of these functions references
    for func_va, name in [(assn_ctor, 'TAssnTable_vf3'), (task_ctor, 'TTaskTable_vf3')]:
        func_off = va_to_file(func_va, sections)
        if func_off is None:
            continue
        print(f"\n  {name} at VA 0x{func_va:x}:")

        # Scan the first 256 bytes of the function for LEA rip+disp
        for pos in range(func_off, min(func_off + 512, len(data) - 7)):
            b0 = data[pos]
            b1 = data[pos + 1]
            b2 = data[pos + 2]
            if b0 in (0x48, 0x4C) and b1 == 0x8D:
                modrm = b2
                if (modrm & 0xC7) == 0x05:
                    disp = struct.unpack_from('<i', data, pos + 3)[0]
                    instr_va = file_to_va(pos, sections)
                    target = instr_va + 7 + disp
                    sec = section_for_va(target, sections) if is_valid_ptr(target, sections) else ''
                    if sec:
                        print(f"    0x{instr_va:x}: LEA -> 0x{target:x} ({sec})")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
