#!/usr/bin/env python3
"""Phase 2d: Map the full task record layout by setting distinctive values
on ALL settable class 1 fields and scanning for them near the known Priority address.

Key knowledge: Priority=777 at 0x23b7ef6dd3c (address may shift between runs).
Data is in a flat buffer organized by type groups.
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

def find_priority(handle, regions, task):
    """Find the real Priority address using differential scan."""
    task.SetField(188743705, "987")
    time.sleep(0.5)
    hits_a = set(find_all_pattern(handle, regions, struct.pack('<h', 987)))
    task.SetField(188743705, "123")
    time.sleep(0.5)
    hits_b = set(find_all_pattern(handle, regions, struct.pack('<h', 123)))
    prio_addrs = sorted(hits_a & hits_b)

    # Filter out direct-storage block
    for pa in prio_addrs:
        ctx = read_mem(handle, pa - 32, 64)
        if not ctx:
            continue
        fid_check = struct.unpack_from('<I', ctx, 32 - 24)[0]
        if (fid_check & 0xFFFF0000) != 0x0b400000:
            return pa
    return None


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    regions = scan_all_regions(handle)

    # Step 1: Set distinctive Priority and find its address
    task.SetField(188743705, "777")
    time.sleep(0.3)
    prio_addr = find_priority(handle, regions, task)
    task.SetField(188743705, "777")  # restore probe value
    time.sleep(0.3)

    if not prio_addr:
        print("ERROR: Priority not found"); return

    print(f"Priority at {prio_addr:#x}")

    # Step 2: Set distinctive values on ALL settable class 1 fields
    # Use unique values so we can identify each field in memory
    probes = []

    # i16 fields
    probes.append(('Priority', 188743705, 'i16', "777", 777))
    probes.append(('PercentComplete', 188743712, 'i16', "42%", 42))
    # PercentWorkComplete - try
    try:
        task.SetField(188743713, "37%")
        probes.append(('PercentWorkComplete', 188743713, 'i16', "37%", 37))
    except:
        pass

    # i64 fields (stored as double * 100 for costs)
    task.SetField(188743688, "$12345")  # FixedCost
    probes.append(('FixedCost', 188743688, 'double', "$12345", 1234500.0))

    # bool fields
    task.SetField(188743751, "Yes")  # Marked
    probes.append(('Marked', 188743751, 'bool', "Yes", 1))

    time.sleep(0.5)

    # Step 3: Read a BIG area around Priority
    before = 4096
    after = 4096
    scan_start = prio_addr - before
    chunk = read_mem(handle, scan_start, before + after)
    prio_off = before

    if not chunk or len(chunk) < before + after:
        print("ERROR: Can't read memory"); return

    # Verify Priority
    val = struct.unpack_from('<h', chunk, prio_off)[0]
    print(f"  Priority value at expected offset: {val}")

    # Step 4: Search for ALL probe values
    print(f"\n{'='*100}")
    print(f"FIELD POSITIONS RELATIVE TO PRIORITY ({prio_addr:#x})")
    print(f"{'='*100}")

    results = []

    # Search for each probe value in the chunk
    for name, fid, typ, com_str, expected in probes:
        if typ == 'i16':
            pat = struct.pack('<h', expected)
            size = 2
        elif typ == 'i32':
            pat = struct.pack('<i', expected)
            size = 4
        elif typ == 'double':
            pat = struct.pack('<d', expected)
            size = 8
        elif typ == 'bool':
            pat = struct.pack('<h', expected)  # try as i16
            size = 2
        else:
            continue

        hits = []
        pos = 0
        while pos < len(chunk) - size:
            p = chunk.find(pat, pos)
            if p == -1: break
            rel = p - prio_off
            hits.append((p, rel))
            pos = p + 1

        if hits:
            for p, rel in hits:
                results.append((name, typ, rel, expected))
                print(f"  {name:<25s} ({typ:>6s}) = {expected:>12} at prio{rel:+5d} (addr {scan_start+p:#x})")

    # Also search for Duration values that we know
    # Duration is still at default 5 days = 24000 min (set to 42% complete changes actual/remaining)
    for label, val in [("Duration=24000", 24000), ("ActualDuration", 10080), ("RemainingDuration", 13920)]:
        pat = struct.pack('<i', val)
        pos = 0
        while pos < len(chunk) - 4:
            p = chunk.find(pat, pos)
            if p == -1: break
            rel = p - prio_off
            if -512 < rel < 512:  # only show nearby hits
                print(f"  {label:<25s} (  i32) = {val:>12} at prio{rel:+5d}")
            pos = p + 1

    # Search for date values 0x3d1212c0 and 0x3d1827d8 (the repeating dates)
    for label, val_hex in [("Date_0x3d1212c0", 0x3d1212c0), ("Date_0x3d1827d8", 0x3d1827d8)]:
        pat = struct.pack('<I', val_hex)
        pos = 0
        hits = []
        while pos < len(chunk) - 4:
            p = chunk.find(pat, pos)
            if p == -1: break
            rel = p - prio_off
            if -512 < rel < 512:
                hits.append(rel)
            pos = p + 1
        if hits:
            print(f"  {label:<25s} (date32) = {val_hex:#x} at: {', '.join(f'prio{r:+d}' for r in hits)}")

    # Step 5: Analyze the layout by TYPE GROUPS
    print(f"\n{'='*100}")
    print(f"TYPE GROUP ANALYSIS")
    print(f"{'='*100}")

    # i32 area: scan backwards from prio-170 (after dates end) for contiguous i32 values
    # that look like durations/slacks (range 0-100000)
    print("\n  i32 values (duration/slack range) near priority:")
    for off in range(prio_off - 300, prio_off - 100, 4):
        val = struct.unpack_from('<i', chunk, off)[0]
        if off + 4 <= len(chunk):
            rel = off - prio_off
            if val != 0 or (off - prio_off) % 4 == 0:
                u16_0 = struct.unpack_from('<H', chunk, off)[0]
                u16_1 = struct.unpack_from('<H', chunk, off + 2)[0] if off + 4 <= len(chunk) else 0
                print(f"    prio{rel:+5d}: i32={val:10d} (0x{val & 0xFFFFFFFF:08x})  u16=[{u16_0:5d},{u16_1:5d}]")

    # double area: scan for 8-byte doubles near priority
    print("\n  Double values near priority:")
    for off in range(prio_off - 120, prio_off, 8):
        dbl = struct.unpack_from('<d', chunk, off)[0]
        rel = off - prio_off
        if dbl != 0.0:
            print(f"    prio{rel:+5d}: double={dbl:.2f}")

    # i16 area: from priority onward
    print("\n  i16 values from priority:")
    for off in range(prio_off, min(prio_off + 64, len(chunk) - 2), 2):
        val = struct.unpack_from('<h', chunk, off)[0]
        rel = off - prio_off
        print(f"    prio{rel:+5d}: i16={val}")

    # Cleanup
    task.SetField(188743705, "500")
    task.SetField(188743712, "0%")
    task.SetField(188743751, "No")
    task.SetField(188743688, "$0")
    try:
        task.SetField(188743713, "0%")
    except:
        pass

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
