#!/usr/bin/env python3
"""Decode date/time values in the flat buffer by correlating with COM fields.

Strategy: Read many date-type fields from COM, convert to various candidate
encodings, and search for matches in the prio flat buffer region.
"""

import ctypes
import struct
import sys
import time
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


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    handle = k32.OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    prio_addr = 0x23b7ef6dd3c

    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    # Read COM date fields
    print("COM Date/Time Fields:")
    dates = {}
    for name in ['Start', 'Finish', 'EarlyStart', 'EarlyFinish', 'LateStart', 'LateFinish',
                  'BaselineStart', 'BaselineFinish', 'Created', 'Deadline',
                  'ConstraintDate', 'ActualStart', 'ActualFinish',
                  'Start1', 'Finish1']:
        try:
            val = getattr(task, name)
            if val and hasattr(val, 'year'):
                # Convert pywintypes.datetime to Python datetime
                dt = datetime(val.year, val.month, val.day, val.hour, val.minute, val.second)
                dates[name] = dt
                print(f"  {name:20s} = {dt}")
            elif val:
                print(f"  {name:20s} = {val} (not a datetime)")
            else:
                print(f"  {name:20s} = None/NA")
        except Exception as e:
            print(f"  {name:20s} = ERROR: {e}")

    # Also read numeric fields for correlation
    print("\nCOM Numeric Fields:")
    nums = {}
    for name in ['Duration', 'Work', 'Cost', 'Priority', 'PercentComplete',
                  'RemainingDuration', 'RemainingWork', 'RemainingCost',
                  'ActualDuration', 'ActualWork', 'ActualCost',
                  'BaselineDuration', 'BaselineWork', 'BaselineCost',
                  'ID', 'UniqueID', 'OutlineLevel', 'OutlineNumber']:
        try:
            val = getattr(task, name)
            nums[name] = val
            print(f"  {name:20s} = {val}")
        except Exception as e:
            print(f"  {name:20s} = ERROR: {e}")

    # Project-level dates
    print("\nProject Dates:")
    proj_dates = {}
    for name in ['StartDate', 'FinishDate', 'StatusDate', 'CurrentDate']:
        try:
            val = getattr(proj, name)
            if val and hasattr(val, 'year'):
                dt = datetime(val.year, val.month, val.day, val.hour, val.minute, val.second)
                proj_dates[name] = dt
                print(f"  {name:20s} = {dt}")
        except Exception as e:
            print(f"  {name:20s} = ERROR: {e}")

    # Read flat buffer
    print("\n" + "=" * 80)
    print("FLAT BUFFER ANALYSIS")
    buf = read_mem(handle, prio_addr - 128, 384)
    if not buf:
        print("ERROR: Can't read flat buffer")
        k32.CloseHandle(handle)
        return

    # Extract all non-zero u32 values
    print("\nAll non-zero u32 values in flat buffer:")
    u32_vals = {}
    for off in range(0, 384, 4):
        u32 = struct.unpack_from('<I', buf, off)[0]
        if u32 != 0 and u32 != 0xFFFFFFFF:
            rel = off - 128
            u32_vals[rel] = u32
            print(f"  prio{rel:+4d}: 0x{u32:08x} ({u32:12d})")

    # Try various date encodings
    print("\n" + "=" * 80)
    print("DATE ENCODING HYPOTHESES")

    # Common MS Project epoch candidates
    epochs = [
        ("1/1/1984 00:00", datetime(1984, 1, 1, 0, 0, 0)),
        ("12/31/1983 00:00", datetime(1983, 12, 31, 0, 0, 0)),
        ("1/1/2000 00:00", datetime(2000, 1, 1, 0, 0, 0)),
        ("12/30/1899 00:00", datetime(1899, 12, 30, 0, 0, 0)),
    ]

    for epoch_name, epoch in epochs:
        print(f"\nEpoch: {epoch_name}")
        for dname, dt in dates.items():
            if dt.year < 1900:
                continue
            delta = dt - epoch
            total_minutes = int(delta.total_seconds() / 60)
            total_tenth_min = total_minutes * 10

            # Check if any u32 in buffer matches
            for rel, u32 in u32_vals.items():
                if u32 == total_minutes:
                    print(f"  MATCH! prio{rel:+4d} = {u32} = {dname} as minutes from {epoch_name}")
                elif u32 == total_tenth_min:
                    print(f"  MATCH! prio{rel:+4d} = {u32} = {dname} as 1/10 min from {epoch_name}")

    # Also try: what if the values at prio-100/-96/-92 are the same date?
    # All three = 940800 (0x000E5B00)
    print("\n" + "=" * 80)
    print("DECODING 940800 (at prio-100,-96,-92)")
    val = 940800
    for epoch_name, epoch in epochs:
        # As minutes
        dt_min = epoch + timedelta(minutes=val)
        print(f"  {val} minutes from {epoch_name}: {dt_min}")
        # As 1/10 minutes
        dt_tenth = epoch + timedelta(minutes=val / 10)
        print(f"  {val} 1/10min from {epoch_name}: {dt_tenth}")

    # Also decode 0x41424F80
    print("\n" + "=" * 80)
    print("DECODING 0x41424F80 = 1094864768")
    val2 = 0x41424F80
    for epoch_name, epoch in epochs:
        dt_min = epoch + timedelta(minutes=val2)
        print(f"  {val2} minutes from {epoch_name}: {dt_min}")
        dt_tenth = epoch + timedelta(minutes=val2 / 10)
        print(f"  {val2} 1/10min from {epoch_name}: {dt_tenth}")

    # Check Duration correlation
    print("\n" + "=" * 80)
    print("DURATION/WORK CORRELATION")
    dur = nums.get('Duration', 0)
    work = nums.get('Work', 0)
    print(f"Duration = {dur} (COM units = minutes)")
    print(f"  Duration * 10 = {int(dur * 10)}")
    print(f"  Duration in 1/10 min hex: 0x{int(dur * 10):08x}")
    print(f"  Looking for Duration*10 = {int(dur*10)} in buffer...")
    for rel, u32 in u32_vals.items():
        if u32 == int(dur * 10):
            print(f"    FOUND at prio{rel:+4d}!")

    print(f"\nWork = {work} (COM units = minutes)")
    if work > 0:
        print(f"  Work * 10 = {int(work * 10)}")
        print(f"  Work in 1/10 min hex: 0x{int(work * 10):08x}")

    # Check prio+200 value
    prio200 = struct.unpack_from('<I', buf, 200 + 128)[0]
    print(f"\nprio+200 = {prio200} = 0x{prio200:08x}")
    print(f"  As 1/10 minutes: {prio200 / 10:.1f} minutes")
    if prio200 / 10 == dur:
        print(f"  ** MATCHES Duration! **")

    # Check other stored values against known COM outputs
    print("\n" + "=" * 80)
    print("U32 VALUE IDENTIFICATION")
    known_values = {
        int(dur * 10): f"Duration*10 ({dur} min)",
        int(work * 10): f"Work*10 ({work} min)" if work > 0 else None,
        int(nums.get('Priority', 0)): f"Priority ({nums.get('Priority', 0)})" if nums.get('Priority', 0) else None,
        int(nums.get('ID', 0)): f"ID ({nums.get('ID', 0)})" if nums.get('ID', 0) else None,
        int(nums.get('UniqueID', 0)): f"UniqueID ({nums.get('UniqueID', 0)})" if nums.get('UniqueID', 0) else None,
        int(nums.get('OutlineLevel', 0)): f"OutlineLevel ({nums.get('OutlineLevel', 0)})" if nums.get('OutlineLevel', 0) else None,
    }
    known_values = {k: v for k, v in known_values.items() if v is not None and k > 0}

    for rel, u32 in sorted(u32_vals.items()):
        matches = [v for k, v in known_values.items() if k == u32]
        match_str = f"  <- {', '.join(matches)}" if matches else ""
        print(f"  prio{rel:+4d}: {u32:12d} (0x{u32:08x}){match_str}")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
