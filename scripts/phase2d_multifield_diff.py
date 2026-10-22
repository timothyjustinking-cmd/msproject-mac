#!/usr/bin/env python3
"""Multi-field differential scan on Task 2.

Change multiple SC1 fields to distinctive values simultaneously,
then find where ALL changes land to confirm the record layout.
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
    import pywintypes
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
        return None
    minute = min_x10 // 10
    if minute >= 1440:
        return None
    dt = datetime(1984, 1, 1) + timedelta(days=day - 1)
    return f"{dt.strftime('%Y-%m-%d')} {minute//60:02d}:{minute%60:02d}"


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    handle = k32.OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    task2_prio_addr = 0x23b7ef6b4bc

    task2 = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t and t.ID == 2:
            task2 = t
            break

    if not task2:
        print("ERROR: Task 2 not found"); return

    # Save original values
    orig = {
        'priority': task2.Priority,
        'duration': task2.Duration,
        'start': task2.Start,
        'pct': task2.PercentComplete,
    }
    print(f"Task 2 originals:")
    print(f"  Priority: {orig['priority']}")
    print(f"  Duration: {orig['duration']} min")
    print(f"  Start: {orig['start']}")
    print(f"  PercentComplete: {orig['pct']}")

    # Read a WIDE region before changes: 2048 bytes centered on Priority
    region_start = task2_prio_addr - 1024
    region_size = 2048
    data_before = read_mem(handle, region_start, region_size)
    if not data_before:
        print("ERROR: Can't read memory"); return
    prio_off = 1024  # offset of Priority within buffer

    # Change multiple fields to distinctive values
    print("\nChanging fields...")
    task2.Priority = 777
    task2.Duration = "5d"  # 5 days = 2400 min (different from 1440)
    task2.Start = pywintypes.Time(datetime(2026, 6, 1, 8, 0, 0).timetuple())
    task2.PercentComplete = 42
    time.sleep(0.5)

    new_vals = {
        'priority': task2.Priority,
        'duration': task2.Duration,
        'start': task2.Start,
        'pct': task2.PercentComplete,
    }
    print(f"Task 2 new values:")
    print(f"  Priority: {new_vals['priority']}")
    print(f"  Duration: {new_vals['duration']} min")
    s = task2.Start
    print(f"  Start: {s.year}-{s.month:02d}-{s.day:02d} {s.hour:02d}:{s.minute:02d}")
    print(f"  PercentComplete: {new_vals['pct']}")

    # Read after changes
    data_after = read_mem(handle, region_start, region_size)
    if not data_after:
        print("ERROR: Can't read after"); return

    # Find ALL u32 changes
    print(f"\n{'='*90}")
    print(f"ALL u32 changes in {region_size}-byte region around Priority:")
    print(f"{'='*90}")

    changes = []
    for off in range(0, min(len(data_before), len(data_after)) - 3, 4):
        v_before = struct.unpack_from('<I', data_before, off)[0]
        v_after = struct.unpack_from('<I', data_after, off)[0]
        if v_before != v_after:
            rel = off - prio_off
            addr = region_start + off

            # Try to identify the change
            d_before = decode_date(v_before)
            d_after = decode_date(v_after)

            label = ""
            if v_after == 777: label = " << PRIORITY (u16 at low bits)"
            elif v_after == 24000: label = " << DURATION*10 (2400min)"
            elif v_after == 42: label = " << PERCENT_COMPLETE"

            # Check u16 at this offset
            u16_before = struct.unpack_from('<H', data_before, off)[0]
            u16_after = struct.unpack_from('<H', data_after, off)[0]
            if u16_after == 777 and not label:
                label = " << PRIORITY (u16)"
            if u16_after == 42:
                label = " << PERCENT_COMPLETE (u16)"

            before_str = d_before if d_before else f"0x{v_before:08X} ({v_before})"
            after_str = d_after if d_after else f"0x{v_after:08X} ({v_after})"

            print(f"  prio{rel:+5d} [0x{addr:012x}]: {before_str:>30} -> {after_str:<30}{label}")
            changes.append((rel, v_before, v_after, label))

    # Also check u16 changes (Priority is u16)
    print(f"\nAdditional u16-only changes:")
    for off in range(0, min(len(data_before), len(data_after)) - 1, 2):
        u16_b = struct.unpack_from('<H', data_before, off)[0]
        u16_a = struct.unpack_from('<H', data_after, off)[0]
        if u16_b != u16_a:
            # Check if this was already reported as u32 change
            u32_off = (off // 4) * 4
            v32_b = struct.unpack_from('<I', data_before, u32_off)[0]
            v32_a = struct.unpack_from('<I', data_after, u32_off)[0]
            if v32_b == v32_a:
                # Only the u16 changed, not the containing u32 — shouldn't happen
                # Actually this means a u16 change that didn't show as u32 change
                rel = off - prio_off
                addr = region_start + off
                label = ""
                if u16_a == 777: label = " << PRIORITY"
                if u16_a == 42: label = " << PERCENT_COMPLETE"
                print(f"  prio{rel:+5d} [0x{addr:012x}]: u16 {u16_b} -> {u16_a}{label}")

    # Compute field offsets relative to record start (prio-168)
    print(f"\n{'='*90}")
    print(f"Changes relative to record start (prio-168):")
    print(f"{'='*90}")
    for rel, vb, va, label in changes:
        rec_off = rel + 168
        db = decode_date(vb)
        da = decode_date(va)
        before_str = db if db else f"{vb}"
        after_str = da if da else f"{va}"
        print(f"  rec+{rec_off:>4d} (prio{rel:+5d}): {before_str} -> {after_str}{label}")

    print(f"\nTotal u32 changes: {len(changes)}")

    # Restore
    print("\nRestoring Task 2...")
    task2.Priority = orig['priority']
    task2.PercentComplete = orig['pct']
    task2.Start = orig['start']
    task2.Duration = f"{int(orig['duration'])}m"
    time.sleep(0.3)
    print(f"Restored: Prio={task2.Priority}, Dur={task2.Duration}, Pct={task2.PercentComplete}")

    k32.CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
