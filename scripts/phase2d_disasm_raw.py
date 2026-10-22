#!/usr/bin/env python3
"""Phase 2d: Raw hex dump of vfunc[158] with manual decode around SHL site.

Dumps raw bytes and searches for key patterns to understand addressing.
"""

import ctypes
import ctypes.wintypes as wt
import struct
import sys

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x00000002
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)

class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [
        ('dwSize', wt.DWORD), ('cntUsage', wt.DWORD),
        ('th32ProcessID', wt.DWORD),
        ('th32DefaultHeapID', ctypes.POINTER(ctypes.c_ulong)),
        ('th32ModuleID', wt.DWORD), ('cntThreads', wt.DWORD),
        ('th32ParentProcessID', wt.DWORD),
        ('pcPriClassBase', ctypes.c_long), ('dwFlags', wt.DWORD),
        ('szExeFile', ctypes.c_char * 260),
    ]

class MODULEENTRY32(ctypes.Structure):
    _fields_ = [
        ('dwSize', wt.DWORD), ('th32ModuleID', wt.DWORD),
        ('th32ProcessID', wt.DWORD), ('GlblcntUsage', wt.DWORD),
        ('ProccntUsage', wt.DWORD),
        ('modBaseAddr', ctypes.POINTER(ctypes.c_byte)),
        ('modBaseSize', wt.DWORD), ('hModule', wt.HMODULE),
        ('szModule', ctypes.c_char * 256),
        ('szExePath', ctypes.c_char * 260),
    ]

def find_pid():
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32(); pe.dwSize = ctypes.sizeof(pe)
    pids = []
    if kernel32.Process32First(snap, ctypes.byref(pe)):
        while True:
            name = pe.szExeFile.decode('utf-8', errors='replace').lower()
            if 'winproj' in name:
                pids.append((pe.th32ProcessID, name))
            if not kernel32.Process32Next(snap, ctypes.byref(pe)):
                break
    kernel32.CloseHandle(snap)
    return pids

def get_base(pid):
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    me = MODULEENTRY32(); me.dwSize = ctypes.sizeof(me)
    base = None
    if kernel32.Module32First(snap, ctypes.byref(me)):
        while True:
            name = me.szModule.decode('utf-8', errors='replace').lower()
            if 'winproj' in name:
                base = ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value
                break
            if not kernel32.Module32Next(snap, ctypes.byref(me)):
                break
    kernel32.CloseHandle(snap)
    return base

def rpm(handle, address, size):
    buf = ctypes.create_string_buffer(size)
    br = ctypes.c_size_t(0)
    ok = kernel32.ReadProcessMemory(handle, ctypes.c_void_p(address), buf, size, ctypes.byref(br))
    if not ok:
        raise OSError(f"RPM fail 0x{address:x}")
    return buf.raw[:br.value]

def hex_dump(data, base_va, mod_base, highlight_off=None):
    """Pretty hex dump with 16 bytes per line."""
    for off in range(0, len(data), 16):
        row = data[off:off+16]
        rva = (base_va + off) - mod_base
        hex_str = ' '.join(f'{b:02x}' for b in row)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
        marker = " <<<" if highlight_off is not None and off <= highlight_off < off + 16 else ""
        print(f"  0x{rva:06x}: {hex_str:<48s} {ascii_str}{marker}")

