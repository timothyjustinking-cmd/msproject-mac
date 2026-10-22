#!/usr/bin/env python3
"""Phase 2d: Trace indexed storage (class 3) read path in vtable[158].

Disassemble the area around the storage_class==3 check and trace how
secondary_idx (PropInfoData+0x15/sub_type field) maps to array position.
Also examine the jbe 0x5037dc target (handles classes 0-10).
"""

import ctypes
import struct
import os
import sys

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
except ImportError:
    print("ERROR: capstone required"); sys.exit(1)

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
    if not hProcess:
        return None
    hMods = (ctypes.c_void_p * 1024)()
    cbNeeded = ctypes.c_ulong()
    if not psapi.EnumProcessModulesEx(hProcess, ctypes.byref(hMods), ctypes.sizeof(hMods),
                                       ctypes.byref(cbNeeded), 0x03):
        CloseHandle(hProcess)
        return None
    base = hMods[0]
    CloseHandle(hProcess)
    return base


def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok:
        return None
    return buf.raw[:n.value]


def disasm(data, base_addr, count=200):
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    return list(md.disasm(data, base_addr))[:count]


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
    if not IMAGE_BASE:
        print("ERROR: Can't get image base"); sys.exit(1)

    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")

    # The storage class dispatcher jbe target for classes 0-10
    # From the disassembly: cmp r10d, 0xa; jbe 0x...5037dc
    # This is a far jump target in another code section
    # The RVA offset from the jbe target minus IMAGE_BASE

    # Let's read the far jump targets from the storage dispatcher
    disp_rva = 0x4A1610
    disp_addr = IMAGE_BASE + disp_rva
    disp_data = read_mem(handle, disp_addr, 0x100)
    if not disp_data:
        print("ERROR: Can't read dispatcher"); sys.exit(1)

    insns = disasm(disp_data, disp_addr, 50)

    # Find the jbe target for classes 0-10
    class_0_10_target = None
    class_21_target = None
    class_11_target = None

    for i, insn in enumerate(insns):
        if insn.mnemonic == 'cmp' and '0xa' in insn.op_str:
            for j in range(i+1, min(i+3, len(insns))):
                if insns[j].mnemonic == 'jbe':
                    try:
                        class_0_10_target = int(insns[j].op_str, 0)
                    except ValueError:
                        pass
                    break
        if insn.mnemonic == 'cmp' and '0x15' in insn.op_str:
            for j in range(i+1, min(i+3, len(insns))):
                if insns[j].mnemonic == 'ja':
                    try:
                        class_21_target = int(insns[j].op_str, 0)
                    except ValueError:
                        pass
                    break

    print(f"\n  Class 0-10 handler target: {class_0_10_target:#x}" if class_0_10_target else "  Class 0-10 target: not found")
    print(f"  Class >21 handler target: {class_21_target:#x}" if class_21_target else "  Class >21 target: not found")

    # Disassemble the class 0-10 handler
    if class_0_10_target:
        handler_data = read_mem(handle, class_0_10_target, 0x300)
        if handler_data:
            print(f"\n{'='*90}")
            print(f"CLASS 0-10 HANDLER at {class_0_10_target:#x} (RVA {class_0_10_target - IMAGE_BASE:#x})")
            print(f"{'='*90}")

            handler_insns = disasm(handler_data, class_0_10_target, 150)
            for insn in handler_insns:
                markers = []
                op = insn.op_str.lower()
                if '+0x4a]' in op:
                    markers.append('storage_class')
                if '+0x28]' in op or '+ 0x28]' in op:
                    markers.append('slot_index')
                if '+0x48]' in op:
                    markers.append('type_code')
                if '+0x40]' in op:
                    markers.append('field_id')
                if '+0x44]' in op:
                    markers.append('field_size')

                marker_str = f"  <-- {', '.join(markers)}" if markers else ''
                print(f"  {insn.address:#x}: {insn.mnemonic:8s} {insn.op_str}{marker_str}")

    # Now look at vtable[158] around the key slot_index references
    # We know from earlier: slot_index loaded at multiple points
    # The key one is where it's used to index into an array
    vt158_addr = IMAGE_BASE + 0x239380

    # Focus on the area around 0x239807 (where slot_index is loaded for indexed read)
    # RVA 0x239807 - 0x239380 = 0x487 offset into vtable[158]
    focus_offset = 0x400
    focus_addr = vt158_addr + focus_offset
    focus_data = read_mem(handle, focus_addr, 0x800)
    if focus_data:
        print(f"\n{'='*90}")
        print(f"VTABLE[158] INDEXED READ SECTION (offset +{focus_offset:#x})")
        print(f"{'='*90}")

        focus_insns = disasm(focus_data, focus_addr, 300)

        # Look for patterns like:
        # mov reg, [R14+0x28]  ; load slot_index
        # imul/shl/lea         ; compute array offset
        # mov reg, [base + reg*scale]  ; indexed array access

        interesting = []
        for i, insn in enumerate(focus_insns):
            op = insn.op_str.lower()
            is_interesting = False
            markers = []

            if '+0x28]' in op:
                markers.append('slot_index')
                is_interesting = True
            if '+0x4a]' in op:
                markers.append('storage_class')
                is_interesting = True
            if insn.mnemonic in ('imul', 'shl', 'lea') and any(r in op for r in ['r8', 'edx', 'ecx']):
                is_interesting = True
            if insn.mnemonic == 'call':
                is_interesting = True
            if insn.mnemonic.startswith('cmp'):
                is_interesting = True
            if insn.mnemonic in ('jmp', 'je', 'jne', 'ja', 'jb', 'jbe', 'jae', 'jle', 'jge'):
                is_interesting = True
            if 'r14' in op and ('+' in op):
                is_interesting = True

            if is_interesting:
                marker_str = f"  <-- {', '.join(markers)}" if markers else ''
                interesting.append(f"  {insn.address:#x}: {insn.mnemonic:8s} {insn.op_str}{marker_str}")

        print(f"\n  Interesting instructions ({len(interesting)} of {len(focus_insns)} total):")
        for line in interesting[:80]:
            print(line)

    # Also disassemble the key helper sub_23EC60 (main getter dispatch)
    # and sub_23A624 which is called near slot_index loads
    helper_rva = 0x23A624
    helper_addr = IMAGE_BASE + helper_rva
    helper_data = read_mem(handle, helper_addr, 0x200)
    if helper_data:
        print(f"\n{'='*90}")
        print(f"SUB_{helper_rva:X} (called near slot_index loads) at {helper_addr:#x}")
        print(f"{'='*90}")

        helper_insns = disasm(helper_data, helper_addr, 80)
        for insn in helper_insns:
            print(f"  {insn.address:#x}: {insn.mnemonic:8s} {insn.op_str}")
            if insn.mnemonic == 'ret' or insn.mnemonic == 'int3':
                break

    # Also check sub_226950 which appears to be another helper
    helper2_rva = 0x226950
    helper2_addr = IMAGE_BASE + helper2_rva
    helper2_data = read_mem(handle, helper2_addr, 0x200)
    if helper2_data:
        print(f"\n{'='*90}")
        print(f"SUB_{helper2_rva:X} at {helper2_addr:#x}")
        print(f"{'='*90}")

        helper2_insns = disasm(helper2_data, helper2_addr, 60)
        for insn in helper2_insns:
            print(f"  {insn.address:#x}: {insn.mnemonic:8s} {insn.op_str}")
            if insn.mnemonic == 'ret' or insn.mnemonic == 'int3':
                break

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
