#!/usr/bin/env python3
"""Phase 2d: Full decode of vtable[158] dispatch logic.

Traces ALL storage class comparisons, ALL type code checks, and ALL call targets
to produce a complete map of how each field type/storage combination reads data.

Storage class is at PropInfoData+0x4A (WORD)
Engine type code is at PropInfoData+0x48 (WORD)
Field size is at PropInfoData+0x44 (DWORD)
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

        # Read full function - it's ~4KB but read extra for jump targets
        code = rpm(handle, mod_base + vt158_rva, 8192)
        if not code:
            print("Cannot read function!"); return

        # Find the actual function end
        func_end = 4096

        print(f"\n{'='*90}")
        print(f"FULL vtable[158] DISPATCH ANALYSIS (RVA 0x{vt158_rva:x})")
        print(f"{'='*90}")

        # ===================================================================
        # PART 1: Find all storage_class checks (CMP [R14+0x4A], imm)
        # R14 points to the PropInfoData entry in this function
        # ===================================================================
        print(f"\n--- STORAGE CLASS CHECKS (R14+0x4A) ---")

        for i in range(func_end - 5):
            # 66 41 83 7E 4A xx = CMP WORD [R14+0x4A], imm8
            if (i + 5 < len(code) and code[i] == 0x66 and code[i+1] == 0x41 and
                code[i+2] == 0x83 and code[i+3] == 0x7E and code[i+4] == 0x4A):
                val = code[i+5]
                # Find what follows (JE/JNE)
                next_bytes = code[i+6:i+12]
                jmp_info = ""
                if next_bytes[0] == 0x0F:
                    if next_bytes[1] == 0x84:
                        disp = struct.unpack_from('<i', next_bytes, 2)[0]
                        target = vt158_rva + i + 12 + disp
                        jmp_info = f" -> JE 0x{target:x}"
                    elif next_bytes[1] == 0x85:
                        disp = struct.unpack_from('<i', next_bytes, 2)[0]
                        target = vt158_rva + i + 12 + disp
                        jmp_info = f" -> JNE 0x{target:x}"
                elif next_bytes[0] == 0x74:
                    target = vt158_rva + i + 8 + struct.unpack_from('<b', next_bytes, 1)[0]
                    jmp_info = f" -> JE 0x{target:x}"
                elif next_bytes[0] == 0x75:
                    target = vt158_rva + i + 8 + struct.unpack_from('<b', next_bytes, 1)[0]
                    jmp_info = f" -> JNE 0x{target:x}"

                print(f"  +0x{i:03x}: CMP WORD [R14+0x4A], 0x{val:02x} ({val}){jmp_info}")

            # 66 41 39 46 4A or similar patterns
            # Also check for MOVZX from +0x4A and subsequent CMP
            if (i + 4 < len(code) and code[i] == 0x41 and code[i+1] == 0x0F and
                code[i+2] == 0xB7 and (code[i+3] & 0xC7) == 0x46):
                off = code[i+4] if (code[i+3] & 0x40) else 0
                if off == 0x4A:
                    reg = (code[i+3] >> 3) & 7
                    regs = ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI']
                    print(f"  +0x{i:03x}: MOVZX {regs[reg]}, WORD [R14+0x4A] (load storage_class)")

        # ===================================================================
        # PART 2: Find all engine type checks (R14+0x48)
        # ===================================================================
        print(f"\n--- ENGINE TYPE CODE CHECKS (R14+0x48) ---")

        for i in range(func_end - 5):
            # 45 0F B7 46 48 = MOVZX R8D, WORD [R14+0x48]
            if (i + 4 < len(code) and code[i] == 0x45 and code[i+1] == 0x0F and
                code[i+2] == 0xB7 and code[i+3] == 0x46 and code[i+4] == 0x48):
                print(f"  +0x{i:03x}: MOVZX R8D, WORD [R14+0x48] (load engine_type_code)")

            # 41 0F B7 .. 48 = other MOVZX with R14+0x48
            if (i + 4 < len(code) and code[i] == 0x41 and code[i+1] == 0x0F and
                code[i+2] == 0xB7):
                modrm = code[i+3]
                if (modrm & 0xC7) == 0x46:
                    off = code[i+4]
                    if off == 0x48:
                        reg = (modrm >> 3) & 7
                        regs = ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI']
                        print(f"  +0x{i:03x}: MOVZX {regs[reg]}, WORD [R14+0x48] (load engine_type_code)")

        # ===================================================================
        # PART 3: Find field_id checks (AND 0x3F000000)
        # ===================================================================
        print(f"\n--- FIELD_ID PREFIX CHECKS (AND 0x3F000000) ---")

        for i in range(func_end - 6):
            # 25 00 00 00 3F = AND EAX, 0x3F000000
            if code[i] == 0x25:
                val = struct.unpack_from('<I', code, i+1)[0]
                if val == 0x3F000000:
                    print(f"  +0x{i:03x}: AND EAX, 0x3F000000")
                    # Find subsequent CMP
                    for j in range(i+5, min(i+80, func_end)):
                        if code[j] == 0x3D:
                            cmp_val = struct.unpack_from('<I', code, j+1)[0]
                            print(f"    +0x{j:03x}: CMP EAX, 0x{cmp_val:08x} (prefix 0x{cmp_val>>24:02x})")
                        elif j + 5 < len(code) and code[j] == 0x81 and code[j+1] == 0xF8:
                            # CMP EAX, imm32 (alternate encoding)
                            pass

        # ===================================================================
        # PART 4: Find all CALL targets (direct calls)
        # ===================================================================
        print(f"\n--- DIRECT CALL TARGETS ---")

        calls = {}
        for i in range(func_end - 5):
            if code[i] == 0xE8:
                disp = struct.unpack_from('<i', code, i+1)[0]
                target = vt158_rva + i + 5 + disp
                if 0x1000 < target < 0x2000000:
                    if target not in calls:
                        calls[target] = []
                    calls[target].append(i)

        for target in sorted(calls.keys()):
            offsets = calls[target]
            off_str = ', '.join(f'+0x{o:03x}' for o in offsets[:5])
            if len(offsets) > 5:
                off_str += f' ({len(offsets)} total)'
            print(f"  RVA 0x{target:x}: called from [{off_str}]")

        # ===================================================================
        # PART 5: Find all indirect calls (vtable dispatches)
        # ===================================================================
        print(f"\n--- INDIRECT CALL TARGETS ---")

        for i in range(func_end - 6):
            # FF 15 = CALL [RIP+disp32] (indirect via memory)
            if code[i] == 0xFF and code[i+1] == 0x15:
                disp = struct.unpack_from('<i', code, i+2)[0]
                target_ptr_rva = vt158_rva + i + 6 + disp
                # Read the pointer value
                ptr_data = rpm(handle, mod_base + target_ptr_rva, 8)
                if ptr_data:
                    ptr_val = struct.unpack_from('<Q', ptr_data, 0)[0]
                    func_rva = ptr_val - mod_base if ptr_val > mod_base else 0
                    if 0 < func_rva < 0x2000000:
                        print(f"  +0x{i:03x}: CALL [0x{target_ptr_rva:x}] -> RVA 0x{func_rva:x}")

            # FF 90/91/92 xx xx xx xx = CALL [RAX+disp32] (vtable call)
            if code[i] == 0xFF:
                modrm = code[i+1]
                mod = (modrm >> 6) & 3
                op = (modrm >> 3) & 7
                rm = modrm & 7

                if op == 2 and mod == 2 and rm != 4:  # CALL [reg+disp32]
                    disp = struct.unpack_from('<i', code, i+2)[0]
                    slot = disp // 8
                    regs = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI']
                    print(f"  +0x{i:03x}: CALL [{regs[rm]}+0x{disp:x}] (vtable slot {slot})")

            # REX.W + FF 90/91: 48 FF 90 xx xx xx xx
            if i + 6 < len(code) and code[i] in (0x48, 0x49) and code[i+1] == 0xFF:
                modrm = code[i+2]
                mod = (modrm >> 6) & 3
                op = (modrm >> 3) & 7
                rm = modrm & 7

                if op == 2 and mod == 2 and rm != 4:
                    disp = struct.unpack_from('<i', code, i+3)[0]
                    slot = disp // 8
                    ext_rm = rm + (8 if code[i] == 0x49 else 0)
                    regs = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI',
                            'R8','R9','R10','R11','R12','R13','R14','R15']
                    print(f"  +0x{i:03x}: CALL [{regs[ext_rm]}+0x{disp:x}] (vtable slot {slot})")

        # ===================================================================
        # PART 6: Flags field checks (0xC000 mask at +0x4C)
        # ===================================================================
        print(f"\n--- FLAGS FIELD CHECKS ---")

        for i in range(func_end - 6):
            # 41 F7 46 4C = TEST [R14+0x4C], imm32
            if (i + 7 < len(code) and code[i] == 0x41 and code[i+1] == 0xF7 and
                code[i+2] == 0x46 and code[i+3] == 0x4C):
                val = struct.unpack_from('<I', code, i+4)[0]
                print(f"  +0x{i:03x}: TEST DWORD [R14+0x4C], 0x{val:08x}")

            # 41 8B 46 4C = MOV EAX, [R14+0x4C]
            if (i + 3 < len(code) and code[i] == 0x41 and code[i+1] == 0x8B and
                code[i+2] == 0x46 and code[i+3] == 0x4C):
                print(f"  +0x{i:03x}: MOV EAX, [R14+0x4C] (load flags)")

            # 25 00 C0 00 00 = AND EAX, 0x0000C000
            if code[i] == 0x25:
                val = struct.unpack_from('<I', code, i+1)[0]
                if val in (0xC000, 0x4000, 0x8000, 0x0200, 0x1FF):
                    print(f"  +0x{i:03x}: AND EAX, 0x{val:08x}")

        # ===================================================================
        # PART 7: Slot/offset loads (R14+0x28 = slot_index, R14+0x44 = field_size)
        # ===================================================================
        print(f"\n--- SLOT INDEX / FIELD SIZE LOADS ---")

        for i in range(func_end - 4):
            # 41 8B 46 28 = MOV EAX, [R14+0x28]
            # 45 8B 46 28 = MOV R8D, [R14+0x28]
            if i + 3 < len(code):
                if code[i] in (0x41, 0x45) and code[i+1] == 0x8B and code[i+2] == 0x46:
                    off = code[i+3]
                    if off in (0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x50, 0x54, 0x58):
                        ext = 'R8D' if code[i] == 0x45 else ''
                        reg = (code[i+2] >> 3) & 7 if not ext else 0
                        field_map = {
                            0x28: 'slot_index', 0x2C: 'unknown_2C', 0x30: 'unknown_30',
                            0x34: 'unknown_34', 0x38: 'unknown_38', 0x3C: 'unknown_3C',
                            0x40: 'field_id', 0x44: 'field_size',
                            0x48: 'type_code', 0x4A: 'storage_class', 0x4C: 'flags',
                            0x50: 'dirty_bit_idx', 0x54: 'linked_field',
                            0x58: 'secondary_idx',
                        }
                        name = field_map.get(off, f'field_+{off:02x}')
                        regs8 = ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI']
                        if ext:
                            rname = 'R8D'
                        else:
                            rname = regs8[(code[i+2] >> 3) & 7]
                        print(f"  +0x{i:03x}: MOV {rname}, [R14+0x{off:02x}] ({name})")

            # 41 8B 4E xx = MOV ECX, [R14+xx]
            if i + 3 < len(code) and code[i] == 0x41 and code[i+1] == 0x8B and code[i+2] == 0x4E:
                off = code[i+3]
                if off in (0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x50, 0x54, 0x58):
                    field_map = {
                        0x28: 'slot_index', 0x2C: 'unknown_2C', 0x30: 'unknown_30',
                        0x40: 'field_id', 0x44: 'field_size',
                        0x50: 'dirty_bit_idx', 0x54: 'linked_field',
                    }
                    name = field_map.get(off, f'field_+{off:02x}')
                    print(f"  +0x{i:03x}: MOV ECX, [R14+0x{off:02x}] ({name})")

        # ===================================================================
        # PART 8: Summary of dispatch logic
        # ===================================================================
        print(f"\n{'='*90}")
        print(f"DISPATCH ARCHITECTURE SUMMARY")
        print(f"{'='*90}")
        print(f"""
