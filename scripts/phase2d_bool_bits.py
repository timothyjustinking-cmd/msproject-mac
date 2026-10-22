#!/usr/bin/env python3
"""Find boolean field bit positions by cross-task comparison.

Boolean fields are packed as individual bits in flag bytes.
For each boolean property, find which bit matches across all tasks.
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

    prio_addrs = {
        'T1': 0x23b7ef6dd3c,
        'T2': 0x23b7ef6b4bc,
        'T3': 0x23b7ef6c47c,
        'PST': 0x23b7ef6df7c,
    }

    # Read 576-byte records
    READ_BEFORE = 336
    READ_AFTER = 240
    TOTAL = READ_BEFORE + READ_AFTER

    task_mem = {}
    for name, addr in prio_addrs.items():
        data = read_mem(handle, addr - READ_BEFORE, TOTAL)
        if data and len(data) == TOTAL:
            task_mem[name] = data

    # Get COM tasks
    com_tasks = {}
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t:
            com_tasks[f'T{t.ID}'] = t
    try:
        pst = proj.ProjectSummaryTask
        if pst:
            com_tasks['PST'] = pst
    except:
        pass

    # Boolean properties with their COM values
    bool_props = [
        ('Critical',        [('T1', 0), ('T2', 1), ('T3', 0), ('PST', 1)]),
        ('Milestone',       [('T1', 0), ('T2', 0), ('T3', 1), ('PST', 0)]),
        ('Marked',          [('T1', 0), ('T2', 0), ('T3', 0), ('PST', 0)]),
        ('Rollup',          [('T1', 0), ('T2', 1), ('T3', 0), ('PST', 0)]),
        ('HideBar',         [('T1', 0), ('T2', 0), ('T3', 0), ('PST', 0)]),
        ('Summary',         [('T1', 0), ('T2', 1), ('T3', 0), ('PST', 1)]),
        ('Active',          [('T1', 1), ('T2', 1), ('T3', 1), ('PST', 1)]),
        ('Manual',          [('T1', 0), ('T2', 1), ('T3', 1), ('PST', 0)]),
    ]

    # Read actual values from COM to verify
    print("=== Boolean property COM values ===\n")
    for prop_name, _ in bool_props:
        vals = {}
        for tname, task in com_tasks.items():
            if tname not in task_mem:
                continue
            try:
                v = getattr(task, prop_name)
                vals[tname] = 1 if v else 0
            except:
                vals[tname] = None
        print(f"  {prop_name:25s}: {vals}")
        # Update the expected values
        for i, (tname, _) in enumerate(bool_props):
            pass  # keep original expectations

    # Re-read actual COM values properly
    bool_data = {}
    for prop_name, _ in bool_props:
        vals = {}
        for tname, task in com_tasks.items():
            if tname not in task_mem:
                continue
            try:
                v = getattr(task, prop_name)
                vals[tname] = 1 if v else 0
            except:
                vals[tname] = None
        bool_data[prop_name] = vals

    # Also read FixedDuration, EffortDriven, LevelAssignments, LevelingCanSplit
    extra_bools = ['FixedDuration', 'EffortDriven', 'LevelAssignments', 'LevelingCanSplit',
                   'IgnoreResourceCalendar', 'Confirmed', 'ResponsePending',
                   'Estimated']
    for prop_name in extra_bools:
        vals = {}
        for tname, task in com_tasks.items():
            if tname not in task_mem:
                continue
            try:
                v = getattr(task, prop_name)
                vals[tname] = 1 if v else 0
            except:
                vals[tname] = '?'
        bool_data[prop_name] = vals
        print(f"  {prop_name:25s}: {vals}")

    # Search for individual bit matches across all tasks
    print(f"\n{'='*80}")
    print("=== Bit-level boolean search ===\n")
    print("Searching for bits that match COM boolean values across all tasks...\n")

    for prop_name, vals in bool_data.items():
        # Need at least 3 tasks with numeric values
        valid = {tn: v for tn, v in vals.items() if isinstance(v, int) and tn in task_mem}
        if len(valid) < 3:
            continue

        unique_vals = set(valid.values())
        is_unique = len(unique_vals) >= 2

        matches = []
        for byte_off in range(TOTAL):
            prio_off = byte_off - READ_BEFORE
            for bit in range(8):
                mask = 1 << bit
                all_match = True
                for tn, expected in valid.items():
                    actual_byte = task_mem[tn][byte_off]
                    actual_bit = 1 if (actual_byte & mask) else 0
                    if actual_bit != expected:
                        all_match = False
                        break
                if all_match:
                    matches.append((prio_off, bit))

        if is_unique:
            if len(matches) <= 5:
                match_strs = [f"prio{off:+d}.bit{bit}" for off, bit in matches]
                tag = " ** CONFIRMED **" if len(matches) == 1 else ""
                print(f"  {prop_name:25s} [UNIQUE] {', '.join(match_strs)}{tag}")
            else:
                # Show only matches in the known boolean region (-212 to -204)
                in_region = [(off, bit) for off, bit in matches if -220 <= off <= -200]
                match_strs = [f"prio{off:+d}.bit{bit}" for off, bit in in_region]
                print(f"  {prop_name:25s} [UNIQUE] {len(matches)} total, "
                      f"in bool region: {', '.join(match_strs) if match_strs else 'none'}")
        else:
            # Ambiguous (all same value) - show count only
            count_in_region = sum(1 for off, bit in matches if -220 <= off <= -200)
            print(f"  {prop_name:25s} [AMBIG ] {len(matches)} matches "
                  f"({count_in_region} in bool region)")

    # Additionally, try to find the exact ConstraintDate position
    print(f"\n{'='*80}")
    print("=== ConstraintDate search ===\n")
    # T1 has ConstraintType=SNET(4), so ConstraintDate should be its Start date
    # T2, T3, PST have ConstraintType=ASAP(0), so ConstraintDate should be NA (0xFFFFFFFF)
    for tname, task in com_tasks.items():
        if tname not in task_mem:
            continue
        try:
            cd = task.ConstraintDate
            print(f"  {tname}: ConstraintDate = {cd}")
        except Exception as e:
            print(f"  {tname}: ConstraintDate ERROR: {e}")

    # Also probe Status field values
    print(f"\n=== Status field ===\n")
    for tname, task in com_tasks.items():
        try:
            print(f"  {tname}: Status={task.Status}")
        except:
            print(f"  {tname}: Status=?")

    # Check what u16 value at prio-102 is (adjacent to ConstraintType at prio-104)
    print(f"\n=== Probing prio-102 to prio-88 ===\n")
    for tname in ['T1', 'T2', 'T3', 'PST']:
        addr = prio_addrs[tname]
        data = read_mem(handle, addr - 108, 32)
        if data:
            vals = []
            for off in range(0, 32, 2):
                v = struct.unpack_from('<H', data, off)[0]
                poff = off - 108
                vals.append(f"prio{poff:+d}={v}")
            print(f"  {tname}: {', '.join(vals)}")

    # Also probe the unknown region between dates and slack
    print(f"\n=== Probing prio-108 to prio-86 (u32) ===\n")
    for tname in ['T1', 'T2', 'T3', 'PST']:
        addr = prio_addrs[tname]
        data = read_mem(handle, addr - 108, 28)
        if data:
            vals = []
            for off in range(0, 28, 4):
                v = struct.unpack_from('<I', data, off)[0]
                poff = off - 108
                vals.append(f"prio{poff:+d}={v}")
            print(f"  {tname}: {', '.join(vals)}")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
