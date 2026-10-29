#!/usr/bin/env python3
"""Phase 2d: Analyze the 48 differing vtable functions between TAssnTable and TTaskTable.

Key findings so far:
- TAssnTable vtable at 0x141038b08 (200 entries)
- TTaskTable vtable at 0x141048648 (428 entries)
- 152 of 200 slots match, 48 differ
- Shared: vfunc[87] (GetFieldValue) and vfunc[158] (binary search/SHL cache lookup)
- No LEA refs to the sorted assignment arrays (156 at 0x1412aa6f0, 191 at 0x1412ac58c)

This script:
1. Lists all 48 differing function pairs
2. Disassembles each differing function to find what data they reference
3. Looks for MOV/LEA patterns that load sorted array or cache pointers
4. Identifies the TAssnTable initialization/constructor function
"""

import struct
from pathlib import Path

IMAGE_BASE = 0x140000000
WINPROJ = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
BASE = Path(__file__).resolve().parents[1]


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


def find_rip_relative_refs(data, sections, func_off, max_bytes=512):
    """Find all RIP-relative references in a function."""
    refs = []
    pos = func_off
    end = min(func_off + max_bytes, len(data) - 7)

    while pos < end:
        b0 = data[pos]

        # Check for RET, INT3 as function boundary
        if b0 == 0xC3 or b0 == 0xCC:
            # Check if this is likely the end (followed by padding or another function)
            if pos > func_off + 4:
                next_b = data[pos + 1] if pos + 1 < len(data) else 0
                if next_b == 0xCC or next_b == 0x90 or (next_b & 0xF0) == 0x40:
                    break

        # LEA reg, [rip+disp32]: REX.W 8D modrm(xx 000 101)
        if b0 in (0x48, 0x4C) and pos + 6 < end:
            b1 = data[pos + 1]
            b2 = data[pos + 2]
            if b1 == 0x8D and (b2 & 0xC7) == 0x05:
                disp = struct.unpack_from('<i', data, pos + 3)[0]
                instr_va = file_to_va(pos, sections)
                if instr_va:
                    target = instr_va + 7 + disp
                    reg_idx = (b2 >> 3) & 7
                    if b0 == 0x4C:
                        reg_idx += 8
                    regs = ['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi',
                            'r8','r9','r10','r11','r12','r13','r14','r15']
                    reg = regs[reg_idx] if reg_idx < 16 else f'r{reg_idx}'
                    refs.append(('LEA', instr_va, target, reg))
                pos += 7
                continue

        # MOV reg, [rip+disp32]: REX.W 8B modrm(xx 000 101)
        if b0 in (0x48, 0x4C) and pos + 6 < end:
            b1 = data[pos + 1]
            b2 = data[pos + 2]
            if b1 == 0x8B and (b2 & 0xC7) == 0x05:
                disp = struct.unpack_from('<i', data, pos + 3)[0]
                instr_va = file_to_va(pos, sections)
                if instr_va:
                    target = instr_va + 7 + disp
                    reg_idx = (b2 >> 3) & 7
                    if b0 == 0x4C:
                        reg_idx += 8
                    regs = ['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi',
                            'r8','r9','r10','r11','r12','r13','r14','r15']
                    reg = regs[reg_idx] if reg_idx < 16 else f'r{reg_idx}'
                    refs.append(('MOV', instr_va, target, reg))
                pos += 7
                continue

        # CMP [rip+disp32], ...: various opcodes
        if b0 in (0x48, 0x4C) and pos + 6 < end:
            b1 = data[pos + 1]
            b2 = data[pos + 2]
            if b1 in (0x39, 0x3B, 0x89, 0x8B) and (b2 & 0xC7) == 0x05:
                disp = struct.unpack_from('<i', data, pos + 3)[0]
                instr_va = file_to_va(pos, sections)
                if instr_va:
                    target = instr_va + 7 + disp
                    ops = {0x39: 'CMP', 0x3B: 'CMP', 0x89: 'MOV_STORE', 0x8B: 'MOV_LOAD'}
                    refs.append((ops.get(b1, f'OP_{b1:02x}'), instr_va, target, ''))
                pos += 7
                continue

        # CALL [rip+disp32]: FF 15 disp32
        if b0 == 0xFF and pos + 5 < end:
            b1 = data[pos + 1]
            if b1 == 0x15:
                disp = struct.unpack_from('<i', data, pos + 2)[0]
                instr_va = file_to_va(pos, sections)
                if instr_va:
                    target = instr_va + 6 + disp
                    refs.append(('CALL_IND', instr_va, target, ''))
                pos += 6
                continue

        pos += 1

    return refs


