#!/usr/bin/env python3
"""Differential scan of the task record to map field offsets.

Dynamically finds record bases by searching for raw Duration value,
then sets field values via COM and diffs the record to find offsets.
"""

import ctypes
import struct
import sys
import time
import os

os.environ['PYTHONIOENCODING'] = 'utf-8'

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

def scan_all_regions(handle):
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

def find_all_pattern(handle, regions, pattern):
    hits = []
    for base, size in regions:
        data = read_mem(handle, base, size)
        if not data: continue
        offset = 0
        while True:
            pos = data.find(pattern, offset)
            if pos == -1: break
            hits.append(base + pos)
            offset = pos + 1
    return hits

def find_record_bases(handle, regions, IMAGE_BASE):
    """Find task record bases by searching for raw Duration=2400 at +0x740
    and validating with a date32 at +0x7c4."""
    raw_dur_pat = struct.pack('<i', 2400)
    hits = find_all_pattern(handle, regions, raw_dur_pat)
    print(f"  Found {len(hits)} locations with i32=2400")

    valid = []
    for hit_addr in hits:
        record_base = hit_addr - 0x740
        test_data = read_mem(handle, record_base + 0x7c0, 8)
        if not test_data or len(test_data) < 8:
            continue
        created_raw = struct.unpack_from('<I', test_data, 4)[0]  # +0x7c4
        if 0x3d100000 <= created_raw <= 0x3d200000:
            valid.append(record_base)

    return valid


def diff_records(before, after, record_base, label=""):
    """Compare two record snapshots and show all byte differences."""
    changes = []
    for i in range(min(len(before), len(after))):
        if before[i] != after[i]:
            changes.append(i)

    if not changes:
        print(f"  {label}: NO changes")
        return []

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

        interp = ""
        if size >= 1 and size < 2:
            interp += f" u8: {old_bytes[0]} -> {new_bytes[0]}"
        if size >= 2:
            old_i16 = struct.unpack_from('<h', before, gstart)[0]
            new_i16 = struct.unpack_from('<h', after, gstart)[0]
            interp += f" i16: {old_i16} -> {new_i16}"
        if size >= 4:
            old_i32 = struct.unpack_from('<i', before, gstart)[0]
            new_i32 = struct.unpack_from('<i', after, gstart)[0]
            old_u32 = struct.unpack_from('<I', before, gstart)[0]
            new_u32 = struct.unpack_from('<I', after, gstart)[0]
            interp += f" i32: {old_i32} -> {new_i32} (u32: {old_u32:#x} -> {new_u32:#x})"
        if size >= 8:
            old_dbl = struct.unpack_from('<d', before, gstart)[0]
            new_dbl = struct.unpack_from('<d', after, gstart)[0]
            interp += f" dbl: {old_dbl:.2f} -> {new_dbl:.2f}"

        print(f"    +{gstart:#06x}: [{old_hex}] -> [{new_hex}]{interp}")
        results.append((gstart, size, old_bytes, new_bytes))

    return results


