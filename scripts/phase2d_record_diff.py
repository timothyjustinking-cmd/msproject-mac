#!/usr/bin/env python3
"""Differential scan of the task record to map field offsets.

We know record_base candidates at 0x23b75edc834 and 0x23b75edd644.
Set field values via COM, read the record, find what changed.

Strategy: read "before" snapshot, change a field, read "after", diff.
"""

import ctypes
import struct
import sys
import time

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


def diff_records(before, after, record_base, label=""):
    """Compare two record snapshots and show all byte differences."""
    changes = []
    for i in range(min(len(before), len(after))):
        if before[i] != after[i]:
            changes.append(i)

    if not changes:
        print(f"  {label}: NO changes")
        return []

    # Group consecutive changes
    groups = []
    start = changes[0]
    end = changes[0]
    for c in changes[1:]:
        if c == end + 1:
            end = c
        else:
            groups.append((start, end + 1))
            start = c
            end = c
    groups.append((start, end + 1))

    print(f"  {label}: {len(changes)} bytes changed in {len(groups)} groups")
    results = []
    for gstart, gend in groups:
        size = gend - gstart
        old_bytes = before[gstart:gend]
        new_bytes = after[gstart:gend]
        old_hex = ' '.join(f'{b:02x}' for b in old_bytes)
        new_hex = ' '.join(f'{b:02x}' for b in new_bytes)

        # Try interpreting as different types
        interp = ""
        if size >= 2:
            old_i16 = struct.unpack_from('<h', before, gstart)[0]
            new_i16 = struct.unpack_from('<h', after, gstart)[0]
            interp += f" i16: {old_i16} → {new_i16}"
        if size >= 4:
            old_i32 = struct.unpack_from('<i', before, gstart)[0]
            new_i32 = struct.unpack_from('<i', after, gstart)[0]
            interp += f" i32: {old_i32} → {new_i32}"
        if size >= 8:
            old_dbl = struct.unpack_from('<d', before, gstart)[0]
            new_dbl = struct.unpack_from('<d', after, gstart)[0]
            interp += f" dbl: {old_dbl:.2f} → {new_dbl:.2f}"

        print(f"    +{gstart:#06x}: [{old_hex}] → [{new_hex}]{interp}")
        results.append((gstart, size, old_bytes, new_bytes))

    return results


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

    # Use BOTH candidates — we'll see which one responds to changes
    record_bases = [0x23b75edc834, 0x23b75edd644]
    record_size = 0xC00  # Read 3KB to cover up to +0x910 and beyond

    # Fields to probe with distinctive values
    # Format: (name, field_id, set_val, reset_val)
    probes = [
        ("Priority",         188743705, "777",     "500"),
        ("PercentComplete",  188743712, "42%",     "0%"),
        ("Marked",           188743751, "Yes",     "No"),
        ("FixedCost",        188743688, "$12345",  "$0"),
        ("Milestone",        188743704, "Yes",     "No"),
        ("HideBar",          188743789, "Yes",     "No"),
        ("ConstraintType",   188743697, "Must Start On", "As Soon As Possible"),
    ]

    for probe_name, fid, set_val, reset_val in probes:
        print(f"\n{'='*80}")
        print(f"PROBING: {probe_name} = '{set_val}'")
        print(f"{'='*80}")

        # Read "before" snapshots
        befores = []
        for rb in record_bases:
            data = read_mem(handle, rb, record_size)
            befores.append(data)

        # Set the field
        try:
            task.SetField(fid, set_val)
        except Exception as e:
            print(f"  SetField FAILED: {e}")
            continue
        time.sleep(0.5)

        # Read "after" snapshots
        afters = []
        for rb in record_bases:
            data = read_mem(handle, rb, record_size)
            afters.append(data)

        # Diff each candidate
        for idx, (rb, bef, aft) in enumerate(zip(record_bases, befores, afters)):
            if bef and aft:
                diff_records(bef, aft, rb, f"Record {idx} ({rb:#x})")

        # Reset the field
        try:
            task.SetField(fid, reset_val)
        except:
            pass
        time.sleep(0.3)

    # Also do a DUAL probe: set ConstraintType to Must Start On, which requires a date
    print(f"\n{'='*80}")
    print(f"PROBING: ConstraintType = 'Must Start On' (with date)")
    print(f"{'='*80}")

    befores = [read_mem(handle, rb, record_size) for rb in record_bases]
    try:
        task.SetField(188743697, "Must Start On")
        time.sleep(0.3)
        task.SetField(188743698, "Tue 20/10/26")  # ConstraintDate
    except Exception as e:
        print(f"  SetField FAILED: {e}")
    time.sleep(0.5)
    afters = [read_mem(handle, rb, record_size) for rb in record_bases]
    for idx, (rb, bef, aft) in enumerate(zip(record_bases, befores, afters)):
        if bef and aft:
            diff_records(bef, aft, rb, f"Record {idx}")
    # Reset
    try:
        task.SetField(188743697, "As Soon As Possible")
    except:
        pass
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
