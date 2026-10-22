#!/usr/bin/env python3
"""Find OutlineLevel byte position by comparing all tasks.

Read u16/u32 values at many offsets from each task's Priority address
and compare with COM OutlineLevel to find the match.
"""

import ctypes
import ctypes.wintypes
import struct
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

try:
    import win32com.client
except ImportError:
    print("ERROR: pywin32 required"); sys.exit(1)

k32 = ctypes.WinDLL('kernel32', use_last_error=True)

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


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    handle = k32.OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    # Known Priority addresses
    prio_addrs = {
        'Task1': 0x23b7ef6dd3c,
        'Task2': 0x23b7ef6b4bc,
        'Task3': 0x23b7ef6c47c,
        'PST':   0x23b7ef6dd3c + 576,  # stride +1 from Task 1
    }

    # Get COM OutlineLevel for each
    com_ol = {}
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t:
            com_ol[f'Task{t.ID}'] = t.OutlineLevel
            print(f"Task {t.ID}: '{t.Name}' OL={t.OutlineLevel}, Dur={t.Duration}min, ID={t.ID}")
    try:
        pst = proj.ProjectSummaryTask
        if pst:
            com_ol['PST'] = pst.OutlineLevel
            print(f"PST: '{pst.Name}' OL={pst.OutlineLevel}, ID={pst.ID}")
    except:
        pass

    # Also get COM ID for cross-reference
    com_id = {}
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t:
            com_id[f'Task{t.ID}'] = t.ID
    com_id['PST'] = 0

    # Read 800 bytes around each task's Priority
    scan_range = 400  # bytes before and after Priority
    print(f"\nReading {2*scan_range} bytes around each task's Priority...\n")

    task_data = {}
    for name, addr in prio_addrs.items():
        data = read_mem(handle, addr - scan_range, 2 * scan_range)
        if data:
            task_data[name] = data
        else:
            print(f"  WARNING: Can't read {name} at 0x{addr:012x}")

    # For each u16 offset in [-400, +400], check if the value matches COM OL for ALL tasks
    print("Scanning for OutlineLevel (u16 that matches COM OL across ALL tasks):")
    print(f"  COM OL values: {com_ol}\n")

    matches = []
    for off in range(-scan_range, scan_range, 2):
        buf_off = off + scan_range
        all_match = True
        values = {}
        for name in prio_addrs:
            if name not in task_data or name not in com_ol:
                all_match = False
                continue
            data = task_data[name]
            if buf_off + 2 > len(data):
                all_match = False
                continue
            val = struct.unpack_from('<H', data, buf_off)[0]
            values[name] = val
            if val != com_ol[name]:
                all_match = False

        if all_match and len(values) >= 3:  # At least 3 tasks match
            matches.append((off, values))

    if matches:
        print(f"Found {len(matches)} offsets where u16 matches OL for all tasks:")
        for off, vals in matches:
            print(f"  prio{off:+5d}: {vals}")
    else:
        print("No u16 offset matches OL for all tasks!")
        # Try u32 instead
        print("\nTrying u32...")
        for off in range(-scan_range, scan_range, 4):
            buf_off = off + scan_range
            all_match = True
            values = {}
            for name in prio_addrs:
                if name not in task_data or name not in com_ol:
                    all_match = False
                    continue
                data = task_data[name]
                if buf_off + 4 > len(data):
                    all_match = False
                    continue
                val = struct.unpack_from('<I', data, buf_off)[0]
                values[name] = val
                if val != com_ol[name]:
                    all_match = False

            if all_match and len(values) >= 3:
                matches.append((off, values))

        if matches:
            print(f"Found {len(matches)} u32 offsets:")
            for off, vals in matches:
                print(f"  prio{off:+5d}: {vals}")
        else:
            print("No u32 offset matches either!")

    # Also search for ID field (should be different for each task)
    print(f"\nSearching for task ID field (u16/u32):")
    print(f"  COM IDs: {com_id}")
    for off in range(-scan_range, scan_range, 2):
        buf_off = off + scan_range
        all_match = True
        values = {}
        for name in prio_addrs:
            if name not in task_data or name not in com_id:
                all_match = False
                continue
            data = task_data[name]
            if buf_off + 4 > len(data):
                all_match = False
                continue
            val = struct.unpack_from('<I', data, buf_off)[0]
            values[name] = val
            if val != com_id[name]:
                all_match = False

        if all_match and len(values) >= 3:
            print(f"  prio{off:+5d} (u32): {values}")

    for off in range(-scan_range, scan_range, 2):
        buf_off = off + scan_range
        all_match = True
        values = {}
        for name in prio_addrs:
            if name not in task_data or name not in com_id:
                all_match = False
                continue
            data = task_data[name]
            if buf_off + 2 > len(data):
                all_match = False
                continue
            val = struct.unpack_from('<H', data, buf_off)[0]
            values[name] = val
            if val != com_id[name]:
                all_match = False

        if all_match and len(values) >= 3:
            print(f"  prio{off:+5d} (u16): {values}")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
