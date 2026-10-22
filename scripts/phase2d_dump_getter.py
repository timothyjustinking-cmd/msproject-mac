#!/usr/bin/env python3
"""Phase 2d: Dump the common getter1 implementation at RVA 0x373670.

The standard getter1 trampoline at 0x4b43a0 does:
  MOV R8D, EDX   ; R8D = param2 (field variation?)
  XOR EDX, EDX   ; EDX = 0
  JMP 0x373670   ; jump to real implementation

Also dumps getter2 functions per type to extract hardcoded TASKNODE offsets.
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

def dump_func(handle, mod_base, rva, max_bytes=512, label=""):
    """Dump a function's bytes, looking for RET."""
    va = mod_base + rva
    code = rpm(handle, va, max_bytes)
    if not code:
        print(f"  Cannot read at RVA 0x{rva:x}")
        return None

    if label:
        print(f"\n  --- {label} (RVA 0x{rva:x}) ---")

    # Find first RET (C3) not immediately after a prefix
    func_end = len(code)
    for i in range(len(code)):
        if code[i] == 0xC3 and (i == 0 or code[i-1] not in (0x0F, 0x66)):
            func_end = i + 1
            break

    for j in range(0, min(func_end + 32, len(code)), 16):
        row = code[j:min(j+16, len(code))]
        hex_str = ' '.join(f'{b:02x}' for b in row)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
        marker = ""
        if any(code[j+k] == 0xC3 for k in range(min(16, len(row))) if j + k < func_end):
            marker = " <RET"
        print(f"    +0x{j:03x} (RVA 0x{rva+j:06x}): {hex_str:<48s} {ascii_str}{marker}")

    print(f"    Function size: ~{func_end} bytes")
    return code[:max_bytes]

