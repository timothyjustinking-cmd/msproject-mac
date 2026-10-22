#!/usr/bin/env python3
"""Probe the cost double region in the flat buffer.

class_slot 0-6: Work, ActualWork, RemainingWork, Cost, ActualCost, FixedCost, RemainingCost
All type=5 (i64), stored as double*100.

Strategy: Assign a resource to the task, set costs, and track which doubles change.
Also probe the duration i32 region more precisely.
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


def diff_and_decode(before, after, buf_start, prio_addr, label):
    """Diff two buffers and decode changes by type."""
    changes = []
    for i in range(min(len(before), len(after))):
        if before[i] != after[i]:
            changes.append(i)

    if not changes:
        print(f"  {label}: no changes")
        return

    # Group contiguous byte changes
    groups = []
    current = [changes[0]]
    for c in changes[1:]:
        if c == current[-1] + 1:
            current.append(c)
        else:
            groups.append(current)
            current = [c]
    groups.append(current)

    print(f"  {label}: {len(changes)} bytes changed in {len(groups)} groups")
    for g in groups:
        off = g[0]
        n = len(g)
        addr = buf_start + off
        rel = addr - prio_addr
        old_bytes = before[off:off + n]
        new_bytes = after[off:off + n]

        # Skip version counter
        if rel == -264 and n == 1:
            continue
        # Skip dirty flag
        if rel in (-267, -266) and n <= 2:
            continue

        if n == 8:
            old_dbl = struct.unpack('<d', old_bytes)[0]
            new_dbl = struct.unpack('<d', new_bytes)[0]
            print(f"    prio{rel:+d}: dbl {old_dbl:.2f} -> {new_dbl:.2f} "
                  f"(${old_dbl / 100:.2f} -> ${new_dbl / 100:.2f})")
        elif n == 4:
            old_i32 = struct.unpack('<I', old_bytes)[0]
            new_i32 = struct.unpack('<I', new_bytes)[0]
            if 0x3d000000 <= new_i32 <= 0x3e000000:
                print(f"    prio{rel:+d}: DATE {old_i32:#x} -> {new_i32:#x}")
            else:
                old_s = struct.unpack('<i', old_bytes)[0]
                new_s = struct.unpack('<i', new_bytes)[0]
                print(f"    prio{rel:+d}: i32 {old_s} -> {new_s}")
        elif n == 2:
            old_i16 = struct.unpack('<h', old_bytes)[0]
            new_i16 = struct.unpack('<h', new_bytes)[0]
            print(f"    prio{rel:+d}: i16 {old_i16} -> {new_i16}")
        elif n == 1:
            print(f"    prio{rel:+d}: {old_bytes[0]:#04x} -> {new_bytes[0]:#04x} "
                  f"(bits: {old_bytes[0] ^ new_bytes[0]:#010b})")
        else:
            print(f"    prio{rel:+d}: ({n}B) {old_bytes.hex()} -> {new_bytes.hex()}")


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

    print(f"PID: {pid}")
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    prio_addr = 0x23b7ef6dd3c
    buf_start = prio_addr - 0x300
    buf_size = 0x800

    # Reset
    task.Duration = 2400
    task.Priority = 500
    task.PercentComplete = 0
    task.FixedCost = 0.0
    task.Marked = False
    task.Milestone = False
    task.HideBar = False
    try:
        task.ConstraintType = 0
    except:
        pass
    time.sleep(0.5)

    # == STEP 1: Map the DOUBLE region by setting FixedCost to unique values ==
    print("\n=== STEP 1: FixedCost double mapping ===")
    print("Setting FixedCost to $77777 to find ALL cost doubles that update")

    before = read_mem(handle, buf_start, buf_size)
    task.FixedCost = 77777.0
    time.sleep(0.5)
    after = read_mem(handle, buf_start, buf_size)
    diff_and_decode(before, after, buf_start, prio_addr, "FixedCost = $77777")

    # Read all doubles in the cost region (prio-104 to prio-4)
    print("\n  Current double values in cost region:")
    data = read_mem(handle, prio_addr - 112, 120)
    if data:
        for off in range(0, 112, 8):
            dbl = struct.unpack_from('<d', data, off)[0]
            rel = off - 112
            if dbl != 0.0:
                print(f"    prio{rel:+d}: {dbl:.2f} (${dbl / 100:.2f})")

    # == STEP 2: Read COM values to cross-reference ==
    print(f"\n  COM Cost = ${task.Cost}")
    print(f"  COM ActualCost = ${task.ActualCost}")
    print(f"  COM FixedCost = ${task.FixedCost}")
    print(f"  COM RemainingCost = ${task.RemainingCost}")
    try:
        print(f"  COM Work = {task.Work}")
        print(f"  COM ActualWork = {task.ActualWork}")
        print(f"  COM RemainingWork = {task.RemainingWork}")
    except Exception as e:
        print(f"  COM Work fields: {e}")

    # == STEP 3: Set PercentComplete to create ActualCost ==
    print("\n=== STEP 3: PercentComplete 0 -> 50 with FixedCost=$77777 ===")
    before = read_mem(handle, buf_start, buf_size)
    task.PercentComplete = 50
    time.sleep(0.5)
    after = read_mem(handle, buf_start, buf_size)
    diff_and_decode(before, after, buf_start, prio_addr, "PercentComplete=50")

    print(f"\n  COM values after 50%:")
    print(f"  Cost = ${task.Cost}")
    print(f"  ActualCost = ${task.ActualCost}")
    print(f"  FixedCost = ${task.FixedCost}")
    print(f"  RemainingCost = ${task.RemainingCost}")

    # Dump cost region again
    print("\n  Double values in cost region:")
    data = read_mem(handle, prio_addr - 112, 120)
    if data:
        for off in range(0, 112, 8):
            dbl = struct.unpack_from('<d', data, off)[0]
            rel = off - 112
            if dbl != 0.0:
                print(f"    prio{rel:+d}: {dbl:.2f} (${dbl / 100:.2f})")

    # == STEP 4: Reset FixedCost and check which doubles clear ==
    print("\n=== STEP 4: FixedCost $77777 -> $0 ===")
    before = read_mem(handle, buf_start, buf_size)
    task.FixedCost = 0.0
    time.sleep(0.5)
    after = read_mem(handle, buf_start, buf_size)
    diff_and_decode(before, after, buf_start, prio_addr, "FixedCost=0")

    print(f"\n  COM values after FixedCost=0:")
    print(f"  Cost = ${task.Cost}")
    print(f"  ActualCost = ${task.ActualCost}")
    print(f"  RemainingCost = ${task.RemainingCost}")

    # == STEP 5: Map durations more precisely ==
    print("\n=== STEP 5: Duration region mapping ===")
    task.PercentComplete = 0
    task.FixedCost = 0.0
    time.sleep(0.3)

    # Dump the duration region (prio-200 to prio-168)
    print("  Duration region (prio-200 to prio-168):")
    data = read_mem(handle, prio_addr - 200, 40)
    if data:
        for off in range(0, 40, 4):
            val = struct.unpack_from('<I', data, off)[0]
            val_s = struct.unpack_from('<i', data, off)[0]
            rel = off - 200
            if val != 0:
                print(f"    prio{rel:+d}: {val_s:8d} (unsigned: {val}, ticks: {val_s}/10={val_s/10:.0f} min)")

    # Change Duration and track ALL changes in this region
    print("\n  Duration 2400 -> 4800 (5d -> 10d):")
    before = read_mem(handle, buf_start, buf_size)
    task.Duration = 4800
    time.sleep(0.5)
    after = read_mem(handle, buf_start, buf_size)
    diff_and_decode(before, after, buf_start, prio_addr, "Duration 2400->4800")

    # Dump updated region
    print("\n  Duration region after change:")
    data = read_mem(handle, prio_addr - 200, 40)
    if data:
        for off in range(0, 40, 4):
            val_s = struct.unpack_from('<i', data, off)[0]
            rel = off - 200
            if val_s != 0:
                print(f"    prio{rel:+d}: {val_s:8d} (ticks/10={val_s/10:.0f} min)")

    # == STEP 6: Map the i16 fields more completely ==
    print("\n=== STEP 6: Full i16 region dump (prio-8 to prio+16) ===")
    data = read_mem(handle, prio_addr - 8, 24)
    if data:
        for off in range(0, 24, 2):
            val = struct.unpack_from('<h', data, off)[0]
            rel = off - 8
            print(f"    prio{rel:+d}: i16={val} (unsigned: {val & 0xFFFF:#06x})")

    # == STEP 7: Map boolean flags region ==
    print("\n=== STEP 7: Flag bytes region (prio-267 to prio-205) ===")
    data = read_mem(handle, prio_addr - 270, 70)
    if data:
        for off in range(0, 70):
            val = data[off]
            rel = off - 270
            if val != 0:
                print(f"    prio{rel:+d}: {val:#04x} ({val:08b})")

    # Reset
    task.Duration = 2400
    task.PercentComplete = 0
    task.FixedCost = 0.0
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
