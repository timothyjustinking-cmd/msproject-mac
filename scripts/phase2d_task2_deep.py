#!/usr/bin/env python3
"""Deep investigation of Task 2's flat buffer record.

The differential scan found Task 2's Priority at 0x23b7ef6b4bc,
but the surrounding record fields don't match COM values.
This script correlates multiple SC1 field values with the memory layout.
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
        return None
    minute = min_x10 // 10
    if minute >= 1440:
        return None
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

    task2_prio_addr = 0x23b7ef6b4bc  # from differential scan
    task1_prio_addr = 0x23b7ef6dd3c

    # Read Task 2's COM values for SC1 fields
    task2 = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t and t.ID == 2:
            task2 = t
            break

    if not task2:
        print("ERROR: Task 2 not found"); return

    print(f"=== Task 2 COM values ===")
    print(f"Name: {task2.Name}")
    print(f"Priority: {task2.Priority}")
    print(f"OutlineLevel: {task2.OutlineLevel}")
    print(f"Duration: {task2.Duration} min")
    s = task2.Start
    f = task2.Finish
    print(f"Start: {s.year}-{s.month:02d}-{s.day:02d} {s.hour:02d}:{s.minute:02d}")
    print(f"Finish: {f.year}-{f.month:02d}-{f.day:02d} {f.hour:02d}:{f.minute:02d}")
    print(f"PercentComplete: {task2.PercentComplete}")
    print(f"Milestone: {task2.Milestone}")
    print(f"Summary: {task2.Summary}")
    print(f"Critical: {task2.Critical}")

    try:
        print(f"ConstraintType: {task2.ConstraintType}")
    except:
        pass
    try:
        print(f"Type: {task2.Type}")
    except:
        pass
    try:
        print(f"FixedCost: {task2.FixedCost}")
    except:
        pass

    # Encode expected packed dates
    start_packed = encode_date(s.year, s.month, s.day, s.hour, s.minute)
    finish_packed = encode_date(f.year, f.month, f.day, f.hour, f.minute)
    print(f"\nExpected packed Start:  0x{start_packed:08X}")
    print(f"Expected packed Finish: 0x{finish_packed:08X}")
    print(f"Expected Duration/10:   {int(task2.Duration * 10)}")
    print(f"Expected OutlineLevel:  {task2.OutlineLevel}")

    # Also get Task 1's values for comparison
    task1 = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t and t.ID == 1:
            task1 = t
            break

    if task1:
        t1s = task1.Start
        print(f"\nTask 1 Start: {t1s.year}-{t1s.month:02d}-{t1s.day:02d} {t1s.hour:02d}:{t1s.minute:02d}")
        print(f"Task 1 OL: {task1.OutlineLevel}")
        print(f"Task 1 Duration: {task1.Duration}")

    # Read large region around Task 2's Priority address
    # Priority is at some offset within a record. Read 600 bytes before and after
    region_start = task2_prio_addr - 600
    region_size = 1200
    data = read_mem(handle, region_start, region_size)
    if not data:
        print("ERROR: Can't read memory"); return

    prio_offset = 600  # offset within our buffer where Priority is

    print(f"\n=== Memory around Task 2 Priority (0x{task2_prio_addr:012x}) ===")
    print(f"(offsets relative to Priority address)")

    # Search for expected values in the region
    print(f"\nSearching for Task 2's packed Start (0x{start_packed:08X})...")
    needle = struct.pack('<I', start_packed)
    for off in range(0, len(data) - 3, 4):
        if data[off:off+4] == needle:
            rel = off - prio_offset
            print(f"  Found at prio{rel:+d} (buf+{off})")

    print(f"Searching for Task 2's packed Finish (0x{finish_packed:08X})...")
    needle = struct.pack('<I', finish_packed)
    for off in range(0, len(data) - 3, 4):
        if data[off:off+4] == needle:
            rel = off - prio_offset
            print(f"  Found at prio{rel:+d} (buf+{off})")

    dur_val = int(task2.Duration * 10)
    print(f"Searching for Duration*10 = {dur_val}...")
    needle = struct.pack('<I', dur_val)
    for off in range(0, len(data) - 3, 4):
        if data[off:off+4] == needle:
            rel = off - prio_offset
            print(f"  Found at prio{rel:+d} (buf+{off})")

    print(f"Searching for OutlineLevel = {task2.OutlineLevel} (as u32)...")
    needle = struct.pack('<I', task2.OutlineLevel)
    for off in range(0, len(data) - 3, 4):
        if data[off:off+4] == needle:
            val_before = struct.unpack_from('<I', data, off-4)[0] if off >= 4 else 0
            val_after = struct.unpack_from('<I', data, off+4)[0] if off+4 < len(data)-3 else 0
            rel = off - prio_offset
            if abs(rel) <= 400:  # only show nearby
                print(f"  Found at prio{rel:+d} (context: ..{val_before}.. [{task2.OutlineLevel}] ..{val_after}..)")

    # Hex dump the record region (record+0 to record+576 assuming record starts at prio-168)
    rec_start_off = prio_offset - 168
    print(f"\n=== Hex dump: assumed record (prio-168 to prio+408) ===")
    for off in range(rec_start_off, min(rec_start_off + 576, len(data) - 16), 16):
        rel = off - prio_offset
        hex_vals = ' '.join(f'{data[off+k]:02x}' for k in range(min(16, len(data)-off)))
        u32s = []
        for k in range(0, 16, 4):
            if off + k + 4 <= len(data):
                v = struct.unpack_from('<I', data, off+k)[0]
                d = decode_date(v)
                if d:
                    u32s.append(f"[{d}]")
                else:
                    u32s.append(f"{v}")
            else:
                u32s.append("?")
        print(f"  prio{rel:+4d}: {hex_vals}  | {' '.join(u32s)}")

    # Now do a SECOND differential: change OutlineLevel (indent Task 2)
    # Actually, OutlineLevel is read-only. Let me change Duration instead.
    print(f"\n=== Differential test: Change Task 2 Duration ===")
    old_dur = task2.Duration
    print(f"Original Duration: {old_dur} min")

    # Read before
    data_before = read_mem(handle, region_start, region_size)

    # Change duration
    task2.Duration = "3d"  # 3 days = 1440 min
    time.sleep(0.3)
    new_dur = task2.Duration
    print(f"New Duration: {new_dur} min")

    # Read after
    data_after = read_mem(handle, region_start, region_size)

    # Find ALL changes
    print(f"\nChanges in {region_size}-byte region:")
    changes = []
    for off in range(0, min(len(data_before), len(data_after)) - 3, 4):
        v_before = struct.unpack_from('<I', data_before, off)[0]
        v_after = struct.unpack_from('<I', data_after, off)[0]
        if v_before != v_after:
            rel = off - prio_offset
            d_before = decode_date(v_before)
            d_after = decode_date(v_after)
            print(f"  prio{rel:+4d}: 0x{v_before:08X} -> 0x{v_after:08X}"
                  f"  ({d_before or v_before} -> {d_after or v_after})")
            changes.append((rel, v_before, v_after))

    # Restore
    task2.Duration = f"{int(old_dur)}m"
    time.sleep(0.3)
    print(f"\nDuration restored to {task2.Duration} min")

    print(f"\nTotal changes: {len(changes)}")

    k32.CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
