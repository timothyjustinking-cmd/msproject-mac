#!/usr/bin/env python3
"""Systematically map SC1 fields by cross-task comparison.

For each COM-readable property, read its value from all 4 tasks,
then search for matching byte patterns in each task's 576-byte record.
Fields that match at the SAME prio-relative offset across all tasks
are confirmed field positions.
"""

import ctypes
import ctypes.wintypes
import struct
import sys
from datetime import datetime, timedelta

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

def encode_date(dt_val):
    """Encode a COM date as packed u32: (day_1indexed << 16) | (min_of_day * 10)"""
    if dt_val is None:
        return None
    try:
        epoch = datetime(1984, 1, 1)
        if hasattr(dt_val, 'year'):
            d = (dt_val - epoch).days + 1
            m = dt_val.hour * 60 + dt_val.minute
            return (d << 16) | (m * 10)
    except:
        pass
    return None

def decode_date(packed):
    if packed == 0xFFFFFFFF or packed == 0:
        return "NA"
    d = packed >> 16
    m = (packed & 0xFFFF) // 10
    if d < 1 or d > 50000 or m >= 1440:
        return f"inv(0x{packed:08x})"
    dt = datetime(1984, 1, 1) + timedelta(days=d - 1)
    return f"{dt.strftime('%Y-%m-%d')} {m // 60:02d}:{m % 60:02d}"


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
        'T1': 0x23b7ef6dd3c,
        'T2': 0x23b7ef6b4bc,
        'T3': 0x23b7ef6c47c,
        'PST': 0x23b7ef6dd3c + 576,
    }

    # Read 576-byte records for each task (from prio-336 to prio+240)
    # Total 576 bytes = the stride
    READ_BEFORE = 336
    READ_AFTER = 240
    TOTAL = READ_BEFORE + READ_AFTER

    task_mem = {}
    for name, addr in prio_addrs.items():
        data = read_mem(handle, addr - READ_BEFORE, TOTAL)
        if data and len(data) == TOTAL:
            task_mem[name] = data
        else:
            print(f"WARNING: Can't read {name}")

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

    # Define properties to probe with their expected storage types
    # (property_name, com_attribute, value_type, description)
    # value_type: 'u16', 'u32', 'i32', 'date', 'double', 'bool_bit', 'duration_x10'
    props = [
        # I16 fields (type_code=2)
        ('Priority', 'Priority', 'u16', 'rec_off=2'),
        ('PercentComplete', 'PercentComplete', 'u16', 'rec_off=5'),
        ('PercentWorkComplete', 'PercentWorkComplete', 'u16', 'rec_off=5'),
        ('ConstraintType', 'ConstraintType', 'u16', 'rec_off=3'),
        ('OutlineLevel', 'OutlineLevel', 'u16', 'rec_off=20'),
        ('Type', 'Type', 'u16', 'rec_off=29'),
        ('FixedCostAccrual', 'FixedCostAccrual', 'u16', 'rec_off=13'),
        ('Status', 'Status', 'u16', 'rec_off=51'),

        # Duration fields (type_code=3) - stored as value * 10
        ('Duration', 'Duration', 'duration_x10', 'rec_off=6'),
        ('ActualDuration', 'ActualDuration', 'duration_x10', 'rec_off=6'),
        ('RemainingDuration', 'RemainingDuration', 'duration_x10', 'rec_off=6'),
        ('LevelingDelay', 'LevelingDelay', 'duration_x10', 'rec_off=28'),
        ('FreeSlack', 'FreeSlack', 'duration_x10', 'rec_off=6'),
        ('TotalSlack', 'TotalSlack', 'duration_x10', 'rec_off=6'),
        ('StartSlack', 'StartSlack', 'duration_x10', 'rec_off=6'),

        # Date fields (type_code=19)
        ('Start', 'Start', 'date', 'rec_off=4'),
        ('Finish', 'Finish', 'date', 'rec_off=27'),
        ('EarlyStart', 'EarlyStart', 'date', 'rec_off=4'),
        ('EarlyFinish', 'EarlyFinish', 'date', 'rec_off=27'),
        ('LateStart', 'LateStart', 'date', 'rec_off=4'),
        ('LateFinish', 'LateFinish', 'date', 'rec_off=27'),
        ('ActualStart', 'ActualStart', 'date', 'rec_off=4'),
        ('ActualFinish', 'ActualFinish', 'date', 'rec_off=27'),
        ('Created', 'Created', 'date', ''),
        ('Stop', 'Stop', 'date', ''),
        ('Resume', 'Resume', 'date', ''),

        # U32 fields
        ('ID', 'ID', 'u32', 'rec_off=37'),
        ('UniqueID', 'UniqueID', 'u32', ''),

        # Boolean fields (type_code=11)
        ('Critical', 'Critical', 'bool', 'rec_off=17'),
        ('Milestone', 'Milestone', 'bool', ''),
        ('Marked', 'Marked', 'bool', ''),
        ('Rollup', 'Rollup', 'bool', ''),
        ('HideBar', 'HideBar', 'bool', ''),
        ('Summary', 'Summary', 'bool', ''),
        ('Active', 'Active', 'bool', ''),
        ('IsManual', 'Manual', 'bool', ''),

        # Work/Cost doubles (type_code=5)
        ('Work', 'Work', 'work_min', 'stored as min*1000'),
        ('ActualWork', 'ActualWork', 'work_min', ''),
        ('RemainingWork', 'RemainingWork', 'work_min', ''),
        ('Cost', 'Cost', 'cost_cents', 'stored as $*100'),
        ('FixedCost', 'FixedCost', 'cost_cents', ''),
        ('ActualCost', 'ActualCost', 'cost_cents', ''),
        ('RemainingCost', 'RemainingCost', 'cost_cents', ''),
    ]

    # Read all COM values
    print("=== Reading COM values ===\n")
    com_values = {}  # {prop_name: {task_name: value}}
    for prop_name, attr, vtype, desc in props:
        vals = {}
        for tname, task in com_tasks.items():
            if tname not in task_mem:
                continue
            try:
                v = getattr(task, attr)
                if vtype == 'date':
                    packed = encode_date(v)
                    vals[tname] = packed
                elif vtype == 'duration_x10':
                    vals[tname] = int(v * 10) if v is not None else None
                elif vtype == 'work_min':
                    vals[tname] = int(v * 1000) if v is not None else None
                elif vtype == 'cost_cents':
                    vals[tname] = int(round(v * 100)) if v is not None else None
                elif vtype == 'bool':
                    vals[tname] = 1 if v else 0
                else:
                    vals[tname] = int(v) if v is not None else None
            except Exception as e:
                vals[tname] = None
        com_values[prop_name] = vals

    # Print COM values
    for prop_name, attr, vtype, desc in props:
        vals = com_values[prop_name]
        val_strs = []
        for tn in ['T1', 'T2', 'T3', 'PST']:
            v = vals.get(tn)
            if v is None:
                val_strs.append(f"{tn}=?")
            elif vtype == 'date':
                val_strs.append(f"{tn}=0x{v:08x}({decode_date(v)})")
            else:
                val_strs.append(f"{tn}={v}")
        print(f"  {prop_name:25s} [{vtype:12s}] {' | '.join(val_strs)}")

    # Now search for matching offsets
    print(f"\n{'='*80}")
    print("=== Cross-task offset search ===\n")

    confirmed = []

    for prop_name, attr, vtype, desc in props:
        vals = com_values[prop_name]
        # Need at least 3 tasks with non-None values
        valid_tasks = {tn: v for tn, v in vals.items() if v is not None and tn in task_mem}
        if len(valid_tasks) < 3:
            continue

        # Skip if all values are the same (ambiguous)
        unique_vals = set(valid_tasks.values())

        # Check if values are distinguishing (at least 2 different values)
        is_distinguishing = len(unique_vals) >= 2

        # Search for matching offset
        matches = []

        if vtype in ('u16',):
            # Search as u16
            for off in range(0, TOTAL - 1, 2):
                prio_off = off - READ_BEFORE
                all_match = True
                for tn, expected in valid_tasks.items():
                    actual = struct.unpack_from('<H', task_mem[tn], off)[0]
                    if actual != expected:
                        all_match = False
                        break
                if all_match:
                    matches.append(('u16', prio_off))

        elif vtype in ('u32', 'duration_x10'):
            for off in range(0, TOTAL - 3, 4):
                prio_off = off - READ_BEFORE
                all_match = True
                for tn, expected in valid_tasks.items():
                    actual = struct.unpack_from('<I', task_mem[tn], off)[0]
                    if actual != expected:
                        all_match = False
                        break
                if all_match:
                    matches.append(('u32', prio_off))

        elif vtype == 'date':
            for off in range(0, TOTAL - 3, 4):
                prio_off = off - READ_BEFORE
                all_match = True
                for tn, expected in valid_tasks.items():
                    actual = struct.unpack_from('<I', task_mem[tn], off)[0]
                    if actual != expected:
                        all_match = False
                        break
                if all_match:
                    matches.append(('date', prio_off))

        elif vtype == 'bool':
            # Search as u16 flag (0 or 1)
            for off in range(0, TOTAL - 1, 2):
                prio_off = off - READ_BEFORE
                all_match = True
                for tn, expected in valid_tasks.items():
                    actual = struct.unpack_from('<H', task_mem[tn], off)[0]
                    if actual != expected:
                        all_match = False
                        break
                if all_match:
                    matches.append(('u16', prio_off))

        elif vtype in ('work_min', 'cost_cents'):
            # Search as double (8 bytes)
            for off in range(0, TOTAL - 7, 8):
                prio_off = off - READ_BEFORE
                all_match = True
                for tn, expected in valid_tasks.items():
                    actual = struct.unpack_from('<d', task_mem[tn], off)[0]
                    if abs(actual - expected) > 0.5:
                        all_match = False
                        break
                if all_match:
                    matches.append(('double', prio_off))

        status = "UNIQUE" if is_distinguishing else "AMBIG"
        if matches:
            # Filter: prefer matches that are unique
            match_strs = [f"prio{poff:+d}({fmt})" for fmt, poff in matches]
            marker = " <<<" if is_distinguishing and len(matches) <= 3 else ""
            confirmed_tag = ""
            if is_distinguishing and len(matches) == 1:
                confirmed_tag = " ** CONFIRMED **"
                confirmed.append((prop_name, vtype, matches[0][0], matches[0][1], desc))
            print(f"  {prop_name:25s} [{status:6s}] matches: {', '.join(match_strs[:15])}{confirmed_tag}")
        else:
            print(f"  {prop_name:25s} [{status:6s}] NO MATCH in scanned range")

    print(f"\n{'='*80}")
    print("=== CONFIRMED FIELD POSITIONS (unique value + single match) ===\n")
    for prop_name, vtype, fmt, prio_off, desc in confirmed:
        print(f"  prio{prio_off:+5d} ({fmt:6s})  {prop_name:25s}  {desc}")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
