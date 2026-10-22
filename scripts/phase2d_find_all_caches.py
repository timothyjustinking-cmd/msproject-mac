#!/usr/bin/env python3
"""Find ALL descriptor caches by scanning for propinfo vtable pointers
with any field_id prefix."""

import ctypes
import ctypes.wintypes as wt
import struct
import sys
from collections import defaultdict

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

    propinfo_vt = mod_base + PROPINFO_VT_RVA
    custom_vt = mod_base + CUSTOM_VT_RVA

    # Known: task at 0x130be10 (0x0b40), resource at 0x12e0d30 (0x0c40)
    # Looking for: assignment (0x0f40), and any others

    # Scan the entire .data section area (roughly 0x1200000 to 0x1500000)
    scan_ranges = [
        (0x1200000, 0x1500000),  # main data region
        (0x0f00000, 0x1200000),  # earlier data
    ]

    CHUNK_SIZE = 0x40000  # 256KB chunks
    caches = {}  # prefix -> (rva, count, first_fid)

    for scan_start, scan_end in scan_ranges:
        print(f"Scanning 0x{scan_start:x} to 0x{scan_end:x}...")

        for chunk_rva in range(scan_start, scan_end, CHUNK_SIZE):
            data = rpm(handle, mod_base + chunk_rva, CHUNK_SIZE)
            if not data:
                continue

            for off in range(0, len(data) - 16, 8):
                vt = struct.unpack_from('<Q', data, off)[0]
                if vt != propinfo_vt and vt != custom_vt:
                    continue

                fid = struct.unpack_from('<I', data, off + 8)[0]
                prefix = fid >> 16

                # Only consider likely field_id prefixes
                if prefix < 0x0a00 or prefix > 0x1000:
                    continue

                rva = chunk_rva + off

                # Check if this looks like a cache start
                # Must have 5+ consecutive entries with same prefix
                valid = 0
                for i in range(min(20, (len(data) - off) // REAL_STRIDE)):
                    eoff = off + i * REAL_STRIDE
                    if eoff + 16 > len(data):
                        break
                    evt = struct.unpack_from('<Q', data, eoff)[0]
                    if evt != propinfo_vt and evt != custom_vt:
                        break
                    efid = struct.unpack_from('<I', data, eoff + 8)[0]
                    if (efid >> 16) != prefix:
                        break
                    valid += 1

                if valid >= 10:
                    key = f"0x{prefix:04x}"
                    if key not in caches:
                        caches[key] = (rva, valid, fid)

    print(f"\nAll caches found:")

    NAMES = {
        0x0b40: "Task",
        0x0c40: "Resource",
        0x0f40: "Assignment",
        0x0b80: "TaskAssignment?",
    }

    for prefix_str, (rva, sample_count, first_fid) in sorted(caches.items()):
        # Count total entries
        total = 0
        for i in range(5000):
            addr = mod_base + rva + i * REAL_STRIDE
            edata = rpm(handle, addr, 16)
            if not edata:
                break
            vt = struct.unpack_from('<Q', edata, 0)[0]
            if vt != propinfo_vt and vt != custom_vt:
                break
            total += 1

        prefix_val = int(prefix_str, 16)
        name = NAMES.get(prefix_val, "Unknown")
        print(f"  {prefix_str} ({name:15s}) at RVA 0x{rva:07x}, "
              f"{total:5d} entries, first_fid=0x{first_fid:08x}")

    kernel32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