def func_size_estimate(data, func_off, max_bytes=2048):
    """Estimate function size by looking for RET/INT3 boundaries."""
    for i in range(max_bytes):
        pos = func_off + i
        if pos >= len(data):
            return i
        b = data[pos]
        if b == 0xC3:
            # Check if followed by padding
            if pos + 1 < len(data) and data[pos + 1] in (0xCC, 0x90):
                return i + 1
            # Could be a mid-function RET, but use as rough estimate
            if i > 4:
                return i + 1
    return max_bytes


def main():
    print("Phase 2d: TAssnTable vs TTaskTable Differing Functions")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)

    # Extract both vtables
    assn_vt_va = 0x141038b08
    task_vt_va = 0x141048648
    assn_vt_off = va_to_file(assn_vt_va, sections)
    task_vt_off = va_to_file(task_vt_va, sections)

    assn_vt = [struct.unpack_from('<Q', data, assn_vt_off + i * 8)[0] for i in range(200)]
    task_vt = [struct.unpack_from('<Q', data, task_vt_off + i * 8)[0] for i in range(428)]

    # Known important data addresses
    known_addrs = {
        0x140f8bda0: 'TASK_DESCRIPTOR',
        0x14130be10: 'TASK_CACHE',
        0x1412a9060: 'TASK_SORTED_ARRAY',
        0x140f8bf60: 'RSC_DESCRIPTOR',
        0x12e0d30 + IMAGE_BASE: 'RSC_CACHE',
        0x1412aa6f0: 'ASSN_SORTED_156',
        0x1412ac58c: 'ASSN_SORTED_191',
        0x140f63870: 'ASSN_UNSORTED_721',
        0x1410e6b70: 'ASSN_OLE_DB_564',
        0x140f689e0: 'ASSN_UNSORTED_636',
        0x140f6a8f0: 'ASSN_UNSORTED_777',
        0x141038b08: 'ASSNTABLE_VTABLE',
        0x141048648: 'TASKTABLE_VTABLE',
    }

    # =========================================================================
    # 1. List all differing slots and their function pairs
    # =========================================================================
    print("\n1. ALL 48 DIFFERING VTABLE SLOTS")
    print("-" * 70)
    print(f"  {'Slot':>4}  {'TAssnTable':>16}  {'TTaskTable':>16}  Notes")
    print(f"  {'----':>4}  {'----------':>16}  {'----------':>16}  -----")

    diff_slots = []
    for i in range(min(200, len(task_vt))):
        if assn_vt[i] != task_vt[i]:
            diff_slots.append(i)
            print(f"  [{i:3d}]  0x{assn_vt[i]:x}  0x{task_vt[i]:x}")

    print(f"\n  Total differing: {len(diff_slots)}")

    # =========================================================================
    # 2. Analyze each differing TAssnTable function for data references
    # =========================================================================
    print(f"\n{'='*70}")
    print("2. DATA REFERENCES IN TAssnTable-SPECIFIC FUNCTIONS")
    print("-" * 70)

    interesting_funcs = []

    for slot in diff_slots:
        assn_func = assn_vt[slot]
        task_func = task_vt[slot]
        assn_off = va_to_file(assn_func, sections)
        if assn_off is None:
            continue

        refs = find_rip_relative_refs(data, sections, assn_off, max_bytes=512)
        fsize = func_size_estimate(data, assn_off)

        # Filter for interesting refs (pointing to .data or .rdata)
        data_refs = []
        for op, iaddr, target, reg in refs:
            if is_valid_ptr(target, sections):
                sec = section_for_va(target, sections)
                label = known_addrs.get(target, '')
                if sec in ('.data', '.rdata'):
                    data_refs.append((op, iaddr, target, reg, sec, label))

        if data_refs:
            interesting_funcs.append((slot, assn_func, task_func, fsize, data_refs))
            print(f"\n  vfunc[{slot}] @ 0x{assn_func:x} (size ~{fsize}b):")
            for op, iaddr, target, reg, sec, label in data_refs:
                lbl = f"  ** {label} **" if label else ''
                print(f"    0x{iaddr:x}: {op:10s} {reg:5s} -> 0x{target:x} ({sec}){lbl}")

    # =========================================================================
    # 3. Same analysis for TTaskTable equivalents (for comparison)
    # =========================================================================
    print(f"\n{'='*70}")
    print("3. DATA REFERENCES IN CORRESPONDING TTaskTable FUNCTIONS")
    print("-" * 70)

    for slot, assn_func, task_func, _, _ in interesting_funcs[:10]:
        task_off = va_to_file(task_func, sections)
        if task_off is None:
            continue

        refs = find_rip_relative_refs(data, sections, task_off, max_bytes=512)
        fsize = func_size_estimate(data, task_off)

        data_refs = []
        for op, iaddr, target, reg in refs:
            if is_valid_ptr(target, sections):
                sec = section_for_va(target, sections)
                label = known_addrs.get(target, '')
                if sec in ('.data', '.rdata'):
                    data_refs.append((op, iaddr, target, reg, sec, label))

        if data_refs:
            print(f"\n  vfunc[{slot}] @ 0x{task_func:x} (size ~{fsize}b):")
            for op, iaddr, target, reg, sec, label in data_refs:
                lbl = f"  ** {label} **" if label else ''
                print(f"    0x{iaddr:x}: {op:10s} {reg:5s} -> 0x{target:x} ({sec}){lbl}")

    # =========================================================================
    # 4. Look for functions that reference descriptor-like .rdata addresses
    # =========================================================================
    print(f"\n{'='*70}")
    print("4. LOOKING FOR DESCRIPTOR REFERENCES NEAR KNOWN DESCRIPTORS")
    print("-" * 70)

    # The known Task descriptor is at 0x140f8bda0
    # Resource descriptor at 0x140f8bf60
    # Check if any TAssnTable function references addresses near these
    desc_range_start = 0x140f85000
    desc_range_end = 0x140f90000

    for slot in diff_slots:
        assn_func = assn_vt[slot]
        assn_off = va_to_file(assn_func, sections)
        if assn_off is None:
            continue

        refs = find_rip_relative_refs(data, sections, assn_off, max_bytes=1024)
        for op, iaddr, target, reg in refs:
            if desc_range_start <= target < desc_range_end:
                sec = section_for_va(target, sections) if is_valid_ptr(target, sections) else '?'
                label = known_addrs.get(target, '')
                print(f"  vfunc[{slot}] @ 0x{iaddr:x}: {op} {reg} -> 0x{target:x} ({sec}) {label}")

    # =========================================================================
    # 5. Dump raw bytes of the smallest differing functions
    # =========================================================================
    print(f"\n{'='*70}")
    print("5. RAW BYTES OF SMALLEST DIFFERING FUNCTIONS")
    print("-" * 70)

    sizes = []
    for slot in diff_slots:
        assn_func = assn_vt[slot]
        assn_off = va_to_file(assn_func, sections)
        if assn_off is None:
            continue
        fsize = func_size_estimate(data, assn_off)
        sizes.append((fsize, slot, assn_func, assn_off))

    sizes.sort()

    for fsize, slot, func_va, func_off in sizes[:8]:
        raw = data[func_off:func_off + min(fsize + 4, 64)]
        hex_str = ' '.join(f'{b:02x}' for b in raw)
        print(f"  vfunc[{slot}] @ 0x{func_va:x} (~{fsize}b): {hex_str}")

    # =========================================================================
    # 6. Check for descriptor structure near assignment field ID arrays
    # =========================================================================
    print(f"\n{'='*70}")
    print("6. SCANNING FOR DESCRIPTOR-LIKE STRUCTURES WITH 0x0f4 MARKER")
    print("-" * 70)

    # A descriptor has +0x24 = field_id_marker with prefix 0x0f4
    # Scan .rdata for 0x0f404000 (like Task has 0x0b404000 at +0x24)
    for s in sections:
        if s['name'] not in ('.rdata', '.data'):
            continue
        for pos in range(0, s['raw_size'] - 4, 4):
            foff = s['raw_ptr'] + pos
            v = struct.unpack_from('<I', data, foff)[0]
            if v == 0x0f404000:
                va = IMAGE_BASE + s['virt_addr'] + pos
                # Check surrounding context
                print(f"\n  Found 0x0f404000 at VA 0x{va:x} ({s['name']}):")
                # Show -0x28 to +0x48 around it (it would be at +0x24 in descriptor)
                desc_start = foff - 0x24
                desc_va = va - 0x24
                for off in range(0, 0x70, 8):
                    d = struct.unpack_from('<Q', data, desc_start + off)[0]
                    ptr_sec = section_for_va(d, sections) if is_valid_ptr(d, sections) else ''
                    label = known_addrs.get(d, '')
                    print(f"    +0x{off:02x}: 0x{d:016x} {ptr_sec} {label}")

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
