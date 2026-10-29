#!/usr/bin/env python3
"""Phase 2d: Deep static analysis — extract everything possible without a running process.

Targets:
1. String table at 0x140fad020 (referenced by field dispatcher)
2. String table at 0x140fa05f0 (referenced by string comparison function)
3. All 10 vtable signatures — map each to its function pointers
4. The 115 index array at 0x1410fb248
5. Category name identification from field ID ranges
6. Secondary descriptor chain at 0x140f8be10
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


def read_cstring(data, off, max_len=256):
    end = off
    while end < off + max_len and end < len(data) and data[end] != 0:
        end += 1
    return data[off:end].decode('ascii', errors='replace')


def read_wstring(data, off, max_chars=256):
    chars = []
    for i in range(max_chars):
        c = struct.unpack_from('<H', data, off + i*2)[0]
        if c == 0:
            break
        chars.append(chr(c))
    return ''.join(chars)


def is_pointer(val, sections):
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


def main():
    print("Phase 2d: Deep Static Analysis")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # ===== 1. String table at 0x140fad020 =====
    print("\n1. STRING TABLE at VA 0x140fad020 (dispatcher reference)")
    print("-" * 50)
    str_va = 0x140fad020
    str_off = va_to_file(str_va, sections)
    if str_off:
        # Check if it's a pointer table or direct strings
        first_qword = struct.unpack_from('<Q', data, str_off)[0]
        print(f"  First 8 bytes: 0x{first_qword:016x}")
        if is_pointer(first_qword, sections):
            print("  -> Pointer table (array of string pointers)")
            for i in range(64):
                ptr = struct.unpack_from('<Q', data, str_off + i*8)[0]
                if not is_pointer(ptr, sections):
                    print(f"  End of pointer table at index {i}")
                    break
                ptr_off = va_to_file(ptr, sections)
                if ptr_off:
                    s = read_cstring(data, ptr_off)
                    if s and all(32 <= ord(c) < 127 for c in s[:20]):
                        print(f"  [{i:3d}] 0x{ptr:x} -> \"{s[:80]}\"")
                    else:
                        # Try wide string
                        ws = read_wstring(data, ptr_off)
                        if ws and len(ws) > 2:
                            print(f"  [{i:3d}] 0x{ptr:x} -> L\"{ws[:80]}\"")
                        else:
                            print(f"  [{i:3d}] 0x{ptr:x} -> (binary: {data[ptr_off:ptr_off+16].hex()})")
        else:
            # Direct data — try ASCII
            s = read_cstring(data, str_off, 512)
            if s and len(s) > 2:
                print(f"  ASCII string: \"{s[:200]}\"")
            else:
                # Try wide string
                ws = read_wstring(data, str_off, 256)
                if ws and len(ws) > 2:
                    print(f"  Wide string: L\"{ws[:200]}\"")
                else:
                    print(f"  Raw hex: {data[str_off:str_off+64].hex(' ')}")

        # Scan nearby for strings
        print("\n  Nearby strings (scanning ±0x200):")
        for delta in range(-0x200, 0x200, 8):
            off = str_off + delta
            if off < 0 or off + 8 > len(data):
                continue
            val = struct.unpack_from('<Q', data, off)[0]
            if is_pointer(val, sections):
                p_off = va_to_file(val, sections)
                if p_off:
                    s = read_cstring(data, p_off, 64)
                    if s and len(s) > 3 and all(32 <= ord(c) < 127 for c in s[:20]):
                        va_here = str_va + delta
                        print(f"    VA 0x{va_here:x} [+{delta:+#x}]: -> \"{s[:60]}\"")

    # ===== 2. String table at 0x140fa05f0 =====
    print(f"\n{'='*70}")
    print("2. STRING TABLE at VA 0x140fa05f0 (comparison function reference)")
    print("-" * 50)
    str2_va = 0x140fa05f0
    str2_off = va_to_file(str2_va, sections)
    if str2_off:
        first_qword = struct.unpack_from('<Q', data, str2_off)[0]
        print(f"  First 8 bytes: 0x{first_qword:016x}")

        # Check for a table of (field_name_ptr, field_id) pairs
        # or a flat array of string pointers
        if is_pointer(first_qword, sections):
            print("  -> Pointer table")
            for i in range(32):
                ptr = struct.unpack_from('<Q', data, str2_off + i*8)[0]
                if not is_pointer(ptr, sections):
                    # Check if it's an integer field ID
                    val32 = struct.unpack_from('<I', data, str2_off + i*8)[0]
                    if 0x0b400000 <= val32 <= 0x0f4fffff:
                        print(f"  [{i:3d}] FIELD_ID: 0x{val32:08x} ({field_map.get(val32, '?')})")
                        continue
                    print(f"  End at index {i}: 0x{ptr:016x}")
                    break
                ptr_off = va_to_file(ptr, sections)
                if ptr_off:
                    s = read_cstring(data, ptr_off, 128)
                    if s and len(s) > 1:
                        print(f"  [{i:3d}] 0x{ptr:x} -> \"{s[:80]}\"")
                    else:
                        ws = read_wstring(data, ptr_off, 64)
                        if ws and len(ws) > 1:
                            print(f"  [{i:3d}] 0x{ptr:x} -> L\"{ws[:80]}\"")
                        else:
                            print(f"  [{i:3d}] 0x{ptr:x} -> {data[ptr_off:ptr_off+16].hex()}")
        else:
            s = read_cstring(data, str2_off, 512)
            if s and len(s) > 2:
                print(f"  ASCII: \"{s[:200]}\"")
            else:
                ws = read_wstring(data, str2_off, 256)
                if ws and len(ws) > 2:
                    print(f"  Wide: L\"{ws[:200]}\"")
                else:
                    print(f"  Raw: {data[str2_off:str2_off+64].hex(' ')}")

    # ===== 3. All 10 vtable signatures from .rdata =====
    print(f"\n{'='*70}")
    print("3. VTABLE SIGNATURES (10 distinct types for field cache entries)")
    print("-" * 50)

    # From phase2d_find_init_data.py, we found 10 unique vtable signatures in .rdata.
    # The vtable at 0x141050670 was entry 0's vtable. Let's find all 10 by scanning
    # the cache entry vtable slot locations.
    # But since cache is zero in binary, we need to find them differently.
    # The vtables are in .rdata and have the pattern of ~20 function pointers.
    # We know one vtable: 0x141050670.
    # Let's look at the secondary descriptors at 0x140f8be10 for more vtable refs.

    # First, let's analyze the known vtable thoroughly
    known_vtable_va = 0x141050670
    known_vtable_off = va_to_file(known_vtable_va, sections)
    print(f"\n  Known vtable at 0x{known_vtable_va:x}:")
    vtable_funcs = []
    for i in range(20):
        fptr = struct.unpack_from('<Q', data, known_vtable_off + i*8)[0]
        sec = section_for_va(fptr, sections) if is_pointer(fptr, sections) else '?'
        vtable_funcs.append(fptr)
        print(f"    vfunc[{i:2d}]: 0x{fptr:x} ({sec})")

    # Generate a signature from the first 5 function pointers
    def vtable_sig(off, count=20):
        funcs = []
        for i in range(count):
            f = struct.unpack_from('<Q', data, off + i*8)[0]
            if not is_pointer(f, sections) or section_for_va(f, sections) != '.text':
                break
            funcs.append(f)
        return tuple(funcs)

    known_sig = vtable_sig(known_vtable_off)
    print(f"  Signature length: {len(known_sig)} functions")

    # Now scan .rdata for vtable-like sequences (series of .text pointers)
    # Focus on the region around 0x141050000 where we know vtables live
    print(f"\n  Scanning .rdata for field cache vtables...")
    rdata = None
    for s in sections:
        if s['name'] == '.rdata':
            rdata = s
            break

    vtable_candidates = []
    if rdata:
        rdata_start = rdata['raw_ptr']
        rdata_size = rdata['raw_size']
        rdata_va = IMAGE_BASE + rdata['virt_addr']

        # Scan for sequences of 15+ .text pointers (vtable pattern)
        # Focus on region 0x141040000 - 0x141060000
        scan_start_va = 0x141040000
        scan_end_va = 0x141070000
        scan_start_off = va_to_file(scan_start_va, sections)
        scan_end_off = va_to_file(scan_end_va, sections)

        if scan_start_off and scan_end_off:
            pos = scan_start_off
            while pos < scan_end_off:
                # Check for a run of .text pointers
                run_len = 0
                for j in range(25):
                    fp = struct.unpack_from('<Q', data, pos + j*8)[0]
                    if is_pointer(fp, sections) and section_for_va(fp, sections) == '.text':
                        run_len += 1
                    else:
                        break

                if run_len >= 10:
                    va = scan_start_va + (pos - scan_start_off)
                    sig = vtable_sig(pos, 20)
                    vtable_candidates.append((va, run_len, sig))
                    pos += run_len * 8
                else:
                    pos += 8

    print(f"  Found {len(vtable_candidates)} vtable-like sequences (>=10 .text pointers)")

    # Group by unique signature
    sig_groups = {}
    for va, length, sig in vtable_candidates:
        if sig not in sig_groups:
            sig_groups[sig] = []
        sig_groups[sig].append((va, length))

    print(f"  Unique signatures: {len(sig_groups)}")
    for i, (sig, locations) in enumerate(sig_groups.items()):
        first_va = locations[0][0]
        length = locations[0][1]
        funcs_preview = [f"0x{f:x}" for f in sig[:3]]
        is_known = "  <-- KNOWN (entry 0)" if first_va == known_vtable_va else ""
        print(f"\n  Signature #{i}: {length} funcs at VA 0x{first_va:x} ({len(locations)} instances){is_known}")
        print(f"    First 3 funcs: {', '.join(funcs_preview)}")
        # Show all unique function addresses in this vtable
        unique_funcs = sorted(set(sig))
        print(f"    Unique functions: {len(unique_funcs)}")
        for f in unique_funcs[:8]:
            print(f"      0x{f:x}")

    # ===== 4. The 115 index array at 0x1410fb248 =====
    print(f"\n{'='*70}")
    print("4. INDEX ARRAY at VA 0x1410fb248 (115 entries)")
    print("-" * 50)
    idx_va = 0x1410fb248
    idx_off = va_to_file(idx_va, sections)
    if idx_off:
        # Read the header
        header = struct.unpack_from('<I', data, idx_off)[0]
        print(f"  Header value: {header}")

        # Read 115 u32 values after the header
        indices = []
        for i in range(115):
            val = struct.unpack_from('<I', data, idx_off + 4 + i*4)[0]
            indices.append(val)

        print(f"  Range: {min(indices)} - {max(indices)}")
        print(f"  Unique values: {len(set(indices))}")
        print(f"  Sorted: {sorted(indices) == indices}")

        # Check if these are indices into the 1444-entry field array
        print(f"\n  Distribution across categories:")
        fid_off = va_to_file(0x1410e77a0, sections)
        task_fids = [struct.unpack_from('<I', data, fid_off + i*4)[0] for i in range(1444)]

        cat_dist = Counter()
        for idx in indices:
            if 0 <= idx < 1444:
                if idx < 1021:
                    cat_dist['Standard'] += 1
                elif idx < 1043:
                    cat_dist['CategoryA'] += 1
                elif idx < 1046:
                    cat_dist['CategoryB'] += 1
                elif idx < 1069:
                    cat_dist['CategoryC'] += 1
                elif idx < 1069:
                    cat_dist['CategoryD'] += 1
                elif idx < 1084:
                    cat_dist['CategoryE'] += 1
                elif idx < 1134:
                    cat_dist['CategoryF'] += 1
                elif idx < 1156:
                    cat_dist['CategoryG'] += 1
                else:
                    cat_dist['Extended'] += 1
            else:
                cat_dist[f'OUT_OF_RANGE({idx})'] += 1
        print(f"  {dict(cat_dist)}")

        # Show the field names these indices map to
        print(f"\n  Fields at these indices (showing named ones):")
        for i, idx in enumerate(indices):
            if 0 <= idx < 1444:
                fid = task_fids[idx]
                name = field_map.get(fid, '')
                if name:
                    print(f"    [{i:3d}] index={idx:4d} fid=0x{fid:08x} -> {name}")

        # Check what's AFTER the 115 entries
        print(f"\n  Data after index array:")
        after_off = idx_off + 4 + 115*4
        for i in range(10):
            val = struct.unpack_from('<I', data, after_off + i*4)[0]
            print(f"    +{4 + 115*4 + i*4:4d}: 0x{val:08x} ({val})")

    # ===== 5. Secondary descriptor chain at 0x140f8be10 =====
    print(f"\n{'='*70}")
    print("5. SECONDARY DESCRIPTOR CHAIN at VA 0x140f8be10")
    print("-" * 50)
    desc2_va = 0x140f8be10
    desc2_off = va_to_file(desc2_va, sections)
    if desc2_off:
        # Walk the chain - each descriptor may point to the next
        chain = []
        current_va = desc2_va
        current_off = desc2_off
        visited = set()

        for step in range(20):
            if current_va in visited:
                print(f"  Cycle detected at 0x{current_va:x}")
                break
            visited.add(current_va)

            # Read descriptor fields
            vals = []
            for i in range(16):
                v = struct.unpack_from('<Q', data, current_off + i*8)[0]
                vals.append(v)

            print(f"\n  Descriptor #{step} at VA 0x{current_va:x}:")
            for i, v in enumerate(vals):
                sec = section_for_va(v, sections) if is_pointer(v, sections) else ''
                label = ''
                if i == 0 and is_pointer(v, sections):
                    label = '<- possible RTTI/vtable'
                elif section_for_va(v, sections) == '.text' if is_pointer(v, sections) else False:
                    label = '<- .text function'
                elif is_pointer(v, sections):
                    label = f'<- {sec}'
                elif 0x0b400000 <= (v & 0xFFFFFFFF) <= 0x0f4fffff:
                    fid = v & 0xFFFFFFFF
                    label = f'<- FIELD_ID 0x{fid:08x} ({field_map.get(fid, "?")})'
                elif 0 < v < 0x10000:
                    label = f'= {v}'

                print(f"    +0x{i*8:02x}: 0x{v:016x}  {label}")

            # Check if +0x58 or +0x08 points to another descriptor
            next_ptr = vals[11] if len(vals) > 11 else 0  # +0x58
            if is_pointer(next_ptr, sections) and section_for_va(next_ptr, sections) == '.rdata':
                current_va = next_ptr
                current_off = va_to_file(next_ptr, sections)
                if current_off is None:
                    break
            else:
                print(f"  Chain ends (no .rdata pointer at +0x58)")
                break

    # ===== 6. Category identification from field ID enum values =====
    print(f"\n{'='*70}")
    print("6. CATEGORY IDENTIFICATION FROM FIELD ID ENUM VALUES")
    print("-" * 50)

    fid_off = va_to_file(0x1410e77a0, sections)
    task_fids = [struct.unpack_from('<I', data, fid_off + i*4)[0] for i in range(1444)]

    # PjField enum: the field_id has form 0x0b4XXYY where XX encodes category
    # Let's analyze the sub-nibble pattern
    categories = {
        'Standard': (0, 1021),
        'CategoryA': (1021, 1043),
        'CategoryB': (1043, 1046),
        'CategoryC': (1046, 1069),
        'CategoryD': (1069, 1082),
        'CategoryE': (1082, 1084),
        'CategoryF': (1084, 1134),
        'CategoryG': (1134, 1156),
        'Extended': (1156, 1444),
    }

    for cat_name, (lo, hi) in categories.items():
        fids = task_fids[lo:hi]
        if not fids:
            continue

        # Extract the sub-range byte (bits 8-15 of the field_id)
        sub_bytes = set()
        for fid in fids:
            sub = (fid >> 8) & 0xFF
            sub_bytes.add(sub)

        # Look at the raw IDs
        min_fid = min(fids)
        max_fid = max(fids)
        named_in_cat = [(fid, field_map.get(fid, '')) for fid in fids if fid in field_map]

        print(f"\n  {cat_name} (indices {lo}-{hi-1}, {hi-lo} fields):")
        print(f"    ID range: 0x{min_fid:08x} - 0x{max_fid:08x}")
        print(f"    Sub-bytes: {sorted(sub_bytes)}")
        print(f"    Named fields: {len(named_in_cat)}/{len(fids)}")

        # Show sample named fields
        for fid, name in named_in_cat[:8]:
            print(f"      0x{fid:08x} = {name}")
        if len(named_in_cat) > 8:
            print(f"      ... and {len(named_in_cat) - 8} more")

    # ===== 7. Look for field type descriptors (type → property) mappings =====
    print(f"\n{'='*70}")
    print("7. FIELD TYPE CODE ANALYSIS")
    print("-" * 50)

    # The PjFieldType enum defines type codes. From entry 0 we know type=5=pjWork.
    # Common PjFieldType values:
    type_names = {
        0: 'pjCost',
        1: 'pjDate',
        2: 'pjDuration',
        3: 'pjFinish',
        4: 'pjFlag',
        5: 'pjWork',      # confirmed from entry 0
        6: 'pjNumber',
        7: 'pjStart',
        8: 'pjText',
        9: 'pjResource',
        10: 'pjIndicator',
        11: 'pjPercent',
    }

    # Can we find a type code table in .rdata? Look for sequences of small integers
    # (0-20) that could be type codes, near the field ID arrays
    print("  Looking for type code arrays near field data...")

    # Check the region right after the main descriptor
    desc_off = va_to_file(0x140f8bda0, sections)
    desc_va = 0x140f8bda0
    # The descriptor is at least 0x70 bytes. Check what follows.
    print(f"\n  Data after main descriptor (0x140f8bda0 + 0x70):")
    for i in range(16):
        v = struct.unpack_from('<Q', data, desc_off + 0x70 + i*8)[0]
        sec = section_for_va(v, sections) if is_pointer(v, sections) else ''
        print(f"    +0x{0x70 + i*8:02x}: 0x{v:016x}  {sec}")

    # ===== 8. Check the index mapping table at 0x1413b0440 =====
    print(f"\n{'='*70}")
    print("8. INDEX MAPPING TABLE at VA 0x1413b0440 (descriptor +0x40)")
    print("-" * 50)
    map_va = 0x1413b0440
    map_off = va_to_file(map_va, sections)
    if map_off:
        sec_name = section_for_va(map_va, sections)
        print(f"  Section: {sec_name}")

        # Read first 128 bytes to understand structure
        print(f"  First 128 bytes:")
        for i in range(16):
            v = struct.unpack_from('<Q', data, map_off + i*8)[0]
            sec = section_for_va(v, sections) if is_pointer(v, sections) else ''
            v32a = struct.unpack_from('<I', data, map_off + i*8)[0]
            v32b = struct.unpack_from('<I', data, map_off + i*8 + 4)[0]
            print(f"    +0x{i*8:02x}: 0x{v:016x}  (u32: {v32a}, {v32b})  {sec}")

        # Check if this is a mapping array (field_id → cache_index or vice versa)
        # Try reading as u16 pairs
        print(f"\n  As u16 values:")
        u16s = []
        for i in range(64):
            v = struct.unpack_from('<H', data, map_off + i*2)[0]
            u16s.append(v)
        print(f"    First 32: {u16s[:32]}")
        print(f"    Next 32:  {u16s[32:64]}")

    print(f"\n{'='*70}")
    print("ANALYSIS COMPLETE")
    print("=" * 70)


if __name__ == '__main__':
    main()
