#!/usr/bin/env python3
"""Phase 2d: Dump and analyze the primary vtable[158] at RVA 0x239380.

This is THE field read dispatcher in TBkndPropContainer.
Called from vfunc[87] with:
  - RCX = prop container +0x1E8 sub-object
  - EDX = field_id or slot info
  - R8 = output buffer
  - R9D = output buffer size

It dispatches based on storage_class from the PropInfoData cache entry.
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
        # The primary vtable[158] dispatcher
        vt158_rva = 0x239380

        print(f"\n{'='*80}")
        print(f"PRIMARY FIELD DISPATCHER: vtable[158] at RVA 0x{vt158_rva:x}")
        print(f"{'='*80}")

        # Read a large chunk - this is likely a big function with switch dispatch
        code = rpm(handle, mod_base + vt158_rva, 4096)
        if not code:
            print("Cannot read function!")
            return

        # Find function end (look for RET after reasonable size)
        # This could be very large with switch/case for 16 storage classes
        func_end = len(code)
        # Find the first RET that's after at least 100 bytes
        for i in range(100, len(code)):
            if code[i] == 0xC3 and code[i-1] not in (0x0F, 0x66):
                # Check if this looks like a real function end (followed by padding or next function)
                if i + 1 < len(code) and (code[i+1] in (0x90, 0xCC, 0x40, 0x48, 0x41)):
                    func_end = i + 1
                    break

        print(f"Function size: ~{func_end} bytes")

        # Full hex dump
        print(f"\nFull dump (up to 2048 bytes):")
        for j in range(0, min(func_end + 16, 2048), 16):
            if j >= len(code):
                break
            row = code[j:min(j+16, len(code))]
            hex_str = ' '.join(f'{b:02x}' for b in row)
            marker = ""
            for k in range(min(16, len(row))):
                if j+k < func_end and code[j+k] == 0xC3:
                    if j+k == 0 or code[j+k-1] not in (0x0F, 0x66):
                        marker = " <RET"
            print(f"    +0x{j:03x} (0x{vt158_rva+j:06x}): {hex_str:<48s}{marker}")

        # Find comparison/test/CMP instructions (switch dispatch on storage_class)
        print(f"\nCMP/TEST/switch patterns:")
        for i in range(min(func_end, len(code)) - 4):
            # CMP reg, imm8: 83 F8+reg imm8
            if code[i] == 0x83 and (code[i+1] & 0xF8) == 0xF8:
                reg = code[i+1] & 7
                val = code[i+2]
                regs = ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI']
                print(f"    +0x{i:03x}: CMP {regs[reg]}, 0x{val:02x} ({val})")

            # CMP reg, imm32: 81 F8+reg imm32 or 3D imm32 (CMP EAX, imm32)
            if code[i] == 0x3D and i + 4 < len(code):
                val = struct.unpack_from('<i', code, i+1)[0]
                if 0 <= val < 0x10000:
                    print(f"    +0x{i:03x}: CMP EAX, 0x{val:x} ({val})")

            # JA/JB/JE/JNE: conditional branches (potential switch cases)
            if code[i] == 0x0F and i + 5 < len(code):
                if code[i+1] in (0x84, 0x85, 0x87, 0x82, 0x83, 0x86, 0x8F, 0x8E, 0x8D, 0x8C):
                    disp = struct.unpack_from('<i', code, i+2)[0]
                    target = vt158_rva + i + 6 + disp
                    ops = {0x84:'JE', 0x85:'JNE', 0x87:'JA', 0x82:'JB', 0x83:'JAE',
                           0x86:'JBE', 0x8F:'JG', 0x8E:'JLE', 0x8D:'JGE', 0x8C:'JL'}
                    print(f"    +0x{i:03x}: {ops.get(code[i+1],'?')} 0x{target:x}")

        # Find AND instructions (mask for field_id ranges like 0x3F000000)
        print(f"\nAND instructions:")
        for i in range(min(func_end, len(code)) - 5):
            # AND EAX, imm32: 25 xx xx xx xx
            if code[i] == 0x25:
                val = struct.unpack_from('<I', code, i+1)[0]
                if val > 0xFF:
                    print(f"    +0x{i:03x}: AND EAX, 0x{val:08x}")

            # AND reg, imm32: 81 E0+reg imm32
            if code[i] == 0x81 and (code[i+1] & 0xF8) == 0xE0:
                reg = code[i+1] & 7
                val = struct.unpack_from('<I', code, i+2)[0]
                regs = ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI']
                if val > 0xFF:
                    print(f"    +0x{i:03x}: AND {regs[reg]}, 0x{val:08x}")

        # Find LEA instructions for vtable/table loads
        print(f"\nLEA (RIP-relative) targets:")
        for i in range(min(func_end, len(code)) - 7):
            prefix = 0
            pos = i
            if 0x40 <= code[pos] <= 0x4F:
                prefix = code[pos]
                pos += 1

            if pos + 6 < len(code) and code[pos] == 0x8D:
                modrm = code[pos + 1]
                if (modrm & 0xC7) == 0x05:
                    reg = (modrm >> 3) & 7
                    if prefix and (prefix & 4):
                        reg += 8
                    disp = struct.unpack_from('<i', code, pos + 2)[0]
                    if prefix:
                        target_rva = vt158_rva + i + 7 + disp
                    else:
                        target_rva = vt158_rva + pos + 6 + disp

                    regs = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI',
                            'R8','R9','R10','R11','R12','R13','R14','R15']
                    print(f"    +0x{i:03x}: LEA {regs[reg]}, RVA 0x{target_rva:x}")

        # Find CALL targets
        print(f"\nCALL targets (first 50):")
        call_count = 0
        for i in range(min(func_end, len(code)) - 5):
            if code[i] == 0xE8:
                disp = struct.unpack_from('<i', code, i+1)[0]
                target = vt158_rva + i + 5 + disp
                if 0x1000 < target < 0x2000000:
                    print(f"    +0x{i:03x}: CALL RVA 0x{target:x}")
                    call_count += 1
                    if call_count >= 50:
                        print(f"    ... (truncated)")
                        break

        # Find indirect calls [REG+disp] (vtable dispatches)
        print(f"\nIndirect CALL [REG+disp]:")
        for i in range(min(func_end, len(code)) - 6):
            if code[i] == 0xFF:
                modrm = code[i+1]
                mod = (modrm >> 6) & 3
                reg = (modrm >> 3) & 7
                rm = modrm & 7

                if reg == 2 and mod == 2 and rm != 4:
                    disp = struct.unpack_from('<i', code, i+2)[0]
                    slot = disp // 8
                    regs = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI']
                    print(f"    +0x{i:03x}: CALL [{regs[rm]}+0x{disp:x}] (slot {slot})")

    finally:
        kernel32.CloseHandle(handle)


if __name__ == '__main__':
    main()
