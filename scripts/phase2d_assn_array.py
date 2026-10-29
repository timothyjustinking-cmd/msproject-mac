#!/usr/bin/env python3
"""Phase 2d: Extract the Assignment field ID array and compare vtable layouts.

Discoveries so far:
- Dense array of packed u32 0x0f4xxxxx field IDs found near VA 0x140f63870 (.rdata)
- TAssnTable main vtable: 200 entries at 0x141038b08
- TTaskTable main vtable: 428 entries at 0x141048648
- TTaskAssnContainer main vtable: 200 entries at 0x14102c148

Goals:
1. Find exact bounds of the assignment engine field ID array
2. Compare TAssnTable vtable with TTaskTable to find equivalent vfunc_87/vfunc_158
3. Find LEA references to the assignment field array from .text
4. Search for a descriptor-like structure used by TAssnTable
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


def is_code_ptr(val, sections):
    rva = val - IMAGE_BASE
    for s in sections:
        if s['name'] == '.text' and s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
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
    print("Phase 2d: Assignment Field ID Array Extraction")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # =========================================================================
    # 1. Extract the assignment field ID array near VA 0x140f63870
    # =========================================================================
    print("\n1. EXTRACTING ASSIGNMENT FIELD ID ARRAY")
    print("-" * 50)

    # Scan around the area for consecutive 0x0f4xxxxx u32 values
    scan_va = 0x140f63800
    scan_off = va_to_file(scan_va, sections)

    # Find the start: walk backward from where we know there are 0x0f4 values
    known_off = va_to_file(0x140f63870, sections)

    # Walk backward to find start
    start_off = known_off
    while start_off >= scan_off:
        val = struct.unpack_from('<I', data, start_off - 4)[0]
        if (val >> 20) == 0x0f4 and val < 0x0f410000:
            start_off -= 4
        else:
            break

    # Walk forward to find end
    end_off = known_off
    while True:
        val = struct.unpack_from('<I', data, end_off)[0]
        if (val >> 20) == 0x0f4 and val < 0x0f410000:
            end_off += 4
        else:
            break

    start_va = file_to_va(start_off, sections)
    end_va = file_to_va(end_off, sections)
    count = (end_off - start_off) // 4

    print(f"  Array start: VA 0x{start_va:x}")
    print(f"  Array end:   VA 0x{end_va:x}")
    print(f"  Count: {count} u32 field IDs")

    # Extract all field IDs
    assn_ids = []
    for i in range(count):
        fid = struct.unpack_from('<I', data, start_off + i * 4)[0]
        assn_ids.append(fid)

    # Check if sorted
    is_sorted = all(assn_ids[i] <= assn_ids[i+1] for i in range(len(assn_ids)-1))
    print(f"  Sorted: {is_sorted}")
    print(f"  Range: 0x{min(assn_ids):08x} - 0x{max(assn_ids):08x}")

    # Check for header (count before the array)
    header_off = start_off - 4
    header_val = struct.unpack_from('<I', data, header_off)[0]
    header_va = file_to_va(header_off, sections)
    print(f"  Header at VA 0x{header_va:x}: 0x{header_val:08x} ({header_val})")
    # Also check 8 bytes before
    pre8 = struct.unpack_from('<Q', data, start_off - 8)[0]
    print(f"  8 bytes before: 0x{pre8:016x}")

    # Named fields
    named = sum(1 for fid in assn_ids if fid in field_map)
    print(f"  Named: {named}/{count}")

    # Show first 20
    print(f"\n  First 20 fields:")
    for i in range(min(20, count)):
        fid = assn_ids[i]
        name = field_map.get(fid, '')
        print(f"    [{i:3d}] 0x{fid:08x}  {name}")

    # Show last 10
    if count > 30:
        print(f"  Last 10 fields:")
        for i in range(count - 10, count):
            fid = assn_ids[i]
            name = field_map.get(fid, '')
            print(f"    [{i:3d}] 0x{fid:08x}  {name}")

    # =========================================================================
    # 2. Search ALL of .rdata for sorted 0x0f4 arrays (there may be multiple)
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. ALL ASSIGNMENT FIELD ID ARRAYS IN .RDATA")
    print("-" * 50)

    for s in sections:
        if s['name'] not in ('.rdata', '.data'):
            continue

        arrays_found = 0
        pos = 0
        while pos < s['raw_size'] - 16:
            foff = s['raw_ptr'] + pos
            v0 = struct.unpack_from('<I', data, foff)[0]
            v1 = struct.unpack_from('<I', data, foff + 4)[0]
            v2 = struct.unpack_from('<I', data, foff + 8)[0]
            v3 = struct.unpack_from('<I', data, foff + 12)[0]

            # Check for 4+ consecutive 0x0f4xxxxx values
            if all((v >> 20) == 0x0f4 and v < 0x0f410000 for v in [v0, v1, v2, v3]):
                # Found a run, extend it
                run_start = foff
                run_end = foff + 16
                while run_end + 4 <= s['raw_ptr'] + s['raw_size']:
                    nv = struct.unpack_from('<I', data, run_end)[0]
                    if (nv >> 20) == 0x0f4 and nv < 0x0f410000:
                        run_end += 4
                    else:
                        break
                run_count = (run_end - run_start) // 4
                if run_count >= 10:
                    run_va = IMAGE_BASE + s['virt_addr'] + (run_start - s['raw_ptr'])
                    # Check if sorted
                    vals = [struct.unpack_from('<I', data, run_start + i*4)[0] for i in range(run_count)]
                    sorted_flag = all(vals[i] <= vals[i+1] for i in range(len(vals)-1))
                    arrays_found += 1
                    print(f"  {s['name']} VA 0x{run_va:x}: {run_count} IDs, sorted={sorted_flag}, "
                          f"range 0x{min(vals):08x}-0x{max(vals):08x}")
                pos = (run_end - s['raw_ptr'])
            else:
                pos += 4

    # =========================================================================
    # 3. Compare vtable slots between TAssnTable and TTaskTable
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. VTABLE COMPARISON: TAssnTable vs TTaskTable")
    print("-" * 50)

    # TAssnTable: 200 entries at 0x141038b08
    # TTaskTable: 428 entries at 0x141048648
    assn_vt_va = 0x141038b08
    task_vt_va = 0x141048648
    assn_vt_off = va_to_file(assn_vt_va, sections)
    task_vt_off = va_to_file(task_vt_va, sections)

    # Extract both vtables
    assn_vt = []
    for i in range(200):
        val = struct.unpack_from('<Q', data, assn_vt_off + i * 8)[0]
        assn_vt.append(val)

    task_vt = []
    for i in range(428):
        val = struct.unpack_from('<Q', data, task_vt_off + i * 8)[0]
        task_vt.append(val)

    # Count shared functions
    assn_set = set(assn_vt)
    task_set = set(task_vt)
    shared = assn_set & task_set
    print(f"  TAssnTable: {len(assn_vt)} entries")
    print(f"  TTaskTable: {len(task_vt)} entries")
    print(f"  Shared function addresses: {len(shared)}")

    # Key slots from TTaskTable:
    # vfunc[87] = high-level GetFieldValue
    # vfunc[158] = bottom-level field lookup (SHL RDX, 6)
    if len(task_vt) > 158:
        task_vf87 = task_vt[87]
        task_vf158 = task_vt[158]
        print(f"\n  TTaskTable key slots:")
        print(f"    vfunc[87]  = 0x{task_vf87:x} (GetFieldValue)")
        print(f"    vfunc[158] = 0x{task_vf158:x} (field lookup with SHL)")

        # Check if TAssnTable has the same functions
        for i, val in enumerate(assn_vt):
            if val == task_vf87:
                print(f"    -> TAssnTable also has vfunc[87] at slot [{i}]")
            if val == task_vf158:
                print(f"    -> TAssnTable also has vfunc[158] at slot [{i}]")

    # Find equivalent slots by looking for same function at same index
    print(f"\n  Matching slots (same index, same function):")
    matches = 0
    for i in range(min(len(assn_vt), len(task_vt))):
        if assn_vt[i] == task_vt[i]:
            matches += 1
            if matches <= 30:
                print(f"    [{i:3d}] 0x{assn_vt[i]:x}")
    if matches > 30:
        print(f"    ... ({matches} total matching slots)")

    # Show where they DIFFER
    print(f"\n  First 30 differing slots (indices present in both):")
    diffs = 0
    for i in range(min(len(assn_vt), len(task_vt))):
        if assn_vt[i] != task_vt[i]:
            diffs += 1
            if diffs <= 30:
                print(f"    [{i:3d}] Assn=0x{assn_vt[i]:x}  Task=0x{task_vt[i]:x}")
    print(f"  Total differing: {diffs}")

    # =========================================================================
    # 4. Find LEA references to assignment field ID arrays from .text
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. LEA REFERENCES TO ASSIGNMENT FIELD ID ARRAYS")
    print("-" * 50)

    target_vas = [
        (start_va, f"Engine array at 0x{start_va:x}"),
        (0x1410e6b70, "OLE DB array at 0x1410e6b70"),
    ]

    text_sec = None
    for s in sections:
        if s['name'] == '.text':
            text_sec = s
            break

    if text_sec:
        for target_va, desc in target_vas:
            found = []
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
                    found.append(instr_va)

            print(f"\n  {desc}: {len(found)} LEA references")
            for va in found[:10]:
                print(f"    VA 0x{va:x}")

    # =========================================================================
    # 5. Check what's at the TAssnTable vtable constructor function
    # =========================================================================
    print(f"\n{'='*70}")
    print("5. TAssnTable FIRST FEW VTABLE FUNCTIONS")
    print("-" * 50)

    # The largest TAssnTable vtable has 200 entries
    # Show the first 50 entries and look for patterns
    print(f"  Vtable at VA 0x{assn_vt_va:x}:")
    for i in range(min(50, len(assn_vt))):
        in_task = '(shared)' if assn_vt[i] in task_set else ''
        print(f"    [{i:3d}] 0x{assn_vt[i]:x} {in_task}")

    # =========================================================================
    # 6. Save assignment engine field ID array to TSV
    # =========================================================================
    print(f"\n{'='*70}")
    print("6. SAVING ASSIGNMENT ENGINE FIELD IDS")
    print("-" * 50)

    out_path = ANALYSIS / "assignment_engine_field_ids.tsv"
    with open(out_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['index', 'field_id_hex', 'field_id_dec', 'property_name'])
        for i, fid in enumerate(assn_ids):
            name = field_map.get(fid, '')
            w.writerow([i, f'0x{fid:08x}', fid, name])
    print(f"  Wrote {count} field IDs to {out_path}")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
