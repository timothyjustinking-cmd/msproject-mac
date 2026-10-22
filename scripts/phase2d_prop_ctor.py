#!/usr/bin/env python3
"""Phase 2d: Trace the prop container constructor to find its vtable.

From vfunc[100] at 0x231D90:
  - Allocates 0x3A0 bytes
  - LEA R8, [0xF8BDA0] (Task descriptor)
  - Calls constructor at RVA 0x1E8910
  - Stores result at TTaskTable+0x5E8

The constructor sets the vtable pointer at offset 0 of the new object.
We need to find that vtable to identify vtable[158] (the field read dispatcher).
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

def read_rtti_name(handle, mod_base, vtable_rva):
    vtable_va = mod_base + vtable_rva
    col_ptr_data = rpm(handle, vtable_va - 8, 8)
    if not col_ptr_data:
        return None
    col_ptr = struct.unpack_from('<Q', col_ptr_data, 0)[0]
    col_data = rpm(handle, col_ptr, 24)
    if not col_data:
        return None
    td_rva = struct.unpack_from('<I', col_data, 0x0C)[0]
    if td_rva == 0 or td_rva > 0x2000000:
        return None
    td_va = mod_base + td_rva
    name_data = rpm(handle, td_va + 0x10, 128)
    if not name_data:
        return None
    null_pos = name_data.find(b'\x00')
    if null_pos > 0:
        mangled = name_data[:null_pos].decode('ascii', errors='replace')
        if mangled.startswith('.?AV') and mangled.endswith('@@'):
            return mangled[4:-2]
        return mangled
    return None


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
        # =================================================================
        # PART 1: Read the constructor at 0x1E8910
        # =================================================================
        ctor_rva = 0x1E8910
        print(f"\n{'='*80}")
        print(f"PART 1: PROP CONTAINER CONSTRUCTOR (RVA 0x{ctor_rva:x})")
        print(f"{'='*80}")

        ctor_code = rpm(handle, mod_base + ctor_rva, 1024)
        if not ctor_code:
            print("Cannot read constructor!")
            return

        # Find function end
        func_end = len(ctor_code)
        for i in range(20, len(ctor_code)):  # skip prolog
            if ctor_code[i] == 0xC3 and ctor_code[i-1] not in (0x0F, 0x66):
                func_end = i + 1
                break

        print(f"  Function size: {func_end} bytes")
        print(f"  Full dump:")
        for j in range(0, min(func_end + 16, 512), 16):
            row = ctor_code[j:min(j+16, len(ctor_code))]
            hex_str = ' '.join(f'{b:02x}' for b in row)
            marker = ""
            for k in range(min(16, len(row))):
                if j+k < func_end and ctor_code[j+k] == 0xC3 and (j+k == 0 or ctor_code[j+k-1] not in (0x0F, 0x66)):
                    marker = " <RET"
            print(f"    +0x{j:03x}: {hex_str:<48s}{marker}")

        # Find all LEA RIP-relative instructions (vtable assignments)
        print(f"\n  LEA instructions:")
        for i in range(min(func_end, len(ctor_code)) - 7):
            prefix = 0
            pos = i
            if 0x40 <= ctor_code[pos] <= 0x4F:
                prefix = ctor_code[pos]
                pos += 1

            if pos + 6 < len(ctor_code) and ctor_code[pos] == 0x8D:
                modrm = ctor_code[pos + 1]
                if (modrm & 0xC7) == 0x05:  # RIP-relative
                    reg = (modrm >> 3) & 7
                    if prefix and (prefix & 4):
                        reg += 8
                    disp = struct.unpack_from('<i', ctor_code, pos + 2)[0]
                    if prefix:
                        target_rva = ctor_rva + i + 7 + disp
                    else:
                        target_rva = ctor_rva + pos + 6 + disp

                    regs = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI',
                            'R8','R9','R10','R11','R12','R13','R14','R15']

                    rtti = read_rtti_name(handle, mod_base, target_rva)
                    rtti_str = f" RTTI={rtti}" if rtti else ""
                    print(f"    +0x{i:03x}: LEA {regs[reg]}, [RIP+0x{disp:x}] = RVA 0x{target_rva:x}{rtti_str}")

        # Find all CALL instructions
        print(f"\n  CALL targets:")
        for i in range(min(func_end, len(ctor_code)) - 5):
            if ctor_code[i] == 0xE8:
                disp = struct.unpack_from('<i', ctor_code, i+1)[0]
                target = ctor_rva + i + 5 + disp
                if 0x1000 < target < 0x2000000:
                    print(f"    +0x{i:03x}: CALL RVA 0x{target:x}")

        # =================================================================
        # PART 2: Trace into sub-constructors to find vtable
        # =================================================================
        print(f"\n{'='*80}")
        print(f"PART 2: SUB-CONSTRUCTOR ANALYSIS")
        print(f"{'='*80}")

        # Read each called function's first 256 bytes to find vtable LEAs
        call_targets = []
        for i in range(min(func_end, len(ctor_code)) - 5):
            if ctor_code[i] == 0xE8:
                disp = struct.unpack_from('<i', ctor_code, i+1)[0]
                target = ctor_rva + i + 5 + disp
                if 0x1000 < target < 0x2000000:
                    call_targets.append(target)

        for ct_rva in call_targets:
            sub_code = rpm(handle, mod_base + ct_rva, 512)
            if not sub_code:
                continue

            # Find LEAs in the sub-function
            vtable_leas = []
            sub_end = min(400, len(sub_code))
            for k in range(20, sub_end):
                if sub_code[k] == 0xC3 and sub_code[k-1] not in (0x0F, 0x66):
                    sub_end = k + 1
                    break

            for i in range(min(sub_end, len(sub_code)) - 7):
                prefix = 0
                pos = i
                if 0x40 <= sub_code[pos] <= 0x4F:
                    prefix = sub_code[pos]
                    pos += 1

                if pos + 6 < len(sub_code) and sub_code[pos] == 0x8D:
                    modrm = sub_code[pos + 1]
                    if (modrm & 0xC7) == 0x05:
                        reg = (modrm >> 3) & 7
                        if prefix and (prefix & 4):
                            reg += 8
                        disp = struct.unpack_from('<i', sub_code, pos + 2)[0]
                        if prefix:
                            target_rva = ct_rva + i + 7 + disp
                        else:
                            target_rva = ct_rva + pos + 6 + disp

                        rtti = read_rtti_name(handle, mod_base, target_rva)
                        if rtti:
                            regs = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI',
                                    'R8','R9','R10','R11','R12','R13','R14','R15']
                            vtable_leas.append((i, regs[reg], target_rva, rtti))

            if vtable_leas:
                print(f"\n  Sub-function RVA 0x{ct_rva:x} (size ~{sub_end}):")
                for off, reg, target_rva, rtti in vtable_leas:
                    print(f"    +0x{off:03x}: LEA {reg}, RVA 0x{target_rva:x} -> {rtti}")

                    # For each vtable, check slot 158
                    vt158_data = rpm(handle, mod_base + target_rva + 158 * 8, 8)
                    if vt158_data:
                        vt158_va = struct.unpack_from('<Q', vt158_data, 0)[0]
                        vt158_rva = vt158_va - mod_base if vt158_va > mod_base else 0
                        if 0x1000 < vt158_rva < 0x1500000:
                            print(f"      vtable[158] -> RVA 0x{vt158_rva:x}")

                            # Dump vtable[158] function
                            vt158_code = rpm(handle, vt158_va, 128)
                            if vt158_code:
                                print(f"      First 64 bytes of vtable[158]:")
                                for j in range(0, 64, 16):
                                    row = vt158_code[j:min(j+16, len(vt158_code))]
                                    hex_str = ' '.join(f'{b:02x}' for b in row)
                                    print(f"        +0x{j:03x}: {hex_str}")

            # Also check sub-sub-constructors (one level deeper)
            sub_calls = []
            for i in range(min(sub_end, len(sub_code)) - 5):
                if sub_code[i] == 0xE8:
                    disp = struct.unpack_from('<i', sub_code, i+1)[0]
                    target = ct_rva + i + 5 + disp
                    if 0x1000 < target < 0x2000000 and target != ct_rva:
                        sub_calls.append(target)

            for sc_rva in sub_calls[:5]:
                ss_code = rpm(handle, mod_base + sc_rva, 512)
                if not ss_code:
                    continue

                ss_end = min(400, len(ss_code))
                for k in range(20, ss_end):
                    if ss_code[k] == 0xC3 and ss_code[k-1] not in (0x0F, 0x66):
                        ss_end = k + 1
                        break

                ss_vtables = []
                for i in range(min(ss_end, len(ss_code)) - 7):
                    prefix = 0
                    pos = i
                    if 0x40 <= ss_code[pos] <= 0x4F:
                        prefix = ss_code[pos]
                        pos += 1

                    if pos + 6 < len(ss_code) and ss_code[pos] == 0x8D:
                        modrm = ss_code[pos + 1]
                        if (modrm & 0xC7) == 0x05:
                            reg = (modrm >> 3) & 7
                            if prefix and (prefix & 4):
                                reg += 8
                            disp = struct.unpack_from('<i', ss_code, pos + 2)[0]
                            if prefix:
                                target_rva = sc_rva + i + 7 + disp
                            else:
                                target_rva = sc_rva + pos + 6 + disp

                            rtti = read_rtti_name(handle, mod_base, target_rva)
                            if rtti:
                                ss_vtables.append((i, target_rva, rtti))

                if ss_vtables:
                    print(f"\n    Sub-sub RVA 0x{sc_rva:x} (size ~{ss_end}):")
                    for off, target_rva, rtti in ss_vtables:
                        print(f"      +0x{off:03x}: RVA 0x{target_rva:x} -> {rtti}")

                        vt158_data = rpm(handle, mod_base + target_rva + 158 * 8, 8)
                        if vt158_data:
                            vt158_va = struct.unpack_from('<Q', vt158_data, 0)[0]
                            vt158_rva = vt158_va - mod_base if vt158_va > mod_base else 0
                            if 0x1000 < vt158_rva < 0x1500000:
                                print(f"        vtable[158] -> RVA 0x{vt158_rva:x}")

    finally:
        kernel32.CloseHandle(handle)


if __name__ == '__main__':
    main()
