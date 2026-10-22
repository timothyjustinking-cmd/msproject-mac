#!/usr/bin/env python3
"""Phase 2d: Global differential scan to find class-1 field storage.

The TASKNODE sparse block is separately allocated from the task object.
Direct (class 1) fields are stored at fixed offsets in a different structure.

Strategy:
1. Set Priority to a unique value (987)
2. Search ALL heap memory for that value as i32
3. Change Priority to another unique value (543)
4. Search again for the new value
5. The addresses that were 987 and are now 543 = Priority's storage
6. Read surrounding memory to find the task object structure
"""

import ctypes
import struct
import os
import sys
import time

try:
    import win32com.client
except ImportError:
    print("ERROR: pywin32 required"); sys.exit(1)

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


def scan_all_regions(handle):
    regions = []
    MEM_COMMIT = 0x1000
    READABLE = {0x02, 0x04, 0x20, 0x40}

    class MBI(ctypes.Structure):
        _fields_ = [
            ("BaseAddress", ctypes.c_void_p),
            ("AllocationBase", ctypes.c_void_p),
            ("AllocationProtect", ctypes.c_ulong),
            ("PartitionId", ctypes.c_ushort),
            ("RegionSize", ctypes.c_size_t),
            ("State", ctypes.c_ulong),
            ("Protect", ctypes.c_ulong),
            ("Type", ctypes.c_ulong),
        ]

    addr = 0
    mbi = MBI()
    while addr < 0x7FFFFFFFFFFF:
        result = k32.VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
        if result == 0:
            break
        if mbi.State == MEM_COMMIT and mbi.Protect in READABLE:
            if 0x1000 <= mbi.RegionSize <= 0x10000000:
                regions.append((mbi.BaseAddress, mbi.RegionSize))
        addr = (mbi.BaseAddress or 0) + mbi.RegionSize
        if addr <= (mbi.BaseAddress or 0):
            break
    return regions


