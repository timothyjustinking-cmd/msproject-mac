#!/usr/bin/env python3
"""Phase 2d: Read the static helper tables used by getter2 functions.

All getter2 functions that read TASKNODE data call a common helper at ~RVA 0x369437
with two static table pointers:
  R8 -> RVA 0x14AC5B8
  R9 -> RVA 0x14AC6B0

These tables are used to resolve the TASKNODE from a context object.

Also: resolve the helper function to understand how it uses the tables.
Also: try using COM to find a live TTaskTable and prop container.
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
        # =====================================================================
        # PART 1: Read the helper tables
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 1: STATIC HELPER TABLES")
        print(f"{'='*80}")

        table_r8_rva = 0x14AC5B8
        table_r9_rva = 0x14AC6B0

        print(f"\n  Table R8 at RVA 0x{table_r8_rva:x}:")
        data_r8 = rpm(handle, mod_base + table_r8_rva, 256)
        if data_r8:
            for j in range(0, 256, 16):
                row = data_r8[j:j+16]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
                print(f"    +0x{j:03x}: {hex_str}  {ascii_str}")

            # Look for pointers (qwords in the high address range)
            print(f"\n    Pointer-like values:")
            for off in range(0, 256, 8):
                val = struct.unpack_from('<Q', data_r8, off)[0]
                if mod_base < val < mod_base + 0x2000000:
                    rva = val - mod_base
                    print(f"      +0x{off:02x}: 0x{val:016x} (RVA 0x{rva:x})")

        print(f"\n  Table R9 at RVA 0x{table_r9_rva:x}:")
        data_r9 = rpm(handle, mod_base + table_r9_rva, 256)
        if data_r9:
            for j in range(0, 256, 16):
                row = data_r9[j:j+16]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
                print(f"    +0x{j:03x}: {hex_str}  {ascii_str}")

            print(f"\n    Pointer-like values:")
            for off in range(0, 256, 8):
                val = struct.unpack_from('<Q', data_r9, off)[0]
                if mod_base < val < mod_base + 0x2000000:
                    rva = val - mod_base
                    print(f"      +0x{off:02x}: 0x{val:016x} (RVA 0x{rva:x})")

        # =====================================================================
        # PART 2: Dump the common helper function at RVA ~0x369437
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 2: COMMON HELPER FUNCTION (TASKNODE RESOLVER)")
        print(f"{'='*80}")

        # The helper is called from getter2 at RVA 0x7DDEC0 + 0x2D = 0x7DDEED
        # E8 45 B5 B8 FF -> CALL (0x7DDEF2 + 0xFFB8B545)
        # 0xFFB8B545 = -0x474ABB
        # Target = 0x7DDEF2 - 0x474ABB = 0x369437

        helper_rva = 0x369437
        print(f"\n  Helper at RVA 0x{helper_rva:x}:")
        helper_code = rpm(handle, mod_base + helper_rva, 512)
        if helper_code:
            for j in range(0, min(512, len(helper_code)), 16):
                row = helper_code[j:min(j+16, len(helper_code))]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                print(f"    +0x{j:03x}: {hex_str}")
                if any(b == 0xC3 for b in row) and j > 0:
                    break

        # =====================================================================
        # PART 3: Verify helper CALL target from multiple getter2 functions
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 3: VERIFY COMMON HELPER FROM DIFFERENT GETTER2 FUNCTIONS")
        print(f"{'='*80}")

        getter2_rvas = [0x7ddec0, 0x7ddf50, 0x7ddfb0, 0x7de010, 0x7de230, 0x7de450]

        for g2_rva in getter2_rvas:
            g2_code = rpm(handle, mod_base + g2_rva, 128)
            if not g2_code:
                continue

            # Find E8 call in first 64 bytes
            for i in range(min(64, len(g2_code)) - 5):
                if g2_code[i] == 0xE8:
                    disp = struct.unpack_from('<i', g2_code, i+1)[0]
                    target_rva = g2_rva + i + 5 + disp
                    if 0x300000 < target_rva < 0x400000:  # in code section
                        # Also find R8 and R9 LEA targets
                        r8_target = None
                        r9_target = None
                        for k in range(i):
                            if k + 7 <= len(g2_code):
                                if g2_code[k] == 0x4C and g2_code[k+1] == 0x8D and g2_code[k+2] == 0x05:
                                    d = struct.unpack_from('<i', g2_code, k+3)[0]
                                    r8_target = g2_rva + k + 7 + d
                                elif g2_code[k] == 0x4C and g2_code[k+1] == 0x8D and g2_code[k+2] == 0x0D:
                                    d = struct.unpack_from('<i', g2_code, k+3)[0]
                                    r9_target = g2_rva + k + 7 + d

                        r8_str = f"R8=0x{r8_target:x}" if r8_target else "R8=?"
                        r9_str = f"R9=0x{r9_target:x}" if r9_target else "R9=?"
                        print(f"  Getter2 0x{g2_rva:x}: CALL 0x{target_rva:x} {r8_str} {r9_str}")
                        break

        # =====================================================================
        # PART 4: Try to find TTaskTable via COM object chain
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 4: SEARCHING FOR TTASKTABLE VIA KNOWN VTABLE")
        print(f"{'='*80}")

        # TTaskTable vtable is at RVA 0x1048648
        ttask_vtable_va = mod_base + 0x1048648
        print(f"  TTaskTable vtable VA: 0x{ttask_vtable_va:x}")

        # Instead of scanning all heap, scan specific .data areas where global pointers live
        # The Task descriptor at RVA 0xF8BDA0 is a global. Near it might be a TTaskTable pointer.

        print(f"\n  Scanning .data around descriptor globals for TTaskTable pointers...")

        # Scan a wide area of .data section for the TTaskTable vtable pointer
        scan_start_rva = 0xF80000
        scan_size = 0x100000  # 1MB

        scan_data = rpm(handle, mod_base + scan_start_rva, scan_size)
        if scan_data:
            found_count = 0
            for off in range(0, len(scan_data) - 8, 8):
                val = struct.unpack_from('<Q', scan_data, off)[0]
                if val == ttask_vtable_va:
                    rva = scan_start_rva + off
                    print(f"  FOUND TTaskTable instance at RVA 0x{rva:x}")

                    # Dump first 64 bytes of the instance
                    instance_data = rpm(handle, mod_base + rva, 256)
                    if instance_data:
                        for j in range(0, 256, 16):
                            row = instance_data[j:j+16]
                            hex_str = ' '.join(f'{b:02x}' for b in row)
                            print(f"      +0x{j:03x}: {hex_str}")

                    found_count += 1
                    if found_count >= 3:
                        break

            if found_count == 0:
                print("  Not found in .data section scan")

        # Also try scanning around 0x1200000 - 0x1600000 (.data segment)
        print(f"\n  Scanning wider .data range...")
        for scan_rva in [0x1200000, 0x1300000, 0x1400000, 0x1500000]:
            chunk = rpm(handle, mod_base + scan_rva, 0x100000)
            if chunk:
                for off in range(0, len(chunk) - 8, 8):
                    val = struct.unpack_from('<Q', chunk, off)[0]
                    if val == ttask_vtable_va:
                        rva = scan_rva + off
                        print(f"  FOUND at RVA 0x{rva:x}")

        # =====================================================================
        # PART 5: Scan for prop container by looking at the sub-object vtable
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 5: LOOK AT VFUNC[100] TO FIND PROP CONTAINER CLASS")
        print(f"{'='*80}")

        vf100_rva = 0x231D90
        vf100_code = rpm(handle, mod_base + vf100_rva, 256)
        if vf100_code:
            print(f"\n  vfunc[100] at RVA 0x{vf100_rva:x}:")
            for j in range(0, min(256, len(vf100_code)), 16):
                row = vf100_code[j:min(j+16, len(vf100_code))]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                print(f"    +0x{j:03x}: {hex_str}")
                if any(b == 0xC3 for b in row) and j > 16:
                    break

            # Look for LEA instructions that reference vtable addresses
            print(f"\n  LEA instructions (potential vtable references):")
            for i in range(len(vf100_code) - 7):
                # LEA reg, [RIP+disp]
                if vf100_code[i] in (0x48, 0x4C) and vf100_code[i+1] == 0x8D:
                    modrm = vf100_code[i+2]
                    if (modrm & 0xC7) == 0x05:  # [RIP+disp32]
                        disp = struct.unpack_from('<i', vf100_code, i+3)[0]
                        target_rva = vf100_rva + i + 7 + disp
                        reg = (modrm >> 3) & 7
                        regs = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI'] if vf100_code[i] == 0x48 else ['R8','R9','R10','R11','R12','R13','R14','R15']
                        if 0x800000 < target_rva < 0x2000000:
                            print(f"      +0x{i:03x}: LEA {regs[reg]}, [RIP+0x{disp:x}] -> RVA 0x{target_rva:x}")

        # =====================================================================
        # PART 6: Produce merged mapping with what we have
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 6: TAIL DATA LAYOUT ANALYSIS (first 30 entries)")
        print(f"{'='*80}")

        # Read all entries to understand the tail layout
        desc_va = mod_base + 0xf8bda0
        desc_data = rpm(handle, desc_va, 0x10)
        cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]

        type_names = {0:'null', 2:'i16', 3:'i32', 5:'i64', 8:'text', 9:'obj_ref',
                      11:'bool', 19:'date32', 20:'comp_dur', 21:'order_id', 29:'virtual', 72:'guid'}

        print(f"\n  {'idx':>4s} {'field_id':>10s} {'type':>6s} {'slot':>4s} {'flg':>10s} | +0x40   +0x48 +0x4C +0x50 +0x54 | +0x60    +0x70 +0x74 +0x78 +0x7C | +0x80 +0x84 +0x88")

        for idx in range(30):
            entry_va = cache_ptr + idx * 0xA8
            entry_data = rpm(handle, entry_va, 0xA8)
            if not entry_data or len(entry_data) < 0xA8:
                continue

            vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
            vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
            if vtable_rva not in (0x1050670, 0x0fb4a80):
                continue

            field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
            type_code = struct.unpack_from('<I', entry_data, 0x0C)[0]
            slot_idx = struct.unpack_from('<I', entry_data, 0x18)[0]
            flags = struct.unpack_from('<I', entry_data, 0x1C)[0]

            # Tail fields
            t40 = struct.unpack_from('<I', entry_data, 0x40)[0]
            t48 = struct.unpack_from('<I', entry_data, 0x48)[0]
            t4c = struct.unpack_from('<I', entry_data, 0x4C)[0]
            t50 = struct.unpack_from('<I', entry_data, 0x50)[0]
            t54 = struct.unpack_from('<i', entry_data, 0x54)[0]  # signed

            t60 = struct.unpack_from('<i', entry_data, 0x60)[0]  # signed or field_id
            t70 = struct.unpack_from('<i', entry_data, 0x70)[0]
            t74 = struct.unpack_from('<i', entry_data, 0x74)[0]
            t78 = struct.unpack_from('<i', entry_data, 0x78)[0]
            t7c = struct.unpack_from('<I', entry_data, 0x7C)[0]

            t80 = struct.unpack_from('<I', entry_data, 0x80)[0]
            t84 = struct.unpack_from('<I', entry_data, 0x84)[0]
            t88 = struct.unpack_from('<I', entry_data, 0x88)[0]

            tn = type_names.get(type_code, str(type_code))

            # Format t60 as hex if it looks like a field_id
            t60_str = f"0x{t60 & 0xFFFFFFFF:08x}" if (t60 & 0xFFFFFFFF) > 0x0B000000 else f"{t60:6d}"

            print(f"  {idx:4d} 0x{field_id:08x} {tn:>6s} {slot_idx:4d} 0x{flags:08x} | {t40:5d} {t48:5d} {t4c:5d} {t50:5d} {t54:6d} | {t60_str} {t70:6d} {t74:6d} {t78:6d} {t7c:6d} | {t80:5d} {t84:5d} {t88:5d}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
