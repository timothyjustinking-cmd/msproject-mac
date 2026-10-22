#!/usr/bin/env python3
"""Compare Duration storage for Task 1 vs Task 2.

Do Duration-only differential on both tasks to see if Duration
is at a consistent offset from Priority.
"""

import ctypes
import ctypes.wintypes
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

def snapshot_region(handle, start, size):
    return read_mem(handle, start, size)

def find_u32_changes(before, after, target_value=None):
    """Find u32 positions that changed, optionally matching a target new value."""
    changes = []
    for off in range(0, min(len(before), len(after)) - 3, 4):
        vb = struct.unpack_from('<I', before, off)[0]
        va = struct.unpack_from('<I', after, off)[0]
        if vb != va:
            if target_value is None or va == target_value:
                changes.append((off, vb, va))
    return changes


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    handle = k32.OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    task1_prio = 0x23b7ef6dd3c
    task2_prio = 0x23b7ef6b4bc

    # Get tasks
    tasks = {}
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t and t.ID in (1, 2):
            tasks[t.ID] = t

    # Read wide regions around both Priority addresses
    # Include area before AND after each task's priority
    region_size = 2048

    print("=== Task 1 Duration Differential ===")
    t1 = tasks[1]
    t1_orig_dur = t1.Duration
    print(f"Task 1 original Duration: {t1_orig_dur} min")

    # Snapshot before
    t1_start = task1_prio - 1024
    t1_before = snapshot_region(handle, t1_start, region_size)

    # Change Task 1 Duration
    t1.Duration = "10d"  # 4800 min
    time.sleep(0.3)
    print(f"Task 1 new Duration: {t1.Duration} min")
    new_dur_t1 = int(t1.Duration * 10)  # 48000

    t1_after = snapshot_region(handle, t1_start, region_size)

    # Find where the new Duration value appears
    dur_changes_t1 = find_u32_changes(t1_before, t1_after, new_dur_t1)
    print(f"\nPositions where {new_dur_t1} appeared (Duration*10):")
    for off, vb, va in dur_changes_t1:
        rel = off - 1024  # relative to Priority
        print(f"  prio{rel:+5d}: {vb} -> {va}")

    # Also show ALL changes
    all_changes_t1 = find_u32_changes(t1_before, t1_after)
    print(f"\nAll u32 changes ({len(all_changes_t1)} total):")
    for off, vb, va in all_changes_t1:
        rel = off - 1024
        label = " << DURATION" if va == new_dur_t1 else ""
        print(f"  prio{rel:+5d}: {vb} -> {va}{label}")

    # Restore Task 1
    t1.Duration = f"{int(t1_orig_dur)}m"
    time.sleep(0.3)

    print(f"\n{'='*60}")
    print("=== Task 2 Duration Differential ===")
    t2 = tasks[2]
    t2_orig_dur = t2.Duration
    print(f"Task 2 original Duration: {t2_orig_dur} min")

    # Snapshot before
    t2_start = task2_prio - 1024
    t2_before = snapshot_region(handle, t2_start, region_size)

    # Change Task 2 Duration
    t2.Duration = "10d"  # 4800 min
    time.sleep(0.3)
    print(f"Task 2 new Duration: {t2.Duration} min")
    new_dur_t2 = int(t2.Duration * 10)

    t2_after = snapshot_region(handle, t2_start, region_size)

    dur_changes_t2 = find_u32_changes(t2_before, t2_after, new_dur_t2)
    print(f"\nPositions where {new_dur_t2} appeared (Duration*10):")
    for off, vb, va in dur_changes_t2:
        rel = off - 1024
        print(f"  prio{rel:+5d}: {vb} -> {va}")

    all_changes_t2 = find_u32_changes(t2_before, t2_after)
    print(f"\nAll u32 changes ({len(all_changes_t2)} total):")
    for off, vb, va in all_changes_t2:
        rel = off - 1024
        label = " << DURATION" if va == new_dur_t2 else ""
        print(f"  prio{rel:+5d}: {vb} -> {va}{label}")

    # Restore Task 2
    t2.Duration = f"{int(t2_orig_dur)}m"
    time.sleep(0.3)

    # Summary
    print(f"\n{'='*60}")
    print("=== COMPARISON ===")
    t1_dur_offsets = [off - 1024 for off, _, va in dur_changes_t1]
    t2_dur_offsets = [off - 1024 for off, _, va in dur_changes_t2]
    print(f"Task 1 Duration*10 at prio offsets: {t1_dur_offsets}")
    print(f"Task 2 Duration*10 at prio offsets: {t2_dur_offsets}")

    if t1_dur_offsets and t2_dur_offsets:
        print(f"\nTask 1 Duration at prio{t1_dur_offsets[0]:+d}")
        print(f"Task 2 Duration at prio{t2_dur_offsets[0]:+d}")
        diff = t1_dur_offsets[0] - t2_dur_offsets[0]
        print(f"Difference: {diff} bytes")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
