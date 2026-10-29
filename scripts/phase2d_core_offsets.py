#!/usr/bin/env python3
"""Phase 2d: Map the 24+20+23 core field offsets to field names,
examine the secondary descriptor, and search for additional offset arrays.

Key finding: The structure at 0x1414c2a68 holds:
  - Task field IDs (1444), Resource field IDs (562), Assignment field IDs (564)
  - SQL DDL strings for MSP database tables
  - A gap of 72 u32 values between Task field IDs end and Resource field IDs
  - That gap contains 24 Task + 20 Resource + 23 Assignment core byte offsets
    separated by a GUID

This script maps those core offsets, then searches for the FULL offset table.
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
    print("Phase 2d: Core Field Offsets & Full Offset Search")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # ===== Read field IDs =====
    fid_off = va_to_file(0x1410e77a0, sections)
    task_fids = [struct.unpack_from('<I', data, fid_off + i*4)[0] for i in range(1444)]

    rsc_off = va_to_file(0x1410e8f50, sections)
    rsc_fids = [struct.unpack_from('<I', data, rsc_off + i*4)[0] for i in range(562)]

    assn_off = va_to_file(0x1410e6b70, sections)
    assn_fids = [struct.unpack_from('<I', data, assn_off + i*4)[0] for i in range(564)]

    # ===== Read gap data (72 u32 values) =====
    gap_va = 0x1410e8e30  # just after 1444 task field IDs
    gap_off = va_to_file(gap_va, sections)
    gap = [struct.unpack_from('<I', data, gap_off + i*4)[0] for i in range(72)]

    # Parse the gap: 24 Task offsets, GUID, 20 Resource offsets, ~24 Assignment offsets
    task_offsets = gap[0:24]
    guid_bytes = gap[24:28]  # {6EE5AD02-059B-11D2-A0D1-00C04FB926E0}
    rsc_offsets = gap[28:48]
    assn_offsets = gap[48:71]  # 23 values (last one is 0x0 padding)

    guid_str = "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}" % (
        guid_bytes[0],
        guid_bytes[1] & 0xFFFF, (guid_bytes[1] >> 16) & 0xFFFF,
        guid_bytes[2] & 0xFF, (guid_bytes[2] >> 8) & 0xFF,
        (guid_bytes[2] >> 16) & 0xFF, (guid_bytes[2] >> 24) & 0xFF,
        guid_bytes[3] & 0xFF, (guid_bytes[3] >> 8) & 0xFF,
        (guid_bytes[3] >> 16) & 0xFF, (guid_bytes[3] >> 24) & 0xFF,
    )
    print(f"GUID separator: {guid_str}")

    # ===== Map core Task offsets =====
    print(f"\n{'='*70}")
    print(f"CORE TASK FIELD OFFSETS (24 entries)")
    print(f"{'='*70}")
    print(f"{'Idx':>4} {'FieldID':>12} {'Offset':>8} {'Name'}")
    for i, (fid, off) in enumerate(zip(task_fids[:24], task_offsets)):
        name = field_map.get(fid, '???')
        print(f"{i:4d} 0x{fid:08x} 0x{off:04x}   {name}")

    # ===== Map core Resource offsets =====
    print(f"\n{'='*70}")
    print(f"CORE RESOURCE FIELD OFFSETS (20 entries)")
    print(f"{'='*70}")
    print(f"{'Idx':>4} {'FieldID':>12} {'Offset':>8} {'Name'}")
    for i, (fid, off) in enumerate(zip(rsc_fids[:20], rsc_offsets)):
        name = field_map.get(fid, '???')
        print(f"{i:4d} 0x{fid:08x} 0x{off:04x}   {name}")

    # ===== Map core Assignment offsets =====
    print(f"\n{'='*70}")
    print(f"CORE ASSIGNMENT FIELD OFFSETS (23 entries)")
    print(f"{'='*70}")
    print(f"{'Idx':>4} {'FieldID':>12} {'Offset':>8} {'Name'}")
    for i, (fid, off) in enumerate(zip(assn_fids[:23], assn_offsets)):
        name = field_map.get(fid, '???')
        print(f"{i:4d} 0x{fid:08x} 0x{off:04x}   {name}")

    # ===== Examine the main descriptor at 0x140f8bda0 =====
    print(f"\n{'='*70}")
    print(f"MAIN DESCRIPTOR AT 0x140f8bda0 (extended dump)")
    print(f"{'='*70}")
    desc_va = 0x140f8bda0
    desc_off = va_to_file(desc_va, sections)

    for i in range(0, 0x100, 8):
        qval = struct.unpack_from('<Q', data, desc_off + i)[0]
        u32a, u32b = struct.unpack_from('<II', data, desc_off + i)
        u16vals = struct.unpack_from('<HHHH', data, desc_off + i)
        notes = []
        if 0x140000000 <= qval <= 0x145000000:
            sec = section_for_va(qval, sections)
            notes.append(f"-> {sec} @ 0x{qval:x}")
        if any(v == 1444 for v in u16vals):
            notes.append("HAS 1444!")
        note_str = f"  {'; '.join(notes)}" if notes else ""
        print(f"  +0x{i:02x}: 0x{qval:016x}  ({u32a:#010x} | {u32b:#010x}){note_str}")

    # ===== Examine the secondary descriptor at 0x140f8be10 =====
    print(f"\n{'='*70}")
    print(f"SECONDARY DESCRIPTOR AT 0x140f8be10")
    print(f"{'='*70}")
    sec_desc_va = 0x140f8be10
    sec_desc_off = va_to_file(sec_desc_va, sections)

    for i in range(0, 0x100, 8):
        qval = struct.unpack_from('<Q', data, sec_desc_off + i)[0]
        u32a, u32b = struct.unpack_from('<II', data, sec_desc_off + i)
        u16vals = struct.unpack_from('<HHHH', data, sec_desc_off + i)
        notes = []
        if 0x140000000 <= qval <= 0x145000000:
            sec = section_for_va(qval, sections)
            notes.append(f"-> {sec} @ 0x{qval:x}")
        for v in u16vals:
            if v in (562, 564, 1444):
                notes.append(f"HAS {v}!")
        note_str = f"  {'; '.join(notes)}" if notes else ""
        print(f"  +0x{i:02x}: 0x{qval:016x}  ({u32a:#010x} | {u32b:#010x}){note_str}")

    # ===== Look for the FULL offset array =====
    # Strategy: search .rdata for arrays of 1444 u16 values where each is < 0x2000
    # (typical byte offsets into a record < 8KB)
    print(f"\n{'='*70}")
    print(f"SEARCHING .rdata FOR LARGE OFFSET ARRAYS")
    print(f"{'='*70}")

    rdata = next(s for s in sections if s['name'] == '.rdata')
    rdata_start = rdata['raw_ptr']
    rdata_end = rdata_start + rdata['raw_size']
    rdata_va_base = IMAGE_BASE + rdata['virt_addr']

    # Search for runs of >=100 consecutive u16 values < 0x2000 (non-zero)
    print("\nSearching for runs of u16 < 0x2000...")
    best_runs_u16 = []
    run_start = None
    run_len = 0
    for pos in range(rdata_start, rdata_end - 2, 2):
        val = struct.unpack_from('<H', data, pos)[0]
        if 0 < val < 0x2000:
            if run_start is None:
                run_start = pos
            run_len += 1
        else:
            if run_len >= 100:
                va = rdata_va_base + (run_start - rdata_start)
                best_runs_u16.append((va, run_len, run_start))
            run_start = None
            run_len = 0

    if run_len >= 100:
        va = rdata_va_base + (run_start - rdata_start)
        best_runs_u16.append((va, run_len, run_start))

    best_runs_u16.sort(key=lambda x: -x[1])
    print(f"Found {len(best_runs_u16)} runs of >=100 u16 values:")
    for va, length, foff in best_runs_u16[:15]:
        sample = [struct.unpack_from('<H', data, foff + j*2)[0] for j in range(min(20, length))]
        increasing = sum(1 for j in range(1, min(length, 200))
                         if struct.unpack_from('<H', data, foff + j*2)[0]
                         >= struct.unpack_from('<H', data, foff + (j-1)*2)[0])
        print(f"  VA 0x{va:x}: {length} values, first={sample[:10]}, incr={increasing}")

    # Also search for runs of u32 < 0x2000
    print("\nSearching for runs of u32 < 0x2000...")
    best_runs_u32 = []
    run_start = None
    run_len = 0
    for pos in range(rdata_start, rdata_end - 4, 4):
        val = struct.unpack_from('<I', data, pos)[0]
        if val < 0x2000:
            if run_start is None:
                run_start = pos
            run_len += 1
        else:
            if run_len >= 50:
                va = rdata_va_base + (run_start - rdata_start)
                best_runs_u32.append((va, run_len, run_start))
            run_start = None
            run_len = 0

    best_runs_u32.sort(key=lambda x: -x[1])
    print(f"Found {len(best_runs_u32)} runs of >=50 u32 values:")
    for va, length, foff in best_runs_u32[:20]:
        sample = [struct.unpack_from('<I', data, foff + j*4)[0] for j in range(min(20, length))]
        unique = len(set(struct.unpack_from('<I', data, foff + j*4)[0]
                         for j in range(min(length, 300))))
        print(f"  VA 0x{va:x}: {length} values, unique={unique}, first={sample[:8]}")

    # ===== Check runtime cache entry 0 =====
    print(f"\n{'='*70}")
    print(f"RUNTIME CACHE ENTRY 0 (at 0x14130be10)")
    print(f"{'='*70}")
    cache_va = 0x14130be10
    cache_off = va_to_file(cache_va, sections)
    entry0 = data[cache_off:cache_off + 0x40]
    print(f"Entry 0 (0x40 bytes):")
    for i in range(0, 0x40, 8):
        qval = struct.unpack_from('<Q', entry0, i)[0]
        u32a, u32b = struct.unpack_from('<II', entry0, i)
        u16s = struct.unpack_from('<HHHH', entry0, i)
        u8s = [entry0[i+j] for j in range(8)]
        notes = ""
        if 0x140000000 <= qval <= 0x145000000:
            notes = f"  -> {section_for_va(qval, sections)} @ 0x{qval:x}"
        print(f"  +0x{i:02x}: 0x{qval:016x}  u32({u32a:#x},{u32b:#x}) u16{u16s} u8{u8s}{notes}")

    # Also check entry 1 (should be zeros)
    entry1 = data[cache_off + 0x40:cache_off + 0x80]
    is_zero = all(b == 0 for b in entry1)
    print(f"\nEntry 1 (0x40 bytes): {'ALL ZEROS' if is_zero else 'HAS DATA!'}")
    if not is_zero:
        for i in range(0, 0x40, 8):
            qval = struct.unpack_from('<Q', entry1, i)[0]
            if qval != 0:
                print(f"  +0x{i:02x}: 0x{qval:016x}")

    # ===== Write core offset TSV =====
    out_path = ANALYSIS / "core_field_offsets.tsv"
    with open(out_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['record_type', 'index', 'field_id_hex', 'record_offset_hex',
                     'record_offset_dec', 'property_name'])
        for i, (fid, off) in enumerate(zip(task_fids[:24], task_offsets)):
            w.writerow(['Task', i, f'0x{fid:08x}', f'0x{off:04x}', off,
                         field_map.get(fid, '')])
        for i, (fid, off) in enumerate(zip(rsc_fids[:20], rsc_offsets)):
            w.writerow(['Resource', i, f'0x{fid:08x}', f'0x{off:04x}', off,
                         field_map.get(fid, '')])
        for i, (fid, off) in enumerate(zip(assn_fids[:23], assn_offsets)):
            w.writerow(['Assignment', i, f'0x{fid:08x}', f'0x{off:04x}', off,
                         field_map.get(fid, '')])
    print(f"\nWrote core offsets to {out_path}")

    print("\nDone.")


if __name__ == '__main__':
    main()
