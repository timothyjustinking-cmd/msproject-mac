#!/usr/bin/env python3
"""Wide-range scan for packed dates in the task object's memory region.

Packed date format: (day_1indexed << 16) | (minute_of_day * 10)
Day 1 = 1984-01-01, sentinel = 0xFFFFFFFF

Scan a much wider range around prio to find ALL stored dates.
Also correlate with COM date values using differential testing.
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
        return None, "NA"
    day = packed >> 16
    min_x10 = packed & 0xFFFF
    if day < 1 or day > 50000 or min_x10 > 14400:
        return None, "INVALID"
    minute = min_x10 // 10
    if minute >= 1440:
        return None, "INVALID"
    dt = datetime(1984, 1, 1) + timedelta(days=day - 1)
    hours = minute // 60
    mins = minute % 60
    return dt.replace(hour=hours, minute=mins), f"{dt.strftime('%Y-%m-%d')} {hours:02d}:{mins:02d}"

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
    tn_addr = 0x23b6384bad0

    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    # Read COM date fields
    print("COM Date Fields:")
    com_dates = {}
    for name in ['Start', 'Finish', 'EarlyStart', 'EarlyFinish',
                  'LateStart', 'LateFinish', 'ConstraintDate', 'Created']:
        try:
            val = getattr(task, name)
            if val and hasattr(val, 'year'):
                dt = datetime(val.year, val.month, val.day, val.hour, val.minute, val.second)
                packed = encode_date(dt.year, dt.month, dt.day, dt.hour, dt.minute)
                com_dates[name] = (dt, packed)
                print(f"  {name:20s} = {dt}  packed=0x{packed:08X}")
        except:
            pass

    # Wide scan: prio-2048 to prio+2048
    print(f"\n{'='*80}")
    print("WIDE SCAN: prio-2048 to prio+2048 for packed dates")

    scan_range = 2048
    buf = read_mem(handle, prio_addr - scan_range, scan_range * 2)
    if not buf:
        print("ERROR: Can't read wide range")
        k32.CloseHandle(handle)
        return

    # Find all valid packed dates in reasonable range (2020-2030)
    date_hits = []
    for off in range(0, len(buf) - 3, 4):
        u32 = struct.unpack_from('<I', buf, off)[0]
        dt_obj, dt_str = decode_date(u32)
        if dt_obj and 2020 <= dt_obj.year <= 2030:
            rel = off - scan_range
            date_hits.append((rel, u32, dt_str))

    print(f"Found {len(date_hits)} valid packed dates (2020-2030):")
    for rel, packed, dt_str in date_hits:
        # Check if it matches a known COM date
        matches = [name for name, (dt, p) in com_dates.items() if p == packed]
        match_str = f"  <- {', '.join(matches)}" if matches else ""
        print(f"  prio{rel:+5d}: 0x{packed:08X} = {dt_str}{match_str}")

    # ================================================================
    # DIFFERENTIAL TEST: Change Start date, see what moves
    # ================================================================
    print(f"\n{'='*80}")
    print("DIFFERENTIAL TEST: Changing Start to 2026-03-01 08:00")

    # Snapshot before
    snap_before = read_mem(handle, prio_addr - scan_range, scan_range * 2)

    # Change start date
    new_start = encode_date(2026, 3, 1, 8, 0)
    print(f"New Start packed: 0x{new_start:08X}")

    try:
        import pywintypes
        new_start_dt = pywintypes.Time(datetime(2026, 3, 1, 8, 0, 0).timetuple())
        task.Start = new_start_dt
        time.sleep(0.5)
        # Read back
        actual_start = task.Start
        print(f"Start set to: {actual_start}")
        actual_finish = task.Finish
        print(f"Finish now: {actual_finish}")
    except Exception as e:
        print(f"  Set Start failed: {e}")
        try:
            # Alternative: set via field value
            task.Start = "3/1/2026 8:00 AM"
            time.sleep(0.5)
            print(f"  Start after text set: {task.Start}")
        except Exception as e2:
            print(f"  Also failed: {e2}")

    # Snapshot after
    snap_after = read_mem(handle, prio_addr - scan_range, scan_range * 2)

    if snap_before and snap_after:
        print(f"\nChanged u32 positions:")
        changes = 0
        for off in range(0, len(snap_before) - 3, 4):
            u32_b = struct.unpack_from('<I', snap_before, off)[0]
            u32_a = struct.unpack_from('<I', snap_after, off)[0]
            if u32_b != u32_a:
                rel = off - scan_range
                # Decode both as dates
                _, dt_b = decode_date(u32_b)
                _, dt_a = decode_date(u32_a)
                print(f"  prio{rel:+5d}: 0x{u32_b:08X} -> 0x{u32_a:08X}  ({dt_b} -> {dt_a})")
                changes += 1
        print(f"  Total changed u32s: {changes}")

    # Find all positions of the NEW start date
    new_start_packed = encode_date(2026, 3, 1, 8, 0)
    new_start_bytes = struct.pack('<I', new_start_packed)
    print(f"\nNew Start (0x{new_start_packed:08X}) positions in wide range:")
    for off in range(0, len(snap_after) - 3, 4):
        u32 = struct.unpack_from('<I', snap_after, off)[0]
        if u32 == new_start_packed:
            rel = off - scan_range
            print(f"  prio{rel:+5d}")

    # Reset start back to original
    print("\nResetting Start...")
    try:
        orig = com_dates['Start'][0]
        task.Start = pywintypes.Time(orig.timetuple())
        time.sleep(0.3)
        print(f"  Start restored: {task.Start}")
    except Exception as e:
        print(f"  Reset failed: {e}")

    # ================================================================
    # TASKNODE date scan
    # ================================================================
    print(f"\n{'='*80}")
    print("TASKNODE date scan")

    tn_header = read_mem(handle, tn_addr, 8)
    if tn_header:
        tn_size = struct.unpack_from('<I', tn_header, 0)[0]
        tn_count = struct.unpack_from('<I', tn_header, 4)[0]
        tn_data = read_mem(handle, tn_addr, min(tn_size + 8, 4096))
        if tn_data:
            # Check data section for packed dates
            data_start = 8 + tn_count * 8
            dates_in_tn = []
            for off in range(data_start, len(tn_data) - 3, 4):
                u32 = struct.unpack_from('<I', tn_data, off)[0]
                dt_obj, dt_str = decode_date(u32)
                if dt_obj and 2020 <= dt_obj.year <= 2030:
                    dates_in_tn.append((off, u32, dt_str))

            print(f"Packed dates in TASKNODE data: {len(dates_in_tn)}")
            for off, packed, dt_str in dates_in_tn:
                # Find which field_id maps to this offset
                data_off = off - data_start
                fid_match = ""
                for i in range(tn_count):
                    idx_off = 8 + i * 8
                    fid = struct.unpack_from('<I', tn_data, idx_off)[0]
                    foff = struct.unpack_from('<I', tn_data, idx_off + 4)[0]
                    if foff == data_off:
                        fid_match = f" fid=0x{fid:04X}"
                        break
                print(f"  tn+{off}: 0x{packed:08X} = {dt_str}{fid_match}")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
