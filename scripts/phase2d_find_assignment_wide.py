#!/usr/bin/env python3
"""Wide search for assignment record using multiple distinctive values."""

import ctypes
import ctypes.wintypes as wt
import struct
import sys
from datetime import datetime, timedelta

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

try:
    import win32com.client
except ImportError:
    print("ERROR: pywin32 required"); sys.exit(1)

k32 = ctypes.WinDLL('kernel32', use_last_error=True)

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ('BaseAddress', ctypes.c_void_p),
        ('AllocationBase', ctypes.c_void_p),
        ('AllocationProtect', wt.DWORD),
        ('RegionSize', ctypes.c_size_t),
        ('State', wt.DWORD),
        ('Protect', wt.DWORD),
        ('Type', wt.DWORD),
    ]

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

def find_pid():
    snap = k32.CreateToolhelp32Snapshot(0x2, 0)
    pe = PROCESSENTRY32()
    pe.dwSize = ctypes.sizeof(pe)
    pids = []
    if k32.Process32First(snap, ctypes.byref(pe)):
        while True:
            if b'WINPROJ' in pe.szExeFile.upper():
                pids.append(pe.th32ProcessID)
            if not k32.Process32Next(snap, ctypes.byref(pe)):
                break
    k32.CloseHandle(snap)
    return pids

def rpm(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = k32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]

EPOCH = datetime(1984, 1, 1)
def decode_date(packed):
    if packed == 0xFFFFFFFF: return "NA"
    if packed == 0: return "0"
    d = packed >> 16
    m = (packed & 0xFFFF) // 10
    if d < 1 or d > 50000 or m >= 1440:
        return f"inv(0x{packed:08x})"
    dt = EPOCH + timedelta(days=d - 1)
    return f"{dt.strftime('%Y-%m-%d')} {m // 60:02d}:{m % 60:02d}"

def main():
    pids = find_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    pid = pids[0]
    handle = k32.OpenProcess(0x0010 | 0x0400, False, pid)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    t1 = proj.Tasks.Item(1)
    a = t1.Assignments.Item(1)

    uid = a.UniqueID  # 2097159 = 0x200007
    work = a.Work      # 2400.0 min
    cost = a.Cost      # 4000.0
    units = a.Units    # 1.0

    print(f"Assignment: UID={uid} (0x{uid:08x}), Work={work}, Cost={cost}, Units={units}")

    # Search for the UniqueID as u32 - very distinctive value
    uid_bytes = struct.pack('<I', uid)
    print(f"Searching for UID bytes: {uid_bytes.hex()}")

    # Also try different Work encodings
    work_encodings = {
        'Work×1000': struct.pack('<d', work * 1000),   # 2400000.0
        'Work_raw': struct.pack('<d', work),             # 2400.0
        'Work×10': struct.pack('<d', work * 10),         # 24000.0
        'Work×60000': struct.pack('<d', work * 60000),   # minute→ms×1000?
    }

    # VirtualQueryEx to enumerate committed memory regions
    MEM_COMMIT = 0x1000
    addr = 0
    regions = []
    mbi = MEMORY_BASIC_INFORMATION()

    while addr < 0x7FFFFFFFFFFF:
        result = ctypes.windll.kernel32.VirtualQueryEx(
            handle, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
        if result == 0:
            break
        base_addr = mbi.BaseAddress or 0
        region_size = mbi.RegionSize or 0
        if mbi.State == MEM_COMMIT and region_size > 0:
            regions.append((base_addr, region_size))
        next_addr = base_addr + region_size
        if next_addr <= addr:
            break
        addr = next_addr

    print(f"Scanning {len(regions)} committed memory regions...")

    # Phase 1: Find UID locations
    uid_locs = []
    total_scanned = 0
    for base, size in regions:
        if size > 0x10000000:  # skip huge regions
            continue
        data = rpm(handle, base, min(size, 0x1000000))
        if not data:
            continue
        total_scanned += len(data)
        pos = 0
        while pos < len(data) - 4:
            idx = data.find(uid_bytes, pos)
            if idx < 0:
                break
            uid_locs.append(base + idx)
            pos = idx + 4

    print(f"Scanned {total_scanned / 1024 / 1024:.1f} MB")
    print(f"Found {len(uid_locs)} locations with UID=0x{uid:08x}")

    # For each UID location, check if it could be the assignment record
    # The UID might be at a different offset than expected. Let's check nearby for Work values.
    print(f"\nChecking each UID location for nearby Work/Cost/Units values...")

    for loc in uid_locs:
        # Read 512 bytes around the UID location
        read_start = loc - 256
        data = rpm(handle, read_start, 512)
        if not data or len(data) < 512:
            continue

        uid_off = 256  # UID is at offset 256 in our read buffer

        # Search for Work×1000=2400000.0 double within this 512-byte window
        work_target = struct.pack('<d', work * 1000)
        work_raw_target = struct.pack('<d', work)

        for off in range(0, 504, 8):
            if data[off:off+8] == work_target or data[off:off+8] == work_raw_target:
                rel = off - uid_off
                w = struct.unpack_from('<d', data, off)[0]

                # Also check for Cost nearby
                cost_hits = []
                for co in range(0, 504, 8):
                    cv = struct.unpack_from('<d', data, co)[0]
                    if abs(cv - cost * 100) < 0.01 or abs(cv - cost) < 0.01:
                        cost_hits.append((co - uid_off, cv))

                units_hits = []
                for uo in range(0, 504, 8):
                    uv = struct.unpack_from('<d', data, uo)[0]
                    if abs(uv - units) < 0.001 and uo != off:
                        units_hits.append((uo - uid_off, uv))

                if cost_hits or units_hits:
                    print(f"\n  UID at 0x{loc:012x}, Work({w}) at UID+{rel}")
                    print(f"    Cost matches: {[(f'+{r}', v) for r, v in cost_hits]}")
                    print(f"    Units matches: {[(f'+{r}', v) for r, v in units_hits]}")

                    # Hex dump around UID
                    dump_start = max(0, uid_off - 64)
                    dump_end = min(512, uid_off + 192)
                    print(f"    Hex dump (UID-64 to UID+192):")
                    for i in range(dump_start, dump_end, 16):
                        addr_here = read_start + i
                        hex_str = ' '.join(f'{data[i+j]:02x}' for j in range(min(16, dump_end-i)))
                        marker = " <-- UID" if i <= uid_off < i + 16 else ""
                        print(f"      0x{addr_here:012x}: {hex_str}{marker}")

    k32.CloseHandle(handle)
    print("\nDone.")

if __name__ == '__main__':
    main()
