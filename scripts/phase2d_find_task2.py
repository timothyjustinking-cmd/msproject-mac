#!/usr/bin/env python3
"""Find Task 2 and Task 3's flat buffer records by process-wide search.

Set distinctive priorities, then search ALL of process memory for the u16
marker values. This will locate each task's 576-byte record regardless
of where it was allocated.
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

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", ctypes.wintypes.DWORD),
        ("RegionSize", ctypes.c_size_t),
        ("State", ctypes.wintypes.DWORD),
        ("Protect", ctypes.wintypes.DWORD),
        ("Type", ctypes.wintypes.DWORD),
    ]

MEM_COMMIT = 0x1000

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

def search_memory_u16(handle, value):
    """Search entire process memory for a u16 value, return addresses where found."""
    results = []
    needle = struct.pack('<H', value)
    addr = 0
    mbi = MEMORY_BASIC_INFORMATION()
    mbi_size = ctypes.sizeof(mbi)
    while addr < 0x7FFFFFFFFFFF:
        ret = k32.VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), mbi_size)
        if ret == 0: break
        base = mbi.BaseAddress or 0
        size = mbi.RegionSize or 0
        if mbi.State == MEM_COMMIT and size > 0 and size < 0x10000000:
            protect = mbi.Protect & 0xFF
            if protect not in (0x01, 0x00):
                data = read_mem(handle, base, size)
                if data:
                    offset = 0
                    while True:
                        idx = data.find(needle, offset)
                        if idx == -1: break
                        results.append(base + idx)
                        if len(results) >= 500: return results
                        offset = idx + 2  # u16 aligned
        next_addr = base + max(size, 0x1000)
        if next_addr <= addr: break
        addr = next_addr
    return results

def decode_date(packed):
    if packed == 0xFFFFFFFF:
        return "NA"
    day = packed >> 16
    min_x10 = packed & 0xFFFF
    if day < 1 or day > 50000 or min_x10 > 14400:
        return "inv"
    minute = min_x10 // 10
    if minute >= 1440:
        return "inv"
    dt = datetime(1984, 1, 1) + timedelta(days=day - 1)
    return f"{dt.strftime('%Y-%m-%d')} {minute//60:02d}:{minute%60:02d}"

def encode_date(year, month, day, hour=8, minute=0):
    dt = datetime(year, month, day)
    day_num = (dt - datetime(1984, 1, 1)).days + 1
    min_of_day = hour * 60 + minute
    return (day_num << 16) | (min_of_day * 10)


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
    STRIDE = 576

    # Use VERY distinctive priority values that won't appear randomly
    # Use prime numbers > 900 and < 1000
    markers = {1: 937, 2: 941, 3: 947}

    # Set markers
    print("Setting distinctive priorities:")
    task_info = {}
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is None:
            continue
        marker = markers.get(i, 500)
        t.Priority = marker

        info = {
            'name': t.Name,
            'marker': marker,
            'start': None,
            'finish': None,
            'duration': t.Duration,
        }
        try:
            s = t.Start
            info['start'] = datetime(s.year, s.month, s.day, s.hour, s.minute, s.second)
            f = t.Finish
            info['finish'] = datetime(f.year, f.month, f.day, f.hour, f.minute, f.second)
        except:
            pass
        task_info[i] = info
        print(f"  Task {i} '{info['name']}': Priority={marker}, "
              f"Start={info['start']}, Dur={info['duration']}")

    time.sleep(0.5)

    # Search process memory for each marker
    for task_id, info in task_info.items():
        marker = info['marker']
        print(f"\n{'='*80}")
        print(f"Searching for Task {task_id} '{info['name']}' (Priority={marker})...")

        # First, use differential: search with marker, then change to different value and search again
        hits = search_memory_u16(handle, marker)
        print(f"  Found {len(hits)} hits for u16 value {marker}")

        # Filter: check if there's a valid record structure at each hit
        candidates = []
        for addr in hits:
            # The Priority field is at record+168. So record start = addr - 168.
            rec_start = addr - 168
            rec_data = read_mem(handle, rec_start, 380)
            if not rec_data or len(rec_data) < 380:
                continue

            # Check if this looks like a valid task record
            created = struct.unpack_from('<I', rec_data, 0)[0]
            start_val = struct.unpack_from('<I', rec_data, 4)[0]
            finish_val = struct.unpack_from('<I', rec_data, 8)[0]
            duration = struct.unpack_from('<I', rec_data, 368)[0]

            # Check Created date
            created_str = decode_date(created)
            start_str = decode_date(start_val)
            finish_str = decode_date(finish_val)

            is_valid = ("2026" in created_str and
                        "inv" not in start_str and "NA" not in start_str)

            if is_valid:
                candidates.append({
                    'addr': addr,
                    'rec_start': rec_start,
                    'created': created_str,
                    'start': start_str,
                    'finish': finish_str,
                    'duration': duration,
                })

        if candidates:
            print(f"  Valid candidates: {len(candidates)}")
            for c in candidates:
                # Check if Start matches COM Start
                match = ""
                if info['start']:
                    expected = encode_date(info['start'].year, info['start'].month,
                                          info['start'].day, info['start'].hour,
                                          info['start'].minute)
                    actual_start = struct.unpack_from('<I', read_mem(handle, c['rec_start'] + 4, 4), 0)[0]
                    match = " MATCH" if actual_start == expected else " MISMATCH"

                dist = c['addr'] - prio_addr
                print(f"    0x{c['rec_start']:012x} (prio{dist:+d})")
                print(f"      Created={c['created']}, Start={c['start']}, Finish={c['finish']}")
                print(f"      Duration={c['duration']}/10={c['duration']/10:.0f}min{match}")

                # Show distance from Task 1's record
                task1_rec = prio_addr - 168
                offset_from_t1 = c['rec_start'] - task1_rec
                print(f"      Offset from Task 1 record: {offset_from_t1} bytes ({offset_from_t1/576:.2f} strides)")
        else:
            print(f"  No valid task record candidates found!")

    # Reset priorities
    print(f"\n{'='*80}")
    print("Resetting priorities...")
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t:
            t.Priority = 500

    k32.CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
