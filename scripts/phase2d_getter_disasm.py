#!/usr/bin/env python3
"""Disassemble getter2 functions to understand how byte_offset reaches the flat data buffer.

Key getter2 functions (from field_master_decoded.tsv):
  0x7de230 - Priority (i16, byte_off=0x910, class_slot=13)
  0x7ddfb0 - Duration/FreeSlack (i32, byte_off=0x740)
  0x7de2d0 - Start/EarlyStart dates (date32, byte_off=0x1a0)
  0x7de090 - Finish/LateFinish dates (date32, byte_off=0x1a0)
  0x7ddec0 - ConstraintType (i16, byte_off=0x730)
  0x7ddf50 - Created (date32, byte_off=0x7c4)
  0x7de010 - DurationUnits (i16, byte_off=0x74a)
  0x1ecb50 - Boolean getter (no byte_offset)
"""

import ctypes
import struct
import sys

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
OpenProcess = k32.OpenProcess
ReadProcessMemory = k32.ReadProcessMemory
CloseHandle = k32.CloseHandle

def get_pid():
    import subprocess
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq WINPROJ.EXE', '/FO', 'CSV', '/NH'],
                       capture_output=True, text=True)
    for line in r.stdout.strip().split('\n'):
        if 'WINPROJ' in line:
            return int(line.split(',')[1].strip('"'))
    return None

def get_image_base(pid):
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    hProcess = OpenProcess(0x0410 | 0x0010, False, pid)
    if not hProcess:
        return None
    hMods = (ctypes.c_void_p * 1024)()
    cbNeeded = ctypes.c_ulong()
    if not psapi.EnumProcessModulesEx(hProcess, ctypes.byref(hMods), ctypes.sizeof(hMods),
                                       ctypes.byref(cbNeeded), 0x03):
        CloseHandle(hProcess)
        return None
    base = hMods[0]
    CloseHandle(hProcess)
    return base

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok:
        return None
    return buf.raw[:n.value]


