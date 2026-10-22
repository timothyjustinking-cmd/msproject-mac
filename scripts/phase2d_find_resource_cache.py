#!/usr/bin/env python3
"""Find the resource descriptor cache by scanning for 0x0c4xxxxx field IDs
with propinfo vtable pointer."""

import ctypes
import ctypes.wintypes as wt
import struct
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

REAL_STRIDE = 0xA8
PROPINFO_VT_RVA = 0x1050670
CUSTOM_VT_RVA = 0x0fb4a80

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
    snap = kernel32.CreateToolhelp32Snapshot(0x2, 0)
    pe = PROCESSENTRY32()
    pe.dwSize = ctypes.sizeof(pe)
    pids = []
    if kernel32.Process32First(snap, ctypes.byref(pe)):
        while True:
            if b'WINPROJ' in pe.szExeFile.upper():
                pids.append(pe.th32ProcessID)
            if not kernel32.Process32Next(snap, ctypes.byref(pe)):
                break
    kernel32.CloseHandle(snap)
    return pids

def get_base(pid):
    snap = kernel32.CreateToolhelp32Snapshot(0x8 | 0x10, pid)
    me = MODULEENTRY32()
    me.dwSize = ctypes.sizeof(me)
    base = None
    if kernel32.Module32First(snap, ctypes.byref(me)):
        while True:
            if b'WINPROJ' in me.szModule.upper():
                base = ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value
                break
            if not kernel32.Module32Next(snap, ctypes.byref(me)):
                break
    kernel32.CloseHandle(snap)
    return base

def rpm(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = kernel32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok:
        return None
    return buf.raw[:n.value]


def main():
    pids = find_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    pid = pids[0]
    mod_base = get_base(pid)
    print(f"Module base: 0x{mod_base:x}")

    handle = kernel32.OpenProcess(0x0010 | 0x0400, False, pid)
    if not handle:
        print("ERROR: OpenProcess failed"); sys.exit(1)

    propinfo_vt = mod_base + PROPINFO_VT_RVA
    custom_vt = mod_base + CUSTOM_VT_RVA

    # Known task cache at 0x130be10, 1444 entries
    task_cache_rva = 0x130be10
    task_end_rva = task_cache_rva + 1444 * REAL_STRIDE
    print(f"Task cache: 0x{task_cache_rva:x} to 0x{task_end_rva:x}")

    # Scan a large region around the task cache for other caches
    # Resource field IDs: 0x0c40xxxx
    # Assignment field IDs: 0x0b80xxxx
    scan_start = task_cache_rva - 0x100000  # 1MB before
    scan_end = task_end_rva + 0x100000   # 1MB after

    print(f"Scanning RVA range 0x{scan_start:x} to 0x{scan_end:x}...")

    # Read in chunks
    CHUNK_SIZE = 0x20000  # 128KB

    caches_found = {}

    for chunk_start in range(max(scan_start, 0x100000), scan_end, CHUNK_SIZE):
        data = rpm(handle, mod_base + chunk_start, CHUNK_SIZE)
        if not data:
            continue

        for off in range(0, len(data) - 16, 8):
            vt = struct.unpack_from('<Q', data, off)[0]
            if vt != propinfo_vt and vt != custom_vt:
                continue

            fid = struct.unpack_from('<I', data, off + 8)[0]
            prefix = fid >> 16

            # Check if this is a potential cache start
            if prefix not in (0x0b40, 0x0c40, 0x0b80):
                continue

            rva = chunk_start + off

            # Verify it's a cache start: check next few entries at 0xA8 stride
            valid_count = 0
            for i in range(10):
                entry_off = off + i * REAL_STRIDE
                if entry_off + 16 > len(data):
                    break
                evt = struct.unpack_from('<Q', data, entry_off)[0]
                if evt != propinfo_vt and evt != custom_vt:
                    break
                efid = struct.unpack_from('<I', data, entry_off + 8)[0]
                eprefix = efid >> 16
                if eprefix != prefix:
                    break
                valid_count += 1

            if valid_count >= 5:
                key = f"0x{prefix:04x}"
                if key not in caches_found or rva < caches_found[key][0]:
                    caches_found[key] = (rva, valid_count, fid)

    print(f"\nCaches found:")
    for prefix, (rva, count, fid) in sorted(caches_found.items()):
        # Count total entries
        total = 0
        for i in range(3000):
            addr = mod_base + rva + i * REAL_STRIDE
            data = rpm(handle, addr, 16)
            if not data:
                break
            vt = struct.unpack_from('<Q', data, 0)[0]
            if vt != propinfo_vt and vt != custom_vt:
                break
            total += 1

        print(f"  prefix={prefix} at RVA 0x{rva:x}, "
              f"first_fid=0x{fid:08x}, total entries={total}")

    kernel32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
