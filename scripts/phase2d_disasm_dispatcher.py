#!/usr/bin/env python3
"""Phase 2d: Analyze the field dispatcher function at VA 0x140258810.

This function is called with (this, field_id, ???) and dispatches to the
appropriate handler. It likely contains or references the field → offset mapping.

Also analyze FUN_140239380 (TTaskTable::vfunc_158) which is the dual-mode
field accessor that reads from the 0x40-byte cache entries.
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


def section_for_va(va, sections):
    rva = va - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return s['name']
    return '?'


def disasm_basic(data, start_off, start_va, length, sections):
    """Basic x64 disassembly showing raw bytes, relative addresses, and branch targets."""
    pos = 0
    lines = []
    while pos < length:
        off = start_off + pos
        va = start_va + pos
        # Read up to 15 bytes
        raw = data[off:off + 15]
        if len(raw) == 0:
            break

        b = raw[0]
        instr_len = 1
        note = ""

        # Decode common x64 instructions
        # REX prefix
        rex = 0
        idx = 0
        if 0x40 <= raw[idx] <= 0x4F:
            rex = raw[idx]
            idx += 1
            instr_len += 1

        opcode = raw[idx]

        # LEA reg, [rip+disp32]: 8D
        if opcode == 0x8D and idx + 1 < len(raw):
            modrm = raw[idx + 1]
            if (modrm & 0xC7) == 0x05:  # mod=00, rm=101 = [rip+disp32]
                reg = (modrm >> 3) & 7
                if rex & 0x04:
                    reg += 8
                disp = struct.unpack_from('<i', raw, idx + 2)[0]
                target = va + idx + 6 + disp  # RIP after this instruction
                instr_len = idx + 6
                sec = section_for_va(target, sections)
                reg_name = ['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi',
                            'r8','r9','r10','r11','r12','r13','r14','r15'][reg]
                note = f"LEA {reg_name}, [rip+{disp:#x}] = 0x{target:x} ({sec})"

        # CALL rel32: E8
        elif opcode == 0xE8:
            disp = struct.unpack_from('<i', raw, idx + 1)[0]
            target = va + idx + 5 + disp
            instr_len = idx + 5
            sec = section_for_va(target, sections)
            note = f"CALL 0x{target:x} ({sec})"

        # JMP rel32: E9
        elif opcode == 0xE9:
            disp = struct.unpack_from('<i', raw, idx + 1)[0]
            target = va + idx + 5 + disp
            instr_len = idx + 5
            note = f"JMP 0x{target:x}"

        # JMP rel8: EB
        elif opcode == 0xEB:
            disp = struct.unpack_from('<b', raw, idx + 1)[0]
            target = va + idx + 2 + disp
            instr_len = idx + 2
            note = f"JMP short 0x{target:x}"

        # Jcc rel32: 0F 80..8F
        elif opcode == 0x0F and idx + 1 < len(raw) and 0x80 <= raw[idx+1] <= 0x8F:
            cc = raw[idx+1] & 0x0F
            cc_names = ['O','NO','B','AE','E','NE','BE','A','S','NS','P','NP','L','GE','LE','G']
            disp = struct.unpack_from('<i', raw, idx + 2)[0]
            target = va + idx + 6 + disp
            instr_len = idx + 6
            note = f"J{cc_names[cc]} 0x{target:x}"

        # Jcc rel8: 70..7F
        elif 0x70 <= opcode <= 0x7F:
            cc = opcode & 0x0F
            cc_names = ['O','NO','B','AE','E','NE','BE','A','S','NS','P','NP','L','GE','LE','G']
            disp = struct.unpack_from('<b', raw, idx + 1)[0]
            target = va + idx + 2 + disp
            instr_len = idx + 2
            note = f"J{cc_names[cc]} short 0x{target:x}"

        # MOV reg, imm32: B8+r
        elif 0xB8 <= opcode <= 0xBF and not rex:
            reg = opcode - 0xB8
            imm = struct.unpack_from('<I', raw, idx + 1)[0]
            instr_len = idx + 5
            reg_names = ['eax','ecx','edx','ebx','esp','ebp','esi','edi']
            note = f"MOV {reg_names[reg]}, 0x{imm:08x}"
            if 0x0b400000 <= imm <= 0x0f4fffff:
                note += " (FIELD_ID!)"

        # CMP with imm32
        elif opcode == 0x3B or opcode == 0x3D or opcode == 0x81:
            if opcode == 0x81 and idx + 1 < len(raw):
                modrm = raw[idx + 1]
                if (modrm >> 3 & 7) == 7:  # CMP
                    if (modrm & 0xC0) == 0xC0:  # register
                        reg = modrm & 7
                        if rex & 1:
                            reg += 8
                        imm = struct.unpack_from('<I', raw, idx + 2)[0]
                        instr_len = idx + 6
                        reg_names = ['eax','ecx','edx','ebx','esp','ebp','esi','edi',
                                     'r8d','r9d','r10d','r11d','r12d','r13d','r14d','r15d']
                        note = f"CMP {reg_names[reg]}, 0x{imm:08x}"
                        if 0x0b400000 <= imm <= 0x0f4fffff:
                            note += " (FIELD_ID!)"
            elif opcode == 0x3D:
                imm = struct.unpack_from('<I', raw, idx + 1)[0]
                instr_len = idx + 5
                note = f"CMP eax, 0x{imm:08x}"
                if 0x0b400000 <= imm <= 0x0f4fffff:
                    note += " (FIELD_ID!)"

        # MOV with RIP-relative addressing
        elif opcode == 0x8B and idx + 1 < len(raw):
            modrm = raw[idx + 1]
            if (modrm & 0xC7) == 0x05:  # [rip+disp32]
                reg = (modrm >> 3) & 7
                if rex & 0x04:
                    reg += 8
                disp = struct.unpack_from('<i', raw, idx + 2)[0]
                target = va + idx + 6 + disp
                instr_len = idx + 6
                sec = section_for_va(target, sections)
                reg_name = ['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi',
                            'r8','r9','r10','r11','r12','r13','r14','r15'][reg]
                if rex & 0x08:
                    note = f"MOV {reg_name}, [rip+{disp:#x}] = 0x{target:x} ({sec})"
                else:
                    ereg = reg_name.replace('r', 'e') if not reg_name.startswith('r8') else reg_name + 'd'
                    note = f"MOV {ereg}, [rip+{disp:#x}] = 0x{target:x} ({sec})"

        # RET: C3
        elif opcode == 0xC3:
            note = "RET"

        # Default: try to estimate instruction length from prefix
        if instr_len == 1 and not note:
            # Very rough: just show the byte
            instr_len = 1 + idx  # include REX

        hex_bytes = raw[:min(instr_len, 12)].hex(' ')
        lines.append((va, hex_bytes, note))
        pos += instr_len

    return lines


def main():
    print("Phase 2d: Dispatcher Function Analysis")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)

    # ===== Analyze field dispatcher at 0x140258810 =====
    print("FIELD DISPATCHER at VA 0x140258810")
    print("-" * 50)
    func_va = 0x140258810
    func_off = va_to_file(func_va, sections)

    lines = disasm_basic(data, func_off, func_va, 512, sections)
    for va, hex_bytes, note in lines:
        if note:
            print(f"  0x{va:x}: {hex_bytes:30s}  {note}")
        else:
            print(f"  0x{va:x}: {hex_bytes}")

    # ===== Analyze TTaskTable::vfunc_158 at 0x140239380 =====
    print(f"\n{'='*70}")
    print("TTaskTable::vfunc_158 at VA 0x140239380")
    print("-" * 50)
    func2_va = 0x140239380
    func2_off = va_to_file(func2_va, sections)

    lines2 = disasm_basic(data, func2_off, func2_va, 512, sections)
    for va, hex_bytes, note in lines2:
        if note:
            print(f"  0x{va:x}: {hex_bytes:30s}  {note}")
        else:
            print(f"  0x{va:x}: {hex_bytes}")

    # ===== Look at the function at 0x1402228f0 (vfunc[0] from vtable) =====
    print(f"\n{'='*70}")
    print("VTABLE vfunc[0] at VA 0x1402228f0 (field object base method)")
    print("-" * 50)
    func3_va = 0x1402228f0
    func3_off = va_to_file(func3_va, sections)

    lines3 = disasm_basic(data, func3_off, func3_va, 256, sections)
    for va, hex_bytes, note in lines3:
        if note:
            print(f"  0x{va:x}: {hex_bytes:30s}  {note}")
        else:
            print(f"  0x{va:x}: {hex_bytes}")

    # ===== Scan for field ID constants in the dispatcher =====
    print(f"\n{'='*70}")
    print("FIELD ID CONSTANTS IN CODE NEAR DISPATCHER")
    print("-" * 50)

    # Read 8KB of code around the dispatcher
    scan_off = func_off - 0x1000
    scan_va = func_va - 0x1000
    for i in range(0, 0x4000, 4):
        val = struct.unpack_from('<I', data, scan_off + i)[0]
        if val == 0x0b400000 or val == 0x0b4005a3 or val == 0x04400009:
            print(f"  VA 0x{scan_va + i:x}: 0x{val:08x}")
        elif 0x0b400000 <= val <= 0x0b4005a3:
            # Check if it's in a CMP or MOV immediate context
            # (preceded by 3D, 3B, B8-BF, or 81)
            if i >= 1:
                prev = data[scan_off + i - 1]
                if prev in (0x3D, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF):
                    print(f"  VA 0x{scan_va + i:x}: 0x{val:08x} (after opcode 0x{prev:02x})")
                elif i >= 2:
                    prev2 = data[scan_off + i - 2]
                    if prev2 == 0x81:
                        print(f"  VA 0x{scan_va + i:x}: 0x{val:08x} (CMP reg, imm32)")

    print("\nDone.")


if __name__ == '__main__':
    main()
