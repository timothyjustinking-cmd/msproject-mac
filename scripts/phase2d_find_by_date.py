#!/usr/bin/env python3
"""Find all task records by searching for their unique packed Start dates.

Each task has a unique Start date. Search for the 4-byte packed date
value, then verify the surrounding 576-byte record structure.
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

def search_memory(handle, needle, max_results=50):
    results = []
    addr = 0
    mbi = MEMORY_BASIC_INFORMATION()
    mbi_size = ctypes.sizeof(mbi)
    while addr < 0x7FFFFFFFFFFF:
        ret = k32.VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), mbi_size)
        if ret == 0: break
        base = mbi.BaseAddress or 0
        size = mbi.RegionSize or 0
        if mbi.State == MEM_COMMIT and size > 0 and size < 0x10000000:
            protect = mbi.Protect & 0xFF
            if protect not in (0x01, 0x00):
                data = read_mem(handle, base, size)
                if data:
                    offset = 0
                    while True:
                        idx = data.find(needle, offset)
                        if idx == -1: break
                        results.append(base + idx)
                        if len(results) >= max_results: return results
                        offset = idx + 1
        next_addr = base + max(size, 0x1000)
        if next_addr <= addr: break
        addr = next_addr
    return results

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

def encode_date(year, month, day, hour=8, minute=0):
    dt = datetime(year, month, day)
    day_num = (dt - datetime(1984, 1, 1)).days + 1
    min_of_day = hour * 60 + minute
    return (day_num << 16) | (min_of_day * 10)


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

    # First, make Task 2 have a UNIQUE start date by moving it
    # Task 2 currently: Start=2026-01-19
    # Set to a very distinctive date: 2026-07-07 08:00
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

    # Save original values
    orig_start2 = task2.Start
    print(f"Task 2 original Start: {orig_start2}")

    # Set distinctive start
    import pywintypes
    distinctive_date = datetime(2026, 7, 7, 8, 0, 0)
    task2.Start = pywintypes.Time(distinctive_date.timetuple())
    time.sleep(0.5)
    actual_start = task2.Start
    actual_dt = datetime(actual_start.year, actual_start.month, actual_start.day,
                         actual_start.hour, actual_start.minute, actual_start.second)
    print(f"Task 2 set to: {actual_dt}")

    # Encode the actual start date (may differ from requested due to calendar)
    packed_start = encode_date(actual_dt.year, actual_dt.month, actual_dt.day,
                               actual_dt.hour, actual_dt.minute)
    print(f"Packed Start for Task 2: 0x{packed_start:08X}")

    # Search for this packed date
    needle = struct.pack('<I', packed_start)
    hits = search_memory(handle, needle)
    print(f"\nFound {len(hits)} hits for packed date 0x{packed_start:08X}")

    # For each hit, check if it's at the Start offset (+4) of a valid record
    for addr in hits:
        # If this is at record+4 (Start field), record start = addr - 4
        rec_start = addr - 4
        rec_data = read_mem(handle, rec_start, 380)
        if not rec_data or len(rec_data) < 380:
            continue

        created = struct.unpack_from('<I', rec_data, 0)[0]
        finish = struct.unpack_from('<I', rec_data, 8)[0]
        prio = struct.unpack_from('<H', rec_data, 168)[0]
        outline = struct.unpack_from('<I', rec_data, 264)[0]
        duration = struct.unpack_from('<I', rec_data, 368)[0]

        created_str = decode_date(created)
        finish_str = decode_date(finish)

        # Validate: Created should be 2026, Priority 500, OutlineLevel 1
        if "2026" in created_str and prio == 500 and outline == 1:
            offset_from_t1 = rec_start - task1_rec
            print(f"\n  VALID RECORD at 0x{rec_start:012x}")
            print(f"    Created:  {created_str}")
            print(f"    Start:    {decode_date(packed_start)}")
            print(f"    Finish:   {finish_str}")
            print(f"    Priority: {prio}")
            print(f"    Outline:  {outline}")
            print(f"    Duration: {duration}/10 = {duration/10:.0f} min")
            print(f"    Offset from Task 1 rec: {offset_from_t1} bytes ({offset_from_t1/576:.2f} strides)")

            # Also check the pointer/container region at +280-352
            print(f"    Container pointers:")
            for coff in [284, 300, 316, 332, 348]:
                ptr_data = read_mem(handle, rec_start + coff, 8)
                if ptr_data:
                    ptr = struct.unpack_from('<Q', ptr_data, 0)[0]
                    if 0x023b00000000 < ptr < 0x023c00000000:
                        print(f"      +{coff}: 0x{ptr:012x}")

    # Also search at record+12 (EarlyStart) and record+52 (ConstraintDate?)
    print(f"\nSearching at EarlyStart offset (+12)...")
    for addr in hits:
        rec_start = addr - 12
        rec_data = read_mem(handle, rec_start, 380)
        if not rec_data or len(rec_data) < 380:
            continue
        created = struct.unpack_from('<I', rec_data, 0)[0]
        prio = struct.unpack_from('<H', rec_data, 168)[0]
        outline = struct.unpack_from('<I', rec_data, 264)[0]
        if "2026" in decode_date(created) and prio == 500 and outline == 1:
            offset_from_t1 = rec_start - task1_rec
            print(f"  Also valid at EarlyStart offset! 0x{rec_start:012x} (t1_rec{offset_from_t1:+d})")

    # Restore Task 2 start
    print(f"\nRestoring Task 2 start...")
    task2.Start = orig_start2
    time.sleep(0.3)
    print(f"Task 2 Start restored: {task2.Start}")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
