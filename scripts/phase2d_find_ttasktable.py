#!/usr/bin/env python3
"""Phase 2d: Find a TTaskTable instance and read its members.

Goal: find what R13 (4th param of vfunc[158]) points to.
R13 = R9 of vfunc[87] = 4th param of vfunc[87]
OR R9 = [something + 0xB18] at a later call site.

Strategy: find a TTaskTable instance by scanning for vtable pointers,
then read its key members (0x58, 0x5E8, etc.) to find the separate
0x40-stride PropInfoData array.
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
        ttable_vtable_rva = 0x1048648
        ttable_vtable_va = mod_base + ttable_vtable_rva
        propinfo_vtable_va = mod_base + 0x1050670
        cache_rva = 0x130be10
        cache_va = mod_base + cache_rva

        print(f"TTaskTable vtable VA: 0x{ttable_vtable_va:x}")
        print(f"PropInfoData vtable VA: 0x{propinfo_vtable_va:x}")
        print(f"Task cache base VA: 0x{cache_va:x}")

        vtable_bytes = struct.pack('<Q', ttable_vtable_va)

        # =====================================================================
        # Strategy 1: Scan .data for TTaskTable vtable pointers
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"SCANNING .DATA FOR TTaskTable VTABLE POINTERS")
        print(f"{'='*80}")

        ttable_instances = []

        for start_rva in range(0x1100000, 0x1600000, 0x100000):
            data = rpm(handle, mod_base + start_rva, 0x100000)
            if not data:
                continue

            for i in range(0, len(data) - 8, 8):
                if data[i:i+8] == vtable_bytes:
                    instance_va = mod_base + start_rva + i
                    instance_rva = start_rva + i
                    ttable_instances.append(instance_va)
                    print(f"  Found TTaskTable instance at VA 0x{instance_va:x} (RVA 0x{instance_rva:x})")

        if not ttable_instances:
            print("  No TTaskTable instances found in .data. Trying broader scan...")
            # The object might be on the heap
            # Try looking at known data structures that might reference it

            # TBkndTaskElem reads *(this+0x30)->vtable[25] to get TTaskTable
            # The global application object might have references

            # Alternative: use the descriptor to find related objects
            desc_va = mod_base + 0xf8bda0
            desc_data = rpm(handle, desc_va, 0x70)
            rtti_ptr = struct.unpack_from('<Q', desc_data, 0x00)[0]
            print(f"\n  Descriptor RTTI ptr: 0x{rtti_ptr:x} (RVA 0x{rtti_ptr - mod_base:x})")

            # Search for pointers TO the descriptor (someone references it)
            desc_bytes = struct.pack('<Q', desc_va)
            print(f"\n  Searching for references to descriptor VA 0x{desc_va:x}...")

            for start_rva in range(0x1100000, 0x1600000, 0x100000):
                data = rpm(handle, mod_base + start_rva, 0x100000)
                if not data:
                    continue
                for i in range(0, len(data) - 8, 8):
                    if data[i:i+8] == desc_bytes:
                        ref_va = mod_base + start_rva + i
                        ref_rva = start_rva + i
                        print(f"    Reference at VA 0x{ref_va:x} (RVA 0x{ref_rva:x})")
                        # Read surrounding context
                        context_va = ref_va - 0x40
                        ctx = rpm(handle, context_va, 0x100)
                        if ctx:
                            # Check if this is part of an object with a vtable
                            for off in range(0, 0x100, 8):
                                ptr = struct.unpack_from('<Q', ctx, off)[0]
                                ptr_rva = ptr - mod_base if mod_base < ptr < mod_base + 0x20000000 else None
                                if ptr_rva and 0x1000000 < ptr_rva < 0x1100000:
                                    # Could be a vtable pointer (.rdata)
                                    candidate_va = context_va + off
                                    candidate_rva = candidate_va - mod_base
                                    print(f"      Possible object at VA 0x{candidate_va:x} (RVA 0x{candidate_rva:x}), vtable RVA 0x{ptr_rva:x}")

        # =====================================================================
        # For each TTaskTable instance, read key members
        # =====================================================================
        for inst_va in ttable_instances[:5]:
            print(f"\n{'='*80}")
            print(f"TTaskTable INSTANCE AT VA 0x{inst_va:x}")
            print(f"{'='*80}")

            # Read the instance (at least 0xC00 bytes to cover member_0xB18)
            inst_data = rpm(handle, inst_va, 0xC00)
            if not inst_data:
                print("  Cannot read instance data")
                continue

            # Key members:
            # +0x00: vtable ptr
            # +0x58: secondary data base (for IMUL*0x78)
            # +0x5E8: cached prop container + 0x1E8

            vtable = struct.unpack_from('<Q', inst_data, 0x00)[0]
            member_58 = struct.unpack_from('<Q', inst_data, 0x58)[0]
            member_5e8 = struct.unpack_from('<Q', inst_data, 0x5E8)[0]

            m58_rva = member_58 - mod_base if mod_base < member_58 < mod_base + 0x20000000 else None
            m5e8_rva = member_5e8 - mod_base if mod_base < member_5e8 < mod_base + 0x20000000 else None

            print(f"  vtable: 0x{vtable:x}")
            print(f"  member_0x58: 0x{member_58:x} (RVA 0x{m58_rva:x})" if m58_rva else f"  member_0x58: 0x{member_58:x} (heap?)")
            print(f"  member_0x5E8: 0x{member_5e8:x} (RVA 0x{m5e8_rva:x})" if m5e8_rva else f"  member_0x5E8: 0x{member_5e8:x} (heap/null)")

            # Read member_0x58 contents
            if member_58:
                m58_data = rpm(handle, member_58, 0x200)
                if m58_data:
                    print(f"\n  member_0x58 contents (first 0x100 bytes):")
                    for j in range(0, min(0x100, len(m58_data)), 16):
                        row = m58_data[j:j+16]
                        hex_str = ' '.join(f'{b:02x}' for b in row)
                        print(f"    +0x{j:02x}: {hex_str}")

                    # Check if at 0x78 stride we see sensible data
                    print(f"\n  member_0x58 at 0x78 stride:")
                    for idx in range(3):
                        off = idx * 0x78
                        if off + 0x10 <= len(m58_data):
                            vals = []
                            for q in range(0, min(0x20, len(m58_data) - off), 4):
                                v = struct.unpack_from('<I', m58_data, off + q)[0]
                                vals.append(f"0x{v:08x}")
                            print(f"    [{idx}] +0x{off:02x}: {' '.join(vals[:8])}")

            # Read member_0x5E8 (prop container + 0x1E8)
            if member_5e8:
                # The prop container base is member_5e8 - 0x1E8
                container_base = member_5e8 - 0x1E8
                container_data = rpm(handle, container_base, 0x3A0)
                if container_data:
                    print(f"\n  PropContainer at VA 0x{container_base:x} (member_0x5E8 - 0x1E8)")
                    print(f"  Container first 0x40 bytes:")
                    for j in range(0, 0x40, 16):
                        row = container_data[j:j+16]
                        hex_str = ' '.join(f'{b:02x}' for b in row)
                        print(f"    +0x{j:02x}: {hex_str}")

                    # The +0x1E8 sub-object
                    print(f"\n  Sub-object at container+0x1E8:")
                    for j in range(0x1E8, min(0x280, len(container_data)), 16):
                        row = container_data[j:min(j+16, len(container_data))]
                        hex_str = ' '.join(f'{b:02x}' for b in row)
                        vals = []
                        for q in range(0, min(16, len(row)), 8):
                            if j + q + 8 <= len(container_data):
                                v = struct.unpack_from('<Q', container_data, j + q)[0]
                                vrva = v - mod_base if mod_base < v < mod_base + 0x20000000 else None
                                if vrva:
                                    vals.append(f"RVA 0x{vrva:x}")
                                elif v < 0x10000:
                                    vals.append(f"{v}")
                                else:
                                    vals.append(f"0x{v:x}")
                        print(f"    +0x{j:03x}: {hex_str}  ({', '.join(vals)})")

                    # Also check if the container has any pointer to a 0x40-stride array
                    print(f"\n  Scanning container for PropInfoData vtable ptrs...")
                    pi_vtable_bytes = struct.pack('<Q', propinfo_vtable_va)
                    for j in range(0, len(container_data) - 8, 8):
                        ptr = struct.unpack_from('<Q', container_data, j)[0]
                        if ptr == propinfo_vtable_va:
                            print(f"    PropInfoData vtable at container+0x{j:03x}")
                        # Also check if any pointer leads to a PropInfoData array
                        if mod_base < ptr < mod_base + 0x20000000:
                            ptr_rva = ptr - mod_base
                            if 0x1100000 < ptr_rva < 0x1600000:
                                # .data pointer - check if it starts with PropInfoData vtable
                                target = rpm(handle, ptr, 16)
                                if target and target[:8] == pi_vtable_bytes:
                                    print(f"    container+0x{j:03x} -> RVA 0x{ptr_rva:x} -> PropInfoData vtable! (possible array)")
                                    # Check 0x40 stride
                                    target2 = rpm(handle, ptr + 0x40, 8)
                                    if target2 and target2[:8] == pi_vtable_bytes:
                                        print(f"      -> 0x40 stride: YES, next entry also has PropInfoData vtable!")
                                    target3 = rpm(handle, ptr + 0xA8, 8)
                                    if target3 and target3[:8] == pi_vtable_bytes:
                                        print(f"      -> 0xA8 stride: YES (matches known cache)")

            # Read member around 0xB18 (used in second call: MOV R9, [RBX+0xB18])
            if len(inst_data) >= 0xB20:
                member_b18 = struct.unpack_from('<Q', inst_data, 0xB18)[0]
                mb18_rva = member_b18 - mod_base if mod_base < member_b18 < mod_base + 0x20000000 else None
                print(f"\n  member_0xB18: 0x{member_b18:x}", end="")
                if mb18_rva:
                    print(f" (RVA 0x{mb18_rva:x})")
                else:
                    print(f" (heap or value)")

                if member_b18 and member_b18 > 0x10000:
                    b18_data = rpm(handle, member_b18, 0x100)
                    if b18_data:
                        # Check if this points to a 0x40-stride PropInfoData array
                        is_pi = b18_data[:8] == pi_vtable_bytes
                        print(f"    Points to: {'PropInfoData!' if is_pi else 'other data'}")
                        if is_pi:
                            # Check stride
                            next40 = rpm(handle, member_b18 + 0x40, 8)
                            if next40:
                                is_40 = next40[:8] == pi_vtable_bytes
                                print(f"    At +0x40: {'PropInfoData (0x40 stride!)' if is_40 else 'not PropInfoData'}")
                            next_a8 = rpm(handle, member_b18 + 0xA8, 8)
                            if next_a8:
                                is_a8 = next_a8[:8] == pi_vtable_bytes
                                print(f"    At +0xA8: {'PropInfoData (0xA8 stride)' if is_a8 else 'not PropInfoData'}")
                        else:
                            print(f"    First 0x40 bytes:")
                            for j in range(0, min(0x40, len(b18_data)), 16):
                                row = b18_data[j:j+16]
                                hex_str = ' '.join(f'{b:02x}' for b in row)
                                print(f"      +0x{j:02x}: {hex_str}")

            # Dump ALL pointer members that reference .data
            print(f"\n  ALL .data-pointing members in first 0xC00 bytes:")
            data_ptrs = []
            for j in range(0, min(0xC00, len(inst_data)) - 8, 8):
                ptr = struct.unpack_from('<Q', inst_data, j)[0]
                if ptr == cache_va:
                    data_ptrs.append((j, ptr, "CACHE BASE!"))
                elif mod_base < ptr < mod_base + 0x20000000:
                    ptr_rva = ptr - mod_base
                    if 0x1100000 < ptr_rva < 0x1600000:
                        target = rpm(handle, ptr, 8)
                        if target and target[:8] == pi_vtable_bytes:
                            data_ptrs.append((j, ptr, f"RVA 0x{ptr_rva:x} -> PropInfoData!"))
                        elif ptr == cache_va:
                            data_ptrs.append((j, ptr, f"CACHE BASE"))
                        else:
                            data_ptrs.append((j, ptr, f"RVA 0x{ptr_rva:x}"))

            for off, ptr, note in data_ptrs:
                print(f"    +0x{off:03x}: 0x{ptr:x} — {note}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
