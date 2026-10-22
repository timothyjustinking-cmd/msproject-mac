#!/usr/bin/env python3
"""Phase 2d: Empirical storage mapping via COM write + memory scan.

Strategy:
1. Set a known unique value to a field via COM
2. Read large blocks of task-related memory
3. Search for the value pattern to find exact storage location
4. Repeat for fields in different storage classes to map the layout

This reveals where each storage class actually keeps its data.
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


def scan_heap_regions(handle, pid):
    """Get list of readable memory regions to scan."""
    regions = []

    MEM_COMMIT = 0x1000
    PAGE_READWRITE = 0x04
    PAGE_READONLY = 0x02
    PAGE_EXECUTE_READ = 0x20
    PAGE_EXECUTE_READWRITE = 0x40
    READABLE = {PAGE_READWRITE, PAGE_READONLY, PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE}

    class MEMORY_BASIC_INFORMATION(ctypes.Structure):
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
    mbi = MEMORY_BASIC_INFORMATION()
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


def search_pattern_in_regions(handle, regions, pattern, max_hits=20):
    """Search for a byte pattern across memory regions."""
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
            if len(hits) >= max_hits:
                return hits
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

    # Connect to MS Project
    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    if not proj:
        print("ERROR: No project open"); CloseHandle(handle); sys.exit(1)

    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    if not task:
        print("ERROR: No tasks"); CloseHandle(handle); sys.exit(1)

    print(f"Task: ID={task.ID} Name='{task.Name}'")

    # Enumerate readable memory regions
    print("\nScanning memory regions...")
    regions = scan_heap_regions(handle, pid)
    total_size = sum(s for _, s in regions)
    print(f"  {len(regions)} regions, {total_size / 1024 / 1024:.0f} MB total")

    # Filter to heap-like regions (private, moderate size)
    heap_regions = [(b, s) for b, s in regions if 0x10000 <= s <= 0x4000000]
    heap_size = sum(s for _, s in heap_regions)
    print(f"  {len(heap_regions)} heap-like regions, {heap_size / 1024 / 1024:.0f} MB")

    # Test 1: Set a unique text value in a writable Text field
    print(f"\n{'='*90}")
    print(f"TEST 1: Text field (Text1, class 4/text, direct)")
    print(f"{'='*90}")

    unique_text = "XYZZY_PROBE_42"
    field_id = 188743731  # Text1 (pjTaskText1)

    print(f"  Setting Text1 = '{unique_text}'")
    task.SetField(field_id, unique_text)
    time.sleep(0.5)

    pattern_utf16 = unique_text.encode('utf-16-le')
    pattern_ascii = unique_text.encode('ascii')

    print(f"  Searching for UTF-16LE pattern ({len(pattern_utf16)} bytes)...")
    hits_utf16 = search_pattern_in_regions(handle, heap_regions, pattern_utf16, 30)
    print(f"  Found {len(hits_utf16)} UTF-16LE hits")
    for addr in hits_utf16:
        context = read_mem(handle, addr - 16, 64)
        if context:
            hex_before = context[:16].hex()
            hex_after = context[16+len(pattern_utf16):].hex()
            print(f"    {addr:#018x}: ...{hex_before} | {pattern_utf16.hex()} | {hex_after}...")

    print(f"\n  Searching for ASCII pattern ({len(pattern_ascii)} bytes)...")
    hits_ascii = search_pattern_in_regions(handle, heap_regions, pattern_ascii, 10)
    print(f"  Found {len(hits_ascii)} ASCII hits")
    for addr in hits_ascii:
        print(f"    {addr:#018x}")

    # Test 2: Set a unique number value in Number1
    print(f"\n{'='*90}")
    print(f"TEST 2: Number field (Number1, class 1/direct)")
    print(f"{'='*90}")

    unique_num = 31415926.0
    field_id_num = 188743787  # Number1

    print(f"  Setting Number1 = {unique_num}")
    task.SetField(field_id_num, str(int(unique_num)))
    time.sleep(0.5)

    pattern_double = struct.pack('<d', unique_num)
    pattern_i32 = struct.pack('<i', int(unique_num))

    print(f"  Searching for double pattern {pattern_double.hex()}...")
    hits_double = search_pattern_in_regions(handle, heap_regions, pattern_double, 20)
    print(f"  Found {len(hits_double)} double hits")
    for addr in hits_double:
        context = read_mem(handle, addr - 16, 48)
        if context:
            print(f"    {addr:#018x}: {context.hex()}")

    print(f"\n  Searching for i32 pattern {pattern_i32.hex()}...")
    hits_i32 = search_pattern_in_regions(handle, heap_regions, pattern_i32, 20)
    print(f"  Found {len(hits_i32)} i32 hits")
    for addr in hits_i32:
        context = read_mem(handle, addr - 8, 24)
        if context:
            print(f"    {addr:#018x}: context={context.hex()}")

    # Test 3: Flag1 (boolean, direct storage)
    print(f"\n{'='*90}")
    print(f"TEST 3: Flag field (Flag1, class 1/direct)")
    print(f"{'='*90}")

    field_id_flag = 188743752  # Flag1
    print(f"  Setting Flag1 = Yes")
    task.SetField(field_id_flag, "Yes")
    time.sleep(0.3)

    # Test 4: Cost1 (direct)
    print(f"\n{'='*90}")
    print(f"TEST 4: Cost field (Cost1, class 1/direct)")
    print(f"{'='*90}")

    unique_cost = 98765.43
    field_id_cost = 188743786  # Cost1
    print(f"  Setting Cost1 = {unique_cost}")
    task.SetField(field_id_cost, str(unique_cost))
    time.sleep(0.5)

    pattern_cost = struct.pack('<d', unique_cost * 100)  # costs stored as cents?
    pattern_cost2 = struct.pack('<d', unique_cost)

    print(f"  Searching for cost*100 double pattern...")
    hits_cost = search_pattern_in_regions(handle, heap_regions, pattern_cost, 20)
    print(f"  Found {len(hits_cost)} hits (cost*100)")
    for addr in hits_cost:
        context = read_mem(handle, addr - 8, 24)
        if context:
            print(f"    {addr:#018x}: context={context.hex()}")

    print(f"\n  Searching for cost direct double pattern...")
    hits_cost2 = search_pattern_in_regions(handle, heap_regions, pattern_cost2, 20)
    print(f"  Found {len(hits_cost2)} hits (cost direct)")
    for addr in hits_cost2:
        print(f"    {addr:#018x}")

    # Test 4: Find the task's UniqueID (should be 1 for first task)
    # And use it as an anchor to find the TASKNODE
    print(f"\n{'='*90}")
    print(f"TASKNODE ANCHOR SEARCH")
    print(f"{'='*90}")

    # The task name "Task 1" in UTF-16LE
    task_name = task.Name
    name_pattern = task_name.encode('utf-16-le')
    print(f"  Searching for task name '{task_name}' in UTF-16LE...")
    hits_name = search_pattern_in_regions(handle, heap_regions, name_pattern, 30)
    print(f"  Found {len(hits_name)} hits")

    # For each hit, read a large block around it and look for our unique values
    print(f"\n  Looking for TASKNODE containing multiple known values:")
    for name_addr in hits_name:
        block_start = name_addr - 0x2000
        block = read_mem(handle, block_start, 0x8000)
        if not block:
            continue

        found = []
        for pat_name, pat in [('name', name_pattern),
                               ('num1_i32', pattern_i32),
                               ('cost_x100', pattern_cost),
                               ('text_utf16', pattern_utf16)]:
            pos = block.find(pat)
            if pos >= 0:
                found.append((pat_name, block_start + pos))

        if len(found) >= 2:
            print(f"\n    CANDIDATE at name_addr={name_addr:#x}:")
            for nm, addr in found:
                print(f"      {nm}: {addr:#x} (offset from name: {addr - name_addr:+d})")

    # Deep analysis of most promising text hit
    print(f"\n{'='*90}")
    print(f"TASKNODE DEEP ANALYSIS - around text+cost co-location")
    print(f"{'='*90}")

    for text_addr in hits_utf16:
        block_start = text_addr - 0x1000
        block = read_mem(handle, block_start, 0x3000)
        if not block:
            continue
        cost_pos = block.find(pattern_cost)
        name_pos = block.find(name_pattern)
        i32_pos = block.find(pattern_i32)

        colocated = sum(1 for p in [cost_pos, name_pos, i32_pos] if p >= 0)
        if colocated >= 1:
            text_off = text_addr - block_start
            print(f"\n  Block around text1 at {text_addr:#x}:")
            if name_pos >= 0: print(f"    task_name at +{name_pos:#x} (delta from text: {name_pos - text_off:+d})")
            if cost_pos >= 0: print(f"    cost1*100 at +{cost_pos:#x} (delta from text: {cost_pos - text_off:+d})")
            if i32_pos >= 0: print(f"    num1_i32  at +{i32_pos:#x} (delta from text: {i32_pos - text_off:+d})")

            # Dump 256 bytes around text field
            dump_start = max(0, text_off - 0x60)
            dump_end = min(len(block), text_off + 0x100)
            print(f"\n    Hex dump ({block_start + dump_start:#x} to {block_start + dump_end:#x}):")
            for off in range(dump_start, dump_end, 16):
                chunk = block[off:off+16]
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                addr = block_start + off
                print(f"      {addr:#014x}: {hex_str:<48s} {ascii_str}")

    # Clean up - reset the fields
    print(f"\n  Cleaning up test values...")
    task.SetField(field_id, "")
    task.SetField(field_id_num, "0")
    task.SetField(field_id_cost, "0")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
