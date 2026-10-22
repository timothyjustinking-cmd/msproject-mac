#!/usr/bin/env python3
"""Phase 2d: Full dump of vfunc[87] to trace the call to vfunc[158].

Dumps the complete function and manually finds all vtable calls,
parameter setup, and control flow leading to vfunc[158].
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
        return None
    return buf.raw[:br.value]

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
        vf87_rva = 0x231f00
        vf87_va = mod_base + vf87_rva

        # Read a big chunk (8KB) of vfunc[87]
        code = rpm(handle, vf87_va, 8192)
        if not code:
            print("ERROR: Cannot read vfunc[87]"); sys.exit(1)

        # =====================================================================
        # Find the first call to vtable[158] (0x4F0)
        # =====================================================================
        # From earlier: 0x4F0 found at RVA 0x231fff
        # The bytes are: "48 8B CF 48 8B 80 F0 04 00 00 FF 15 xx xx xx xx"
        #
        # Let me dump bytes 0xE0 to 0x120 (around offset 0xFF)

        print(f"\n{'='*80}")
        print(f"VFUNC[87] AROUND FIRST VTABLE[158] CALL (offsets 0xC0-0x140)")
        print(f"{'='*80}")

        for j in range(0xC0, min(0x140, len(code)), 16):
            row = code[j:j+16]
            rva = vf87_rva + j
            hex_str = ' '.join(f'{b:02x}' for b in row)
            print(f"  +0x{j:03x} (RVA 0x{rva:06x}): {hex_str}")

        # =====================================================================
        # Now let's look at the full flow from vtable[100] call to vtable[158]
        # =====================================================================
        # From prologue analysis:
        # RSI = this (TTaskTable*)
        # R13D = EDX (param2)
        # R14D = R8D (param3)
        # RBX = R9 (param4)
        # RAX loaded vtable[100] pointer

        # Find the CALL to vtable[100] - look for FF 15 (CALL [RIP+disp])
        # after loading vtable[100] at +0x30
        print(f"\n{'='*80}")
        print(f"SEARCHING FOR ALL 'FF 15' INDIRECT CALLS")
        print(f"{'='*80}")

        for i in range(len(code) - 6):
            if code[i] == 0xFF and code[i+1] == 0x15:
                disp = struct.unpack_from('<i', code, i+2)[0]
                target_rva = vf87_rva + i + 6 + disp
                print(f"  +0x{i:03x} (RVA 0x{vf87_rva+i:06x}): CALL [RIP+0x{disp:x}] -> RVA 0x{target_rva:x}")
                # Show setup bytes (24 bytes before)
                pre_start = max(0, i - 24)
                pre = code[pre_start:i]
                hex_str = ' '.join(f'{b:02x}' for b in pre)
                print(f"    Setup: {hex_str}")

        # =====================================================================
        # Full dump: prologue through first 0x200 bytes
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"FULL DUMP: FIRST 0x200 BYTES OF VFUNC[87]")
        print(f"{'='*80}")

        for j in range(0, min(0x200, len(code)), 16):
            row = code[j:j+16]
            rva = vf87_rva + j
            hex_str = ' '.join(f'{b:02x}' for b in row)
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)

            # Annotate key known instructions
            note = ""
            if j <= 0x20 <= j + 16:
                note = "  ; prologue: MOV RAX,[RCX]; MOV RBX,R9; MOV R14D,R8D"
            if j <= 0x30 <= j + 16:
                note = "  ; MOV R13D,EDX; MOV RSI,RCX; MOV RAX,[RAX+0x320]=vtable[100]"

            print(f"  +0x{j:03x} (RVA 0x{rva:06x}): {hex_str:<48s} {ascii_str}{note}")

        # =====================================================================
        # Find all places where RAX is loaded from vtable
        # Pattern: 48 8B 80 xx xx xx xx (MOV RAX, [RAX+disp32])
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"ALL VTABLE LOADS (MOV RAX, [RAX+disp32])")
        print(f"{'='*80}")

        for i in range(len(code) - 7):
            if code[i] == 0x48 and code[i+1] == 0x8B and code[i+2] == 0x80:
                disp = struct.unpack_from('<i', code, i+3)[0]
                slot = disp // 8
                rva = vf87_rva + i
                print(f"  +0x{i:03x} (RVA 0x{rva:06x}): MOV RAX, [RAX+0x{disp:x}] = vtable[{slot}]")

        # Also look for MOV RAX, [reg] (load vtable from this)
        # Pattern: 48 8B 01 = MOV RAX, [RCX]
        #          48 8B 06 = MOV RAX, [RSI]  (RSI = saved this)
        print(f"\n{'='*80}")
        print(f"ALL VTABLE BASE LOADS (MOV RAX, [reg])")
        print(f"{'='*80}")

        for i in range(len(code) - 3):
            if code[i] == 0x48 and code[i+1] == 0x8B:
                modrm = code[i+2]
                mod = (modrm >> 6) & 3
                reg = (modrm >> 3) & 7  # dest
                rm = modrm & 7  # source
                if mod == 0 and reg == 0 and rm in (1, 6, 7):  # MOV RAX, [RCX/RSI/RDI]
                    regs = {1: 'RCX', 6: 'RSI', 7: 'RDI'}
                    rva = vf87_rva + i
                    print(f"  +0x{i:03x} (RVA 0x{rva:06x}): MOV RAX, [{regs[rm]}]")

        # =====================================================================
        # Look for the vtable[158] call sequence in detail
        # At offset ~0xFF: 48 8B 80 F0 04 00 00 = MOV RAX, [RAX+0x4F0]
        # Need to find what sets up RCX, RDX, R8, R9 before the CALL
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"DETAIL: BYTES 0x90 TO 0x120 (around vtable[158] call)")
        print(f"{'='*80}")

        # Manual decode
        decodes = []

        # Let me find the exact location of 0x4F0
        for i in range(0x80, 0x120):
            if i + 7 <= len(code):
                if code[i] == 0x48 and code[i+1] == 0x8B and code[i+2] == 0x80:
                    disp = struct.unpack_from('<i', code, i+3)[0]
                    if disp == 0x4F0:
                        print(f"\n  vtable[158] load at +0x{i:03x}")
                        # Dump 40 bytes before and 30 bytes after
                        for j in range(max(0, i-40), min(i+30, len(code)), 16):
                            row = code[j:min(j+16, len(code))]
                            hex_str = ' '.join(f'{b:02x}' for b in row)
                            marker = " <<<" if j <= i < j + 16 else ""
                            print(f"    +0x{j:03x}: {hex_str}{marker}")

        # =====================================================================
        # Also dump around the SECOND 0x4F0 reference
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"DETAIL: AROUND SECOND VTABLE[158] REFERENCE")
        print(f"{'='*80}")

        found_second = False
        for i in range(0x120, len(code) - 7):
            if code[i] == 0x48 and code[i+1] == 0x8B and code[i+2] == 0x80:
                disp = struct.unpack_from('<i', code, i+3)[0]
                if disp == 0x4F0:
                    print(f"\n  vtable[158] load at +0x{i:03x}")
                    for j in range(max(0, i-48), min(i+48, len(code)), 16):
                        row = code[j:min(j+16, len(code))]
                        hex_str = ' '.join(f'{b:02x}' for b in row)
                        marker = " <<<" if j <= i < j + 16 else ""
                        print(f"    +0x{j:03x}: {hex_str}{marker}")
                    found_second = True

        if not found_second:
            print("  No second reference found with MOV RAX,[RAX+0x4F0] pattern")
            # Search broader
            for i in range(0x120, min(len(code) - 4, 0x800)):
                if struct.unpack_from('<I', code, i)[0] == 0x4F0:
                    pre = code[max(0,i-8):i]
                    post = code[i:min(i+8, len(code))]
                    pre_hex = ' '.join(f'{b:02x}' for b in pre)
                    post_hex = ' '.join(f'{b:02x}' for b in post)
                    print(f"  0x4F0 at +0x{i:03x}: pre=[{pre_hex}] post=[{post_hex}]")

        # =====================================================================
        # Specifically look for the instruction BEFORE vtable[158] call
        # that loads the vtable: MOV RAX, [RSI] or MOV RAX, [RCX]
        # This tells us what object vfunc[158] is called on
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"VTABLE LOADS NEAR VTABLE[158] CALLS")
        print(f"{'='*80}")

        # For each vtable[158] reference, find the nearest prior MOV RAX,[reg]
        for i in range(len(code) - 7):
            if code[i] == 0x48 and code[i+1] == 0x8B and code[i+2] == 0x80:
                disp = struct.unpack_from('<i', code, i+3)[0]
                if disp == 0x4F0:
                    # Search backwards for MOV RAX, [reg]
                    for j in range(i-1, max(0, i-30), -1):
                        if code[j] == 0x48 and code[j+1] == 0x8B:
                            modrm = code[j+2]
                            mod = (modrm >> 6) & 3
                            reg = (modrm >> 3) & 7
                            rm = modrm & 7
                            if mod == 0 and reg == 0:
                                regs = {0:'RAX',1:'RCX',2:'RDX',3:'RBX',5:'RBP',6:'RSI',7:'RDI'}
                                if rm in regs:
                                    print(f"  vtable[158] at +0x{i:03x}: vtable loaded from [{regs[rm]}] at +0x{j:03x}")
                                    # And what was in that register?
                                    break

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
