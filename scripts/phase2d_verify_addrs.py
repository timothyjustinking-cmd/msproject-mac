#!/usr/bin/env python3
"""Verify that known addresses are still valid by checking Priority values,
fix date encoding, and do targeted field dumps."""

import ctypes
import ctypes.wintypes
import struct
import sys
from datetime import datetime, timedelta

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

try:
    import win32com.client
    import pywintypes
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

EPOCH = datetime(1984, 1, 1)

def encode_date(dt_val):
    """Encode a datetime as packed u32: (day_1indexed << 16) | (min_of_day * 10)"""
    if dt_val is None:
        return 0xFFFFFFFF
    try:
        # pywintypes.datetime -> regular datetime
        if hasattr(dt_val, 'year'):
            py_dt = datetime(dt_val.year, dt_val.month, dt_val.day,
                             dt_val.hour, dt_val.minute, dt_val.second)
            d = (py_dt - EPOCH).days + 1
            m = py_dt.hour * 60 + py_dt.minute
            if d >= 1:
                return (d << 16) | (m * 10)
    except Exception as e:
        print(f"  encode_date error: {e} for {dt_val}")
    return None

def decode_date(packed):
    if packed == 0xFFFFFFFF or packed == 0:
        return "NA" if packed == 0xFFFFFFFF else "0"
    d = packed >> 16
    m = (packed & 0xFFFF) // 10
    if d < 1 or d > 50000 or m >= 1440:
        return f"inv(0x{packed:08x})"
    dt = EPOCH + timedelta(days=d - 1)
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

    # Known Priority addresses from previous session
    prio_addrs = {
        'T1': 0x23b7ef6dd3c,
        'T2': 0x23b7ef6b4bc,
        'T3': 0x23b7ef6c47c,
        'PST': 0x23b7ef6dd3c + 576,
    }

    # Step 1: Verify Priority values at known addresses
    print("=== Step 1: Verify Priority at known addresses ===\n")
    for name, addr in prio_addrs.items():
        data = read_mem(handle, addr, 2)
        if data:
            val = struct.unpack_from('<H', data, 0)[0]
            print(f"  {name}: 0x{addr:012x} -> u16={val} (expect 500)")
        else:
            print(f"  {name}: 0x{addr:012x} -> UNREADABLE")

    # Step 2: Check COM task properties with proper date handling
    print("\n=== Step 2: COM task date properties ===\n")
    tasks = {}
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t:
            tasks[f'T{t.ID}'] = t
    try:
        pst = proj.ProjectSummaryTask
        if pst:
            tasks['PST'] = pst
    except:
        pass

    for tname in ['T1', 'T2', 'T3', 'PST']:
        if tname not in tasks:
            print(f"  {tname}: not found")
            continue
        t = tasks[tname]
        print(f"  {tname}: '{t.Name}' ID={t.ID}")
        for prop in ['Start', 'Finish', 'EarlyStart', 'EarlyFinish', 'LateStart', 'LateFinish',
                      'Created', 'ActualStart', 'ActualFinish', 'Stop', 'Resume']:
            try:
                v = getattr(t, prop)
                packed = encode_date(v)
                if packed is not None:
                    print(f"    {prop:20s}: {v} -> packed=0x{packed:08x} -> {decode_date(packed)}")
                else:
                    print(f"    {prop:20s}: {v} -> encode FAILED")
            except Exception as e:
                print(f"    {prop:20s}: ERROR: {e}")

    # Step 3: Dump known field positions and verify
    print("\n=== Step 3: Verify known field positions ===\n")
    known_fields = [
        (-300, 'u32', 'Task ID copy 1'),
        (-280, 'u32', 'Task ID copy 2'),
        (-264, 'u8',  'Version counter'),
        (-216, 'u16', 'OutlineLevel'),
        (-196, 'u32', 'Duration (×10)'),
        (-188, 'u32', 'ActualDuration (×10)'),
        (-184, 'u32', 'RemainingDuration (×10)'),
        (-168, 'date', 'Created'),
        (-164, 'date', 'EarlyStart'),
        (-160, 'date', 'EarlyFinish'),
        (-156, 'date', 'Start'),
        (-152, 'date', 'Finish'),
        (-148, 'date', 'LateStart'),
        (-144, 'date', 'LateFinish'),
        (-104, 'u16', 'ConstraintType (NEW!)'),
        (-100, 'u32', 'TotalSlack'),
        (-96,  'u32', 'FreeSlack'),
        (-92,  'u32', 'StartSlack'),
        (-4,   'u16', 'FixedCostAccrual'),
        (0,    'u16', 'Priority'),
        (+2,   'u16', 'PercentComplete'),
        (+4,   'u16', 'PercentWorkComplete'),
        (+6,   'u16', 'Type'),
        (+96,  'u32', 'Task ID'),
    ]

    for tname in ['T1', 'T2', 'T3', 'PST']:
        addr = prio_addrs[tname]
        print(f"  --- {tname} (prio=0x{addr:012x}) ---")
        for off, fmt, fname in known_fields:
            data = read_mem(handle, addr + off, 8)
            if not data:
                print(f"    prio{off:+5d} [{fmt:4s}] {fname:30s}: UNREADABLE")
                continue
            if fmt == 'u8':
                val = struct.unpack_from('<B', data, 0)[0]
                print(f"    prio{off:+5d} [{fmt:4s}] {fname:30s}: {val}")
            elif fmt == 'u16':
                val = struct.unpack_from('<H', data, 0)[0]
                print(f"    prio{off:+5d} [{fmt:4s}] {fname:30s}: {val}")
            elif fmt == 'u32':
                val = struct.unpack_from('<I', data, 0)[0]
                print(f"    prio{off:+5d} [{fmt:4s}] {fname:30s}: {val}")
            elif fmt == 'date':
                val = struct.unpack_from('<I', data, 0)[0]
                print(f"    prio{off:+5d} [{fmt:4s}] {fname:30s}: 0x{val:08x} = {decode_date(val)}")
        print()

    # Step 4: Check Duration values specifically
    print("=== Step 4: Duration verification ===\n")
    for tname in ['T1', 'T2', 'T3', 'PST']:
        if tname not in tasks:
            continue
        t = tasks[tname]
        com_dur = t.Duration
        addr = prio_addrs[tname]
        data = read_mem(handle, addr - 196, 4)
        if data:
            mem_dur = struct.unpack_from('<I', data, 0)[0]
            match = "MATCH" if mem_dur == int(com_dur * 10) else "MISMATCH"
            print(f"  {tname}: COM Duration={com_dur}min, ×10={int(com_dur*10)}, "
                  f"mem prio-196={mem_dur} [{match}]")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
