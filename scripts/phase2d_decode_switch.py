#!/usr/bin/env python3
"""Phase 2d: Decode the vtable[158] switch/case dispatch table.

The function at RVA 0x239380 uses a compiler-generated switch at offset +0x59E:
  LEA R9, [RIP-relative] = jump table base
  MOVZX EAX, BYTE [R9+R8+offset1] = byte lookup (case->index)
  MOV ECX, [R9+RAX*4+offset2] = dword offset table (index->offset)
  ADD RCX, R9; JMP RCX = computed jump

We need to:
1. Read the byte lookup table to find all case values and their indices
2. Read the dword offset table to find the jump targets for each index
3. Dump each jump target to understand the read mechanism per storage class
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
        vt158_rva = 0x239380

        # Read the full function (up to 8KB to capture everything including jump tables)
        func_code = rpm(handle, mod_base + vt158_rva, 8192)
        if not func_code:
            print("Cannot read function!"); return

        print(f"\n{'='*80}")
        print(f"DECODING vtable[158] SWITCH DISPATCH")
        print(f"{'='*80}")

        # From the previous dump, the switch table pattern is at +0x595:
        # +0x595: CMP EAX, 0x48 (72) - range check
        # +0x598: JA default_case
        # +0x59E: LEA R9, [RIP+disp] - load jump table base
        # Then: MOVZX EAX, BYTE [R9+R8+offset1]
        # Then: MOV ECX, [R9+RAX*4+offset2]
        # Then: ADD RCX, R9; JMP RCX

        # First, let's find ALL switch dispatch patterns by looking for the
        # characteristic LEA + MOVZX + MOV + ADD + JMP sequence

        # Let's carefully decode the instructions around +0x580-0x5C0
        print(f"\nDetailed decode around switch dispatch (+0x580 to +0x620):")
        for j in range(0x580, min(0x620, len(func_code)), 16):
            row = func_code[j:min(j+16, len(func_code))]
            hex_str = ' '.join(f'{b:02x}' for b in row)
            print(f"  +0x{j:03x}: {hex_str}")

        # The key instruction sequence from the summary:
        # 4C 8D 0D xx xx xx xx    = LEA R9, [RIP+disp]
        # 43 0F B6 84 01 xx xx xx xx = MOVZX EAX, BYTE [R9+R8+offset]
        # 41 8B 8C 81 xx xx xx xx = MOV ECX, [R9+RAX*4+offset]
        # 49 03 C9                = ADD RCX, R9
        # FF E1                   = JMP RCX

        # Search for this pattern
        print(f"\nSearching for switch dispatch patterns...")

        for i in range(len(func_code) - 30):
            # LEA R9, [RIP+disp32]: 4C 8D 0D xx xx xx xx
            if (func_code[i] == 0x4C and func_code[i+1] == 0x8D and func_code[i+2] == 0x0D):
                lea_disp = struct.unpack_from('<i', func_code, i+3)[0]
                jump_base_rva = vt158_rva + i + 7 + lea_disp

                print(f"\n  Found LEA R9, [RIP+0x{lea_disp:x}] at +0x{i:03x}")
                print(f"    Jump table base RVA: 0x{jump_base_rva:x}")

                # Look ahead for MOVZX EAX, BYTE [R9+R8+disp32]: 43 0F B6 84 01 xx xx xx xx
                for j in range(i+7, min(i+30, len(func_code)-9)):
                    if (func_code[j] == 0x43 and func_code[j+1] == 0x0F and
                        func_code[j+2] == 0xB6 and func_code[j+3] == 0x84 and func_code[j+4] == 0x01):
                        byte_table_disp = struct.unpack_from('<I', func_code, j+5)[0]
                        byte_table_rva = jump_base_rva + byte_table_disp
                        print(f"    MOVZX EAX, BYTE [R9+R8+0x{byte_table_disp:x}] at +0x{j:03x}")
                        print(f"    Byte lookup table RVA: 0x{byte_table_rva:x}")

                        # Look for MOV ECX, [R9+RAX*4+disp32]: 41 8B 8C 81 xx xx xx xx
                        for k in range(j+9, min(j+25, len(func_code)-8)):
                            if (func_code[k] == 0x41 and func_code[k+1] == 0x8B and
                                func_code[k+2] == 0x8C and func_code[k+3] == 0x81):
                                dword_table_disp = struct.unpack_from('<I', func_code, k+4)[0]
                                dword_table_rva = jump_base_rva + dword_table_disp
                                print(f"    MOV ECX, [R9+RAX*4+0x{dword_table_disp:x}] at +0x{k:03x}")
                                print(f"    Dword offset table RVA: 0x{dword_table_rva:x}")

                                # Find the range check (CMP before the LEA)
                                max_case = None
                                for m in range(max(0, i-20), i):
                                    if func_code[m] == 0x83 and (func_code[m+1] & 0xF8) == 0xF8:
                                        max_case = func_code[m+2]
                                    elif func_code[m] == 0x3D:
                                        max_case = struct.unpack_from('<I', func_code, m+1)[0]

                                if max_case is None:
                                    max_case = 72  # from summary

                                print(f"    Max case value: {max_case}")

                                # Read the byte lookup table
                                byte_table = rpm(handle, mod_base + byte_table_rva, max_case + 1)
                                if byte_table:
                                    print(f"\n    Byte lookup table ({max_case+1} entries):")
                                    max_index = 0
                                    case_to_index = {}
                                    for ci in range(max_case + 1):
                                        idx = byte_table[ci]
                                        case_to_index[ci] = idx
                                        if idx > max_index:
                                            max_index = idx
                                        if ci < 30 or idx != byte_table[0]:
                                            print(f"      case {ci:3d} (0x{ci:02x}) -> index {idx}")

                                    print(f"\n    Max index: {max_index}")
                                    print(f"    Unique indices: {sorted(set(case_to_index.values()))}")

                                    # Read the dword offset table
                                    dword_table = rpm(handle, mod_base + dword_table_rva, (max_index + 1) * 4)
                                    if dword_table:
                                        print(f"\n    Dword offset table ({max_index+1} entries):")
                                        index_to_target = {}
                                        for idx_i in range(max_index + 1):
                                            offset_val = struct.unpack_from('<i', dword_table, idx_i * 4)[0]
                                            target_rva = jump_base_rva + offset_val
                                            index_to_target[idx_i] = target_rva
                                            print(f"      index {idx_i:3d} -> offset 0x{offset_val:08x} -> RVA 0x{target_rva:x}")

                                        # Map case values to targets
                                        print(f"\n    CASE VALUE -> TARGET MAPPING:")
                                        case_groups = {}
                                        for ci in range(max_case + 1):
                                            idx = case_to_index[ci]
                                            target = index_to_target.get(idx, 0)
                                            if target not in case_groups:
                                                case_groups[target] = []
                                            case_groups[target].append(ci)

                                        for target_rva_val in sorted(case_groups.keys()):
                                            cases = case_groups[target_rva_val]
                                            cases_str = ', '.join(f'{c}' for c in cases[:10])
                                            if len(cases) > 10:
                                                cases_str += f'... ({len(cases)} total)'
                                            print(f"      RVA 0x{target_rva_val:x}: cases [{cases_str}]")

                                            # Dump first 64 bytes of each target
                                            target_code = rpm(handle, mod_base + target_rva_val, 128)
                                            if target_code:
                                                print(f"        First 48 bytes:")
                                                for line_off in range(0, 48, 16):
                                                    row = target_code[line_off:min(line_off+16, len(target_code))]
                                                    hex_str = ' '.join(f'{b:02x}' for b in row)
                                                    print(f"          +0x{line_off:03x}: {hex_str}")

                                break  # found the dword table
                        break  # found the byte table

        # Also check if there's a second switch (the type-code dispatch)
        # From the summary: CMP EAX, 0x48 at +0x595 AND another at +0xEB6

        print(f"\n{'='*80}")
        print(f"SEARCHING FOR ALL CMP EAX, 0x48 (type_code range checks)")
        print(f"{'='*80}")

        for i in range(min(4096, len(func_code)) - 3):
            if func_code[i] == 0x83 and func_code[i+1] == 0xF8 and func_code[i+2] == 0x48:
                print(f"  +0x{i:03x}: CMP EAX, 0x48 (72)")
                # Show surrounding bytes
                start = max(0, i-4)
                end = min(len(func_code), i+20)
                hex_str = ' '.join(f'{func_code[b]:02x}' for b in range(start, end))
                print(f"    Context: {hex_str}")

        # Check around +0x580 for the storage_class load
        print(f"\n{'='*80}")
        print(f"STORAGE CLASS LOAD PATTERN (around +0x580)")
        print(f"{'='*80}")
        for j in range(0x570, min(0x5B0, len(func_code)), 16):
            row = func_code[j:min(j+16, len(func_code))]
            hex_str = ' '.join(f'{b:02x}' for b in row)
            print(f"  +0x{j:03x}: {hex_str}")

        # Also dump the second switch area (around +0xE80-0xF20 for type_code dispatch)
        print(f"\n{'='*80}")
        print(f"TYPE CODE DISPATCH (around +0xEB0-0xF30)")
        print(f"{'='*80}")
        for j in range(0xEB0, min(0xF30, len(func_code)), 16):
            if j + 16 <= len(func_code):
                row = func_code[j:j+16]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                print(f"  +0x{j:03x}: {hex_str}")

    finally:
        kernel32.CloseHandle(handle)


if __name__ == '__main__':
    main()