def disasm_simple(code, base_addr, max_instr=40):
    """Simple x86-64 disassembler for common MOV/LEA/ADD/RET patterns.
    Not complete — just enough to decode getter2 functions."""
    pos = 0
    lines = []
    count = 0

    regs64 = ['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi',
              'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15']
    regs32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi',
              'r8d', 'r9d', 'r10d', 'r11d', 'r12d', 'r13d', 'r14d', 'r15d']

    while pos < len(code) and count < max_instr:
        start = pos
        addr = base_addr + pos

        # Read up to 15 bytes for one instruction
        raw = code[pos:pos+15]
        if len(raw) < 1:
            break

        # Simple hex dump for now — we'll annotate patterns
        b = raw[0]

        # RET
        if b == 0xC3:
            lines.append((addr, code[start:pos+1], 'ret'))
            pos += 1
            count += 1
            continue

        # INT3
        if b == 0xCC:
            lines.append((addr, code[start:pos+1], 'int3'))
            pos += 1
            count += 1
            if count > 3 and lines[-2][2] == 'ret':
                break  # stop after ret + padding
            continue

        # NOP
        if b == 0x90:
            lines.append((addr, code[start:pos+1], 'nop'))
            pos += 1
            count += 1
            continue

        # Just dump hex bytes for now with length heuristic
        # For the full decoder, we just show raw bytes
        # Take 1-15 bytes until next obvious instruction boundary

        # Check for REX prefix
        has_rex_w = False
        has_rex_r = False
        has_rex_b = False
        has_rex = False
        if 0x40 <= b <= 0x4F:
            has_rex = True
            has_rex_w = bool(b & 0x08)
            has_rex_r = bool(b & 0x04)
            has_rex_b = bool(b & 0x01)
            pos += 1
            b = code[pos] if pos < len(code) else 0

        # Decode opcode
        desc = ''
        consumed = 0

        # MOV r64, [r/m64+disp32]  (opcode 8B /r with ModRM)
        if b == 0x8B and pos + 1 < len(code):
            modrm = code[pos + 1]
            mod = (modrm >> 6) & 3
            reg = ((modrm >> 3) & 7) | (4 if has_rex_r else 0)
            rm = (modrm & 7) | (1 if has_rex_b else 0)
            rset = regs64 if has_rex_w else regs32

            if mod == 0 and rm == 5:  # RIP-relative
                if pos + 5 < len(code):
                    disp = struct.unpack_from('<i', code, pos + 2)[0]
                    target = addr + (pos - start) + 6 + disp
                    desc = f'mov {rset[reg]}, [rip+{disp:#x}]  ; [{target:#x}]'
                    consumed = 6
            elif mod == 1:  # [reg+disp8]
                disp = struct.unpack_from('<b', code, pos + 2)[0]
                if rm == 4:  # SIB byte
                    sib = code[pos + 2]
                    disp = struct.unpack_from('<b', code, pos + 3)[0]
                    desc = f'mov {rset[reg]}, [SIB+{disp:#x}]'
                    consumed = 4
                else:
                    desc = f'mov {rset[reg]}, [{regs64[rm]}+{disp:#x}]'
                    consumed = 3
            elif mod == 2:  # [reg+disp32]
                if rm == 4:  # SIB
                    sib = code[pos + 2]
                    disp = struct.unpack_from('<i', code, pos + 3)[0]
                    desc = f'mov {rset[reg]}, [SIB+{disp:#x}]'
                    consumed = 7
                else:
                    disp = struct.unpack_from('<i', code, pos + 2)[0]
                    desc = f'mov {rset[reg]}, [{regs64[rm]}+{disp:#x}]'
                    consumed = 6
            elif mod == 0:  # [reg]
                if rm == 4:  # SIB
                    sib = code[pos + 2]
                    desc = f'mov {rset[reg]}, [SIB]'
                    consumed = 3
                else:
                    desc = f'mov {rset[reg]}, [{regs64[rm]}]'
                    consumed = 2
            elif mod == 3:  # reg, reg
                desc = f'mov {rset[reg]}, {rset[rm]}'
                consumed = 2

        # MOV [r/m64+disp], reg  (opcode 89)
        elif b == 0x89 and pos + 1 < len(code):
            modrm = code[pos + 1]
            mod = (modrm >> 6) & 3
            reg = ((modrm >> 3) & 7) | (4 if has_rex_r else 0)
            rm = (modrm & 7) | (1 if has_rex_b else 0)
            rset = regs64 if has_rex_w else regs32

            if mod == 1:
                disp = struct.unpack_from('<b', code, pos + 2)[0]
                desc = f'mov [{regs64[rm]}+{disp:#x}], {rset[reg]}'
                consumed = 3
            elif mod == 2:
                disp = struct.unpack_from('<i', code, pos + 2)[0]
                desc = f'mov [{regs64[rm]}+{disp:#x}], {rset[reg]}'
                consumed = 6

        # LEA r64, [r/m+disp]  (opcode 8D)
        elif b == 0x8D and pos + 1 < len(code):
            modrm = code[pos + 1]
            mod = (modrm >> 6) & 3
            reg = ((modrm >> 3) & 7) | (4 if has_rex_r else 0)
            rm = (modrm & 7) | (1 if has_rex_b else 0)
            rset = regs64 if has_rex_w else regs32

            if mod == 1:
                disp = struct.unpack_from('<b', code, pos + 2)[0]
                desc = f'lea {rset[reg]}, [{regs64[rm]}+{disp:#x}]'
                consumed = 3
            elif mod == 2:
                disp = struct.unpack_from('<i', code, pos + 2)[0]
                desc = f'lea {rset[reg]}, [{regs64[rm]}+{disp:#x}]'
                consumed = 6

        # ADD
        elif b == 0x03 and pos + 1 < len(code):
            modrm = code[pos + 1]
            mod = (modrm >> 6) & 3
            reg = ((modrm >> 3) & 7) | (4 if has_rex_r else 0)
            rm = (modrm & 7) | (1 if has_rex_b else 0)
            rset = regs64 if has_rex_w else regs32
            if mod == 3:
                desc = f'add {rset[reg]}, {rset[rm]}'
                consumed = 2

        # MOVZX r, [r/m+disp]  (0F B7 for word, 0F B6 for byte)
        elif b == 0x0F and pos + 1 < len(code) and code[pos+1] in (0xB6, 0xB7):
            width = 'word' if code[pos+1] == 0xB7 else 'byte'
            if pos + 2 < len(code):
                modrm = code[pos + 2]
                mod = (modrm >> 6) & 3
                reg = ((modrm >> 3) & 7) | (4 if has_rex_r else 0)
                rm = (modrm & 7) | (1 if has_rex_b else 0)
                rset = regs64 if has_rex_w else regs32

                if mod == 1:
                    disp = struct.unpack_from('<b', code, pos + 3)[0]
                    desc = f'movzx {rset[reg]}, {width} [{regs64[rm]}+{disp:#x}]'
                    consumed = 4
                elif mod == 2:
                    disp = struct.unpack_from('<i', code, pos + 3)[0]
                    desc = f'movzx {rset[reg]}, {width} [{regs64[rm]}+{disp:#x}]'
                    consumed = 7
                elif mod == 0 and rm == 4:
                    sib = code[pos + 3]
                    base_reg = sib & 7
                    index_reg = (sib >> 3) & 7
                    scale = 1 << ((sib >> 6) & 3)
                    desc = f'movzx {rset[reg]}, {width} [{regs64[base_reg]}+{regs64[index_reg]}*{scale}]'
                    consumed = 4
                elif mod == 0:
                    desc = f'movzx {rset[reg]}, {width} [{regs64[rm]}]'
                    consumed = 3

        # MOVSXD (63 with REX.W)
        elif b == 0x63 and has_rex_w and pos + 1 < len(code):
            modrm = code[pos + 1]
            mod = (modrm >> 6) & 3
            reg = ((modrm >> 3) & 7) | (4 if has_rex_r else 0)
            rm = (modrm & 7) | (1 if has_rex_b else 0)
            if mod == 1:
                disp = struct.unpack_from('<b', code, pos + 2)[0]
                desc = f'movsxd {regs64[reg]}, dword [{regs64[rm]}+{disp:#x}]'
                consumed = 3
            elif mod == 2:
                disp = struct.unpack_from('<i', code, pos + 2)[0]
                desc = f'movsxd {regs64[reg]}, dword [{regs64[rm]}+{disp:#x}]'
                consumed = 6
            elif mod == 0 and rm == 4:
                sib = code[pos + 2]
                base_reg = sib & 7
                index_reg = (sib >> 3) & 7
                scale = 1 << ((sib >> 6) & 3)
                desc = f'movsxd {regs64[reg]}, dword [{regs64[base_reg]}+{regs64[index_reg]}*{scale}]'
                consumed = 3

        # CMP
        elif b == 0x83 and pos + 2 < len(code):
            modrm = code[pos + 1]
            mod = (modrm >> 6) & 3
            reg_op = (modrm >> 3) & 7
            rm = (modrm & 7) | (1 if has_rex_b else 0)
            rset = regs64 if has_rex_w else regs32
            ops = ['add', 'or', 'adc', 'sbb', 'and', 'sub', 'xor', 'cmp']
            if mod == 3:
                imm = struct.unpack_from('<b', code, pos + 2)[0]
                desc = f'{ops[reg_op]} {rset[rm]}, {imm:#x}'
                consumed = 3

        # TEST
        elif b == 0x85 and pos + 1 < len(code):
            modrm = code[pos + 1]
            mod = (modrm >> 6) & 3
            reg = ((modrm >> 3) & 7) | (4 if has_rex_r else 0)
            rm = (modrm & 7) | (1 if has_rex_b else 0)
            rset = regs64 if has_rex_w else regs32
            if mod == 3:
                desc = f'test {rset[rm]}, {rset[reg]}'
                consumed = 2

        # Jcc short
        elif 0x70 <= b <= 0x7F:
            cc_names = ['o','no','b','ae','e','ne','be','a','s','ns','p','np','l','ge','le','g']
            disp = struct.unpack_from('<b', code, pos + 1)[0]
            target = addr + (pos - start) + 2 + disp
            desc = f'j{cc_names[b-0x70]} {target:#x}'
            consumed = 2

        # JMP short
        elif b == 0xEB:
            disp = struct.unpack_from('<b', code, pos + 1)[0]
            target = addr + (pos - start) + 2 + disp
            desc = f'jmp {target:#x}'
            consumed = 2

        # CALL rel32
        elif b == 0xE8:
            disp = struct.unpack_from('<i', code, pos + 1)[0]
            target = addr + (pos - start) + 5 + disp
            desc = f'call {target:#x}'
            consumed = 5

        # JMP rel32
        elif b == 0xE9:
            disp = struct.unpack_from('<i', code, pos + 1)[0]
            target = addr + (pos - start) + 5 + disp
            desc = f'jmp {target:#x}'
            consumed = 5

        # XOR reg, reg
        elif b == 0x33 and pos + 1 < len(code):
            modrm = code[pos + 1]
            mod = (modrm >> 6) & 3
            reg = ((modrm >> 3) & 7) | (4 if has_rex_r else 0)
            rm = (modrm & 7) | (1 if has_rex_b else 0)
            rset = regs64 if has_rex_w else regs32
            if mod == 3:
                desc = f'xor {rset[reg]}, {rset[rm]}'
                consumed = 2

        # PUSH/POP
        elif 0x50 <= b <= 0x5F:
            reg = (b & 7) | (1 if has_rex_b else 0)
            op = 'push' if b < 0x58 else 'pop'
            desc = f'{op} {regs64[reg]}'
            consumed = 1

        if consumed > 0:
            pos += consumed
            lines.append((addr, code[start:pos], desc))
        else:
            # Unknown, skip 1 byte after opcode
            size = 2 if has_rex else 1
            # Try to figure out instruction length from ModRM if present
            if pos + 1 < len(code):
                modrm = code[pos + 1] if pos + 1 < len(code) else 0
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                size = 2
                if mod == 1: size += 1
                elif mod == 2: size += 4
                if rm == 4 and mod != 3: size += 1  # SIB
            pos += size
            raw_hex = ' '.join(f'{x:02x}' for x in code[start:pos])
            lines.append((addr, code[start:pos], f'??? (opcode {b:#04x}) {raw_hex}'))

        count += 1

    return lines


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")

    getters = [
        ('Priority_getter2', 0x7de230, 'i16, byte_off=0x910, cs=13'),
        ('Duration_getter2', 0x7ddfb0, 'i32, byte_off=0x740, cs=15'),
        ('Start_getter2', 0x7de2d0, 'date32, byte_off=0x1a0, cs=20'),
        ('Finish_getter2', 0x7de090, 'date32, byte_off=0x1a0, cs=21'),
        ('ConstraintType_getter2', 0x7ddec0, 'i16, byte_off=0x730, cs=7'),
        ('Created_getter2', 0x7ddf50, 'date32, byte_off=0x7c4, cs=31'),
        ('DurationUnits_getter2', 0x7de010, 'i16, byte_off=0x74a, cs=45'),
        ('Bool_getter2', 0x1ecb50, 'bool, no byte_off'),
    ]

    for name, rva, desc in getters:
        func_addr = IMAGE_BASE + rva
        code = read_mem(handle, func_addr, 128)
        if not code:
            print(f"\n{'='*80}")
            print(f"FAILED to read {name} at {func_addr:#x} (RVA {rva:#x})")
            continue

        print(f"\n{'='*80}")
        print(f"{name} at RVA {rva:#x} ({func_addr:#x}) — {desc}")
        print(f"{'='*80}")

        # Raw hex dump
        print("  Raw bytes:")
        for i in range(0, min(64, len(code)), 16):
            hex_str = ' '.join(f'{b:02x}' for b in code[i:i+16])
            print(f"    +{i:3d}: {hex_str}")

        # Disassemble
        print("  Disassembly:")
        lines = disasm_simple(code, func_addr)
        for addr, raw, desc in lines:
            hex_str = ' '.join(f'{b:02x}' for b in raw)
            rva_off = addr - IMAGE_BASE
            print(f"    {rva_off:#010x}: {hex_str:<30s} {desc}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
