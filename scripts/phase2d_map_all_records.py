#!/usr/bin/env python3
"""Map all task records in the flat buffer array.

Now that we know Task 2 is at -18 strides from Task 1, scan the entire
record array to find all records and their task assignments.
"""

import ctypes
import ctypes.wintypes
import struct
import sys
import time
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

def decode_date(packed):
    if packed == 0xFFFFFFFF:
        return "NA"
    day = packed >> 16
    min_x10 = packed & 0xFFFF
    if day < 1 or day > 50000 or min_x10 > 14400:
        return None
    minute = min_x10 // 10
    if minute >= 1440:
        return None
    dt = datetime(1984, 1, 1) + timedelta(days=day - 1)
    return f"{dt.strftime('%Y-%m-%d')} {minute//60:02d}:{minute%60:02d}"


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    handle = k32.OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    STRIDE = 576
    prio_addr = 0x23b7ef6dd3c
    task1_rec = prio_addr - 168  # 0x023b7ef6dc94

    # Task 2 is at -18 strides. Project summary at +1 stride.
    # Let's scan a wide range: -30 to +10 strides from Task 1
    scan_start = task1_rec - 30 * STRIDE
    scan_count = 50  # 50 records
    scan_size = scan_count * STRIDE

    print(f"Task 1 record: 0x{task1_rec:012x}")
    print(f"Scanning {scan_count} records from 0x{scan_start:012x}")
    print(f"Range: strides -30 to +{scan_count-30-1} from Task 1\n")

    buf = read_mem(handle, scan_start, scan_size)
    if not buf or len(buf) < scan_size:
        print(f"ERROR: Could not read {scan_size} bytes")
        k32.CloseHandle(handle)
        return

    # Get COM task info for cross-reference
    print("COM task data:")
    com_tasks = {}
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is None:
            continue
        try:
            s = t.Start
            f = t.Finish
            start_str = f"{s.year}-{s.month:02d}-{s.day:02d} {s.hour:02d}:{s.minute:02d}"
            finish_str = f"{f.year}-{f.month:02d}-{f.day:02d} {f.hour:02d}:{f.minute:02d}"
        except:
            start_str = "?"
            finish_str = "?"
        com_tasks[t.ID] = {
            'name': t.Name,
            'start': start_str,
            'finish': finish_str,
            'duration': t.Duration,
            'outline': t.OutlineLevel,
            'priority': t.Priority,
        }
        print(f"  ID={t.ID}: '{t.Name}' OL={t.OutlineLevel} "
              f"Start={start_str} Dur={t.Duration}min Prio={t.Priority}")

    # Also check project summary task
    try:
        pst = proj.ProjectSummaryTask
        if pst:
            s = pst.Start
            f = pst.Finish
            com_tasks[0] = {
                'name': pst.Name,
                'start': f"{s.year}-{s.month:02d}-{s.day:02d} {s.hour:02d}:{s.minute:02d}",
                'finish': f"{f.year}-{f.month:02d}-{f.day:02d} {f.hour:02d}:{f.minute:02d}",
                'duration': pst.Duration,
                'outline': pst.OutlineLevel,
                'priority': pst.Priority,
            }
            print(f"  ID=0 (PST): '{pst.Name}' OL={pst.OutlineLevel} "
                  f"Start={com_tasks[0]['start']} Dur={pst.Duration}min Prio={pst.Priority}")
    except:
        pass

    print(f"\n{'='*100}")
    print(f"{'Stride':>6} {'Rec Addr':>16} {'Created':>20} {'Start':>20} {'Finish':>20} "
          f"{'Prio':>5} {'OL':>3} {'Dur(min)':>8} {'Match':>10}")
    print(f"{'='*100}")

    records = []
    for i in range(scan_count):
        off = i * STRIDE
        stride_num = i - 30  # relative to task 1

        created_raw = struct.unpack_from('<I', buf, off + 0)[0]
        start_raw = struct.unpack_from('<I', buf, off + 4)[0]
        finish_raw = struct.unpack_from('<I', buf, off + 8)[0]
        prio = struct.unpack_from('<H', buf, off + 168)[0]
        outline = struct.unpack_from('<I', buf, off + 264)[0]
        duration = struct.unpack_from('<I', buf, off + 368)[0]

        created_str = decode_date(created_raw)
        start_str = decode_date(start_raw)
        finish_str = decode_date(finish_raw)

        # Check if this looks like a valid record
        is_valid = (created_str is not None and
                    start_str is not None and
                    "2026" in created_str and
                    prio > 0 and prio <= 1000 and
                    outline <= 10)

        if is_valid:
            dur_min = duration / 10.0

            # Try to match to a COM task
            match = ""
            for tid, tinfo in com_tasks.items():
                if (tinfo['outline'] == outline and
                    tinfo['priority'] == prio and
                    abs(tinfo['duration'] - dur_min) < 1):
                    # Check start date match
                    if start_str and tinfo['start'] in start_str:
                        match = f"Task {tid}"
                        break
                    elif start_str and tinfo['start'][:10] == start_str[:10]:
                        match = f"Task {tid}?"
                        break

            rec_addr = scan_start + off
            print(f"{stride_num:>+6d} {rec_addr:>#16x} {created_str:>20} {start_str:>20} "
                  f"{finish_str:>20} {prio:>5} {outline:>3} {dur_min:>8.0f} {match:>10}")

            records.append({
                'stride': stride_num,
                'addr': rec_addr,
                'created': created_str,
                'start': start_str,
                'finish': finish_str,
                'prio': prio,
                'outline': outline,
                'dur_min': dur_min,
                'match': match,
            })

    print(f"\n{'='*100}")
    print(f"Found {len(records)} valid records in scan range")

    if records:
        strides = sorted(r['stride'] for r in records)
        print(f"Record strides: {strides}")
        if len(strides) > 1:
            gaps = [strides[i+1] - strides[i] for i in range(len(strides)-1)]
            print(f"Gaps between records: {gaps}")

    # Now also look at the index table area
    # Since records aren't in task ID order, there must be an index table somewhere
    # Let's check the area BEFORE the first record and AFTER the last
    if records:
        first_rec = min(r['addr'] for r in records)
        last_rec = max(r['addr'] for r in records)
        print(f"\nFirst record: 0x{first_rec:012x}")
        print(f"Last record:  0x{last_rec:012x}")
        print(f"Span: {last_rec - first_rec} bytes ({(last_rec - first_rec) / STRIDE:.1f} strides)")

        # Check 256 bytes BEFORE the first record for index-like structures
        pre_data = read_mem(handle, first_rec - 256, 256)
        if pre_data:
            print(f"\n256 bytes before first record (potential index/header):")
            for j in range(0, 256, 32):
                hex_vals = ' '.join(f'{pre_data[j+k]:02x}' for k in range(min(32, 256-j)))
                addr = first_rec - 256 + j
                print(f"  0x{addr:012x}: {hex_vals}")

        # Check 256 bytes AFTER the last record end
        post_data = read_mem(handle, last_rec + STRIDE, 256)
        if post_data:
            print(f"\n256 bytes after last record (potential trailer/next structure):")
            for j in range(0, 256, 32):
                hex_vals = ' '.join(f'{post_data[j+k]:02x}' for k in range(min(32, 256-j)))
                addr = last_rec + STRIDE + j
                print(f"  0x{addr:012x}: {hex_vals}")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
