#!/usr/bin/env python3
"""Verify the 576-byte per-task flat buffer stride.

The wide date scan found:
- Task 1 dates at prio-168 to prio-144
- Task 2 dates at prio+408 to prio+432
- Delta = 576 bytes = confirmed stride

This script:
1. Reads COM data for all tasks
2. Reads the flat buffer at the predicted addresses
3. Confirms the per-task record layout
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
        return f"inv(d={day},m={min_x10})"
    minute = min_x10 // 10
    if minute >= 1440:
        return f"inv(min={minute})"
    dt = datetime(1984, 1, 1) + timedelta(days=day - 1)
    hours = minute // 60
    mins = minute % 60
    return f"{dt.strftime('%Y-%m-%d')} {hours:02d}:{mins:02d}"

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

    # Read COM data for all tasks
    print("COM Task Data:")
    tasks = []
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is None:
            continue
        info = {"index": i, "Name": t.Name, "ID": t.ID}
        for fname in ['Start', 'Finish', 'EarlyStart', 'EarlyFinish',
                       'LateStart', 'LateFinish', 'Created',
                       'Priority', 'Duration', 'OutlineLevel',
                       'ConstraintDate']:
            try:
                val = getattr(t, fname)
                if val and hasattr(val, 'year'):
                    dt = datetime(val.year, val.month, val.day, val.hour, val.minute, val.second)
                    info[fname] = dt
                    info[fname + '_packed'] = encode_date(dt.year, dt.month, dt.day, dt.hour, dt.minute)
                else:
                    info[fname] = val
            except:
                info[fname] = None

        tasks.append(info)
        print(f"\n  Task {i}: '{info['Name']}' (ID={info['ID']})")
        print(f"    Start={info.get('Start', 'N/A')}, Finish={info.get('Finish', 'N/A')}")
        print(f"    Priority={info.get('Priority', 'N/A')}, Duration={info.get('Duration', 'N/A')}")
        print(f"    OutlineLevel={info.get('OutlineLevel', 'N/A')}")
        print(f"    Created={info.get('Created', 'N/A')}")

    # Read flat buffer for MULTIPLE task strides
    print(f"\n{'='*80}")
    print(f"FLAT BUFFER STRIDE VERIFICATION (stride={STRIDE})")

    # Task 1 record starts at prio-168
    record_base = prio_addr - 168

    for task_idx, task_info in enumerate(tasks):
        task_base = record_base + task_idx * STRIDE
        data = read_mem(handle, task_base, STRIDE)
        if not data:
            print(f"\nTask {task_idx+1}: Can't read at 0x{task_base:012x}")
            continue

        print(f"\n{'='*80}")
        print(f"Task {task_idx+1}: '{task_info['Name']}' (record at 0x{task_base:012x})")
        print(f"  prio equivalent offset: prio{task_idx * STRIDE:+d}" if task_idx > 0 else "  prio equivalent: task 1 (reference)")

        # Check known field positions (offsets from record start)
        checks = [
            (0,   'Created',    'packed_date'),
            (4,   'Start',      'packed_date'),
            (8,   'Finish',     'packed_date'),
            (12,  'EarlyStart', 'packed_date'),
            (16,  'EarlyFinish','packed_date'),
            (20,  'LateStart',  'packed_date'),
            (24,  'LateFinish', 'packed_date'),
            (168, 'Priority',   'u32'),
            (264, 'OutlineLevel','u32'),
            (368, 'Duration',   'duration_tenth'),
        ]

        for off, field, fmt in checks:
            if off + 4 > len(data):
                continue
            u32 = struct.unpack_from('<I', data, off)[0]

            if fmt == 'packed_date':
                decoded = decode_date(u32)
                expected_key = field + '_packed'
                expected = task_info.get(expected_key)
                match = ""
                if expected is not None and u32 == expected:
                    match = " MATCH"
                elif u32 == 0xFFFFFFFF:
                    match = " (sentinel)"
                elif expected is not None:
                    match = f" MISMATCH (expected 0x{expected:08X})"
                print(f"  +{off:3d} ({field:15s}): 0x{u32:08X} = {decoded}{match}")
            elif fmt == 'u32':
                expected = task_info.get(field)
                match = ""
                if expected is not None and u32 == expected:
                    match = " MATCH"
                elif expected is not None:
                    match = f" MISMATCH (expected {expected})"
                print(f"  +{off:3d} ({field:15s}): {u32}{match}")
            elif fmt == 'duration_tenth':
                dur_min = u32 / 10
                expected = task_info.get(field)
                match = ""
                if expected is not None and abs(dur_min - expected) < 1:
                    match = " MATCH"
                elif expected is not None:
                    match = f" MISMATCH (expected {expected})"
                print(f"  +{off:3d} ({field:15s}): {u32} = {dur_min:.0f} min{match}")

        # Also dump first 32 u32s for additional field discovery
        if task_idx == 0:
            print(f"\n  Full record (first 96 u32s, stride={STRIDE}):")
            for off in range(0, min(STRIDE, 384), 4):
                u32 = struct.unpack_from('<I', data, off)[0]
                if u32 == 0:
                    continue
                if u32 == 0xFFFFFFFF:
                    print(f"    +{off:3d}: 0xFFFFFFFF (sentinel)")
                    continue
                dt_str = decode_date(u32)
                is_date = "inv" not in dt_str and "NA" not in dt_str
                annotation = f" date={dt_str}" if is_date else ""
                print(f"    +{off:3d}: 0x{u32:08X} ({u32:10d}){annotation}")

    # Verify Task 2's dates match at stride offset
    if len(tasks) >= 2:
        print(f"\n{'='*80}")
        print("STRIDE VERIFICATION: Task 2 should be at prio+408")
        t2_base = record_base + STRIDE
        t2_data = read_mem(handle, t2_base, 32)
        if t2_data:
            print("  Task 2 date block (record+0 to +28):")
            date_names = ['Created', 'Start', 'Finish', 'EarlyStart', 'EarlyFinish', 'LateStart', 'LateFinish']
            for i, name in enumerate(date_names):
                off = i * 4
                u32 = struct.unpack_from('<I', t2_data, off)[0]
                decoded = decode_date(u32)
                expected_key = name + '_packed'
                expected = tasks[1].get(expected_key) if len(tasks) > 1 else None
                match = " MATCH" if expected and u32 == expected else ""
                print(f"    +{off}: {name:15s} = 0x{u32:08X} = {decoded}{match}")

    # Check slack fields
    print(f"\n{'='*80}")
    print("SLACK FIELD ANALYSIS")
    for task_idx, task_info in enumerate(tasks):
        task_base = record_base + task_idx * STRIDE
        data = read_mem(handle, task_base, STRIDE)
        if not data:
            continue

        # Slack should be at record offsets 68, 72, 76 (prio-100/-96/-92 for task 1)
        print(f"\n  Task {task_idx+1} '{task_info['Name']}':")
        for off in [64, 68, 72, 76, 80]:
            u32 = struct.unpack_from('<I', data, off)[0]
            if u32 == 0: continue
            work_min = u32 / 10
            work_hrs = work_min / 60
            work_days = work_hrs / 8
            print(f"    +{off:3d}: {u32:10d} = {work_min:.0f}min = {work_hrs:.1f}hrs = {work_days:.1f} 8h-days")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