def find_all_pattern(handle, regions, pattern):
    """Search all regions for a pattern, return ALL hits."""
    hits = []
    for base, size in regions:
        data = read_mem(handle, base, size)
        if not data:
            continue
        offset = 0
        while True:
            pos = data.find(pattern, offset)
            if pos == -1:
                break
            hits.append(base + pos)
            offset = pos + 1
    return hits


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    if task.Name in ('0d', '10d', ''):
        task.Name = "Task 1"
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    # Enumerate regions
    regions = scan_all_regions(handle)
    total = sum(s for _, s in regions)
    print(f"  {len(regions)} regions, {total / 1024 / 1024:.0f} MB")

    # Phase 1: Set Priority to unique value A
    val_a = 987
    val_b = 543
    print(f"\n  Setting Priority = {val_a}")
    task.SetField(188743705, str(val_a))
    time.sleep(0.3)

    # Search for val_a as i32
    pattern_a = struct.pack('<i', val_a)
    print(f"  Scanning all memory for i32 {val_a} ({pattern_a.hex()})...")
    hits_a = find_all_pattern(handle, regions, pattern_a)
    print(f"  Found {len(hits_a)} hits")

    # Phase 2: Change Priority to value B
    print(f"\n  Setting Priority = {val_b}")
    task.SetField(188743705, str(val_b))
    time.sleep(0.3)

    # Search for val_b as i32
    pattern_b = struct.pack('<i', val_b)
    print(f"  Scanning all memory for i32 {val_b} ({pattern_b.hex()})...")
    hits_b = find_all_pattern(handle, regions, pattern_b)
    print(f"  Found {len(hits_b)} hits")

    # Find intersection
    set_a = set(hits_a)
    set_b = set(hits_b)
    intersection = set_a & set_b
    print(f"\n  Addresses that had {val_a} and now have {val_b}: {len(intersection)}")

    # Also check: addresses that had val_a and now have val_b verified
    confirmed = []
    for addr in sorted(intersection):
        data = read_mem(handle, addr, 4)
        if data and struct.unpack_from('<i', data, 0)[0] == val_b:
            confirmed.append(addr)

    print(f"  Confirmed {len(confirmed)} Priority storage location(s)")

    if not confirmed:
        # Try i16
        print(f"\n  Retrying with i16...")
        pattern_a16 = struct.pack('<h', val_a)
        pattern_b16 = struct.pack('<h', val_b)

        # Reset and re-scan
        task.SetField(188743705, str(val_a))
        time.sleep(0.3)
        hits_a16 = set(find_all_pattern(handle, regions, pattern_a16))
        print(f"  i16 {val_a} hits: {len(hits_a16)}")

        task.SetField(188743705, str(val_b))
        time.sleep(0.3)
        hits_b16 = set(find_all_pattern(handle, regions, pattern_b16))
        print(f"  i16 {val_b} hits: {len(hits_b16)}")

        intersection16 = hits_a16 & hits_b16
        print(f"  Intersection: {len(intersection16)}")
        for addr in sorted(intersection16)[:20]:
            data = read_mem(handle, addr, 2)
            if data and struct.unpack_from('<h', data, 0)[0] == val_b:
                confirmed.append(addr)

        print(f"  Confirmed i16 locations: {len(confirmed)}")

    # Also do PercentComplete differential
    print(f"\n{'='*100}")
    print(f"DIFFERENTIAL SCAN FOR PERCENTCOMPLETE")
    print(f"{'='*100}")

    pct_a = 73
    pct_b = 37
    task.SetField(188743712, str(pct_a))
    time.sleep(0.3)
    pct_hits_a = set(find_all_pattern(handle, regions, struct.pack('<i', pct_a)))
    print(f"  PercentComplete={pct_a} as i32: {len(pct_hits_a)} hits")

    task.SetField(188743712, str(pct_b))
    time.sleep(0.3)
    pct_hits_b = set(find_all_pattern(handle, regions, struct.pack('<i', pct_b)))
    print(f"  PercentComplete={pct_b} as i32: {len(pct_hits_b)} hits")

    pct_confirmed = []
    pct_intersection = pct_hits_a & pct_hits_b
    print(f"  Intersection: {len(pct_intersection)}")
    for addr in sorted(pct_intersection):
        data = read_mem(handle, addr, 4)
        if data and struct.unpack_from('<i', data, 0)[0] == pct_b:
            pct_confirmed.append(addr)
    print(f"  Confirmed: {len(pct_confirmed)}")

    # Analyze confirmed Priority locations
    all_confirmed = []
    for addr in confirmed:
        all_confirmed.append(('Priority', addr))
    for addr in pct_confirmed:
        all_confirmed.append(('PctComplete', addr))

    if all_confirmed:
        print(f"\n{'='*100}")
        print(f"CONFIRMED FIELD LOCATIONS")
        print(f"{'='*100}")

        for field_name, addr in all_confirmed:
            print(f"\n  {field_name} at {addr:#x}")

        # Group by proximity to find common task objects
        all_addrs = sorted(set(a for _, a in all_confirmed))

        # For each Priority location, look for nearby PctComplete and vice versa
        for pname, paddr in all_confirmed:
            if pname == 'Priority':
                for pcname, pcaddr in all_confirmed:
                    if pcname == 'PctComplete' and abs(paddr - pcaddr) < 0x1000:
                        print(f"\n  CO-LOCATED: Priority={paddr:#x}, PctComplete={pcaddr:#x}, "
                              f"delta={pcaddr - paddr:+d}")

        # Hex dump around confirmed locations
        for field_name, addr in all_confirmed[:3]:
            ctx = read_mem(handle, addr - 0x100, 0x300)
            if ctx:
                print(f"\n  Context around {field_name} at {addr:#x}:")

                # Look for vtable pointers
                for off in range(0, len(ctx) - 8, 8):
                    qword = struct.unpack_from('<Q', ctx, off)[0]
                    if IMAGE_BASE < qword < IMAGE_BASE + 0x2000000:
                        rva = qword - IMAGE_BASE
                        if 0xE00000 < rva < 0x1200000:
                            abs_off = addr - 0x100 + off
                            print(f"    Vtable candidate at {abs_off:#x} (RVA {rva:#x})")

                # Hex dump ±64 bytes
                for off in range(0x100 - 0x40, 0x100 + 0x80, 16):
                    chunk = ctx[off:min(off + 16, len(ctx))]
                    hex_str = ' '.join(f'{b:02x}' for b in chunk)
                    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                    abs_addr = addr - 0x100 + off
                    ann = f" << {field_name}" if off == 0x100 else ""
                    print(f"    {abs_addr:#014x}: {hex_str:<48s} {ascii_str}  {ann}")

    # Clean up
    print(f"\n  Cleaning up...")
    task.SetField(188743705, "500")
    task.SetField(188743712, "42")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