def main():
    # Force UTF-8 output
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')

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

    # Step 1: Find record bases dynamically
    print(f"\n=== FINDING RECORD BASES ===")
    regions = scan_all_regions(handle)
    print(f"  {len(regions)} regions, {sum(s for _,s in regions)/1024/1024:.0f} MB")

    record_bases = find_record_bases(handle, regions, IMAGE_BASE)
    print(f"  Found {len(record_bases)} record candidates")
    for idx, rb in enumerate(record_bases):
        data = read_mem(handle, rb + 0x740, 4)
        dur = struct.unpack_from('<i', data, 0)[0] if data else -1
        data2 = read_mem(handle, rb + 0x7c4, 4)
        created = struct.unpack_from('<I', data2, 0)[0] if data2 else 0
        print(f"    [{idx}] {rb:#x}: dur_raw={dur}, created={created:#x}")

    if not record_bases:
        print("ERROR: No record bases found!"); sys.exit(1)

    record_size = 0xC00

    # Step 2: Probe fields with distinctive values
    probes = [
        ("Priority",         188743705, "777",     "500"),
        ("PercentComplete",  188743712, "42%",     "0%"),
        ("Marked",           188743751, "Yes",     "No"),
        ("FixedCost",        188743688, "$12345",  "$0"),
        ("Milestone",        188743704, "Yes",     "No"),
        ("HideBar",          188743789, "Yes",     "No"),
        ("RollupBar",        188743783, "Yes",     "No"),
        ("LevelAssignments", 188743763, "Yes",     "No"),
        ("LevelingCanSplit", 188743764, "Yes",     "No"),
    ]

    all_results = {}

    for probe_name, fid, set_val, reset_val in probes:
        print(f"\n{'='*80}")
        print(f"PROBING: {probe_name} = '{set_val}'")
        print(f"{'='*80}")

        # Read "before" snapshots
        befores = {}
        for rb in record_bases:
            befores[rb] = read_mem(handle, rb, record_size)

        # Set the field
        try:
            task.SetField(fid, set_val)
        except Exception as e:
            print(f"  SetField FAILED: {e}")
            continue
        time.sleep(0.3)

        # Read "after" snapshots
        afters = {}
        for rb in record_bases:
            afters[rb] = read_mem(handle, rb, record_size)

        # Diff each candidate
        for idx, rb in enumerate(record_bases):
            bef = befores[rb]
            aft = afters[rb]
            if bef and aft:
                changes = diff_records(bef, aft, rb, f"Record {idx} ({rb:#x})")
                if changes:
                    all_results.setdefault(probe_name, []).extend(
                        [(rb, gstart, size) for gstart, size, _, _ in changes]
                    )

        # Reset the field
        try:
            task.SetField(fid, reset_val)
        except:
            pass
        time.sleep(0.2)

    # Step 3: Try ConstraintType with date
    print(f"\n{'='*80}")
    print(f"PROBING: ConstraintType = 'Must Start On' + ConstraintDate")
    print(f"{'='*80}")

    befores = {rb: read_mem(handle, rb, record_size) for rb in record_bases}
    try:
        task.SetField(188743697, "Must Start On")
        time.sleep(0.2)
        task.SetField(188743698, "10/20/26")
    except Exception as e:
        print(f"  SetField FAILED: {e}")
    time.sleep(0.3)
    afters = {rb: read_mem(handle, rb, record_size) for rb in record_bases}
    for idx, rb in enumerate(record_bases):
        if befores[rb] and afters[rb]:
            diff_records(befores[rb], afters[rb], rb, f"Record {idx}")
    try:
        task.SetField(188743697, "As Soon As Possible")
    except:
        pass
    time.sleep(0.2)

    # Step 4: Duration change - set to 10 days (raw should be 4800)
    print(f"\n{'='*80}")
    print(f"PROBING: Duration = '10 days' (raw should change 2400->4800)")
    print(f"{'='*80}")

    befores = {rb: read_mem(handle, rb, record_size) for rb in record_bases}
    try:
        task.SetField(188743709, "10 days")
    except Exception as e:
        print(f"  SetField FAILED: {e}")
    time.sleep(0.3)
    afters = {rb: read_mem(handle, rb, record_size) for rb in record_bases}
    for idx, rb in enumerate(record_bases):
        if befores[rb] and afters[rb]:
            diff_records(befores[rb], afters[rb], rb, f"Record {idx}")
    # Reset
    try:
        task.SetField(188743709, "5 days")
    except:
        pass
    time.sleep(0.2)

    # Step 5: Also scan a wider area around each record for changes
    # Maybe the record addresses shifted and changes are OUTSIDE our 0xC00 window
    print(f"\n{'='*80}")
    print(f"WIDE SCAN: Priority=999 with 16KB window")
    print(f"{'='*80}")

    # Use just the first record base with a much larger read
    rb = record_bases[0]
    wide_size = 0x4000  # 16KB
    wide_before = read_mem(handle, rb - 0x1000, wide_size)  # start 4KB before
    try:
        task.SetField(188743705, "999")
    except Exception as e:
        print(f"  SetField FAILED: {e}")
    time.sleep(0.3)
    wide_after = read_mem(handle, rb - 0x1000, wide_size)
    if wide_before and wide_after:
        diff_records(wide_before, wide_after, rb - 0x1000, f"Wide scan around {rb:#x}")
    try:
        task.SetField(188743705, "500")
    except:
        pass
    time.sleep(0.2)

    # Summary
    print(f"\n{'='*80}")
    print(f"SUMMARY OF FIELD OFFSETS")
    print(f"{'='*80}")
    for name, changes in all_results.items():
        print(f"  {name}: {changes}")

    # Also dump current record state at key offsets
    print(f"\n=== CURRENT RECORD STATE (first candidate) ===")
    rb = record_bases[0]
    data = read_mem(handle, rb, record_size)
    if data:
        for off in range(0x700, min(0x800, len(data)), 4):
            val_i32 = struct.unpack_from('<i', data, off)[0]
            val_u32 = struct.unpack_from('<I', data, off)[0]
            val_i16 = struct.unpack_from('<h', data, off)[0]
            if val_i32 != 0:
                print(f"    +{off:#06x}: i32={val_i32:10d}  u32={val_u32:#010x}  i16={val_i16}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
