#!/usr/bin/env python3
"""Find Task 2's flat buffer record using differential memory scanning.

Strategy:
1. Snapshot entire MS Project heap
2. Change Task 2's Priority 500->999
3. Snapshot again
4. Find exactly which addresses changed from 500 to 999
"""

import ctypes
import ctypes.wintypes
import struct
import sys
import time
from datetime import datetime, timedelta

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

try:
    import win32com.client
except ImportError:
    print("ERROR: pywin32 required"); sys.exit(1)

k32 = ctypes.WinDLL('kernel32', use_last_error=True)

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", ctypes.wintypes.DWORD),
        ("RegionSize", ctypes.c_size_t),
        ("State", ctypes.wintypes.DWORD),
        ("Protect", ctypes.wintypes.DWORD),
        ("Type", ctypes.wintypes.DWORD),
    ]

MEM_COMMIT = 0x1000

def get_pid():
    import subprocess
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq WINPROJ.EXE', '/FO', 'CSV', '/NH'],
                       capture_output=True, text=True)
    for line in r.stdout.strip().split('\n'):
        if 'WINPROJ' in line:
            return int(line.split(',')[1].strip('"'))
    return None

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = k32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]

def decode_date(packed):
    if packed == 0xFFFFFFFF:
        return "NA"
    day = packed >> 16
    min_x10 = packed & 0xFFFF
    if day < 1 or day > 50000 or min_x10 > 14400:
        return "inv"
    minute = min_x10 // 10
    if minute >= 1440:
        return "inv"
    dt = datetime(1984, 1, 1) + timedelta(days=day - 1)
    return f"{dt.strftime('%Y-%m-%d')} {minute//60:02d}:{minute%60:02d}"

def snapshot_heap(handle, range_start=0x023b00000000, range_end=0x023c00000000):
    """Take snapshot of regions in a specific address range, returning (base, data) pairs."""
    regions = []
    addr = range_start
    mbi = MEMORY_BASIC_INFORMATION()
    mbi_size = ctypes.sizeof(mbi)
    total_bytes = 0
    while addr < range_end:
        ret = k32.VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), mbi_size)
        if ret == 0: break
        base = mbi.BaseAddress or 0
        size = mbi.RegionSize or 0
        if base >= range_end:
            break
        if mbi.State == MEM_COMMIT and size > 0 and size < 0x10000000:
            protect = mbi.Protect & 0xFF
            if protect not in (0x01, 0x00):
                data = read_mem(handle, base, size)
                if data:
                    regions.append((base, data))
                    total_bytes += len(data)
        next_addr = base + max(size, 0x1000)
        if next_addr <= addr: break
        addr = next_addr
    return regions, total_bytes


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    handle = k32.OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    prio_addr = 0x23b7ef6dd3c
    task1_rec = prio_addr - 168

    task2 = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t and t.ID == 2:
            task2 = t
            break

    if not task2:
        print("ERROR: Task 2 not found")
        k32.CloseHandle(handle)
        return

    print(f"Task 2: '{task2.Name}', Priority={task2.Priority}")

    # Snapshot BEFORE
    print("Taking snapshot BEFORE...")
    regions_before, total = snapshot_heap(handle)
    print(f"  Captured {len(regions_before)} regions, {total/1024/1024:.1f} MB")

    # Change Priority to distinctive value
    old_prio = task2.Priority
    task2.Priority = 999
    time.sleep(0.3)
    print(f"Task 2 Priority changed to {task2.Priority}")

    # Snapshot AFTER
    print("Taking snapshot AFTER...")
    regions_after, total2 = snapshot_heap(handle)
    print(f"  Captured {len(regions_after)} regions, {total2/1024/1024:.1f} MB")

    # Find addresses where u16 changed from 500 to 999
    print(f"\nSearching for u16 changes: 500 -> 999...")
    needle_before = struct.pack('<H', 500)
    needle_after = struct.pack('<H', 999)

    # Build address map of AFTER data for comparison
    after_map = {}
    for base, data in regions_after:
        after_map[base] = data

    hits = []
    for base_b, data_b in regions_before:
        data_a = after_map.get(base_b)
        if not data_a:
            continue
        min_len = min(len(data_b), len(data_a))
        for off in range(0, min_len - 1, 2):
            val_b = struct.unpack_from('<H', data_b, off)[0]
            val_a = struct.unpack_from('<H', data_a, off)[0]
            if val_b == 500 and val_a == 999:
                addr = base_b + off
                hits.append(addr)

    print(f"Found {len(hits)} addresses where u16 changed 500->999")
    for addr in hits:
        rel = addr - prio_addr
        print(f"  0x{addr:012x} (prio{rel:+d})")

        # Check if this could be a Priority field at record+168
        rec_start = addr - 168
        rec_data = read_mem(handle, rec_start, 380)
        if rec_data and len(rec_data) >= 380:
            created = struct.unpack_from('<I', rec_data, 0)[0]
            start = struct.unpack_from('<I', rec_data, 4)[0]
            finish = struct.unpack_from('<I', rec_data, 8)[0]
            outline = struct.unpack_from('<I', rec_data, 264)[0]
            duration = struct.unpack_from('<I', rec_data, 368)[0]
            prio_check = struct.unpack_from('<H', rec_data, 168)[0]

            created_str = decode_date(created)
            start_str = decode_date(start)
            finish_str = decode_date(finish)

            is_record = ("2026" in created_str or
                         ("2026" in start_str and "inv" not in start_str))

            if is_record:
                offset_from_t1 = rec_start - task1_rec
                print(f"    -> LIKELY RECORD at 0x{rec_start:012x}")
                print(f"       Created:  {created_str}")
                print(f"       Start:    {start_str}")
                print(f"       Finish:   {finish_str}")
                print(f"       Priority: {prio_check}")
                print(f"       Outline:  {outline}")
                print(f"       Duration: {duration}/10 = {duration/10:.0f} min")
                print(f"       Offset from Task 1: {offset_from_t1} bytes ({offset_from_t1/576:.2f} strides)")

    # Reset
    task2.Priority = old_prio
    print(f"\nTask 2 Priority restored to {task2.Priority}")

    k32.CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
