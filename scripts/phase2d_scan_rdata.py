#!/usr/bin/env python3
"""Phase 2d part 2: Scan .rdata for static field descriptor data.

The runtime table at 0x14130be10 (.data) is zero-initialized and populated
at startup. The STATIC schema definitions must be somewhere in .rdata.
This script scans for:
1. Arrays of Task field IDs (0x0b4xxxxx) in .rdata
2. The full descriptor structure at 0x140f8bda0 (beyond first 0x14 bytes)
3. Any other descriptors pointed to by the type descriptor
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
            file_off = rva - s['virt_addr'] + s['raw_ptr']
            if file_off < s['raw_ptr'] + s['raw_size']:
                return file_off
            return None
    return None


def file_to_va(file_off, sections):
    for s in sections:
        if s['raw_ptr'] <= file_off < s['raw_ptr'] + s['raw_size']:
            return file_off - s['raw_ptr'] + s['virt_addr'] + IMAGE_BASE
    return None


def main():
    print("Phase 2d part 2: Scanning for static field descriptor data")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    rdata = next(s for s in sections if s['name'] == '.rdata')
    data_sec = next(s for s in sections if s['name'] == '.data')

    print(f"\n.rdata: file 0x{rdata['raw_ptr']:x}-0x{rdata['raw_ptr']+rdata['raw_size']:x}"
          f"  VA 0x{rdata['virt_addr']+IMAGE_BASE:x}-0x{rdata['virt_addr']+rdata['virt_size']+IMAGE_BASE:x}")
    print(f".data:  file 0x{data_sec['raw_ptr']:x}-0x{data_sec['raw_ptr']+data_sec['raw_size']:x}"
          f"  VA 0x{data_sec['virt_addr']+IMAGE_BASE:x}-0x{data_sec['virt_addr']+data_sec['virt_size']+IMAGE_BASE:x}")

    # 1. Read the FULL descriptor structure at 0x140f8bda0
    desc_va = 0x140f8bda0
    desc_off = va_to_file(desc_va, sections)
    print(f"\n--- Full descriptor at VA 0x{desc_va:x} (file 0x{desc_off:x}) ---")
    # Read 0x100 bytes to see the full structure
    desc_bytes = data[desc_off:desc_off + 0x100]
    for i in range(0, 0x100, 8):
        qval = struct.unpack_from('<Q', desc_bytes, i)[0]
        u32a = struct.unpack_from('<I', desc_bytes, i)[0]
        u32b = struct.unpack_from('<I', desc_bytes, i+4)[0]
        ptr_note = ""
        if 0x140000000 <= qval <= 0x145000000:
            section_name = "?"
            for s in sections:
                if s['virt_addr'] <= (qval - IMAGE_BASE) < s['virt_addr'] + s['virt_size']:
                    section_name = s['name']
            ptr_note = f"  -> ptr in {section_name}"
        print(f"  +0x{i:02x}: 0x{qval:016x}  ({u32a:>10d} | {u32b:>10d}){ptr_note}")

    # 2. Follow pointer chains from the descriptor
    print("\n--- Following descriptor pointers ---")
    ptrs_to_follow = []
    for i in range(0, 0x100, 8):
        qval = struct.unpack_from('<Q', desc_bytes, i)[0]
        if 0x140000000 <= qval <= 0x145000000:
            ptrs_to_follow.append((i, qval))

    for desc_offset, ptr_va in ptrs_to_follow:
        foff = va_to_file(ptr_va, sections)
        if foff is None:
            print(f"\n  +0x{desc_offset:02x} -> VA 0x{ptr_va:x}: not in raw data")
            continue
        section_name = "?"
        for s in sections:
            if s['virt_addr'] <= (ptr_va - IMAGE_BASE) < s['virt_addr'] + s['virt_size']:
                section_name = s['name']
        print(f"\n  +0x{desc_offset:02x} -> VA 0x{ptr_va:x} ({section_name}, file 0x{foff:x}):")
        target_data = data[foff:foff + 0x80]
        for j in range(0, min(0x80, len(target_data)), 8):
            qval = struct.unpack_from('<Q', target_data, j)[0]
            u32a = struct.unpack_from('<I', target_data, j)[0]
            u32b = struct.unpack_from('<I', target_data, j+4)[0]
            ptr_note = ""
            if 0x140000000 <= qval <= 0x145000000:
                for s in sections:
                    if s['virt_addr'] <= (qval - IMAGE_BASE) < s['virt_addr'] + s['virt_size']:
                        ptr_note = f"  -> ptr in {s['name']}"
            print(f"    +0x{j:02x}: 0x{qval:016x}  ({u32a:>10d} | {u32b:>10d}){ptr_note}")

    # 3. Scan .rdata for Task field ID patterns (0x0b4xxxxx)
    print("\n--- Scanning .rdata for Task field ID clusters ---")
    rdata_start = rdata['raw_ptr']
    rdata_end = rdata_start + rdata['raw_size']
    rdata_bytes = data[rdata_start:rdata_end]

    # Find all u32 values that are Task field IDs
    task_id_positions = []
    for i in range(0, len(rdata_bytes) - 4, 4):
        val = struct.unpack_from('<I', rdata_bytes, i)[0]
        if 0x0b400000 <= val <= 0x0b4fffff:
            task_id_positions.append((i, val))

    print(f"  Found {len(task_id_positions)} u32 values matching Task field ID range")

    # Find clusters (consecutive field IDs within 0x40 bytes stride)
    if task_id_positions:
        clusters = []
        current_cluster = [task_id_positions[0]]
        for pos, val in task_id_positions[1:]:
            prev_pos = current_cluster[-1][0]
            gap = pos - prev_pos
            if gap <= 0x80:  # within ~2 entries of 0x40 stride
                current_cluster.append((pos, val))
            else:
                if len(current_cluster) >= 3:
                    clusters.append(list(current_cluster))
                current_cluster = [(pos, val)]
        if len(current_cluster) >= 3:
            clusters.append(current_cluster)

        print(f"  Found {len(clusters)} clusters of 3+ Task field IDs")
        for ci, cluster in enumerate(clusters[:20]):
            file_off = rdata_start + cluster[0][0]
            va = file_to_va(file_off, sections)
            print(f"\n  Cluster {ci}: {len(cluster)} IDs at file 0x{file_off:x} (VA 0x{va:x})")
            # Analyze stride
            if len(cluster) >= 2:
                strides = [cluster[j+1][0] - cluster[j][0] for j in range(min(len(cluster)-1, 10))]
                print(f"    Strides: {[f'0x{s:x}' for s in strides]}")
            for pos, val in cluster[:8]:
                file_off = rdata_start + pos
                va = file_to_va(file_off, sections)
                print(f"    0x{val:08x} at file 0x{file_off:x} (VA 0x{va:x})")
            if len(cluster) > 8:
                print(f"    ... and {len(cluster)-8} more")

    # 4. Also scan for field IDs at 2-byte alignment (in case stored as part of larger structs)
    print("\n--- Scanning .rdata for Task field IDs at 2-byte alignment ---")
    task_id_pos_2b = []
    for i in range(0, len(rdata_bytes) - 4, 2):
        val = struct.unpack_from('<I', rdata_bytes, i)[0]
        if 0x0b400000 <= val <= 0x0b4fffff:
            task_id_pos_2b.append((i, val))
    print(f"  Found {len(task_id_pos_2b)} at 2-byte alignment (vs {len(task_id_positions)} at 4-byte)")

    # 5. Scan .data section too for static field IDs
    print("\n--- Scanning .data for Task field ID clusters ---")
    data_start = data_sec['raw_ptr']
    data_end = data_start + data_sec['raw_size']
    data_bytes = data[data_start:data_end]

    data_id_positions = []
    for i in range(0, len(data_bytes) - 4, 4):
        val = struct.unpack_from('<I', data_bytes, i)[0]
        if 0x0b400000 <= val <= 0x0b4fffff:
            data_id_positions.append((i, val))

    print(f"  Found {len(data_id_positions)} u32 values matching Task field ID range in .data")

    if data_id_positions:
        clusters = []
        current_cluster = [data_id_positions[0]]
        for pos, val in data_id_positions[1:]:
            prev_pos = current_cluster[-1][0]
            gap = pos - prev_pos
            if gap <= 0x80:
                current_cluster.append((pos, val))
            else:
                if len(current_cluster) >= 3:
                    clusters.append(list(current_cluster))
                current_cluster = [(pos, val)]
        if len(current_cluster) >= 3:
            clusters.append(current_cluster)

        print(f"  Found {len(clusters)} clusters of 3+ Task field IDs in .data")
        for ci, cluster in enumerate(clusters[:20]):
            file_off = data_start + cluster[0][0]
            va = file_to_va(file_off, sections)
            print(f"\n  Cluster {ci}: {len(cluster)} IDs at file 0x{file_off:x} (VA 0x{va:x})")
            if len(cluster) >= 2:
                strides = [cluster[j+1][0] - cluster[j][0] for j in range(min(len(cluster)-1, 10))]
                print(f"    Strides: {[f'0x{s:x}' for s in strides]}")
            for pos, val in cluster[:8]:
                file_off = data_start + pos
                va = file_to_va(file_off, sections)
                print(f"    0x{val:08x} at file 0x{file_off:x} (VA 0x{va:x})")
            if len(cluster) > 8:
                print(f"    ... and {len(cluster)-8} more")

    # 6. Look at the .text section for hardcoded field IDs in instructions
    # (Field IDs used as immediate operands: mov reg, 0x0b4xxxxx)
    print("\n--- Scanning .text for immediate Task field IDs ---")
    text = next(s for s in sections if s['name'] == '.text')
    text_bytes = data[text['raw_ptr']:text['raw_ptr'] + text['raw_size']]
    text_id_count = 0
    for i in range(0, len(text_bytes) - 4, 1):
        val = struct.unpack_from('<I', text_bytes, i)[0]
        if 0x0b400000 <= val <= 0x0b4fffff:
            text_id_count += 1
    print(f"  Found {text_id_count} potential Task field ID immediates in .text")

    print("\nDone.")


if __name__ == '__main__':
    main()
