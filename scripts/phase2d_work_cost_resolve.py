#!/usr/bin/env python3
"""Resolve cost/work double mapping by assigning a resource.

With a resource at a specific rate, Work fields populate with hours
and Cost fields split between FixedCost and work-based cost.
This disambiguates all 10+ double positions.

Strategy:
1. Create resource with StandardRate = $100/hr
2. Assign to task (5 day duration = 40 hrs work)
3. Read all doubles -> Work=2400min, Cost=$4000+FixedCost
4. Set PercentComplete=50 -> ActualWork=1200, ActualCost=$2000+FixedCostAccrual
5. Cross-reference with COM values to map each position
"""

import ctypes
import struct
import sys
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

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

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]


def read_doubles(handle, prio_addr, start_rel, count):
    addr = prio_addr + start_rel
    data = read_mem(handle, addr, count * 8)
    if not data:
        return []
    result = []
    for i in range(count):
        val = struct.unpack_from('<d', data, i * 8)[0]
        rel = start_rel + i * 8
        result.append((rel, val))
    return result


def dump_doubles(handle, prio_addr, label, task):
    print(f"\n=== {label} ===")
    try:
        print(f"COM: Work={task.Work}min, ActualWork={task.ActualWork}min, "
              f"RemainingWork={task.RemainingWork}min")
        print(f"     Cost=${task.Cost}, ActualCost=${task.ActualCost}, "
              f"FixedCost=${task.FixedCost}, RemainingCost=${task.RemainingCost}")
    except Exception as e:
        print(f"  COM read error: {e}")

    print("Doubles from prio-108 to prio+4:")
    doubles = read_doubles(handle, prio_addr, -108, 14)
    for rel, val in doubles:
        if abs(val) > 0.001:
            # Try to interpret: x100 for cost, raw for work (minutes)
            as_cost = val / 100
            print(f"  prio{rel:+4d}: {val:20.2f}  (${as_cost:,.2f} or {val:.0f}min)")
        else:
            print(f"  prio{rel:+4d}: {val:20.2f}")


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    # Find first task
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    print(f"Task: ID={task.ID} Name='{task.Name}'")
    prio_addr = 0x23b7ef6dd3c

    # Step 0: Reset task
    task.Duration = 2400  # 5 days
    task.PercentComplete = 0
    task.FixedCost = 0.0
    task.FixedCostAccrual = 3  # Prorated
    time.sleep(0.5)

    # Step 1: Baseline - no resource, no FixedCost
    dump_doubles(handle, prio_addr, "STATE 1: Clean (no resource, no FixedCost)", task)

    # Step 2: Add FixedCost only (no resource yet)
    task.FixedCost = 50000.0
    time.sleep(0.5)
    dump_doubles(handle, prio_addr, "STATE 2: FixedCost=$50000 (no resource)", task)

    # Step 3: Create/find a resource and assign it
    res = None
    for i in range(1, proj.Resources.Count + 1):
        r = proj.Resources.Item(i)
        if r is not None:
            res = r
            break

    if res is None:
        # Create a new resource
        res = proj.Resources.Add("TestResource")
        print(f"Created resource: {res.Name}")
    else:
        print(f"Using existing resource: {res.Name}")

    # Set rate to $100/hr
    res.StandardRate = "$100/h"
    print(f"Resource rate: StandardRate={res.StandardRate}")

    # Assign the resource to the task
    # First check if already assigned
    already_assigned = False
    for j in range(1, task.Assignments.Count + 1):
        a = task.Assignments.Item(j)
        if a.ResourceID == res.ID:
            already_assigned = True
            print(f"Resource already assigned (Assignment ID={a.TaskID})")
            break

    if not already_assigned:
        task.Assignments.Add(task.ID, res.ID)
        print(f"Assigned resource {res.Name} to task {task.Name}")

    time.sleep(1.0)  # Let scheduling engine recalculate

    dump_doubles(handle, prio_addr, "STATE 3: FixedCost=$50000 + Resource@$100/hr, %Complete=0", task)

    # Step 4: Set PercentComplete = 50
    task.PercentComplete = 50
    time.sleep(1.0)

    dump_doubles(handle, prio_addr, "STATE 4: %Complete=50", task)

    # Step 5: Set PercentComplete = 100
    task.PercentComplete = 100
    time.sleep(1.0)

    dump_doubles(handle, prio_addr, "STATE 5: %Complete=100", task)

    # Step 6: Change FixedCost to 0 (isolate work cost)
    task.PercentComplete = 0
    task.FixedCost = 0.0
    time.sleep(1.0)

    dump_doubles(handle, prio_addr, "STATE 6: FixedCost=$0, Resource@$100/hr, %Complete=0", task)

    # Step 7: 50% with resource only (no fixed cost)
    task.PercentComplete = 50
    time.sleep(1.0)

    dump_doubles(handle, prio_addr, "STATE 7: FixedCost=$0, Resource@$100/hr, %Complete=50", task)

    # Also read a wider range to catch any work doubles further out
    print("\n=== EXTENDED RANGE: prio-200 to prio+100 ===")
    for start in range(-200, 100, 80):
        doubles = read_doubles(handle, prio_addr, start, 10)
        for rel, val in doubles:
            if abs(val) > 0.001:
                as_cost = val / 100
                print(f"  prio{rel:+4d}: {val:20.2f}  (${as_cost:,.2f} or {val:.0f}min)")

    # Cleanup: remove assignment, reset
    try:
        for j in range(task.Assignments.Count, 0, -1):
            a = task.Assignments.Item(j)
            if a.ResourceID == res.ID:
                a.Delete()
                print(f"\nRemoved resource assignment")
                break
    except Exception as e:
        print(f"\nNote: Could not remove assignment: {e}")

    task.PercentComplete = 0
    task.FixedCost = 0.0
    time.sleep(0.5)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
