#!/usr/bin/env python3
"""Phase 2d: Find the initialization data for the runtime field cache.

The cache is populated at runtime, but the init code needs source data.
This script:
1. Searches for cross-references to the cache base address in .text
2. Searches for arrays dimensioned to 1444 (u16/u32) in .rdata
3. Examines the descriptor more carefully for hidden pointers
4. Looks at the 0x78 value (120) from the descriptor as a possible record size
"""

import struct
import csv
from pathlib import Path
from collections import Counter

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


def section_for_va(va, sections):
    rva = va - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return s['name']
    return '?'


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
    print("Phase 2d: Finding Init Data for Runtime Cache")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # ===== 1. Search for references to cache base address in .text =====
    print("1. SEARCHING .text FOR REFERENCES TO CACHE AT 0x14130be10")
    print("-" * 50)

    cache_va = 0x14130be10
    # In x64 code, the address might appear as:
    # a) LEA reg, [rip + disp32] — the most common way
    # b) MOV reg, imm64 (rare)
    # c) Referenced via a .rdata pointer

    # Search for the full 8-byte address in .data and .rdata (pointer tables)
    target_bytes = struct.pack('<Q', cache_va)
    for s in sections:
        sec_data = data[s['raw_ptr']:s['raw_ptr'] + s['raw_size']]
        pos = 0
        count = 0
        while True:
            pos = sec_data.find(target_bytes, pos)
            if pos == -1:
                break
            if count < 5:
                ptr_va = IMAGE_BASE + s['virt_addr'] + pos
                print(f"  [{s['name']}] VA 0x{ptr_va:x}: ptr to cache")
            count += 1
            pos += 1
        if count > 0:
            print(f"  [{s['name']}] Total: {count} pointers to cache")

    # Also search for the RVA (used in LEA instructions)
    # LEA reg, [rip + disp32]: the displacement = target_rva - (instr_rva + instr_len)
    # We can't easily compute this without disassembly, so search for the
    # lower 32 bits of the VA instead
    cache_low32 = cache_va & 0xFFFFFFFF  # 0x4130be10
    cache_bytes4 = struct.pack('<I', cache_low32)
    text = next(s for s in sections if s['name'] == '.text')
    text_data = data[text['raw_ptr']:text['raw_ptr'] + text['raw_size']]
    refs = []
    pos = 0
    while True:
        pos = text_data.find(cache_bytes4, pos)
        if pos == -1:
            break
        # This could be a MOV reg, imm32 part of a MOV reg, imm64
        refs.append(IMAGE_BASE + text['virt_addr'] + pos)
        pos += 1
    print(f"\n  .text references to 0x{cache_low32:08x}: {len(refs)}")
    for va in refs[:10]:
        # Show surrounding bytes for context
        off = va_to_file(va, sections)
        ctx = data[off-8:off+12]
        print(f"    VA 0x{va:x}: ...{ctx[:8].hex()} [{ctx[8:12].hex()}] {ctx[12:].hex()}")

    # ===== 2. Analyze the descriptor's sub-range values =====
    print(f"\n{'='*70}")
    print("2. DESCRIPTOR SUB-RANGE ANALYSIS")
    print("-" * 50)

    desc_va = 0x140f8bda0
    desc_off = va_to_file(desc_va, sections)

    # The descriptor at +0x10 has: 0x041303fd05a405a4
    # which is u16: 0x05a4(1444), 0x05a4(1444), 0x03fd(1021), 0x0413(1043)
    # Then +0x18: 0x043a046e042d0416
    # which is u16: 0x0416(1046), 0x042d(1069), 0x046e(1134), 0x043a(1082)
    # Then +0x20: 0x0b4040000484043c
    # which is u16: 0x043c(1084), 0x0484(1156), 0x4000, 0x0b40 (= field_id 0x0b404000)

    sub_ranges = [1444, 1444, 1021, 1043, 1046, 1069, 1134, 1082, 1084, 1156]
    print(f"Sub-range values from descriptor: {sub_ranges}")
    print(f"These likely define field index boundaries for different categories:")
    sorted_sr = sorted(set(sub_ranges))
    print(f"  Sorted unique: {sorted_sr}")
    print(f"  Differences: {[sorted_sr[i+1]-sorted_sr[i] for i in range(len(sorted_sr)-1)]}")

    # ===== 3. Look at what's at Table 1 wrapper more carefully =====
    print(f"\n{'='*70}")
    print("3. TABLE 1 WRAPPER STRUCTURE (complete)")
    print("-" * 50)

    # We found: Table 1 at 0x140fb7e80
    # Before it: ...resource_fids..., 0x0c4000a5, 0x00000000, 0x3ef, ptr_to_table1
    # After it (at 0x140fb9510): 0x9, ptr, [calendar fids], 0x9, ptr, [project fids], ...

    # Let me map the COMPLETE sequence of (count, ptr, fids) records
    # starting from just before Table 1

    # First find where the Resource field IDs start before Table 1
    # Resource IDs end with 0x0c4000a5 at VA 0x140fb7e68
    # Let's search backwards for the start of Resource IDs
    t1_va = 0x140fb7e80
    t1_off = va_to_file(t1_va, sections)

    # Read backward to find Resource field IDs
    print("Scanning backward from Table 1 for record type boundaries...")
    # The pattern before Table 1 is: [rsc_fids...] [0x0c4000a5] [0x00000000] [0x3ef] [ptr]
    # Let me read from further back
    pre_off = t1_off - 0x2000  # 8KB before Table 1
    pre_data = data[pre_off:t1_off]

    # Find where Resource IDs start (look for first 0x0c4xxxxx after a non-Resource value)
    for i in range(len(pre_data) - 4, 0, -4):
        val = struct.unpack_from('<I', pre_data, i)[0]
        if not (0x0c400000 <= val <= 0x0c4fffff) and val != 0:
            # Found the end of non-Resource data
            rsc_start = i + 4
            rsc_count = (len(pre_data) - rsc_start) // 4
            rsc_start_va = t1_va - (len(pre_data) - rsc_start)
            # Check what's just before
            if rsc_start >= 16:
                pre_count = struct.unpack_from('<Q', pre_data, rsc_start - 16)[0]
                pre_ptr = struct.unpack_from('<Q', pre_data, rsc_start - 8)[0]
                print(f"  Resource IDs: {rsc_count} entries starting at VA 0x{rsc_start_va:x}")
                print(f"  Before them: count=0x{pre_count:x} ({pre_count}), ptr=0x{pre_ptr:x}")
                # The ptr should be a self-pointer to the start of the Resource IDs
                if pre_ptr == rsc_start_va:
                    print(f"  Self-pointer confirmed!")
            break

    # Now scan forward from after Table 1 to map all record type arrays
    print(f"\nScanning forward from Table 1 end for all record type arrays...")
    scan_off = t1_off + 1444 * 4  # Start after Table 1
    scan_va = t1_va + 1444 * 4    # VA 0x140fb9510

    arrays_found = []
    while True:
        if scan_off + 16 > len(data):
            break

        count_val = struct.unpack_from('<Q', data, scan_off)[0]
        ptr_val = struct.unpack_from('<Q', data, scan_off + 8)[0]

        # Check if this looks like a (count, self-ptr) header
        if 0 < count_val < 10000 and ptr_val == IMAGE_BASE + (scan_off + 16 - (data_sec_raw := next(s for s in sections if s['name'] == '.rdata')['raw_ptr']) + next(s for s in sections if s['name'] == '.rdata')['virt_addr']):
            # Self-pointer confirmed!
            # Read the field IDs
            first_fid = struct.unpack_from('<I', data, scan_off + 16)[0]
            fid_prefix = (first_fid >> 24) & 0xFF
            arrays_found.append({
                'va': scan_va,
                'count': count_val,
                'prefix': fid_prefix,
                'first_fid': first_fid,
            })
            print(f"  VA 0x{scan_va:x}: count={count_val}, prefix=0x{fid_prefix:02x}, "
                  f"first_fid=0x{first_fid:08x}")

            # Skip past this array
            scan_off += 16 + count_val * 4
            scan_va += 16 + count_val * 4

            # Align to 8 bytes
            if scan_off % 8:
                scan_off += 8 - (scan_off % 8)
                scan_va += 8 - (scan_va % 8)
        else:
            # Not a (count, self-ptr) header, check if it's something else
            # Maybe there's a gap or a different structure
            scan_off += 8
            scan_va += 8

            # Safety: don't scan too far
            if scan_va > t1_va + 0x100000:
                break

    # ===== 4. The 0x78 (120) value from the descriptor =====
    print(f"\n{'='*70}")
    print("4. RECORD SIZE ANALYSIS (descriptor +0x30 = 0x78 = 120)")
    print("-" * 50)
    print("The descriptor has value 0x78 (120) at offset +0x34.")
    print("If this is the number of direct-offset fields, then the remaining")
    print(f"fields ({1444 - 120} = 1324) are computed/virtual.")
    print("Or it could be 42 (0x2a) at +0x38 meaning 42 bytes of record header.")

    # ===== 5. Examine vtable at 0x141050670 to understand field types =====
    print(f"\n{'='*70}")
    print("5. VTABLE ANALYSIS FROM CACHE ENTRY 0")
    print("-" * 50)

    vtable_va = 0x141050670
    vtable_off = va_to_file(vtable_va, sections)

    # Read vtable entries (function pointers)
    vfuncs = []
    for i in range(20):
        fptr = struct.unpack_from('<Q', data, vtable_off + i*8)[0]
        if not (0x140000000 <= fptr <= 0x145000000):
            break
        vfuncs.append(fptr)

    print(f"Vtable at 0x{vtable_va:x}: {len(vfuncs)} function pointers")
    for i, fp in enumerate(vfuncs):
        print(f"  vfunc[{i}]: 0x{fp:x} ({section_for_va(fp, sections)})")

    # Search for OTHER vtables with the same pattern (same or similar functions)
    # This tells us how many different field types exist
    print(f"\nSearching for vtables sharing vfunc[0] = 0x{vfuncs[0]:x}...")
    vf0_bytes = struct.pack('<Q', vfuncs[0])
    rdata = next(s for s in sections if s['name'] == '.rdata')
    rdata_data = data[rdata['raw_ptr']:rdata['raw_ptr'] + rdata['raw_size']]
    pos = 0
    similar_vtables = []
    while True:
        pos = rdata_data.find(vf0_bytes, pos)
        if pos == -1:
            break
        vt_va = IMAGE_BASE + rdata['virt_addr'] + pos
        # Read the full vtable
        vt_off = rdata['raw_ptr'] + pos
        vt_funcs = []
        for i in range(len(vfuncs)):
            fp = struct.unpack_from('<Q', data, vt_off + i*8)[0]
            vt_funcs.append(fp)
        similar_vtables.append((vt_va, vt_funcs))
        pos += 8

    print(f"Found {len(similar_vtables)} vtables starting with 0x{vfuncs[0]:x}")
    # Group by unique vtable signature
    vtable_sigs = {}
    for vt_va, vt_funcs in similar_vtables:
        sig = tuple(vt_funcs)
        if sig not in vtable_sigs:
            vtable_sigs[sig] = []
        vtable_sigs[sig].append(vt_va)

    print(f"Unique vtable signatures: {len(vtable_sigs)}")
    for sig, vas in vtable_sigs.items():
        print(f"  VA 0x{vas[0]:x} ({len(vas)} copies):")
        # Show unique functions in this vtable
        unique_fns = sorted(set(sig))
        print(f"    Unique functions: {[f'0x{f:x}' for f in unique_fns]}")

    # ===== 6. Search for table of (field_id, offset) pairs =====
    print(f"\n{'='*70}")
    print("6. SEARCHING FOR (FIELD_ID, OFFSET) PAIR TABLES")
    print("-" * 50)

    # In many MS Office apps, field metadata is stored as:
    # struct { u32 field_id; u16 offset; u16 type; } entries[N]
    # Total 8 bytes per entry, 1444 * 8 = 11552 bytes

    # Search .rdata for sequences where:
    # - Every 8 bytes, the first 4 bytes are a Task field ID (0x0b4xxxxx)
    # - The next 4 bytes contain a u16 offset < 0x2000 and a u16 type < 256

    rdata_sec = next(s for s in sections if s['name'] == '.rdata')
    rdata_start = rdata_sec['raw_ptr']
    rdata_end = rdata_start + rdata_sec['raw_size']

    # Try stride 8: (u32 field_id, u16 offset, u16 type)
    print("\nSearching for stride-8 (field_id, offset, type) sequences...")
    best_stride8 = []
    for start in range(rdata_start, rdata_end - 32, 4):
        fid = struct.unpack_from('<I', data, start)[0]
        if 0x0b400000 <= fid <= 0x0b4005a3:
            # Found a Task field ID, check if the next values form a pattern
            count = 0
            for j in range(200):
                check_fid = struct.unpack_from('<I', data, start + j*8)[0]
                if 0x0b400000 <= check_fid <= 0x0b4005a3:
                    count += 1
                else:
                    break
            if count >= 20:
                va = IMAGE_BASE + rdata_sec['virt_addr'] + (start - rdata_start)
                best_stride8.append((va, count, start))

    # Deduplicate overlapping runs
    if best_stride8:
        best_stride8.sort(key=lambda x: -x[1])
        print(f"Found {len(best_stride8)} runs (showing top 5):")
        seen = set()
        shown = 0
        for va, count, foff in best_stride8:
            if any(abs(va - s) < 100 for s in seen):
                continue
            seen.add(va)
            shown += 1
            if shown > 5:
                break
            # Show first few entries
            print(f"\n  VA 0x{va:x}: {count} entries at stride 8")
            for j in range(min(10, count)):
                fid = struct.unpack_from('<I', data, foff + j*8)[0]
                extra = struct.unpack_from('<I', data, foff + j*8 + 4)[0]
                u16a = extra & 0xFFFF
                u16b = (extra >> 16) & 0xFFFF
                name = field_map.get(fid, '')
                print(f"    [{j:3d}] fid=0x{fid:08x} ({name:30s}) u16({u16a:#06x}, {u16b:#06x})")

    # Try stride 12: (u32 field_id, u32 offset, u32 type/flags)
    print("\nSearching for stride-12 (field_id, offset, type) sequences...")
    best_stride12 = []
    for start in range(rdata_start, rdata_end - 48, 4):
        fid = struct.unpack_from('<I', data, start)[0]
        if 0x0b400000 <= fid <= 0x0b4005a3:
            count = 0
            for j in range(200):
                check_fid = struct.unpack_from('<I', data, start + j*12)[0]
                if 0x0b400000 <= check_fid <= 0x0b4005a3:
                    count += 1
                else:
                    break
            if count >= 20:
                va = IMAGE_BASE + rdata_sec['virt_addr'] + (start - rdata_start)
                best_stride12.append((va, count, start))

    if best_stride12:
        best_stride12.sort(key=lambda x: -x[1])
        print(f"Found {len(best_stride12)} runs (showing top 5):")
        seen = set()
        shown = 0
        for va, count, foff in best_stride12:
            if any(abs(va - s) < 100 for s in seen):
                continue
            seen.add(va)
            shown += 1
            if shown > 5:
                break
            print(f"\n  VA 0x{va:x}: {count} entries at stride 12")
            for j in range(min(10, count)):
                fid = struct.unpack_from('<I', data, foff + j*12)[0]
                off_val = struct.unpack_from('<I', data, foff + j*12 + 4)[0]
                type_val = struct.unpack_from('<I', data, foff + j*12 + 8)[0]
                name = field_map.get(fid, '')
                print(f"    [{j:3d}] fid=0x{fid:08x} ({name:30s}) off=0x{off_val:08x} type=0x{type_val:08x}")

    print("\nDone.")


if __name__ == '__main__':
    main()
