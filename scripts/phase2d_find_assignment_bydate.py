#!/usr/bin/env python3
"""Find assignment record by searching for packed Start date value."""

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
def encode_date(dt_val):
    try:
        py_dt = datetime(dt_val.year, dt_val.month, dt_val.day,
                         dt_val.hour, dt_val.minute)
        d = (py_dt - EPOCH).days + 1
        m = py_dt.hour * 60 + py_dt.minute
        return (d << 16) | (m * 10)
    except:
        return None

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

    start_packed = encode_date(a.Start)
    finish_packed = encode_date(a.Finish)

    print(f"Assignment Start:  {decode_date(start_packed)} = 0x{start_packed:08x}")
    print(f"Assignment Finish: {decode_date(finish_packed)} = 0x{finish_packed:08x}")
    print(f"Assignment Work={a.Work} min, Cost=${a.Cost}, Units={a.Units}")
    print(f"Assignment UID={a.UniqueID}, TaskUID={a.TaskUniqueID}, ResUID={a.ResourceUniqueID}")

    # Search for the Start+Finish date pair (8 bytes total)
    # They should be adjacent in the flat buffer
    pair = struct.pack('<II', start_packed, finish_packed)
    print(f"\nSearching for Start+Finish pair: {pair.hex()}")

    # Enumerate memory
    MEM_COMMIT = 0x1000
    addr = 0
    regions = []
    mbi = MEMORY_BASIC_INFORMATION()

    while addr < 0x7FFFFFFFFFFF:
        result = k32.VirtualQueryEx(handle, ctypes.c_void_p(addr),
                                     ctypes.byref(mbi), ctypes.sizeof(mbi))
        if result == 0: break
        base_addr = mbi.BaseAddress or 0
        region_size = mbi.RegionSize or 0
        if mbi.State == MEM_COMMIT and region_size > 0:
            regions.append((base_addr, region_size))
        next_addr = base_addr + region_size
        if next_addr <= addr: break
        addr = next_addr

    print(f"Scanning {len(regions)} regions...")

    pair_locs = []
    for base, size in regions:
        if size > 0x10000000: continue
        data = rpm(handle, base, min(size, 0x1000000))
        if not data: continue
        pos = 0
        while pos < len(data) - 8:
            idx = data.find(pair, pos)
            if idx < 0: break
            pair_locs.append(base + idx)
            pos = idx + 4

    print(f"Found {len(pair_locs)} locations with Start+Finish pair")

    # Also search for just Start date alone
    start_bytes = struct.pack('<I', start_packed)
    start_locs = []
    for base, size in regions:
        if size > 0x10000000: continue
        data = rpm(handle, base, min(size, 0x1000000))
        if not data: continue
        pos = 0
        while pos < len(data) - 4:
            idx = data.find(start_bytes, pos)
            if idx < 0: break
            start_locs.append(base + idx)
            pos = idx + 4

    print(f"Found {len(start_locs)} locations with just Start date")

    # For each pair location, dump context and look for known values
    for i, loc in enumerate(pair_locs[:20]):
        print(f"\n--- Pair at 0x{loc:012x} ---")
        # Read 512 bytes before and after
        ctx_start = loc - 256
        data = rpm(handle, ctx_start, 512)
        if not data or len(data) < 512: continue

        pair_off = 256  # pair is at this offset in buffer

        # Look for TaskUniqueID (1) and ResourceUniqueID (1) before the dates
        # Look for Work doubles (various encodings)
        # Look for Cost doubles
        # Look for Units double (1.0)

        units_1_0 = struct.pack('<d', 1.0)

        print(f"  Checking for Units=1.0 nearby...")
        for off in range(0, 504, 8):
            if data[off:off+8] == units_1_0:
                rel = off - pair_off
                print(f"    Units=1.0 at pair{rel:+d}")

        # Check for doubles that could be Work or Cost
        print(f"  Interesting doubles nearby:")
        for off in range(0, 504, 8):
            v = struct.unpack_from('<d', data, off)[0]
            rel = off - pair_off
            if abs(rel) < 200 and v != 0 and abs(v) < 1e12 and abs(v) > 0.001:
                # Skip pointer-like values
                if v > 1e10 and not (v == 2400000.0 or v == 400000.0 or v == 2400.0 or v == 4000.0):
                    continue
                print(f"    pair{rel:+d}: {v}")

        # Check u32 values nearby for TaskUID/ResUID
        for off in range(max(0, pair_off - 128), pair_off, 4):
            v = struct.unpack_from('<I', data, off)[0]
            if v == 1:
                rel = off - pair_off
                print(f"    u32=1 at pair{rel:+d}")

        # Hex dump centered on pair
        print(f"  Hex dump (pair-128 to pair+128):")
        for j in range(pair_off - 128, pair_off + 128, 16):
            if j < 0 or j + 16 > len(data): continue
            addr_here = ctx_start + j
            hex_str = ' '.join(f'{data[j+k]:02x}' for k in range(16))
            marker = ""
            if j <= pair_off < j + 16:
                marker = " <-- START"
            print(f"    0x{addr_here:012x}: {hex_str}{marker}")

    k32.CloseHandle(handle)
    print("\nDone.")

if __name__ == '__main__':
    main()
