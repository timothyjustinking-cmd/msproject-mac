#!/usr/bin/env python3
"""Verify slack field positions from formula against COM values.

Formula says: prio-100=StartSlack, prio-96=FinishSlack, prio-92=FreeSlack
Previous session: prio-100=TotalSlack, prio-96=FreeSlack, prio-92=StartSlack
Formula also says: TotalSlack is dispatch_type=10 (computed), NOT in flat buffer.
"""

import ctypes
import ctypes.wintypes
import struct
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

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

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    prio_addrs = {
        'T1': 0x23b7ef6dd3c,
        'T2': 0x23b7ef6b4bc,
        'T3': 0x23b7ef6c47c,
        'PST': 0x23b7ef6df7c,
    }

    tasks = {}
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t:
            tasks[f'T{t.ID}'] = t
    try:
        tasks['PST'] = proj.ProjectSummaryTask
    except:
        pass

    print("=" * 90)
    print("SLACK FIELD VERIFICATION")
    print("=" * 90)

    slack_props = ['TotalSlack', 'FreeSlack', 'StartSlack', 'FinishSlack']

    for tname in ['T1', 'T2', 'T3', 'PST']:
        if tname not in tasks or tname not in prio_addrs:
            continue
        t = tasks[tname]
        addr = prio_addrs[tname]

        print(f"\n--- {tname}: '{t.Name}' ---")

        # COM values (all in minutes)
        com_vals = {}
        for prop in slack_props:
            try:
                v = getattr(t, prop)
                com_vals[prop] = int(v)
                print(f"  COM {prop:15s} = {v} min (×10 = {int(v*10)})")
            except Exception as e:
                print(f"  COM {prop:15s} = ERROR: {e}")

        # Memory values at the three slack positions
        mem_offsets = [(-100, 'prio-100'), (-96, 'prio-96'), (-92, 'prio-92')]
        print()
        for off, label in mem_offsets:
            data = read_mem(handle, addr + off, 4)
            if not data:
                continue
            mem_val = struct.unpack_from('<I', data, 0)[0]
            # Try to match against COM values (×10)
            matches = []
            for prop, cv in com_vals.items():
                if mem_val == int(cv * 10):
                    matches.append(prop)
            match_str = f" <-- {', '.join(matches)}" if matches else ""
            print(f"  mem[{label}] = {mem_val:10d} (÷10 = {mem_val/10:.0f} min){match_str}")

    # Summary
    print("\n" + "=" * 90)
    print("COMPARISON")
    print("=" * 90)
    print("\nFormula assignment (from field_id in cache):")
    print("  prio-100 = StartSlack  (field 0x0b4001b6, slot 76)")
    print("  prio-96  = FinishSlack (field 0x0b4001b7, slot 77)")
    print("  prio-92  = FreeSlack   (field 0x0b400015, slot 11)")
    print("  TotalSlack = dispatch_type=10 (computed, NOT in flat buffer)")
    print("\nPrevious session assignment:")
    print("  prio-100 = TotalSlack")
    print("  prio-96  = FreeSlack")
    print("  prio-92  = StartSlack")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
