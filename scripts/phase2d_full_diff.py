#!/usr/bin/env python3
"""Full-memory differential scan to find where Priority, PercentComplete,
and Duration are stored.

Uses the Task object properties directly instead of SetField to avoid
format issues.
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


def full_memory_diff(handle, regions, label=""):
    """Take a full memory snapshot (just hash each region for comparison)."""
    snapshot = {}
    for base, size in regions:
        data = read_mem(handle, base, size)
        if data:
            snapshot[(base, size)] = data
    return snapshot


def compare_snapshots(before, after, label=""):
    """Compare two full memory snapshots and find all differences."""
    changes = []
    for key in before:
        if key not in after:
            continue
        bdata = before[key]
        adata = after[key]
        base, size = key
        minlen = min(len(bdata), len(adata))
        i = 0
        while i < minlen:
            if bdata[i] != adata[i]:
                # Found a difference - find the extent
                start = i
                while i < minlen and bdata[i] != adata[i]:
                    i += 1
                changes.append((base + start, start, i - start, bdata[start:i], adata[start:i]))
            else:
                i += 1
    return changes


def search_value_in_changes(changes, old_val_bytes, new_val_bytes, val_label):
    """Find changes that match a specific old->new value transition."""
    matches = []
    for addr, offset, size, old_data, new_data in changes:
        # Check if old_val_bytes appears in old_data and new_val_bytes at same position in new_data
        for i in range(max(0, size - len(old_val_bytes) + 1)):
            if (old_data[i:i+len(old_val_bytes)] == old_val_bytes and
                new_data[i:i+len(new_val_bytes)] == new_val_bytes):
                matches.append(addr + i)
    return matches


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
    print(f"  Current Priority: {task.Priority}")
    print(f"  Current PercentComplete: {task.PercentComplete}")
    print(f"  Current Duration: {task.Duration}")
    print(f"  Current Marked: {task.Marked}")
    print(f"  Current Milestone: {task.Milestone}")

    regions = scan_readable_regions(handle)
    total_mb = sum(s for _, s in regions) / 1024 / 1024
    print(f"\n{len(regions)} readable regions, {total_mb:.0f} MB total")

    # ==========================================
    # TEST 1: Priority 500 -> 777
    # ==========================================
    print(f"\n{'='*80}")
    print(f"TEST 1: Priority 500 -> 777")
    print(f"{'='*80}")

    print("  Taking 'before' snapshot...")
    snap_before = full_memory_diff(handle, regions)
    print(f"  Snapshot: {len(snap_before)} regions captured")

    print("  Setting Priority = 777...")
    task.Priority = 777
    time.sleep(0.5)
    print(f"  Verify: Priority = {task.Priority}")

    print("  Taking 'after' snapshot...")
    snap_after = full_memory_diff(handle, regions)

    print("  Comparing...")
    changes = compare_snapshots(snap_before, snap_after)
    print(f"  Total changes: {len(changes)} groups")

    # Look for the value 777 in various encodings
    searches = [
        ("i16=777", struct.pack('<h', 500), struct.pack('<h', 777)),
        ("i32=777", struct.pack('<i', 500), struct.pack('<i', 777)),
        ("dbl=777", struct.pack('<d', 500.0), struct.pack('<d', 777.0)),
        ("i16=7770 (x10)", struct.pack('<h', 5000), struct.pack('<h', 7770)),
        ("i32=7770 (x10)", struct.pack('<i', 5000), struct.pack('<i', 7770)),
    ]
    for label, old_bytes, new_bytes in searches:
        matches = search_value_in_changes(changes, old_bytes, new_bytes, label)
        if matches:
            print(f"  FOUND {label} at {len(matches)} locations:")
            for m in matches[:10]:
                print(f"    {m:#x}")

    # Show all change groups (filter small ones)
    print(f"\n  All change groups (size >= 2 bytes):")
    for addr, offset, size, old_data, new_data in changes:
        if size < 2:
            continue
        old_hex = ' '.join(f'{b:02x}' for b in old_data[:32])
        new_hex = ' '.join(f'{b:02x}' for b in new_data[:32])
        extra = f" (+{len(old_data)-32} more)" if size > 32 else ""
        print(f"    {addr:#014x} ({size:4d} bytes): [{old_hex}{extra}] -> [{new_hex}{extra}]")
        # Try interpreting
        if size >= 2:
            ov = struct.unpack_from('<h', old_data, 0)[0]
            nv = struct.unpack_from('<h', new_data, 0)[0]
            if ov != nv:
                print(f"      i16: {ov} -> {nv}")
        if size >= 4:
            ov = struct.unpack_from('<i', old_data, 0)[0]
            nv = struct.unpack_from('<i', new_data, 0)[0]
            if ov != nv:
                print(f"      i32: {ov} -> {nv}")
        if size >= 8:
            try:
                ov = struct.unpack_from('<d', old_data, 0)[0]
                nv = struct.unpack_from('<d', new_data, 0)[0]
                if abs(ov) < 1e15 and abs(nv) < 1e15:
                    print(f"      dbl: {ov:.4f} -> {nv:.4f}")
            except:
                pass

    # Also show 1-byte changes
    print(f"\n  Single-byte changes:")
    for addr, offset, size, old_data, new_data in changes:
        if size == 1:
            print(f"    {addr:#014x}: {old_data[0]:#04x} -> {new_data[0]:#04x}")

    # Reset
    print("\n  Resetting Priority = 500...")
    task.Priority = 500
    time.sleep(0.3)

    # ==========================================
    # TEST 2: PercentComplete 0 -> 42
    # ==========================================
    print(f"\n{'='*80}")
    print(f"TEST 2: PercentComplete 0 -> 42")
    print(f"{'='*80}")

    snap_before = full_memory_diff(handle, regions)
    task.PercentComplete = 42
    time.sleep(0.5)
    print(f"  Verify: PercentComplete = {task.PercentComplete}")
    snap_after = full_memory_diff(handle, regions)

    changes = compare_snapshots(snap_before, snap_after)
    print(f"  Total changes: {len(changes)} groups")

    # Look for 42 in various encodings
    searches = [
        ("i16=42", struct.pack('<h', 0), struct.pack('<h', 42)),
        ("i32=42", struct.pack('<i', 0), struct.pack('<i', 42)),
        ("i16=420 (x10)", struct.pack('<h', 0), struct.pack('<h', 420)),
        ("i32=420 (x10)", struct.pack('<i', 0), struct.pack('<i', 420)),
    ]
    for label, old_bytes, new_bytes in searches:
        matches = search_value_in_changes(changes, old_bytes, new_bytes, label)
        if matches:
            print(f"  FOUND {label} at {len(matches)} locations:")
            for m in matches[:10]:
                print(f"    {m:#x}")

    print(f"\n  All change groups (size >= 2 bytes):")
    for addr, offset, size, old_data, new_data in changes:
        if size < 2: continue
        old_hex = ' '.join(f'{b:02x}' for b in old_data[:32])
        new_hex = ' '.join(f'{b:02x}' for b in new_data[:32])
        extra = f" (+{len(old_data)-32} more)" if size > 32 else ""
        print(f"    {addr:#014x} ({size:4d} bytes): [{old_hex}{extra}] -> [{new_hex}{extra}]")
        if size >= 4:
            ov = struct.unpack_from('<i', old_data, 0)[0]
            nv = struct.unpack_from('<i', new_data, 0)[0]
            if ov != nv:
                print(f"      i32: {ov} -> {nv}")
        if size >= 8:
            try:
                ov = struct.unpack_from('<d', old_data, 0)[0]
                nv = struct.unpack_from('<d', new_data, 0)[0]
                if abs(ov) < 1e15 and abs(nv) < 1e15:
                    print(f"      dbl: {ov:.4f} -> {nv:.4f}")
            except:
                pass

    print(f"\n  Single-byte changes:")
    for addr, offset, size, old_data, new_data in changes:
        if size == 1:
            print(f"    {addr:#014x}: {old_data[0]:#04x} -> {new_data[0]:#04x}")

    # Reset
    task.PercentComplete = 0
    time.sleep(0.3)

    # ==========================================
    # TEST 3: Duration change via property
    # ==========================================
    print(f"\n{'='*80}")
    print(f"TEST 3: Duration 24000 -> 48000 (5d -> 10d)")
    print(f"{'='*80}")

    print(f"  Current Duration = {task.Duration}")
    snap_before = full_memory_diff(handle, regions)
    task.Duration = 48000  # 10 days in 1/10-minute units
    time.sleep(0.5)
    print(f"  Verify: Duration = {task.Duration}")
    snap_after = full_memory_diff(handle, regions)

    changes = compare_snapshots(snap_before, snap_after)
    print(f"  Total changes: {len(changes)} groups")

    # Duration stored as raw/10: 2400->4800
    searches = [
        ("i32 2400->4800 (raw/10)", struct.pack('<i', 2400), struct.pack('<i', 4800)),
        ("i32 24000->48000 (raw)", struct.pack('<i', 24000), struct.pack('<i', 48000)),
        ("dbl 2400->4800", struct.pack('<d', 2400.0), struct.pack('<d', 4800.0)),
        ("dbl 24000->48000", struct.pack('<d', 24000.0), struct.pack('<d', 48000.0)),
    ]
    for label, old_bytes, new_bytes in searches:
        matches = search_value_in_changes(changes, old_bytes, new_bytes, label)
        if matches:
            print(f"  FOUND {label} at {len(matches)} locations:")
            for m in matches[:10]:
                # Check if it's in the known record
                for rb_desc, rb_addr in [("Rec0", 0x23b75edc834), ("Rec1", 0x23b75edd644)]:
                    if rb_addr <= m < rb_addr + 0xC00:
                        offset = m - rb_addr
                        print(f"    {m:#x} (= {rb_desc} + {offset:#x})")
                        break
                else:
                    print(f"    {m:#x}")

    # Show >= 4 byte changes
    print(f"\n  All change groups (size >= 4 bytes):")
    for addr, offset, size, old_data, new_data in changes:
        if size < 4: continue
        old_hex = ' '.join(f'{b:02x}' for b in old_data[:32])
        new_hex = ' '.join(f'{b:02x}' for b in new_data[:32])
        extra = f" ..." if size > 32 else ""
        print(f"    {addr:#014x} ({size:4d} bytes): [{old_hex}{extra}] -> [{new_hex}{extra}]")
        if size >= 4:
            ov = struct.unpack_from('<i', old_data, 0)[0]
            nv = struct.unpack_from('<i', new_data, 0)[0]
            if ov != nv:
                print(f"      i32: {ov} -> {nv}")

    # Reset
    task.Duration = 24000
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
