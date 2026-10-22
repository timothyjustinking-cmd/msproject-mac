#!/usr/bin/env python3
"""Investigate the task-to-record mapping in the flat buffer.

The 576-byte stride is confirmed for Task 1 but the record at +576
doesn't match Task 2's COM values. This script:
1. Sets distinctive Priority values on each task
2. Scans the flat buffer to find which record has which Priority
3. Maps the actual task-to-record assignment
"""

import ctypes
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
        return f"inv"
    minute = min_x10 // 10
    if minute >= 1440:
        return "inv"
    dt = datetime(1984, 1, 1) + timedelta(days=day - 1)
    return f"{dt.strftime('%Y-%m-%d')} {dt.hour+minute//60:02d}:{minute%60:02d}"

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
    STRIDE = 576
    record_base = prio_addr - 168

    # Set distinctive priorities on each task
    print("Setting distinctive priorities:")
    tasks = []
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is None:
            continue
        marker = 100 + i * 111  # 211, 322, 433, ...
        t.Priority = marker
        tasks.append((i, t, marker))
        print(f"  Task {i} '{t.Name}': Priority = {marker}")

    time.sleep(0.3)

    # Scan flat buffer for the marker values
    # Read a WIDE range: 20 records worth
    scan_size = STRIDE * 20
    buf = read_mem(handle, record_base - STRIDE * 2, scan_size)
    if not buf:
        print("ERROR: Can't read flat buffer")
        k32.CloseHandle(handle)
        return

    print(f"\nScanning {scan_size} bytes for marker priorities...")
    for task_idx, (task_id, task_obj, marker) in enumerate(tasks):
        marker_bytes = struct.pack('<H', marker)  # Priority is u16
        # Search at Priority offset (168) within each potential record
        found = False
        for rec_off in range(STRIDE * 2, scan_size - STRIDE, 4):
            # Check if this position has the marker
            if rec_off + 2 <= len(buf):
                val = struct.unpack_from('<H', buf, rec_off)[0]
                if val == marker:
                    abs_addr = record_base - STRIDE * 2 + rec_off
                    rel_to_base = rec_off - STRIDE * 2
                    rec_num = rel_to_base // STRIDE
                    rec_inner = rel_to_base % STRIDE
                    if rec_inner == 168:  # This IS a Priority field
                        # Verify by checking surrounding fields
                        rec_start = rec_off - 168
                        created = struct.unpack_from('<I', buf, rec_start)[0]
                        start = struct.unpack_from('<I', buf, rec_start + 4)[0]
                        finish = struct.unpack_from('<I', buf, rec_start + 8)[0]
                        dur = struct.unpack_from('<I', buf, rec_start + 368)[0]
                        outline = struct.unpack_from('<I', buf, rec_start + 264)[0]

                        print(f"\n  Task {task_id} (Priority={marker}) found at record #{rec_num} (byte offset {rel_to_base}):")
                        print(f"    Record start: 0x{record_base - STRIDE*2 + rec_start:012x}")
                        print(f"    Created:  0x{created:08X} = {decode_date(created)}")
                        print(f"    Start:    0x{start:08X} = {decode_date(start)}")
                        print(f"    Finish:   0x{finish:08X} = {decode_date(finish)}")
                        print(f"    Duration: {dur} = {dur/10:.0f} min")
                        print(f"    Outline:  {outline}")

                        # Compare with COM
                        try:
                            com_start = task_obj.Start
                            com_finish = task_obj.Finish
                            com_dur = task_obj.Duration
                            com_start_dt = datetime(com_start.year, com_start.month, com_start.day,
                                                    com_start.hour, com_start.minute, com_start.second)
                            com_packed = encode_date(com_start_dt.year, com_start_dt.month, com_start_dt.day,
                                                     com_start_dt.hour, com_start_dt.minute)
                            match = "MATCH" if start == com_packed else "MISMATCH"
                            print(f"    COM Start={com_start_dt} (0x{com_packed:08X}) -> {match}")
                        except Exception as e:
                            print(f"    COM comparison error: {e}")

                        found = True

        if not found:
            print(f"\n  Task {task_id} (Priority={marker}): NOT FOUND in scan range")

    # Also look for the project summary task (Task 0)
    print(f"\n{'='*80}")
    print("Project summary task (ID 0):")
    try:
        # Project summary task might have OutlineLevel=0
        # Search for records with OutlineLevel=0 at offset +264
        for rec_off in range(STRIDE * 2, scan_size - STRIDE, STRIDE):
            rec_start = rec_off
            if rec_start + 368 < len(buf):
                outline = struct.unpack_from('<I', buf, rec_start + 264)[0]
                prio_val = struct.unpack_from('<H', buf, rec_start + 168)[0]
                dur = struct.unpack_from('<I', buf, rec_start + 368)[0]
                created = struct.unpack_from('<I', buf, rec_start)[0]
                start = struct.unpack_from('<I', buf, rec_start + 4)[0]
                if outline == 0 and prio_val > 0 and prio_val < 1000:
                    rec_num = (rec_off - STRIDE * 2) // STRIDE
                    print(f"  Record #{rec_num}: OutlineLevel=0, Priority={prio_val}")
                    print(f"    Created: 0x{created:08X} = {decode_date(created)}")
                    print(f"    Start: 0x{start:08X} = {decode_date(start)}")
                    print(f"    Duration: {dur} = {dur/10:.0f} min")
    except Exception as e:
        print(f"  Error: {e}")

    # Reset priorities
    print(f"\n{'='*80}")
    print("Resetting priorities to 500...")
    for task_id, task_obj, marker in tasks:
        task_obj.Priority = 500

    k32.CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
