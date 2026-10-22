#!/usr/bin/env python3
"""Phase 2d: Deep-dive into runtime cache entry 0 and its metadata pointer.

Cache entry 0 (at VA 0x14130be10) is the ONLY populated 0x40-byte entry:
  +0x00: 0x141050670 — metadata ptr (.rdata)
  +0x08: 0x0b400000 (field_id=Work), type=5
  +0x18: record_offset=8, flags=0x20028
  +0x30: size=8

This script:
1. Follows the metadata pointer to see what field info is stored there
2. Investigates suspicious u32 arrays found in .rdata search
3. Looks at the function pointers from the main descriptor to understand
   how the cache gets populated
"""

import struct
import csv
from pathlib import Path

IMAGE_BASE = 0x140000000
WINPROJ = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
BASE = Path(r"C:\Users\timot\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile")
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
    print("Phase 2d: Cache Entry & Metadata Deep Dive")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # ===== 1. Follow metadata pointer from cache entry 0 =====
    print("1. METADATA AT 0x141050670 (.rdata)")
    print("-" * 50)
    meta_va = 0x141050670
    meta_off = va_to_file(meta_va, sections)

    # Read 0x100 bytes around this address
    for i in range(-0x40, 0x100, 8):
        qval = struct.unpack_from('<Q', data, meta_off + i)[0]
        u32a, u32b = struct.unpack_from('<II', data, meta_off + i)
        notes = ""
        if 0x140000000 <= qval <= 0x145000000:
            notes = f"  -> {section_for_va(qval, sections)} @ 0x{qval:x}"
        marker = "  <-- entry 0 ptr" if i == 0 else ""
        print(f"  VA 0x{meta_va+i:x} (+{i:#04x}): 0x{qval:016x}  ({u32a:#010x} | {u32b:#010x}){notes}{marker}")

    # Check if the metadata area has a regular structure
    # Look for field IDs in the vicinity
    print("\nChecking for field IDs near metadata pointer...")
    for delta in range(-0x100, 0x200, 4):
        val = struct.unpack_from('<I', data, meta_off + delta)[0]
        if 0x0b400000 <= val <= 0x0b4005a3:
            name = field_map.get(val, '')
            print(f"  VA 0x{meta_va+delta:x} (+{delta:#06x}): 0x{val:08x} = {name}")

    # ===== 2. Investigate suspicious u32 arrays =====
    print(f"\n{'='*70}")
    print("2. SUSPICIOUS u32 ARRAYS")
    print("-" * 50)

    # VA 0x141118520: 778 values, starts with 61, 1444, 1445, 1446...
    arr1_va = 0x141118520
    arr1_off = va_to_file(arr1_va, sections)
    arr1 = [struct.unpack_from('<I', data, arr1_off + i*4)[0] for i in range(100)]
    print(f"\nArray at VA 0x{arr1_va:x}:")
    print(f"  First 100 values:")
    for j in range(0, 100, 10):
        vals = arr1[j:j+10]
        print(f"    [{j:3d}] {' '.join(f'{v:5d}' for v in vals)}")

    # VA 0x1410fb248: 221 values, starts with 115, 0, 0, 2, 3, 4, 5, 8
    arr2_va = 0x1410fb248
    arr2_off = va_to_file(arr2_va, sections)
    arr2 = [struct.unpack_from('<I', data, arr2_off + i*4)[0] for i in range(230)]
    print(f"\nArray at VA 0x{arr2_va:x}:")
    print(f"  First 50 values:")
    for j in range(0, 50, 10):
        vals = arr2[j:j+10]
        print(f"    [{j:3d}] {' '.join(f'{v:5d}' for v in vals)}")

    # Check if arr2 looks like an offset table for fields
    # 115, then increasing values 0, 0, 2, 3, 4, 5, 8 — could be [count, offsets...]
    print(f"\n  Possible interpretation: arr2[0]={arr2[0]} (count?)")
    print(f"  Values 1..115: {arr2[1:116]}")

    # ===== 3. Look at the Table 1 field ID array context =====
    # Table 1 (0x140fb7e80) might be in a different structural context than Table 2
    print(f"\n{'='*70}")
    print("3. TABLE 1 CONTEXT (field IDs at 0x140fb7e80)")
    print("-" * 50)

    t1_va = 0x140fb7e80
    t1_off = va_to_file(t1_va, sections)
    # Look 256 bytes before for context
    print(f"Context before Table 1:")
    for i in range(-0x100, 0, 8):
        off = t1_off + i
        qval = struct.unpack_from('<Q', data, off)[0]
        u32a, u32b = struct.unpack_from('<II', data, off)
        notes = ""
        if 0x140000000 <= qval <= 0x145000000:
            notes = f"  -> {section_for_va(qval, sections)}"
        if u32a == 1444 or u32b == 1444:
            notes += " COUNT=1444!"
        if 0x0b400000 <= u32a <= 0x0b4005a3:
            notes += f" TASK_FID={field_map.get(u32a, '')}"
        print(f"  VA 0x{t1_va+i:x}: 0x{qval:016x} ({u32a:#010x}|{u32b:#010x}){notes}")

    # And after Table 1 ends
    t1_end_va = t1_va + 1444 * 4  # 0x140fb8e10
    t1_end_off = t1_off + 1444 * 4
    print(f"\nContext after Table 1 (ends at VA 0x{t1_end_va:x}):")
    for i in range(0, 0x100, 8):
        off = t1_end_off + i
        qval = struct.unpack_from('<Q', data, off)[0]
        u32a, u32b = struct.unpack_from('<II', data, off)
        notes = ""
        if 0x140000000 <= qval <= 0x145000000:
            notes = f"  -> {section_for_va(qval, sections)}"
        if 0x0c400000 <= u32a <= 0x0c4fffff:
            notes += " RSC_FID"
        if 0x0f400000 <= u32a <= 0x0f4fffff:
            notes += " ASSN_FID"
        if u32a < 0x2000 and u32b < 0x2000 and (u32a > 0 or u32b > 0):
            notes += " SMALL_VALUES"
        print(f"  VA 0x{t1_end_va+i:x}: 0x{qval:016x} ({u32a:#010x}|{u32b:#010x}){notes}")

    # ===== 4. Analyze Table 1 pre-structure (descriptor 2) =====
    # Table 1 at 0x140fb7e80 is referenced by the descriptor at 0x140f8bda0
    # But the descriptor actually points to the runtime cache at 0x14130be10,
    # not to Table 1 directly. So Table 1 might have its OWN wrapper structure.
    print(f"\n{'='*70}")
    print("4. SEARCHING FOR TABLE 1 WRAPPER STRUCTURE")
    print("-" * 50)

    # Search for pointers to Table 1 in .rdata
    rdata = next(s for s in sections if s['name'] == '.rdata')
    target = struct.pack('<Q', t1_va)
    pos = 0
    rdata_bytes = data[rdata['raw_ptr']:rdata['raw_ptr'] + rdata['raw_size']]
    while True:
        pos = rdata_bytes.find(target, pos)
        if pos == -1:
            break
        ptr_va = IMAGE_BASE + rdata['virt_addr'] + pos
        # Read the surrounding structure
        abs_off = rdata['raw_ptr'] + pos
        print(f"\nPointer to Table 1 at VA 0x{ptr_va:x} (.rdata):")
        for k in range(-0x40, 0x60, 8):
            off = abs_off + k
            qval = struct.unpack_from('<Q', data, off)[0]
            u32a, u32b = struct.unpack_from('<II', data, off)
            notes = ""
            if 0x140000000 <= qval <= 0x145000000:
                notes = f"  -> {section_for_va(qval, sections)}"
            marker = " <-- PTR" if k == 0 else ""
            if u32a == 1444 or u32b == 1444:
                notes += " COUNT=1444!"
            print(f"    VA 0x{ptr_va+k:x}: 0x{qval:016x} ({u32a:#010x}|{u32b:#010x}){notes}{marker}")
        pos += 1

    # ===== 5. Look at function at 0x140258810 =====
    # This is the init/resolver function from the descriptor at +0x60
    print(f"\n{'='*70}")
    print("5. FUNCTION AT 0x140258810 (first 32 bytes)")
    print("-" * 50)
    func_va = 0x140258810
    func_off = va_to_file(func_va, sections)
    func_bytes = data[func_off:func_off + 64]
    print(f"  Raw bytes: {func_bytes[:32].hex(' ')}")
    print(f"  Raw bytes: {func_bytes[32:64].hex(' ')}")

    # ===== 6. Read the entry at 0x141050670 as a structure =====
    print(f"\n{'='*70}")
    print("6. FIELD METADATA STRUCTURE (pointed by cache entry 0)")
    print("-" * 50)

    # The metadata at 0x141050670 pointed to by cache entry 0 might be
    # a per-field descriptor in .rdata. Let's see if there's a table of these.
    meta_va = 0x141050670
    meta_off = va_to_file(meta_va, sections)

    # Check if there's a repeating structure around this address
    # Read 0x40 bytes per "entry" for several entries
    stride_candidates = [8, 12, 16, 20, 24, 32]
    for stride in stride_candidates:
        # Check if addresses at (meta_va - N*stride) and (meta_va + N*stride)
        # contain plausible metadata
        count_with_fids = 0
        for n in range(-5, 6):
            check_off = meta_off + n * stride
            if check_off < 0:
                continue
            # Look for field IDs within a few bytes
            for delta in range(0, stride, 4):
                val = struct.unpack_from('<I', data, check_off + delta)[0]
                if 0x0b400000 <= val <= 0x0b4005a3:
                    count_with_fids += 1
                    break
        if count_with_fids >= 6:
            print(f"  Stride {stride}: found field IDs in {count_with_fids}/11 entries")
            # Show the entries
            for n in range(-3, 6):
                check_off = meta_off + n * stride
                vals = [struct.unpack_from('<I', data, check_off + d)[0] for d in range(0, stride, 4)]
                fid_val = None
                for v in vals:
                    if 0x0b400000 <= v <= 0x0b4005a3:
                        fid_val = v
                        break
                name = field_map.get(fid_val, '') if fid_val else ''
                marker = " <-- entry 0 ptr" if n == 0 else ""
                vals_hex = ' '.join(f'{v:08x}' for v in vals)
                print(f"    [{n:+2d}] {vals_hex}  {name}{marker}")

    # Direct approach: just dump 0x200 bytes from the metadata address
    print(f"\n  Raw dump from 0x{meta_va:x}:")
    for i in range(0, 0x80, 16):
        raw = data[meta_off + i:meta_off + i + 16]
        hex_str = ' '.join(f'{b:02x}' for b in raw)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in raw)
        print(f"    +{i:03x}: {hex_str}  {ascii_str}")

    print("\nDone.")


if __name__ == '__main__':
    main()
