#!/usr/bin/env python3
"""Phase 2d: Find the actual offset array by analyzing data structure.

The field ID array at VA 0x1410e77a0 has valid-looking offset data after it,
but not all 1444 entries are valid. Need to find:
1. Where does the valid offset data end?
2. Is there a containing structure with a header?
3. How do the offsets relate to field IDs?
"""

import struct
from collections import Counter
from pathlib import Path

IMAGE_BASE = 0x140000000
WINPROJ = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
BASE = Path(r"C:\Users\timot\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile")

FIELD_IDS_VA = 0x1410e77a0
FIELD_COUNT = 1444


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


def main():
    print("Phase 2d: Finding the real offset array structure")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)

    fid_off = va_to_file(FIELD_IDS_VA, sections)
    field_ids = [struct.unpack_from('<I', data, fid_off + i*4)[0] for i in range(FIELD_COUNT)]

    # Read a LARGE chunk after the field ID array to analyze
    after_off = fid_off + FIELD_COUNT * 4
    after_va = FIELD_IDS_VA + FIELD_COUNT * 4  # 0x1410e8e30
    print(f"Field ID array ends at VA 0x{after_va:x} (file 0x{after_off:x})")

    # Read 8000 u32 values after the field IDs
    post_values = [struct.unpack_from('<I', data, after_off + i*4)[0] for i in range(8000)]

    # Find where the valid "offset" data ends
    # Valid offsets should be < 0x10000 (64KB record max)
    print("\nScanning for valid offset region after field IDs...")
    valid_count = 0
    last_valid = 0
    valid_streak = 0
    max_streak = 0
    streak_start = 0
    best_streak_start = 0

    for i, val in enumerate(post_values):
        if val < 0x10000:
            valid_streak += 1
            if valid_streak > max_streak:
                max_streak = valid_streak
                best_streak_start = streak_start
            valid_count += 1
            last_valid = i
        else:
            if valid_streak > 0:
                streak_start = i + 1
            valid_streak = 0

    print(f"  Values < 0x10000 in first 8000: {valid_count}")
    print(f"  Last valid index: {last_valid}")
    print(f"  Longest valid streak: {max_streak} starting at index {best_streak_start}")

    # More detailed: scan for monotonically increasing runs
    print("\nLooking for monotonically increasing runs...")
    runs = []
    run_start = 0
    for i in range(1, min(3000, len(post_values))):
        if post_values[i] < 0x10000 and post_values[i] >= post_values[i-1] and post_values[i-1] < 0x10000:
            continue
        else:
            run_len = i - run_start
            if run_len >= 5:
                runs.append((run_start, run_len, post_values[run_start], post_values[i-1]))
            run_start = i

    for start, length, first_val, last_val in runs[:10]:
        print(f"  Run at +{start*4:#x} (idx {start}): {length} entries, "
              f"range 0x{first_val:x}-0x{last_val:x}")

    # ===== APPROACH 2: Look for pointers TO an offset array in the descriptor =====
    print("\n" + "=" * 70)
    print("APPROACH 2: Search for the offset array via descriptor pointers")
    print("=" * 70)

    # The descriptor at 0x140f8bda0 has pointers. Let's look for ANY
    # pointer in .rdata that points to a u32 array of reasonable offsets.
    desc_va = 0x140f8bda0
    desc_off = va_to_file(desc_va, sections)

    # Read 0x200 bytes of descriptor
    desc_data = data[desc_off:desc_off + 0x200]
    print(f"\nDescriptor pointers (0x200 bytes at VA 0x{desc_va:x}):")
    for i in range(0, 0x200, 8):
        qval = struct.unpack_from('<Q', desc_data, i)[0]
        if 0x140000000 <= qval <= 0x145000000:
            foff = va_to_file(qval, sections)
            if foff is not None:
                # Check if this points to an array of small u32 values
                sample = [struct.unpack_from('<I', data, foff + j*4)[0] for j in range(20)]
                small_count = sum(1 for v in sample if v < 0x10000)
                in_range = sum(1 for v in sample if 0 < v < 0x2000)
                increasing = sum(1 for j in range(1, len(sample)) if sample[j] >= sample[j-1] and sample[j] < 0x10000)
                for s in sections:
                    if s['virt_addr'] <= (qval - IMAGE_BASE) < s['virt_addr'] + s['virt_size']:
                        sec_name = s['name']
                        break
                if small_count >= 10 and increasing >= 8:
                    print(f"  +0x{i:03x} -> VA 0x{qval:x} ({sec_name}): LIKELY OFFSET ARRAY")
                    print(f"         First 20: {[f'0x{v:x}' for v in sample]}")
                elif small_count >= 5:
                    print(f"  +0x{i:03x} -> VA 0x{qval:x} ({sec_name}): {small_count}/20 small, {increasing} increasing")
                    print(f"         First 10: {[f'0x{v:x}' for v in sample[:10]]}")

    # ===== APPROACH 3: Look at the SECOND copy of field IDs (.rdata 0x140fb7e80) =====
    # Table 1 is in a different structural context — check what wraps it
    print("\n" + "=" * 70)
    print("APPROACH 3: Analyze Table 1 context (0x140fb7e80)")
    print("=" * 70)

    t1_va = 0x140fb7e80
    t1_off = va_to_file(t1_va, sections)

    # Look for pointers TO this array in .rdata
    print(f"Searching .rdata for pointers to VA 0x{t1_va:x}...")
    rdata = next(s for s in sections if s['name'] == '.rdata')
    rdata_bytes = data[rdata['raw_ptr']:rdata['raw_ptr'] + rdata['raw_size']]

    target_bytes = struct.pack('<Q', t1_va)
    ptr_positions = []
    pos = 0
    while True:
        pos = rdata_bytes.find(target_bytes, pos)
        if pos == -1:
            break
        ptr_va = rdata['virt_addr'] + IMAGE_BASE + pos
        ptr_positions.append((pos, ptr_va))
        pos += 1

    print(f"  Found {len(ptr_positions)} pointers to Table 1")
    for fpos, ptr_va in ptr_positions:
        # Check what structure this pointer is embedded in
        print(f"\n  Pointer at VA 0x{ptr_va:x} (file 0x{rdata['raw_ptr'] + fpos:x}):")
        # Dump 64 bytes before and 64 bytes after
        ctx_start = rdata['raw_ptr'] + fpos - 32
        for k in range(0, 80, 8):
            ctx_off = ctx_start + k
            qval = struct.unpack_from('<Q', data, ctx_off)[0]
            u32a = struct.unpack_from('<I', data, ctx_off)[0]
            u32b = struct.unpack_from('<I', data, ctx_off + 4)[0]
            marker = "  <-- PTR" if k == 32 else ""
            ptr_note = ""
            if 0x140000000 <= qval <= 0x145000000 and k != 32:
                for s in sections:
                    if s['virt_addr'] <= (qval - IMAGE_BASE) < s['virt_addr'] + s['virt_size']:
                        ptr_note = f"  -> {s['name']}"
            print(f"    0x{rdata['virt_addr'] + IMAGE_BASE + fpos - 32 + k:x}: "
                  f"0x{qval:016x} ({u32a:#010x}|{u32b:#010x}){ptr_note}{marker}")

    # ===== APPROACH 4: Search for pointers to Table 2 =====
    print("\n" + "=" * 70)
    print("APPROACH 4: Pointers to Table 2 (0x1410e77a0)")
    print("=" * 70)

    t2_va = 0x1410e77a0
    target_bytes2 = struct.pack('<Q', t2_va)
    ptr_positions2 = []
    pos = 0
    while True:
        pos = rdata_bytes.find(target_bytes2, pos)
        if pos == -1:
            break
        ptr_va = rdata['virt_addr'] + IMAGE_BASE + pos
        ptr_positions2.append((pos, ptr_va))
        pos += 1

    # Also search .data section
    data_sec = next(s for s in sections if s['name'] == '.data')
    data_bytes = data[data_sec['raw_ptr']:data_sec['raw_ptr'] + data_sec['raw_size']]
    pos = 0
    while True:
        pos = data_bytes.find(target_bytes2, pos)
        if pos == -1:
            break
        ptr_va = data_sec['virt_addr'] + IMAGE_BASE + pos
        ptr_positions2.append((pos, ptr_va))
        pos += 1

    print(f"  Found {len(ptr_positions2)} pointers to Table 2")
    for fpos, ptr_va in ptr_positions2:
        # Determine which section
        for s in sections:
            if s['virt_addr'] <= (ptr_va - IMAGE_BASE) < s['virt_addr'] + s['virt_size']:
                sec = s
                break
        sec_off = (ptr_va - IMAGE_BASE) - sec['virt_addr'] + sec['raw_ptr']
        print(f"\n  Pointer at VA 0x{ptr_va:x} ({sec['name']}):")
        # Dump context: 64 bytes before, 128 bytes after
        for k in range(-32, 128, 8):
            ctx_off = sec_off + k
            if ctx_off < 0 or ctx_off + 8 > len(data):
                continue
            qval = struct.unpack_from('<Q', data, ctx_off)[0]
            u32a = struct.unpack_from('<I', data, ctx_off)[0]
            u32b = struct.unpack_from('<I', data, ctx_off + 4)[0]
            marker = "  <-- PTR" if k == 0 else ""
            ctx_va = ptr_va + k
            note = ""
            if 0x140000000 <= qval <= 0x145000000 and k != 0:
                for s in sections:
                    if s['virt_addr'] <= (qval - IMAGE_BASE) < s['virt_addr'] + s['virt_size']:
                        note = f"  -> {s['name']}"
            # Check for field count
            if u32a == 1444 or u32b == 1444:
                note += "  [COUNT=1444!]"
            print(f"    0x{ctx_va:x}: 0x{qval:016x} ({u32a:#010x}|{u32b:#010x}){note}{marker}")

    print("\nDone.")


if __name__ == '__main__':
    main()
