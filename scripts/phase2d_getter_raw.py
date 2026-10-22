#!/usr/bin/env python3
"""Dump raw getter2 function bytes and manually trace the key instructions.
Focus on finding: what does the CALL return, and how is byte_offset used.

For simple getters (Duration, Created, ConstraintType), the pattern should be:
  CALL sub_xxx(container) -> RAX = record_ptr
  MOV reg, [RAX + byte_offset]  (read field)

For complex getters (Priority), there may be extra indirection.
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
    if not hProcess: return None
    hMods = (ctypes.c_void_p * 1024)()
    cbNeeded = ctypes.c_ulong()
    if not psapi.EnumProcessModulesEx(hProcess, ctypes.byref(hMods), ctypes.sizeof(hMods),
                                       ctypes.byref(cbNeeded), 0x03):
        CloseHandle(hProcess); return None
    base = hMods[0]
    CloseHandle(hProcess)
    return base

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]


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
        ('Duration_getter2', 0x7ddfb0, 0x740),
        ('ConstraintType_getter2', 0x7ddec0, 0x730),
        ('Created_getter2', 0x7ddf50, 0x7c4),
        ('DurationUnits_getter2', 0x7de010, 0x74a),
        ('Priority_getter2', 0x7de230, 0x910),
        ('Start_getter2', 0x7de2d0, 0x1a0),
    ]

    for name, rva, byte_off in getters:
        func_addr = IMAGE_BASE + rva
        code = read_mem(handle, func_addr, 256)
        if not code:
            continue

        print(f"\n{'='*80}")
        print(f"{name} at RVA {rva:#x}, byte_off={byte_off:#x}")
        print(f"{'='*80}")

        # Find the byte_offset value in the instruction stream
        # It should appear as a 32-bit displacement in a MOV or MOVZX instruction
        byte_off_le = struct.pack('<I', byte_off)

        positions = []
        for i in range(len(code) - 4):
            if code[i:i+4] == byte_off_le:
                positions.append(i)

        if positions:
            for pos in positions:
                # Show context: 10 bytes before and 10 after
                start = max(0, pos - 10)
                end = min(len(code), pos + 14)
                ctx = code[start:end]
                hex_str = ' '.join(f'{b:02x}' for b in ctx)
                # Mark the byte_offset bytes
                mark_start = pos - start
                marks = '   ' * mark_start + '^^^^^^^^^^^' + '   ' * (end - pos - 4)
                print(f"  byte_off at func+{pos}:")
                print(f"    bytes: {hex_str}")
                print(f"    marks: {marks}")

                # Decode the instruction containing byte_offset
                # Look at the 1-3 bytes before the displacement
                opcode_start = pos - 3  # typical for REX+opcode+ModRM
                if opcode_start >= 0:
                    pre = code[opcode_start:pos]
                    # Check for common patterns
                    if len(pre) >= 3:
                        b0, b1, b2 = pre[0], pre[1], pre[2]
                        # 48 8b 80 = MOV RAX, [RAX+disp32]
                        # 8b 80 = MOV EAX, [RAX+disp32]
                        # 0f b7 80 = MOVZX EAX, WORD [RAX+disp32]
                        # 48 8b 81 = MOV RAX, [RCX+disp32]
                        # 0f b7 88 = MOVZX ECX, WORD [RAX+disp32]
                        regs = ['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi']
                        if b1 == 0x8b and (b2 & 0xC7) in (0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87):
                            reg = (b2 >> 3) & 7
                            rm = b2 & 7
                            rex_w = b0 & 0x08 if 0x40 <= b0 <= 0x4f else 0
                            rw = 'r' if rex_w else 'e'
                            print(f"    => MOV {rw}{regs[reg][1:]}, [{regs[rm]}+{byte_off:#x}]")
                        elif b0 == 0x0f and b1 == 0xb7 and (b2 & 0xC0) == 0x80:
                            reg = (b2 >> 3) & 7
                            rm = b2 & 7
                            print(f"    => MOVZX e{regs[reg][1:]}, WORD [{regs[rm]}+{byte_off:#x}]")
                        else:
                            print(f"    => pre-bytes: {' '.join(f'{x:02x}' for x in pre)}")

        else:
            print(f"  byte_off {byte_off:#x} NOT found in instruction stream!")
            # Maybe the byte_offset is shifted or used differently
            # Try searching for byte_off as i16 too
            byte_off_16 = struct.pack('<H', byte_off & 0xFFFF)
            for i in range(len(code) - 2):
                if code[i:i+2] == byte_off_16:
                    ctx = code[max(0,i-5):min(len(code),i+7)]
                    hex_str = ' '.join(f'{b:02x}' for b in ctx)
                    print(f"  Found as i16 at func+{i}: {hex_str}")

        # Now dump the full function up to RET
        print(f"\n  Full hex dump (first 128 bytes):")
        for i in range(0, min(128, len(code)), 16):
            hex_str = ' '.join(f'{b:02x}' for b in code[i:i+16])
            # Annotate interesting patterns
            ann = ""
            for j in range(i, min(i+16, len(code))):
                if j in positions:
                    ann += f" [byte_off@+{j}]"
                if code[j] == 0xC3 and j > 20:
                    ann += f" [RET@+{j}]"
            print(f"    +{i:3d}: {hex_str}{ann}")

    # Also find the CALL target — it's the same in all getters
    # Look at Duration getter for the CALL displacement
    dur_code = read_mem(handle, IMAGE_BASE + 0x7ddfb0, 128)
    if dur_code:
        print(f"\n{'='*80}")
        print(f"CALL TARGET ANALYSIS (from Duration getter)")
        print(f"{'='*80}")
        # Find E8 (CALL) instruction - should be around offset 45
        for i in range(35, 55):
            if dur_code[i] == 0xE8:
                disp = struct.unpack_from('<i', dur_code, i+1)[0]
                call_addr = IMAGE_BASE + 0x7ddfb0 + i + 5 + disp
                call_rva = call_addr - IMAGE_BASE
                print(f"  CALL at func+{i}: E8 disp={disp:#x}")
                print(f"  Target: {call_addr:#x} (RVA {call_rva:#x})")
                # Read first 64 bytes of the target
                target_code = read_mem(handle, call_addr, 64)
                if target_code:
                    print(f"  Target first 64 bytes:")
                    for j in range(0, min(64, len(target_code)), 16):
                        hex_str = ' '.join(f'{b:02x}' for b in target_code[j:j+16])
                        print(f"    +{j:3d}: {hex_str}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
