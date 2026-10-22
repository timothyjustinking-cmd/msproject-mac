#!/usr/bin/env python3
"""Phase 2d: Detailed trace of vtable[158] actual read path.

Focus on the section from +0x800 to +0xC00 where slot_index is used
and the read functions are called, to understand how class-3 data
is actually fetched from the task's extended storage.
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

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    # Read full vtable[158] function
    vt158_rva = 0x239380
    vt158_addr = IMAGE_BASE + vt158_rva
    vt158_data = read_mem(handle, vt158_addr, 0x1800)
    if not vt158_data:
        print("ERROR: Can't read vtable[158]"); sys.exit(1)

    insns = list(md.disasm(vt158_data, vt158_addr))

    # Full annotated disassembly of the read section
    # Focus on the area around the getter calls
    print(f"\n{'='*100}")
    print(f"VTABLE[158] GETTER DISPATCH SECTION (full annotated)")
    print(f"{'='*100}")

    # Find the section where getter1/getter2 from PropInfoData are loaded
    # getter1 is at PropInfoData+0x00, getter2 at +0x08
    for i, insn in enumerate(insns):
        op = insn.op_str.lower()
        offset = insn.address - vt158_addr

        markers = []
        if '+0x4a]' in op: markers.append('STORAGE_CLASS')
        if '+0x28]' in op or '+ 0x28]' in op: markers.append('SLOT_INDEX')
        if '+0x48]' in op: markers.append('TYPE_CODE')
        if '+0x40]' in op: markers.append('FIELD_ID')
        if '+0x44]' in op: markers.append('FIELD_SIZE')
        if '+0x4c]' in op: markers.append('FLAGS')
        # getter1 at offset 0 from PropInfoData (R14)
        if 'r14]' in op and '+' not in op.replace('r14]', ''): markers.append('GETTER1_PTR')
        if '+8]' in op and 'r14' in op: markers.append('GETTER2_PTR')
        if '+0x10]' in op and 'r14' in op: markers.append('PROPINFO+0x10')
        if '+0x18]' in op and 'r14' in op: markers.append('PROPINFO+0x18')

        # Only print interesting instructions
        is_interesting = (
            bool(markers) or
            insn.mnemonic == 'call' or
            insn.mnemonic == 'ret' or
            (insn.mnemonic.startswith('cmp') and any(x in op for x in ['0x', 'r14', 'eax', 'ecx', 'edx'])) or
            (insn.mnemonic in ('je', 'jne', 'ja', 'jb', 'jbe', 'jae', 'jmp') and offset >= 0x400)
        )

        if is_interesting and 0x400 <= offset <= 0x1000:
            marker_str = f"  <-- {', '.join(markers)}" if markers else ''
            rva = insn.address - IMAGE_BASE
            print(f"  +{offset:04X} {rva:#08x}: {insn.mnemonic:8s} {insn.op_str}{marker_str}")

    # Now trace sub_23EC60 which we identified as the main getter dispatch
    print(f"\n{'='*100}")
    print(f"SUB_23EC60 (Main getter dispatch)")
    print(f"{'='*100}")

    sub_addr = IMAGE_BASE + 0x23EC60
    sub_data = read_mem(handle, sub_addr, 0x300)
    if sub_data:
        sub_insns = list(md.disasm(sub_data, sub_addr))
        for insn in sub_insns[:100]:
            rva = insn.address - IMAGE_BASE
            op = insn.op_str.lower()
            markers = []
            if '+0x988]' in op: markers.append('vtable+0x988')
            if '+0x320]' in op: markers.append('vtable+0x320')
            if '+0x58]' in op: markers.append('this+0x58')
            if '+0x80]' in op: markers.append('vtable+0x80')
            marker_str = f"  <-- {', '.join(markers)}" if markers else ''
            print(f"  {rva:#08x}: {insn.mnemonic:8s} {insn.op_str}{marker_str}")
            if insn.mnemonic == 'ret' or insn.mnemonic == 'int3':
                break

    # sub_1AB798 appears to be the actual indexed lookup
    print(f"\n{'='*100}")
    print(f"SUB_1AB798 (appears in indexed lookup chain)")
    print(f"{'='*100}")

    sub_addr = IMAGE_BASE + 0x1AB798
    sub_data = read_mem(handle, sub_addr, 0x400)
    if sub_data:
        sub_insns = list(md.disasm(sub_data, sub_addr))
        for insn in sub_insns[:120]:
            rva = insn.address - IMAGE_BASE
            op = insn.op_str.lower()
            markers = []
            if insn.mnemonic in ('imul', 'shl'):
                markers.append('MULTIPLY/SHIFT')
            if 'lea' == insn.mnemonic and '*' in op:
                markers.append('SCALED_INDEX')
            marker_str = f"  <-- {', '.join(markers)}" if markers else ''
            print(f"  {rva:#08x}: {insn.mnemonic:8s} {insn.op_str}{marker_str}")
            if insn.mnemonic == 'int3':
                break

    # Also look at the PropInfoData getter1/getter2 function pointers
    # These are stored at PropInfoData+0x00 and +0x08
    # Let's see if they're actual function pointers or vtable indices
    print(f"\n{'='*100}")
    print(f"TRACING getter1/getter2 FUNCTION POINTERS")
    print(f"{'='*100}")

    # Read a PropInfoData entry from the runtime cache
    # We need to find a live PropContainer first
    # The descriptor table at RVA 0xF8BDA0 has static descriptors
    desc_rva = 0xF8BDA0
    desc_addr = IMAGE_BASE + desc_rva
    desc_data = read_mem(handle, desc_addr, 0x30)
    if desc_data:
        print(f"  Static Task descriptor at RVA {desc_rva:#x}:")
        for off in range(0, min(0x28, len(desc_data)), 8):
            val = struct.unpack_from('<Q', desc_data, off)[0]
            print(f"    +{off:#04x}: {val:#018x}")

        # Count is at +0x10
        count = struct.unpack_from('<I', desc_data, 0x10)[0]
        print(f"    Count: {count}")

        # Cache pointer is at +0x18 (runtime allocated)
        cache_ptr = struct.unpack_from('<Q', desc_data, 0x18)[0]
        print(f"    Cache ptr: {cache_ptr:#018x}")

        if cache_ptr > 0x10000 and cache_ptr < 0x7FFFFFFFFFFF:
            # Read first few cache entries
            entry_size = 0x50
            cache_data = read_mem(handle, cache_ptr, entry_size * 5)
            if cache_data:
                print(f"\n  First 5 PropInfoData entries from cache:")
                for idx in range(5):
                    base = idx * entry_size
                    getter1 = struct.unpack_from('<Q', cache_data, base + 0x00)[0]
                    getter2 = struct.unpack_from('<Q', cache_data, base + 0x08)[0]
                    slot_idx = struct.unpack_from('<I', cache_data, base + 0x28)[0]
                    field_id = struct.unpack_from('<I', cache_data, base + 0x40)[0]
                    field_sz = struct.unpack_from('<I', cache_data, base + 0x44)[0]
                    type_code = struct.unpack_from('<H', cache_data, base + 0x48)[0]
                    stg_class = struct.unpack_from('<H', cache_data, base + 0x4A)[0]
                    flags = struct.unpack_from('<I', cache_data, base + 0x4C)[0]

                    g1_rva = getter1 - IMAGE_BASE if getter1 > IMAGE_BASE else 0
                    g2_rva = getter2 - IMAGE_BASE if getter2 > IMAGE_BASE else 0

                    print(f"    [{idx}] field_id={field_id:#010x} type={type_code} class={stg_class} "
                          f"slot={slot_idx} size={field_sz} flags={flags:#x}")
                    print(f"        getter1={getter1:#018x} (RVA {g1_rva:#x})")
                    print(f"        getter2={getter2:#018x} (RVA {g2_rva:#x})")
            else:
                print("    Cache data unreadable")
        else:
            print("    Cache pointer invalid (not initialized yet)")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