def main():
    pids = find_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    pid, _ = pids[0]
    mod_base = get_base(pid)
    print(f"Module base: 0x{mod_base:x}")

    handle = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        print("ERROR: OpenProcess failed"); sys.exit(1)

    try:
        func_rva = 0x239380
        func_va = mod_base + func_rva
        code = rpm(handle, func_va, 3000)

        # =================================================================
        # 1. Function prologue (first 128 bytes)
        # =================================================================
        print(f"\n{'='*80}")
        print(f"1. FUNCTION PROLOGUE (RVA 0x{func_rva:x})")
        print("-" * 60)
        hex_dump(code[:128], func_va, mod_base)

        # =================================================================
        # 2. Around the SHL instruction (offset 0x1D9)
        # =================================================================
        shl_off = 0x1D9
        print(f"\n{'='*80}")
        print(f"2. AROUND SHL RDX, 6 (offset +0x{shl_off:x}, RVA 0x{func_rva + shl_off:x})")
        print("-" * 60)

        # Show 80 bytes before and 80 bytes after
        start = max(0, shl_off - 80)
        end = min(len(code), shl_off + 80)
        hex_dump(code[start:end], func_va + start, mod_base, shl_off - start)

        # =================================================================
        # 3. Manual decode of key instructions around SHL
        # =================================================================
        print(f"\n{'='*80}")
        print(f"3. MANUAL DECODE (around SHL)")
        print("-" * 60)

        # The context bytes around SHL:
        # Before SHL: ...48 8B D1 66 44 89 44 24 64 48 C1 E2 06...
        # After SHL:  ...48 89 55 E8 42 8B 7C 2A 0C...

        # Let me extract and decode manually starting from a safe point
        # Let's look at the 60 bytes ending at the SHL
        pre_shl = code[shl_off - 30 : shl_off + 50]
        print(f"  Raw bytes (SHL at byte 30):")
        for i in range(0, len(pre_shl), 16):
            row = pre_shl[i:i+16]
            off_in_func = (shl_off - 30) + i
            rva = func_rva + off_in_func
            hex_str = ' '.join(f'{b:02x}' for b in row)
            marker = " <<<SHL" if i <= 30 < i + 16 else ""
            print(f"    +0x{off_in_func:03x} (RVA 0x{rva:x}): {hex_str}{marker}")

        print(f"\n  Manual decode:")
        # Decode the bytes manually
        instructions = [
            # pre-SHL context
            (0x1BF, 3, "48 8B D1", "MOV RDX, RCX  ; RDX = binary_search_result (index)"),
            (0x1C2, 6, "66 44 89 44 24 64", "MOV WORD [RSP+0x64], R8W  ; store 16-bit param"),
            (0x1C8, 1, "66", "data16 prefix? or combined with next"),
        ]

        # Actually let me just decode the key instruction sequence
        print(f"  Starting from ~RVA 0x{func_rva + shl_off - 20:x}:")

        # I'll manually trace through the hex near SHL
        # From context: ...488bd16644894424 64 48c1e206 488955e8 428b7c2a0c 483bc7 0f82860e0000 428b742a...

        manual = [
            (shl_off - 8,  "48 8B D1",             "MOV RDX, RCX  ; copy search index to RDX"),
            (shl_off - 5,  "66 44 89 44 24 64",    "MOV [RSP+0x64], R8W  ; store param"),
            (shl_off + 1,  "48 C1 E2 06",          "SHL RDX, 6  ; RDX = index * 64 (0x40)  <<< KEY"),
            (shl_off + 5,  "48 89 55 E8",          "MOV [RBP-0x18], RDX  ; save index*64 on stack"),
            (shl_off + 9,  "42 8B 7C 2A 0C",       "MOV EDI, [RDX+R13+0x0C]  ; read type_code from cache"),
            (shl_off + 14, "48 3B C7",             "CMP RAX, RDI  ; compare against expected type?"),
            (shl_off + 17, "0F 82 86 0E 00 00",    "JB +0xE86  ; jump if type mismatch"),
        ]

        for off, hexs, desc in manual:
            rva = func_rva + off
            print(f"    RVA 0x{rva:06x}: {hexs:<28s} {desc}")

        # =================================================================
        # 4. Find where R13 is loaded (search backwards from SHL)
        # =================================================================
        print(f"\n{'='*80}")
        print(f"4. SEARCHING FOR R13 INITIALIZATION")
        print("-" * 60)

        # R13 is typically saved/restored in prologue. Look for:
        # 4C 8B 2D = MOV R13, [RIP+disp32]  (4C = REX.WR, 8B = MOV, 2D = modrm for R13/[RIP+disp])
        # 4C 8D 2D = LEA R13, [RIP+disp32]  (4C = REX.WR, 8D = LEA, 2D = modrm for R13/[RIP+disp])
        # 49 BD = MOV R13, imm64

        for pattern_name, pattern_bytes in [
            ("MOV R13, [RIP+disp32]", bytes([0x4C, 0x8B, 0x2D])),
            ("LEA R13, [RIP+disp32]", bytes([0x4C, 0x8D, 0x2D])),
            ("MOV R13, [reg+disp]",   bytes([0x4C, 0x8B])),  # various modrm
            ("LEA R13, [reg+disp]",   bytes([0x4C, 0x8D])),  # various modrm
        ]:
            for i in range(min(shl_off, len(code) - 7)):
                if code[i:i+len(pattern_bytes)] == pattern_bytes:
                    # For RIP-relative: next byte is displacement
                    rva = func_rva + i
                    if len(pattern_bytes) == 3 and i + 7 <= len(code):
                        disp = struct.unpack_from('<i', code, i+3)[0]
                        target_va = func_va + i + 7 + disp
                        target_rva = target_va - mod_base
                        print(f"  {pattern_name} at RVA 0x{rva:x}")
                        print(f"    disp=0x{disp:x}, target VA=0x{target_va:x} (RVA 0x{target_rva:x})")
                    elif len(pattern_bytes) == 2:
                        modrm = code[i+2]
                        reg = (modrm >> 3) & 7  # should be 5 for R13 (5+8=13)
                        if reg == 5:  # R13
                            mod = (modrm >> 6) & 3
                            rm = modrm & 7
                            context = code[i:min(i+12, len(code))].hex()
                            print(f"  {pattern_name} at RVA 0x{rva:x}")
                            print(f"    modrm=0x{modrm:02x} mod={mod} rm={rm}")
                            print(f"    bytes: {context}")

        # =================================================================
        # 5. Look for IMUL or other stride-related arithmetic
        # =================================================================
        print(f"\n{'='*80}")
        print(f"5. SEARCH FOR 0xA8-RELATED OPERATIONS")
        print("-" * 60)

        # Search for 0xA8 as a byte in the code
        for i in range(len(code) - 1):
            if code[i] == 0xA8:
                rva = func_rva + i
                context = code[max(0,i-4):min(i+8,len(code))].hex()
                # Check if it's in an arithmetic instruction
                if i > 0 and code[i-1] in (0x69, 0x6B):  # IMUL
                    print(f"  IMUL with 0xA8 at RVA 0x{rva:x}: {context}")
                elif i > 1 and code[i-2] in (0x69, 0x6B):
                    print(f"  IMUL with 0xA8 at RVA 0x{rva:x}: {context}")

        # Search for the value 168 = 0xA8 in 32-bit immediates
        for i in range(len(code) - 4):
            val = struct.unpack_from('<I', code, i)[0]
            if val == 0xA8:
                rva = func_rva + i
                # Check context - is this part of an instruction?
                pre = code[max(0,i-3):i].hex()
                post = code[i:min(i+6,len(code))].hex()
                print(f"  u32 0xA8 at RVA 0x{rva:x}: pre={pre} val+post={post}")

        # =================================================================
        # 6. After SHL: trace what happens to RDX next (index*64)
        # =================================================================
        print(f"\n{'='*80}")
        print(f"6. TRACING RDX AFTER SHL (next 300 bytes)")
        print("-" * 60)

        post_shl = code[shl_off + 4 : shl_off + 300]
        # Look for any instruction using RDX
        for i in range(len(post_shl) - 2):
            b = post_shl[i]
            # Check for REX prefix followed by opcode that uses ModRM
            if 0x40 <= b <= 0x4F:
                next_b = post_shl[i+1]
                if next_b in (0x8B, 0x89, 0x03, 0x2B, 0x8D, 0x0F):
                    if i + 2 < len(post_shl):
                        modrm = post_shl[i+2]
                        # Check if RDX is involved (reg=2 or rm=2)
                        reg = (modrm >> 3) & 7
                        rm = modrm & 7
                        if (reg == 2 or rm == 2) and (b & 1 or b & 4):
                            rva = func_rva + shl_off + 4 + i
                            context = post_shl[i:min(i+8,len(post_shl))].hex()
                            off_from_shl = i + 4
                            if off_from_shl < 40:  # only first few instructions
                                print(f"  +{off_from_shl}: RVA 0x{rva:x}: {context}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