def find_record_offsets(code, rva):
    """Search for MOV instructions that read from record+offset patterns."""
    offsets = []
    for i in range(len(code) - 6):
        # Look for instructions reading from [REG+disp32] where disp > 0x100
        # Patterns:
        # 8B 80 xx xx xx xx = MOV EAX, [RAX+disp32]
        # 0F B7 80 xx xx xx xx = MOVZX EAX, WORD [RAX+disp32]
        # 48 8B 80 xx xx xx xx = MOV RAX, [RAX+disp32]
        # etc

        if i + 6 <= len(code):
            # MOV r32, [RAX+disp32]: 8B 80+r xx xx xx xx
            if code[i] == 0x8B and (code[i+1] & 0xC7) == 0x80:
                reg = (code[i+1] >> 3) & 7
                disp = struct.unpack_from('<i', code, i+2)[0]
                if 0x10 < disp < 0x2000:
                    regs = ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI']
                    offsets.append((i, rva+i, disp, f"MOV {regs[reg]}, [RAX+0x{disp:x}]", 4))

        if i + 7 <= len(code):
            # MOVZX r32, WORD [RAX+disp32]: 0F B7 80 xx xx xx xx
            if code[i] == 0x0F and code[i+1] == 0xB7 and (code[i+2] & 0xC7) == 0x80:
                reg = (code[i+2] >> 3) & 7
                disp = struct.unpack_from('<i', code, i+3)[0]
                if 0x10 < disp < 0x2000:
                    regs = ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI']
                    offsets.append((i, rva+i, disp, f"MOVZX {regs[reg]}, WORD [RAX+0x{disp:x}]", 2))

            # MOV RAX, [RAX+disp32]: 48 8B 80 xx xx xx xx
            if code[i] == 0x48 and code[i+1] == 0x8B and (code[i+2] & 0xC7) == 0x80:
                reg = (code[i+2] >> 3) & 7
                disp = struct.unpack_from('<i', code, i+3)[0]
                if 0x10 < disp < 0x2000:
                    regs64 = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI']
                    offsets.append((i, rva+i, disp, f"MOV {regs64[reg]}, [RAX+0x{disp:x}]", 8))

        # Also check [RCX+disp32] and [RDX+disp32]
        for base_reg_idx, base_rm in [(1, 0x81), (2, 0x82), (0, 0x80)]:
            base_names = {0: 'RAX', 1: 'RCX', 2: 'RDX'}
            bn = base_names.get(base_reg_idx, '?')

            if i + 6 <= len(code):
                if code[i] == 0x8B and (code[i+1] & 0xC7) == base_rm:
                    reg = (code[i+1] >> 3) & 7
                    disp = struct.unpack_from('<i', code, i+2)[0]
                    if 0x100 < disp < 0x2000 and base_reg_idx != 0:  # skip RAX, already handled
                        regs = ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI']
                        offsets.append((i, rva+i, disp, f"MOV {regs[reg]}, [{bn}+0x{disp:x}]", 4))

    return offsets

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
        # =====================================================================
        # PART 1: Dump the common getter1 target at RVA 0x373670
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 1: COMMON GETTER1 IMPLEMENTATION (RVA 0x373670)")
        print(f"{'='*80}")
        print(f"Called with: RCX=record_ptr?, EDX=0, R8D=variation_code")

        rva = 0x373670
        code = rpm(handle, mod_base + rva, 2048)
        if code:
            # This function might be large. Dump first 512 bytes
            print(f"\n  First 512 bytes:")
            for j in range(0, 512, 16):
                row = code[j:min(j+16, len(code))]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
                print(f"    +0x{j:03x} (RVA 0x{rva+j:06x}): {hex_str:<48s} {ascii_str}")

            # Search for record offset reads
            print(f"\n  Record offset reads found:")
            offsets = find_record_offsets(code, rva)
            for off, func_rva, disp, desc, size in offsets:
                if disp > 0x100:  # only significant offsets
                    print(f"    At +0x{off:03x} (RVA 0x{func_rva:x}): {desc}")

            # Search for JMP table or switch patterns
            print(f"\n  Jump/call targets (E8/E9):")
            for i in range(min(len(code), 512) - 5):
                if code[i] in (0xE8, 0xE9):
                    disp = struct.unpack_from('<i', code, i+1)[0]
                    target = rva + i + 5 + disp
                    op = "CALL" if code[i] == 0xE8 else "JMP"
                    if 0 < target < 0x2000000:
                        print(f"    +0x{i:03x}: {op} 0x{target:x}")

        # =====================================================================
        # PART 2: Dump ALL unique getter2 functions for each type
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 2: ALL GETTER2 FUNCTIONS WITH RECORD OFFSETS")
        print(f"{'='*80}")

        # Read Task cache to get all getter2 RVAs per type
        desc_va = mod_base + 0xf8bda0
        desc_data = rpm(handle, desc_va, 0x10)
        cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]

        type_getter2s = {}
        for idx in range(1444):
            entry_va = cache_ptr + idx * 0xA8
            entry_data = rpm(handle, entry_va, 0xA8)
            if not entry_data or len(entry_data) < 0xA8:
                continue

            vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
            vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
            if vtable_rva not in (0x1050670, 0x0fb4a80):
                continue

            type_code = struct.unpack_from('<I', entry_data, 0x0C)[0]
            field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
            slot_idx = struct.unpack_from('<I', entry_data, 0x18)[0]
            getter2 = struct.unpack_from('<Q', entry_data, 0x68)[0]
            g2_rva = getter2 - mod_base if getter2 > mod_base else 0

            if g2_rva < 0x1000:
                continue

            if g2_rva not in type_getter2s:
                type_getter2s[g2_rva] = {'types': set(), 'fields': [], 'slots': set()}
            type_getter2s[g2_rva]['types'].add(type_code)
            type_getter2s[g2_rva]['slots'].add(slot_idx)
            type_getter2s[g2_rva]['fields'].append((field_id, type_code, slot_idx))

        type_names = {0:'null', 2:'i16', 3:'i32', 5:'i64', 8:'text', 9:'obj_ref',
                      11:'bool', 19:'date32', 20:'comp_dur', 21:'order_id', 29:'virtual', 72:'guid'}

        print(f"\n  Total unique getter2 RVAs: {len(type_getter2s)}")

        # Sort by types
        all_offsets = {}  # getter_rva -> list of (disp, desc, size)

        for g2_rva in sorted(type_getter2s.keys()):
            info = type_getter2s[g2_rva]
            types_str = ', '.join(f"{type_names.get(t, str(t))}" for t in sorted(info['types']))
            slots_str = ', '.join(str(s) for s in sorted(info['slots'])[:10])
            field_count = len(info['fields'])

            print(f"\n  Getter2 at RVA 0x{g2_rva:x}: types=[{types_str}] {field_count} fields, slots=[{slots_str}]")

            # Dump the function
            func_code = rpm(handle, mod_base + g2_rva, 512)
            if not func_code:
                continue

            # Find function end
            func_end = len(func_code)
            for i in range(len(func_code)):
                if func_code[i] == 0xC3:
                    func_end = i + 1
                    break

            # Short dump
            for j in range(0, min(func_end + 16, 256), 16):
                row = func_code[j:min(j+16, len(func_code))]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                marker = " <RET" if any(func_code[j+k] == 0xC3 for k in range(min(16, len(row))) if j + k < func_end) else ""
                print(f"    +0x{j:03x}: {hex_str}{marker}")

            print(f"    Size: ~{func_end} bytes")

            # Find record offset reads
            offsets = find_record_offsets(func_code[:min(func_end + 64, 512)], g2_rva)
            if offsets:
                print(f"    Record offsets:")
                for off, func_rva, disp, desc, size in offsets:
                    print(f"      {desc}")
                    all_offsets.setdefault(g2_rva, []).append((disp, desc, size))

        # =====================================================================
        # PART 3: Summary of discovered TASKNODE byte offsets
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 3: DISCOVERED TASKNODE BYTE OFFSETS")
        print(f"{'='*80}")

        seen_offsets = set()
        for g2_rva in sorted(all_offsets.keys()):
            info = type_getter2s[g2_rva]
            types_str = ', '.join(f"{type_names.get(t, str(t))}" for t in sorted(info['types']))

            for disp, desc, size in all_offsets[g2_rva]:
                if disp > 0x40 and disp not in seen_offsets:
                    seen_offsets.add(disp)
                    # Find which fields use this getter
                    sample_fields = info['fields'][:5]
                    field_strs = [f"0x{fid:08x}(slot={si})" for fid,tc,si in sample_fields]
                    print(f"  Offset 0x{disp:04x} ({disp:4d}): {desc} types=[{types_str}] fields: {', '.join(field_strs)}")

        if seen_offsets:
            print(f"\n  Total unique offsets: {len(seen_offsets)}")
            print(f"  Range: 0x{min(seen_offsets):x} - 0x{max(seen_offsets):x}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
