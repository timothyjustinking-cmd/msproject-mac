#!/usr/bin/env python3
"""Phase 2d: Trace how slot_index dispatches for indexed storage classes.

The slot_index (PropInfoData+0x28) is passed to sub-functions that handle
class 3 (Baseline1-10/Enterprise), class 5 (Baseline0/user custom), class 9 (lookup).

By tracing what happens to slot_index in the call targets, we can understand
how indexed fields resolve to actual storage locations.
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


def dump_func(handle, mod_base, rva, label, max_bytes=512):
    """Dump and annotate a function."""
    code = rpm(handle, mod_base + rva, max_bytes)
    if not code:
        print(f"  Cannot read function at RVA 0x{rva:x}")
        return None

    # Find function end
    func_end = min(max_bytes, len(code))
    for i in range(20, len(code)):
        if code[i] == 0xC3 and code[i-1] not in (0x0F, 0x66):
            # Verify it's really the end
            if i + 1 < len(code) and code[i+1] in (0x90, 0xCC, 0x40, 0x48, 0x41, 0x4C, 0x45):
                func_end = i + 1
                break

    print(f"\n  {label} at RVA 0x{rva:x} (~{func_end} bytes):")

    # Hex dump first 128 bytes
    for j in range(0, min(func_end, 128), 16):
        row = code[j:min(j+16, len(code))]
        hex_str = ' '.join(f'{b:02x}' for b in row)
        print(f"    +0x{j:03x}: {hex_str}")

    # Find key patterns
    calls = []
    for i in range(min(func_end, len(code)) - 5):
        if code[i] == 0xE8:
            disp = struct.unpack_from('<i', code, i+1)[0]
            target = rva + i + 5 + disp
            if 0x1000 < target < 0x2000000:
                calls.append((i, target))

    if calls:
        print(f"    CALL targets:")
        for off, target in calls:
            print(f"      +0x{off:03x}: CALL 0x{target:x}")

    # Find MUL/IMUL (array indexing patterns)
    for i in range(min(func_end, len(code)) - 4):
        # IMUL reg, reg, imm
        if code[i] in (0x69, 0x6B):
            if code[i] == 0x6B:  # IMUL reg, r/m, imm8
                imm = code[i+2] if (code[i+1] & 0xC0) == 0xC0 else 0
                if imm > 0:
                    print(f"    +0x{i:03x}: IMUL ?, ?, 0x{imm:02x} ({imm}) (array stride)")

        # 4C 6B or 48 6B (REX IMUL)
        if i + 3 < len(code) and code[i] in (0x48, 0x49, 0x4C, 0x4D) and code[i+1] == 0x6B:
            modrm = code[i+2]
            if (modrm & 0xC0) == 0xC0:
                imm = code[i+3]
                src = modrm & 7
                dst = (modrm >> 3) & 7
                if code[i] & 1: src += 8
                if code[i] & 4: dst += 8
                regs = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI',
                        'R8','R9','R10','R11','R12','R13','R14','R15']
                print(f"    +0x{i:03x}: IMUL {regs[dst]}, {regs[src]}, 0x{imm:02x} ({imm}) (array stride)")

    return code[:func_end]


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
        # The key call targets from vtable[158] that receive slot_index:
        # These are the sub-dispatchers for different storage mechanisms

        targets = [
            (0x226E2C, "sub_226E2C (called from +0x3F7, likely flags/direct)"),
            (0x248B40, "sub_248B40 (called from +0x468, likely flags/direct)"),
            (0x23A624, "sub_23A624 (called from +0x6DA)"),
            (0x2682A4, "sub_2682A4 (called from +0x603, likely text/string)"),
            (0x226950, "sub_226950 (called from +0x82F)"),
            (0x4A1610, "sub_4A1610 (called from +0x6F9)"),
            (0x282C60, "sub_282C60 (called from +0xCCB)"),
            (0x268560, "sub_268560 (called from +0xD87)"),
            (0x23EC60, "sub_23EC60 (called from +0xD25, +0xE43)"),
        ]

        print(f"\n{'='*90}")
        print(f"SLOT_INDEX DISPATCH TARGETS")
        print(f"{'='*90}")

        for rva, label in targets:
            dump_func(handle, mod_base, rva, label, max_bytes=384)

        # Now let's also look at the PropInfoData layout to understand
        # what slot_index means in context
        print(f"\n{'='*90}")
        print(f"PROPINFODATA ENTRY LAYOUT (from field descriptors)")
        print(f"{'='*90}")

        # Read the descriptor table for Task fields
        # TTaskTable descriptor at RVA 0xF8BDA0 (from previous analysis)
        desc_rva = 0xF8BDA0
        desc_data = rpm(handle, mod_base + desc_rva, 64)
        if desc_data:
            print(f"\n  Task descriptor header at RVA 0x{desc_rva:x}:")
            for j in range(0, 64, 16):
                row = desc_data[j:min(j+16, len(desc_data))]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                print(f"    +0x{j:03x}: {hex_str}")

            # Extract cache pointer and stride
            cache_ptr_data = rpm(handle, mod_base + desc_rva + 0x10, 8)
            if cache_ptr_data:
                cache_ptr = struct.unpack_from('<Q', cache_ptr_data, 0)[0]
                print(f"\n  Cache array pointer: 0x{cache_ptr:x}")

                # Read first 3 cache entries to verify layout
                entry_stride = 0xA8
                for e in range(3):
                    entry = rpm(handle, cache_ptr + e * entry_stride, entry_stride)
                    if entry:
                        # PropInfoData header (0x40 bytes)
                        print(f"\n  Entry {e} (offset 0x{e * entry_stride:x}):")
                        print(f"    Header (0x40 bytes):")
                        for j in range(0, 0x40, 16):
                            row = entry[j:min(j+16, len(entry))]
                            hex_str = ' '.join(f'{b:02x}' for b in row)
                            print(f"      +0x{j:02x}: {hex_str}")

                        # Tail data (0x68 bytes)
                        print(f"    Tail (0x68 bytes):")
                        for j in range(0x40, min(0xA8, len(entry)), 16):
                            row = entry[j:min(j+16, len(entry))]
                            hex_str = ' '.join(f'{b:02x}' for b in row)
                            print(f"      +0x{j:02x}: {hex_str}")

                        # Decode tail fields
                        if len(entry) >= 0x68:
                            field_id = struct.unpack_from('<I', entry, 0x40)[0]
                            field_size = struct.unpack_from('<I', entry, 0x44)[0]
                            type_code = struct.unpack_from('<H', entry, 0x48)[0]
                            storage_class = struct.unpack_from('<H', entry, 0x4A)[0]
                            flags = struct.unpack_from('<I', entry, 0x4C)[0]
                            dirty_bit = struct.unpack_from('<I', entry, 0x50)[0]
                            linked_field = struct.unpack_from('<I', entry, 0x54)[0]
                            secondary = struct.unpack_from('<I', entry, 0x58)[0]

                            # Header fields
                            slot_index = struct.unpack_from('<I', entry, 0x28)[0]
                            getter1_ptr = struct.unpack_from('<Q', entry, 0x00)[0]
                            getter2_ptr = struct.unpack_from('<Q', entry, 0x08)[0]

                            g1_rva = getter1_ptr - mod_base if getter1_ptr > mod_base else 0
                            g2_rva = getter2_ptr - mod_base if getter2_ptr > mod_base else 0

                            print(f"    Decoded:")
                            print(f"      field_id:      0x{field_id:08x}")
                            print(f"      field_size:    {field_size}")
                            print(f"      type_code:     {type_code}")
                            print(f"      storage_class: {storage_class}")
                            print(f"      flags:         0x{flags:08x}")
                            print(f"      dirty_bit:     {dirty_bit}")
                            print(f"      linked_field:  0x{linked_field:08x}")
                            print(f"      secondary:     {secondary}")
                            print(f"      slot_index:    {slot_index}")
                            print(f"      getter1 RVA:   0x{g1_rva:x}")
                            print(f"      getter2 RVA:   0x{g2_rva:x}")

        # Read a sample of entries with different storage classes
        print(f"\n{'='*90}")
        print(f"SAMPLE ENTRIES BY STORAGE CLASS")
        print(f"{'='*90}")

        # We know the descriptor table has 1444 entries at stride 0xA8
        # Read them all and group by storage class
        if cache_ptr:
            sc_samples = {}  # storage_class -> list of (field_id, slot_index, getter1_rva, getter2_rva)

            for e in range(1444):
                entry = rpm(handle, cache_ptr + e * entry_stride, entry_stride)
                if not entry or len(entry) < 0x60:
                    continue

                field_id = struct.unpack_from('<I', entry, 0x40)[0]
                type_code = struct.unpack_from('<H', entry, 0x48)[0]
                storage_class = struct.unpack_from('<H', entry, 0x4A)[0]
                slot_index = struct.unpack_from('<I', entry, 0x28)[0]
                getter1_ptr = struct.unpack_from('<Q', entry, 0x00)[0]
                getter2_ptr = struct.unpack_from('<Q', entry, 0x08)[0]

                g1_rva = getter1_ptr - mod_base if getter1_ptr > mod_base else 0
                g2_rva = getter2_ptr - mod_base if getter2_ptr > mod_base else 0

                if storage_class not in sc_samples:
                    sc_samples[storage_class] = []

                sc_samples[storage_class].append({
                    'field_id': field_id,
                    'type': type_code,
                    'slot': slot_index,
                    'g1': g1_rva,
                    'g2': g2_rva,
                })

            for sc in sorted(sc_samples.keys()):
                entries = sc_samples[sc]
                slots = sorted(set(e['slot'] for e in entries))
                g1s = sorted(set(e['g1'] for e in entries))
                g2s = sorted(set(e['g2'] for e in entries))

                print(f"\n  Storage class {sc}: {len(entries)} fields")
                print(f"    Slot range: {min(slots)}-{max(slots)}")
                print(f"    Unique getter1 RVAs: {', '.join(f'0x{g:x}' for g in g1s[:5])}")
                print(f"    Unique getter2 RVAs: {', '.join(f'0x{g:x}' for g in g2s[:5])}")

                # Show first 3 entries
                for e in entries[:3]:
                    print(f"    field_id=0x{e['field_id']:08x} type={e['type']:2d} slot={e['slot']:4d} g1=0x{e['g1']:x} g2=0x{e['g2']:x}")

    finally:
        kernel32.CloseHandle(handle)


if __name__ == '__main__':
    main()
