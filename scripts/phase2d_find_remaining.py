#!/usr/bin/env python3
"""Full-memory diff for fields that weren't found in the getter record:
Marked, FixedCost, Milestone, HideBar, ConstraintType.

Also re-finds Duration after setting it via property to confirm record location.
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

def get_image_base(pid):
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    hProcess = OpenProcess(0x0410 | 0x0010, False, pid)
    if not hProcess: return None
    hMods = (ctypes.c_void_p * 1024)()
    cbNeeded = ctypes.c_ulong()
    if not psapi.EnumProcessModulesEx(hProcess, ctypes.byref(hMods), ctypes.sizeof(hMods),
                                       ctypes.byref(cbNeeded), 0x03):
        CloseHandle(hProcess); return None
    base = hMods[0]
    CloseHandle(hProcess)
    return base

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]

def scan_readable_regions(handle):
    regions = []
    MEM_COMMIT = 0x1000
    READABLE = {0x02, 0x04, 0x20, 0x40}
    class MBI(ctypes.Structure):
        _fields_ = [
            ("BaseAddress", ctypes.c_void_p),
            ("AllocationBase", ctypes.c_void_p),
            ("AllocationProtect", ctypes.c_ulong),
            ("PartitionId", ctypes.c_ushort),
            ("RegionSize", ctypes.c_size_t),
            ("State", ctypes.c_ulong),
            ("Protect", ctypes.c_ulong),
            ("Type", ctypes.c_ulong),
        ]
    addr = 0
    mbi = MBI()
    while addr < 0x7FFFFFFFFFFF:
        result = k32.VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
        if result == 0: break
        if mbi.State == MEM_COMMIT and mbi.Protect in READABLE:
            if 0x1000 <= mbi.RegionSize <= 0x10000000:
                regions.append((mbi.BaseAddress, mbi.RegionSize))
        addr = (mbi.BaseAddress or 0) + mbi.RegionSize
        if addr <= (mbi.BaseAddress or 0): break
    return regions


def full_snapshot(handle, regions):
    snap = {}
    for base, size in regions:
        data = read_mem(handle, base, size)
        if data:
            snap[(base, size)] = data
    return snap


def find_value_transitions(before, after, old_bytes, new_bytes):
    """Find all locations where old_bytes changed to new_bytes."""
    matches = []
    vlen = len(old_bytes)
    for key in before:
        if key not in after:
            continue
        bdata = before[key]
        adata = after[key]
        base, size = key
        minlen = min(len(bdata), len(adata))
        for i in range(minlen - vlen + 1):
            if bdata[i:i+vlen] == old_bytes and adata[i:i+vlen] == new_bytes:
                matches.append(base + i)
    return matches


def count_total_changes(before, after):
    """Count total changed bytes."""
    total = 0
    for key in before:
        if key not in after: continue
        bd, ad = before[key], after[key]
        for i in range(min(len(bd), len(ad))):
            if bd[i] != ad[i]:
                total += 1
    return total


def probe_field(handle, regions, task, field_name, set_func, reset_func, searches):
    """Set a field, take snapshot diff, search for value transitions."""
    print(f"\n{'='*80}")
    print(f"PROBING: {field_name}")
    print(f"{'='*80}")

    snap_before = full_snapshot(handle, regions)
    set_func(task)
    time.sleep(0.5)
    snap_after = full_snapshot(handle, regions)

    changed = count_total_changes(snap_before, snap_after)
    print(f"  Total changed bytes: {changed}")

    for label, old_bytes, new_bytes in searches:
        matches = find_value_transitions(snap_before, snap_after, old_bytes, new_bytes)
        if matches:
            print(f"  FOUND {label} at {len(matches)} locations:")
            for m in matches[:5]:
                # Read context around the match
                ctx = read_mem(handle, m - 16, 48)
                if ctx:
                    hex_str = ' '.join(f'{b:02x}' for b in ctx)
                    print(f"    {m:#x}: ...{hex_str}...")
        else:
            print(f"  NOT FOUND: {label}")

    reset_func(task)
    time.sleep(0.3)
    return snap_before, snap_after


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
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

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")
    print(f"Task: ID={task.ID} Name='{task.Name}'")
    print(f"  Priority={task.Priority}")
    print(f"  Duration={task.Duration}")
    print(f"  PercentComplete={task.PercentComplete}")
    print(f"  Marked={task.Marked}")
    print(f"  Milestone={task.Milestone}")
    print(f"  FixedCost={task.FixedCost}")
    print(f"  HideBar={task.HideBar}")

    # First reset task to clean state
    task.Duration = 2400   # 5 days (minutes)
    task.Priority = 500
    task.PercentComplete = 0
    task.Marked = False
    task.Milestone = False
    task.FixedCost = 0.0
    task.HideBar = False
    time.sleep(0.5)

    print(f"\n  After reset:")
    print(f"  Duration={task.Duration}, Priority={task.Priority}")
    print(f"  PercentComplete={task.PercentComplete}, Marked={task.Marked}")
    print(f"  Milestone={task.Milestone}, FixedCost={task.FixedCost}")

    regions = scan_readable_regions(handle)
    total_mb = sum(s for _, s in regions) / 1024 / 1024
    print(f"\n{len(regions)} readable regions, {total_mb:.0f} MB total")

    # PROBE 1: Marked (boolean, should be 0->1 or False->True)
    probe_field(handle, regions, task, "Marked = True",
        lambda t: setattr(t, 'Marked', True),
        lambda t: setattr(t, 'Marked', False),
        [
            ("u8 0->1",   b'\x00', b'\x01'),  # too many false positives probably
            ("i16 0->1",  struct.pack('<h', 0), struct.pack('<h', 1)),
            ("i32 0->1",  struct.pack('<i', 0), struct.pack('<i', 1)),
            ("i32 0->-1", struct.pack('<i', 0), struct.pack('<i', -1)),
        ]
    )

    # PROBE 2: FixedCost = $12345 (stored as double * 100 in TASKNODE)
    # COM FixedCost returns value in $ (12345.0)
    # TASKNODE stores as double * 100 = 1234500.0
    probe_field(handle, regions, task, "FixedCost = $12345",
        lambda t: setattr(t, 'FixedCost', 12345.0),
        lambda t: setattr(t, 'FixedCost', 0.0),
        [
            ("dbl 0->12345.0",     struct.pack('<d', 0.0), struct.pack('<d', 12345.0)),
            ("dbl 0->1234500.0",   struct.pack('<d', 0.0), struct.pack('<d', 1234500.0)),
            ("i32 0->12345",       struct.pack('<i', 0), struct.pack('<i', 12345)),
            ("i32 0->1234500",     struct.pack('<i', 0), struct.pack('<i', 1234500)),
        ]
    )

    # PROBE 3: Milestone = True
    probe_field(handle, regions, task, "Milestone = True",
        lambda t: setattr(t, 'Milestone', True),
        lambda t: setattr(t, 'Milestone', False),
        [
            ("i16 0->1",  struct.pack('<h', 0), struct.pack('<h', 1)),
            ("i32 0->1",  struct.pack('<i', 0), struct.pack('<i', 1)),
            ("i32 0->-1", struct.pack('<i', 0), struct.pack('<i', -1)),
        ]
    )

    # PROBE 4: HideBar = True
    probe_field(handle, regions, task, "HideBar = True",
        lambda t: setattr(t, 'HideBar', True),
        lambda t: setattr(t, 'HideBar', False),
        [
            ("i16 0->1",  struct.pack('<h', 0), struct.pack('<h', 1)),
            ("i32 0->1",  struct.pack('<i', 0), struct.pack('<i', 1)),
            ("i32 0->-1", struct.pack('<i', 0), struct.pack('<i', -1)),
        ]
    )

    # PROBE 5: ConstraintType = 1 (Must Finish On)
    # Use numeric SetField with pjConstraintMustFinishOn = 1
    probe_field(handle, regions, task, "ConstraintType = 1 (MFO)",
        lambda t: t.SetField(188743697, 1),  # ConstraintType
        lambda t: t.SetField(188743697, 0),  # ASAP
        [
            ("i16 0->1",  struct.pack('<h', 0), struct.pack('<h', 1)),
            ("i32 0->1",  struct.pack('<i', 0), struct.pack('<i', 1)),
        ]
    )

    # PROBE 6: Duration change with fresh record search
    # Change from 2400 to 4800 (5d to 10d)
    print(f"\n{'='*80}")
    print(f"PROBING: Duration 2400 -> 4800 (5d -> 10d) with value search")
    print(f"{'='*80}")

    snap_before = full_snapshot(handle, regions)
    task.Duration = 4800
    time.sleep(0.5)
    print(f"  Duration after set: {task.Duration}")
    snap_after = full_snapshot(handle, regions)

    # Search for the raw value transition
    for label, old_bytes, new_bytes in [
        ("i32 2400->4800",   struct.pack('<i', 2400), struct.pack('<i', 4800)),
        ("i16 2400->4800",   struct.pack('<h', 2400), struct.pack('<h', 4800)),
    ]:
        matches = find_value_transitions(snap_before, snap_after, old_bytes, new_bytes)
        if matches:
            print(f"  FOUND {label} at {len(matches)} locations:")
            for m in matches[:10]:
                ctx_after = read_mem(handle, m - 8, 32)
                if ctx_after:
                    hex_str = ' '.join(f'{b:02x}' for b in ctx_after)
                    print(f"    {m:#x}: {hex_str}")

    task.Duration = 2400
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
