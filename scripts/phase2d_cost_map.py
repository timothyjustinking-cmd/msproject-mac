#!/usr/bin/env python3
"""Precisely map cost/work double positions in flat buffer.

Known: FixedCost (slot 5) at prio-52. Need to find the other 9 doubles.
Slots 0-6: Work, ActualWork, RemainingWork, Cost, ActualCost, FixedCost, RemainingCost
Slots 42, 43: unnamed_171, unnamed_172
Slot 82: BaselineFixedCost

Strategy: Set FixedCost, read doubles at all 8-byte-aligned positions near it.
Then set PercentComplete to trigger cost computation and see what doubles appear.
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
    """Read `count` doubles starting at prio+start_rel."""
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

    # Step 1: Reset and set FixedCost to distinctive value
    task.Duration = 2400
    task.Priority = 500
    task.PercentComplete = 0
    task.FixedCost = 0.0
    task.Marked = False
    task.Milestone = False
    time.sleep(0.5)

    # Check alignment: prio_addr = 0x23b7ef6dd3c
    # 0xd3c mod 8 = 4 (not 8-aligned)
    # So doubles at 8-byte alignment from (prio_addr - 4):
    # prio-52 = 0x...d08 -> d08 mod 8 = 0 (aligned!)
    # So doubles are at prio-52, -44, -36, -28, -20, -12, etc. (every 8 bytes, 8-aligned)
    # AND prio-60, -68, -76, -84, -92, -100 (going backward)

    print("\n=== STATE: FixedCost=$0, PercentComplete=0 ===")
    print("Doubles from prio-108 to prio+4:")
    doubles = read_doubles(handle, prio_addr, -108, 14)
    for rel, val in doubles:
        if val != 0.0:
            print(f"  prio{rel:+d}: {val:20.2f}  (${val/100:.2f})")
        else:
            print(f"  prio{rel:+d}: {val:20.2f}")

    # Step 2: Set FixedCost = $77777
    task.FixedCost = 77777.0
    time.sleep(0.5)

    print(f"\n=== STATE: FixedCost=$77777, PercentComplete=0 ===")
    print(f"COM: Cost=${task.Cost}, ActualCost=${task.ActualCost}, "
          f"FixedCost=${task.FixedCost}, RemainingCost=${task.RemainingCost}")

    print("Doubles from prio-108 to prio+4:")
    doubles = read_doubles(handle, prio_addr, -108, 14)
    for rel, val in doubles:
        dollar = f"  (${val/100:.2f})" if val != 0.0 else ""
        # Mark known position
        mark = " <<< FIXEDCOST" if abs(val - 7777700.0) < 1.0 else ""
        print(f"  prio{rel:+d}: {val:20.2f}{dollar}{mark}")

    # Step 3: Set PercentComplete = 50% (prorated accrual)
    task.PercentComplete = 50
    time.sleep(0.5)

    print(f"\n=== STATE: FixedCost=$77777, PercentComplete=50 ===")
    print(f"COM: Cost=${task.Cost}, ActualCost=${task.ActualCost}, "
          f"FixedCost=${task.FixedCost}, RemainingCost=${task.RemainingCost}")
    try:
        print(f"     Work={task.Work}, ActualWork={task.ActualWork}, RemainingWork={task.RemainingWork}")
    except:
        pass

    print("Doubles from prio-108 to prio+4:")
    doubles = read_doubles(handle, prio_addr, -108, 14)
    for rel, val in doubles:
        dollar = f"  (${val/100:.2f})" if abs(val) > 0.001 else ""
        print(f"  prio{rel:+d}: {val:20.2f}{dollar}")

    # Step 4: Set PercentComplete = 100% (all actual)
    task.PercentComplete = 100
    time.sleep(0.5)

    print(f"\n=== STATE: FixedCost=$77777, PercentComplete=100 ===")
    print(f"COM: Cost=${task.Cost}, ActualCost=${task.ActualCost}, "
          f"FixedCost=${task.FixedCost}, RemainingCost=${task.RemainingCost}")

    print("Doubles from prio-108 to prio+4:")
    doubles = read_doubles(handle, prio_addr, -108, 14)
    for rel, val in doubles:
        dollar = f"  (${val/100:.2f})" if abs(val) > 0.001 else ""
        print(f"  prio{rel:+d}: {val:20.2f}{dollar}")

    # Step 5: Change FixedCostAccrual to Start (all cost at start)
    task.PercentComplete = 0
    task.FixedCostAccrual = 1  # Start
    task.PercentComplete = 50
    time.sleep(0.5)

    print(f"\n=== STATE: FixedCost=$77777, Accrual=Start, PercentComplete=50 ===")
    print(f"COM: Cost=${task.Cost}, ActualCost=${task.ActualCost}, "
          f"FixedCost=${task.FixedCost}, RemainingCost=${task.RemainingCost}")

    print("Doubles from prio-108 to prio+4:")
    doubles = read_doubles(handle, prio_addr, -108, 14)
    for rel, val in doubles:
        dollar = f"  (${val/100:.2f})" if abs(val) > 0.001 else ""
        print(f"  prio{rel:+d}: {val:20.2f}{dollar}")

    # Step 6: Set FixedCostAccrual to End
    task.PercentComplete = 0
    task.FixedCostAccrual = 2  # End
    task.PercentComplete = 50
    time.sleep(0.5)

    print(f"\n=== STATE: FixedCost=$77777, Accrual=End, PercentComplete=50 ===")
    print(f"COM: Cost=${task.Cost}, ActualCost=${task.ActualCost}, "
          f"FixedCost=${task.FixedCost}, RemainingCost=${task.RemainingCost}")

    print("Doubles from prio-108 to prio+4:")
    doubles = read_doubles(handle, prio_addr, -108, 14)
    for rel, val in doubles:
        dollar = f"  (${val/100:.2f})" if abs(val) > 0.001 else ""
        print(f"  prio{rel:+d}: {val:20.2f}{dollar}")

    # Reset
    task.PercentComplete = 0
    task.FixedCost = 0.0
    task.FixedCostAccrual = 3  # Prorated
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
