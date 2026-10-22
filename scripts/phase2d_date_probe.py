#!/usr/bin/env python3
"""Map the date cluster in the flat buffer (prio-168 to prio-108).

Known: ~13 date32 slots. ActualStart at prio-140 confirmed.
COM dates: Start, Finish, EarlyStart, EarlyFinish, LateStart, LateFinish,
           ActualStart, ActualFinish, Resume, Stop, Deadline, ConstraintDate.

Strategy: Read dates, then change COM properties one at a time and diff.
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
    import pywintypes
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

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]


def read_dates(handle, prio_addr, start_rel, count):
    addr = prio_addr + start_rel
    data = read_mem(handle, addr, count * 4)
    if not data:
        return []
    result = []
    for i in range(count):
        val = struct.unpack_from('<I', data, i * 4)[0]
        rel = start_rel + i * 4
        result.append((rel, val))
    return result


def date_str(val):
    if val == 0xFFFFFFFF:
        return "NO_DATE (0xFFFFFFFF)"
    if val == 0:
        return "ZERO"
    # MS Project stores dates as minutes since 1/1/1984 00:00
    try:
        base = datetime(1984, 1, 1)
        dt = base + timedelta(minutes=val)
        return f"{dt.strftime('%Y-%m-%d %H:%M')} (0x{val:08x} = {val}min)"
    except:
        return f"0x{val:08x} ({val})"


def dump_region(handle, prio_addr, label, start=-180, end=20):
    print(f"\n=== {label} ===")
    # Dump as u32 values
    dates = read_dates(handle, prio_addr, start, (end - start) // 4)
    for rel, val in dates:
        if val != 0:
            print(f"  prio{rel:+4d}: {date_str(val)}")
        else:
            print(f"  prio{rel:+4d}: 0")


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

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

    print(f"Task: ID={task.ID} Name='{task.Name}'")
    prio_addr = 0x23b7ef6dd3c

    # Reset task first
    task.Duration = 2400  # 5 days
    task.PercentComplete = 0
    task.FixedCost = 0.0
    time.sleep(0.5)

    # Print COM date values
    print("\n--- COM Date Values ---")
    date_props = [
        'Start', 'Finish', 'EarlyStart', 'EarlyFinish',
        'LateStart', 'LateFinish', 'ActualStart', 'ActualFinish',
        'Resume', 'Stop', 'Deadline', 'ConstraintDate',
        'Created', 'FreeSlack', 'TotalSlack',
    ]
    for prop in date_props:
        try:
            val = getattr(task, prop)
            print(f"  {prop:25s} = {val}")
        except Exception as e:
            print(f"  {prop:25s} = ERROR: {e}")

    # Step 1: Baseline - dump full date region
    dump_region(handle, prio_addr, "STATE 1: Clean (5 day task, 0%)", -180, 20)

    # Step 2: Set ConstraintDate to a distinctive date
    # Use SetField for ConstraintDate and ConstraintType
    # pjConstraintAsLateAsPossible = 1, pjConstraintMustStartOn = 2
    task_fid_constraint_type = 0x0b400058  # 188743768
    task_fid_constraint_date = 0x0b400071  # 188743793
    try:
        # Set Must Start On constraint with a specific date
        app.SetField(task_fid_constraint_type, str(2))  # MustStartOn
        app.SetField(task_fid_constraint_date, "11/15/2026")
        time.sleep(0.5)
        print(f"\nSet ConstraintType=MustStartOn, ConstraintDate=11/15/2026")
        print(f"  COM ConstraintDate = {task.ConstraintDate}")
        print(f"  COM ConstraintType = {task.ConstraintType}")
    except Exception as e:
        print(f"  ConstraintDate set failed: {e}")

    dump_region(handle, prio_addr, "STATE 2: After ConstraintDate=11/15/2026", -180, 20)

    # Step 3: Set Deadline
    try:
        task.Deadline = "12/25/2026"
        time.sleep(0.5)
        print(f"\nSet Deadline=12/25/2026")
        print(f"  COM Deadline = {task.Deadline}")
    except Exception as e:
        print(f"  Deadline set failed: {e}")

    dump_region(handle, prio_addr, "STATE 3: After Deadline=12/25/2026", -180, 20)

    # Step 4: Set PercentComplete > 0 to trigger ActualStart
    task.PercentComplete = 50
    time.sleep(0.5)
    print(f"\nSet PercentComplete=50")
    print(f"  COM ActualStart = {task.ActualStart}")
    print(f"  COM ActualFinish = {task.ActualFinish}")
    print(f"  COM Resume = {task.Resume}")
    print(f"  COM Stop = {task.Stop}")

    dump_region(handle, prio_addr, "STATE 4: After %Complete=50", -180, 20)

    # Step 5: Set PercentComplete = 100 to trigger ActualFinish
    task.PercentComplete = 100
    time.sleep(0.5)
    print(f"\nSet PercentComplete=100")
    print(f"  COM ActualStart = {task.ActualStart}")
    print(f"  COM ActualFinish = {task.ActualFinish}")

    dump_region(handle, prio_addr, "STATE 5: After %Complete=100", -180, 20)

    # Reset constraint and deadline
    try:
        app.SetField(task_fid_constraint_type, str(0))  # ASAP
        task.Deadline = "NA"
    except:
        pass
    task.PercentComplete = 0
    time.sleep(0.5)

    # Step 6: Change Start date to something distinctive
    try:
        task.Start = "11/1/2026"
        time.sleep(0.5)
        print(f"\nSet Start=11/1/2026")
        print(f"  COM Start = {task.Start}")
        print(f"  COM Finish = {task.Finish}")
    except Exception as e:
        print(f"  Start set failed: {e}")

    dump_region(handle, prio_addr, "STATE 6: After Start=11/1/2026", -180, 20)

    # Now let's diff STATE 1 vs STATE 6 to find Start position
    # We already have the output printed, so just leave it for analysis

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