vtable[158] at RVA 0x239380 (~4KB function):

1. ENTRY: Loads PropInfoData entry for field_id
   - R14 -> PropInfoData entry (0x68 bytes in the 0xA8-stride cache)

2. EARLY CHECK: storage_class == 10 (computed)
   - +0x14A: CMP [R14+0x4A], 0x0A
   - If computed: calls computation function directly, skips normal read

3. FIELD_ID PREFIX CHECK: AND field_id, 0x3F000000
   - Compares against 0x0B (Task), 0x0C (Resource), 0x0F (Assignment), etc.
   - Routes to the correct record table

4. TYPE CODE SWITCH: switch(engine_type_code) at +0x595
   - Loaded from [R14+0x48]
   - Case 0,1 (null): vtable slot 120 dispatch
   - Case 5 (i64): special handler, checks storage_class==18
   - Case 8,29,72 (text/virtual/guid): string-type read
   - All others: default 8-byte scalar read

5. COMMON READ PATH: Uses slot_index, field_size from PropInfoData
   - Calls getter1 -> getter2 -> actual record byte read
   - Storage class determines WHICH getter pair is used
   - Byte offset in PropInfoData tail determines WHERE in TASKNODE to read
""")

    finally:
        kernel32.CloseHandle(handle)


if __name__ == '__main__':
    main()
