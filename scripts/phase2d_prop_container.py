#!/usr/bin/env python3
"""Phase 2d: Investigate the prop container object that dispatches field reads.

The prop container (0x3A0 bytes) is the KEY dispatcher:
- TTaskTable vfunc[87] (GetFieldValue) calls prop_container.vtable[158]
- The prop container is at TTaskTable+0x5E8
- Its vtable[158] is the bottom-of-chain field lookup
- It dispatches reads based on storage_class from the PropInfoData entry

This script:
1. Finds the prop container vtable by reading vfunc[100] code
2. Dumps the vtable to identify all virtual function pointers
3. Specifically looks at vtable[158] to understand field dispatch
4. Also looks at the prop container's internal state for a live task
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
    """Try to read RTTI class name from vtable's RTTI Complete Object Locator."""
    # vtable[-1] = pointer to RTTI Complete Object Locator
    vtable_va = mod_base + vtable_rva
    col_ptr_data = rpm(handle, vtable_va - 8, 8)
    if not col_ptr_data:
        return None
    col_ptr = struct.unpack_from('<Q', col_ptr_data, 0)[0]

    # Complete Object Locator: at +0x0C is the type descriptor RVA (offset from image base)
    col_data = rpm(handle, col_ptr, 24)
    if not col_data:
        return None

    td_rva = struct.unpack_from('<I', col_data, 0x0C)[0]
    if td_rva == 0 or td_rva > 0x2000000:
        return None

    # Type descriptor: at +0x10 is the mangled name (null-terminated string)
    td_va = mod_base + td_rva
    name_data = rpm(handle, td_va + 0x10, 128)
    if not name_data:
        return None

    # Find null terminator
    null_pos = name_data.find(b'\x00')
    if null_pos > 0:
        mangled = name_data[:null_pos].decode('ascii', errors='replace')
        # Simple demangling: .?AVClassName@@ -> ClassName
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
        # PART 1: Find the PropInfoData vtable at 0x1050670 and read its
        # RTTI to identify the class name
        # =================================================================
        print(f"\n{'='*80}")
        print(f"PART 1: RTTI FOR KEY VTABLES")
        print(f"{'='*80}")

        vtables_to_check = [
            (0x1050670, "PropInfoData vtable"),
            (0x0fb4a80, "CustomPropInfoData vtable"),
            (0x1048648, "TTaskTable vtable (from prior analysis)"),
        ]

        for vt_rva, label in vtables_to_check:
            name = read_rtti_name(handle, mod_base, vt_rva)
            if name:
                print(f"  {label} (RVA 0x{vt_rva:x}): {name}")
            else:
                print(f"  {label} (RVA 0x{vt_rva:x}): RTTI not found")

        # =================================================================
        # PART 2: Read vfunc[100] of TTaskTable to understand prop container init
        # From prior analysis: vfunc[100] allocates 0x3A0 bytes and stores at this+0x5E8
        # The LEA R8 in vfunc[100] references 0xF8BDA0 (the Task descriptor)
        # =================================================================
        print(f"\n{'='*80}")
        print(f"PART 2: PROP CONTAINER VTABLE IDENTIFICATION")
        print(f"{'='*80}")

        # The prop container's vtable is set during allocation.
        # Let's read vfunc[100] to find what vtable it writes.
        tt_vtable_rva = 0x1048648
        tt_vtable_va = mod_base + tt_vtable_rva

        # Read vfunc[100] pointer
        vf100_ptr_data = rpm(handle, tt_vtable_va + 100 * 8, 8)
        vf100_va = struct.unpack_from('<Q', vf100_ptr_data, 0)[0]
        vf100_rva = vf100_va - mod_base
        print(f"\n  TTaskTable vtable[100] -> RVA 0x{vf100_rva:x}")

        # Dump vfunc[100] to find the vtable it stores in the allocated object
        vf100_code = rpm(handle, vf100_va, 512)
        if vf100_code:
            print(f"  Dumping vfunc[100] (first 256 bytes):")
            for j in range(0, 256, 16):
                row = vf100_code[j:min(j+16, len(vf100_code))]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                print(f"    +0x{j:03x}: {hex_str:<48s}")

            # Look for LEA instructions that load vtable addresses
            print(f"\n  LEA instructions (vtable loads):")
            for i in range(len(vf100_code) - 7):
                # 48 8D 05 xx xx xx xx = LEA RAX, [rip+disp32]
                if vf100_code[i] == 0x48 and vf100_code[i+1] == 0x8D:
                    modrm = vf100_code[i+2]
                    if (modrm & 0xC7) == 0x05:  # mod=00, rm=101 (RIP-relative)
                        reg = (modrm >> 3) & 7
                        disp = struct.unpack_from('<i', vf100_code, i+3)[0]
                        target_rva = vf100_rva + i + 7 + disp
                        regs = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI']
                        rtti_name = read_rtti_name(handle, mod_base, target_rva)
                        name_str = f" -> {rtti_name}" if rtti_name else ""
                        print(f"    +0x{i:03x}: LEA {regs[reg]}, [RIP+0x{disp:x}] = RVA 0x{target_rva:x}{name_str}")

                # 4C 8D xx xx xx xx xx = LEA R8-R15, [rip+disp32]
                if vf100_code[i] == 0x4C and vf100_code[i+1] == 0x8D:
                    modrm = vf100_code[i+2]
                    if (modrm & 0xC7) == 0x05:  # RIP-relative
                        reg = ((modrm >> 3) & 7) + 8  # REX.R
                        disp = struct.unpack_from('<i', vf100_code, i+3)[0]
                        target_rva = vf100_rva + i + 7 + disp
                        regs = ['R8','R9','R10','R11','R12','R13','R14','R15']
                        rtti_name = read_rtti_name(handle, mod_base, target_rva)
                        name_str = f" -> {rtti_name}" if rtti_name else ""
                        print(f"    +0x{i:03x}: LEA {regs[reg-8]}, [RIP+0x{disp:x}] = RVA 0x{target_rva:x}{name_str}")

            # Look for CALL instructions
            print(f"\n  CALL targets:")
            for i in range(len(vf100_code) - 5):
                if vf100_code[i] == 0xE8:
                    disp = struct.unpack_from('<i', vf100_code, i+1)[0]
                    target_rva = vf100_rva + i + 5 + disp
                    if 0x1000 < target_rva < 0x2000000:
                        print(f"    +0x{i:03x}: CALL RVA 0x{target_rva:x}")

        # =================================================================
        # PART 3: Read vfunc[87] of TTaskTable again to trace the dispatch
        # vfunc[87] calls prop_container.vtable[158]
        # =================================================================
        print(f"\n{'='*80}")
        print(f"PART 3: VFUNC[87] DISPATCH TRACE")
        print(f"{'='*80}")

        # Read vfunc[87] pointer
        vf87_ptr_data = rpm(handle, tt_vtable_va + 87 * 8, 8)
        vf87_va = struct.unpack_from('<Q', vf87_ptr_data, 0)[0]
        vf87_rva = vf87_va - mod_base
        print(f"\n  TTaskTable vtable[87] -> RVA 0x{vf87_rva:x}")

        # Dump vfunc[87]
        vf87_code = rpm(handle, vf87_va, 512)
        if vf87_code:
            print(f"  Dumping vfunc[87] (first 320 bytes):")
            for j in range(0, 320, 16):
                if j >= len(vf87_code):
                    break
                row = vf87_code[j:min(j+16, len(vf87_code))]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                marker = ""
                for k in range(min(16, len(row))):
                    if vf87_code[j+k] == 0xC3 and (j+k == 0 or vf87_code[j+k-1] not in (0x0F, 0x66)):
                        marker = " <RET"
                        break
                print(f"    +0x{j:03x}: {hex_str:<48s}{marker}")

            # Look for vtable dispatch: CALL [REG+disp] patterns
            print(f"\n  Indirect calls (vtable dispatches):")
            for i in range(min(len(vf87_code), 320) - 3):
                if vf87_code[i] == 0xFF:
                    modrm = vf87_code[i+1]
                    mod = (modrm >> 6) & 3
                    reg = (modrm >> 3) & 7
                    rm = modrm & 7

                    if reg == 2:  # CALL indirect
                        if mod == 2 and rm != 4:  # [REG+disp32]
                            disp = struct.unpack_from('<i', vf87_code, i+2)[0]
                            if 0 < disp < 0x2000:
                                slot = disp // 8
                                regs = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI']
                                print(f"    +0x{i:03x}: CALL [{regs[rm]}+0x{disp:x}] (slot {slot})")

                    # Check with REX prefix
                    if i > 0 and 0x40 <= vf87_code[i-1] <= 0x4F:
                        rex = vf87_code[i-1]
                        if reg == 2:
                            if mod == 2 and rm != 4:
                                disp = struct.unpack_from('<i', vf87_code, i+2)[0]
                                if 0 < disp < 0x2000:
                                    base_idx = rm + (8 if rex & 1 else 0)
                                    slot = disp // 8
                                    print(f"    +0x{i-1:03x}: REX CALL [{['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI','R8','R9','R10','R11','R12','R13','R14','R15'][base_idx]}+0x{disp:x}] (slot {slot})")

        # =================================================================
        # PART 4: Look for the prop container's vtable by reading vfunc[100]
        # and tracing the vtable it writes into the new object
        # =================================================================
        print(f"\n{'='*80}")
        print(f"PART 4: SCAN FOR PROP CONTAINER VTABLE")
        print(f"{'='*80}")

        # From vfunc[100] analysis, we know it:
        # 1. Allocates 0x3A0 bytes
        # 2. Uses LEA R8, [RIP+xx] to load the Task descriptor address (0xF8BDA0)
        # 3. Stores the new object at this+0x5E8
        # The vtable of the prop container is set by a constructor CALL inside vfunc[100]

        # Let's look at every LEA in vfunc[100] that points to a vtable-like address
        if vf100_code:
            # Find all LEA RIP-relative instructions
            lea_targets = []
            for i in range(min(len(vf100_code), 400) - 7):
                prefix = 0
                pos = i
                if 0x40 <= vf100_code[pos] <= 0x4F:
                    prefix = vf100_code[pos]
                    pos += 1

                if pos + 6 < len(vf100_code) and vf100_code[pos] == 0x8D:
                    modrm = vf100_code[pos + 1]
                    if (modrm & 0xC7) == 0x05:  # RIP-relative
                        disp = struct.unpack_from('<i', vf100_code, pos + 2)[0]
                        target_rva = vf100_rva + pos + 6 + disp
                        if prefix:
                            target_rva = vf100_rva + i + 7 + disp  # account for REX prefix

                        # Check if target looks like a vtable (pointer to code in .text)
                        test_data = rpm(handle, mod_base + target_rva, 16)
                        if test_data:
                            ptr = struct.unpack_from('<Q', test_data, 0)[0]
                            ptr_rva = ptr - mod_base if ptr > mod_base else 0
                            is_code = 0x1000 < ptr_rva < 0x1500000  # .text section range
                            rtti_name = read_rtti_name(handle, mod_base, target_rva)
                            name_str = f" RTTI={rtti_name}" if rtti_name else ""
                            if is_code or rtti_name:
                                lea_targets.append((i, target_rva, rtti_name, ptr_rva))
                                print(f"  LEA at +0x{i:03x}: RVA 0x{target_rva:x}{name_str} (first ptr -> RVA 0x{ptr_rva:x})")

        # =================================================================
        # PART 5: Dump vtable[158] of candidate prop container vtables
        # =================================================================
        print(f"\n{'='*80}")
        print(f"PART 5: VTABLE[158] FOR CANDIDATE VTABLES")
        print(f"{'='*80}")

        for i, target_rva, rtti_name, first_ptr_rva in (lea_targets if 'lea_targets' in dir() else []):
            if not rtti_name:
                continue

            # Read vtable[158]
            vt158_ptr_data = rpm(handle, mod_base + target_rva + 158 * 8, 8)
            if vt158_ptr_data:
                vt158_va = struct.unpack_from('<Q', vt158_ptr_data, 0)[0]
                vt158_rva = vt158_va - mod_base if vt158_va > mod_base else 0

                print(f"\n  {rtti_name} vtable[158] -> RVA 0x{vt158_rva:x}")

                if 0x1000 < vt158_rva < 0x1500000:
                    # Dump first 128 bytes
                    vt158_code = rpm(handle, vt158_va, 256)
                    if vt158_code:
                        print(f"  First 128 bytes:")
                        for j in range(0, 128, 16):
                            row = vt158_code[j:min(j+16, len(vt158_code))]
                            hex_str = ' '.join(f'{b:02x}' for b in row)
                            print(f"    +0x{j:03x}: {hex_str:<48s}")

    finally:
        kernel32.CloseHandle(handle)


if __name__ == '__main__':
    main()
