#!/usr/bin/env python3
"""Verify Start vs EarlyStart positions using T1's SNET constraint.

T1 has ConstraintType=SNET(4), so Start and EarlyStart should differ.
This disambiguates which memory position holds which field.
"""

import ctypes
import ctypes.wintypes
import struct
import sys
from datetime import datetime, timedelta

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

EPOCH = datetime(1984, 1, 1)

def encode_date(dt_val):
    if dt_val is None:
        return 0xFFFFFFFF
    try:
        py_dt = datetime(dt_val.year, dt_val.month, dt_val.day,
                         dt_val.hour, dt_val.minute, dt_val.second)
        d = (py_dt - EPOCH).days + 1
        m = py_dt.hour * 60 + py_dt.minute
        if d >= 1:
            return (d << 16) | (m * 10)
    except:
        pass
    return None

def decode_date(packed):
    if packed == 0xFFFFFFFF:
        return "NA"
    if packed == 0:
        return "0"
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

    print("=" * 80)
    print("DATE POSITION VERIFICATION — Start vs EarlyStart")
    print("=" * 80)

    date_offsets = [
        (-168, 'byte 68 (prio-168)'),
        (-164, 'byte 72 (prio-164)'),
        (-160, 'byte 76 (prio-160)'),
        (-156, 'byte 80 (prio-156)'),
        (-152, 'byte 84 (prio-152)'),
        (-148, 'byte 88 (prio-148)'),
        (-144, 'byte 92 (prio-144)'),
        (-140, 'byte 96 (prio-140)'),
        (-136, 'byte 100 (prio-136)'),
        (-132, 'byte 104 (prio-132)'),
        (-128, 'byte 108 (prio-128)'),
        (-124, 'byte 112 (prio-124)'),
        (-120, 'byte 116 (prio-120)'),
        (-116, 'byte 120 (prio-116)'),
        (-112, 'byte 124 (prio-112)'),
        (-108, 'byte 128 (prio-108)'),
    ]

    for tname in ['T1', 'T2', 'T3', 'PST']:
        if tname not in tasks:
            continue
        t = tasks[tname]
        addr = prio_addrs[tname]

        print(f"\n--- {tname}: '{t.Name}' ---")
        print(f"  ConstraintType = {t.ConstraintType}")

        com_dates = {}
        for prop in ['Created', 'Start', 'Finish', 'EarlyStart', 'EarlyFinish',
                      'LateStart', 'LateFinish', 'ActualStart', 'ActualFinish',
                      'Stop', 'Resume', 'ConstraintDate', 'Deadline']:
            try:
                v = getattr(t, prop)
                packed = encode_date(v)
                com_dates[prop] = packed
                print(f"  COM {prop:20s} = {decode_date(packed)} (0x{packed:08x})")
            except Exception as e:
                print(f"  COM {prop:20s} = ERROR: {e}")

        print()
        for off, label in date_offsets:
            data = read_mem(handle, addr + off, 4)
            if not data:
                continue
            mem_val = struct.unpack_from('<I', data, 0)[0]
            matches = []
            for prop, packed in com_dates.items():
                if packed == mem_val:
                    matches.append(prop)
            match_str = f" <-- {', '.join(matches)}" if matches else ""
            print(f"  {label}: 0x{mem_val:08x} = {decode_date(mem_val)}{match_str}")

    # Also check the formula-predicted field_id assignments
    print("\n" + "=" * 80)
    print("FORMULA FIELD-ID ASSIGNMENTS vs COM OBSERVATIONS")
    print("=" * 80)
    print("\nFormula says:")
    print("  byte 72 (prio-164) = field 0x0b400023 = COM 'Start'")
    print("  byte 76 (prio-160) = field 0x0b400024 = COM 'Finish'")
    print("  byte 80 (prio-156) = field 0x0b400025 = COM 'EarlyStart'")
    print("  byte 84 (prio-152) = field 0x0b400026 = COM 'EarlyFinish'")
    print("\nPrevious cross-task analysis said:")
    print("  byte 72 (prio-164) = EarlyStart")
    print("  byte 76 (prio-160) = EarlyFinish")
    print("  byte 80 (prio-156) = Start")
    print("  byte 84 (prio-152) = Finish")

    # For T1 with SNET constraint, check which assignment is correct
    if 'T1' in tasks and 'T1' in prio_addrs:
        t1 = tasks['T1']
        addr = prio_addrs['T1']
        start_packed = encode_date(t1.Start)
        estart_packed = encode_date(t1.EarlyStart)

        print(f"\nT1 verification (ConstraintType={t1.ConstraintType}):")
        print(f"  COM Start      = 0x{start_packed:08x} ({decode_date(start_packed)})")
        print(f"  COM EarlyStart = 0x{estart_packed:08x} ({decode_date(estart_packed)})")

        if start_packed != estart_packed:
            print("  START != EARLYSTART -- can disambiguate!")
            data72 = read_mem(handle, addr - 164, 4)
            data80 = read_mem(handle, addr - 156, 4)
            if data72 and data80:
                val72 = struct.unpack_from('<I', data72, 0)[0]
                val80 = struct.unpack_from('<I', data80, 0)[0]
                print(f"  mem[prio-164] = 0x{val72:08x} ({decode_date(val72)})")
                print(f"  mem[prio-156] = 0x{val80:08x} ({decode_date(val80)})")
                if val72 == start_packed:
                    print("  ==> prio-164 matches COM Start (formula is CORRECT)")
                elif val72 == estart_packed:
                    print("  ==> prio-164 matches COM EarlyStart (previous session was CORRECT)")
                else:
                    print(f"  ==> prio-164 matches NEITHER!")
                if val80 == start_packed:
                    print("  ==> prio-156 matches COM Start (previous session was CORRECT)")
                elif val80 == estart_packed:
                    print("  ==> prio-156 matches COM EarlyStart (formula is CORRECT)")
                else:
                    print(f"  ==> prio-156 matches NEITHER!")
        else:
            print("  START == EARLYSTART -- cannot disambiguate on T1")
            print("  Need a task where these differ (constrained + past deadline)")

    # Also check Finish vs EarlyFinish
    if 'T1' in tasks and 'T1' in prio_addrs:
        t1 = tasks['T1']
        addr = prio_addrs['T1']
        finish_packed = encode_date(t1.Finish)
        efinish_packed = encode_date(t1.EarlyFinish)

        print(f"\n  COM Finish      = 0x{finish_packed:08x} ({decode_date(finish_packed)})")
        print(f"  COM EarlyFinish = 0x{efinish_packed:08x} ({decode_date(efinish_packed)})")

        if finish_packed != efinish_packed:
            print("  FINISH != EARLYFINISH -- can disambiguate!")
            data76 = read_mem(handle, addr - 160, 4)
            data84 = read_mem(handle, addr - 152, 4)
            if data76 and data84:
                val76 = struct.unpack_from('<I', data76, 0)[0]
                val84 = struct.unpack_from('<I', data84, 0)[0]
                print(f"  mem[prio-160] = 0x{val76:08x} ({decode_date(val76)})")
                print(f"  mem[prio-152] = 0x{val84:08x} ({decode_date(val84)})")
                if val76 == finish_packed:
                    print("  ==> prio-160 matches COM Finish (formula is CORRECT)")
                elif val76 == efinish_packed:
                    print("  ==> prio-160 matches COM EarlyFinish (previous session was CORRECT)")
                if val84 == finish_packed:
                    print("  ==> prio-152 matches COM Finish (previous session was CORRECT)")
                elif val84 == efinish_packed:
                    print("  ==> prio-152 matches COM EarlyFinish (formula is CORRECT)")
        else:
            print("  FINISH == EARLYFINISH -- cannot disambiguate on T1")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
