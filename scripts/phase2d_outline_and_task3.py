#!/usr/bin/env python3
"""Find OutlineLevel's true position and locate Task 3 (Milestone).

1. Indent Task 2 under Task 1 (OL 1->2) and find where OL changes
2. Do Priority differential on Task 3 to find its record
"""

import ctypes
import ctypes.wintypes
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

def snapshot_region(handle, start, size):
    return read_mem(handle, start, size)

def find_u32_changes(before, after, base_offset=0):
    changes = []
    for off in range(0, min(len(before), len(after)) - 3, 4):
        vb = struct.unpack_from('<I', before, off)[0]
        va = struct.unpack_from('<I', after, off)[0]
        if vb != va:
            changes.append((off + base_offset, vb, va))
    return changes


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    handle = k32.OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    task1_prio = 0x23b7ef6dd3c
    task2_prio = 0x23b7ef6b4bc

    tasks = {}
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t:
            tasks[t.ID] = t

    # ======================================================
    # PART 1: Find OutlineLevel by indenting Task 2
    # ======================================================
    print("=== PART 1: OutlineLevel differential ===")
    t2 = tasks[2]
    print(f"Task 2 OutlineLevel before: {t2.OutlineLevel}")

    # Read region around Task 2's Priority
    region_size = 2048
    t2_start = task2_prio - 1024
    before = snapshot_region(handle, t2_start, region_size)

    # Indent Task 2 (select it first, then indent)
    # OutlineIndent works on the selected task
    app.SelectTaskField(Row=t2.ID, Column="Name")
    time.sleep(0.2)
    app.OutlineIndent()
    time.sleep(0.5)

    new_ol = t2.OutlineLevel
    print(f"Task 2 OutlineLevel after indent: {new_ol}")

    after = snapshot_region(handle, t2_start, region_size)

    # Find changes
    changes = find_u32_changes(before, after, base_offset=-1024)
    print(f"\nChanges ({len(changes)}):")
    for rel, vb, va in changes:
        label = ""
        if vb == 1 and va == 2:
            label = " << OUTLINELEVEL (1->2)"
        elif abs(va - vb) == 1:
            label = " (incremented by 1)"
        print(f"  prio{rel:+5d}: {vb} -> {va}{label}")

    # Find specifically where OL went from 1 to 2
    ol_positions = [(rel, vb, va) for rel, vb, va in changes if vb == 1 and va == 2]
    print(f"\nPositions where 1->2 (OutlineLevel candidates): {len(ol_positions)}")
    for rel, vb, va in ol_positions:
        print(f"  prio{rel:+5d}")

    # Outdent Task 2 back
    app.SelectTaskField(Row=t2.ID, Column="Name")
    time.sleep(0.2)
    app.OutlineOutdent()
    time.sleep(0.3)
    print(f"Task 2 OutlineLevel restored: {t2.OutlineLevel}")

    # ======================================================
    # PART 2: Find Task 3 (Milestone) by Priority differential
    # ======================================================
    print(f"\n{'='*60}")
    print("=== PART 2: Task 3 Priority differential ===")
    t3 = tasks[3]
    print(f"Task 3: '{t3.Name}', Priority={t3.Priority}, Duration={t3.Duration}, "
          f"OL={t3.OutlineLevel}, Milestone={t3.Milestone}")

    # Use wide process scan like for Task 2
    # Snapshot the same address range as Task 1 and Task 2
    # Task 2 is at prio -10368, so scan from -12000 to +2000
    wide_start = task1_prio - 12000
    wide_size = 14000
    wide_before = snapshot_region(handle, wide_start, wide_size)

    # Change Task 3 Priority
    t3.Priority = 888
    time.sleep(0.3)
    print(f"Task 3 Priority set to {t3.Priority}")

    wide_after = snapshot_region(handle, wide_start, wide_size)

    # Find u16 changes 500->888
    t3_hits = []
    for off in range(0, min(len(wide_before), len(wide_after)) - 1, 2):
        vb = struct.unpack_from('<H', wide_before, off)[0]
        va = struct.unpack_from('<H', wide_after, off)[0]
        if vb == 500 and va == 888:
            addr = wide_start + off
            rel = addr - task1_prio
            t3_hits.append((addr, rel))

    print(f"\nHits where u16 500->888: {len(t3_hits)}")
    for addr, rel in t3_hits:
        stride = rel / 576.0
        print(f"  0x{addr:012x} (t1_prio{rel:+d}, stride {stride:+.2f})")

        # Check surrounding record
        rec_data = read_mem(handle, addr - 200, 400)
        if rec_data:
            # Duration at prio-196
            dur = struct.unpack_from('<I', rec_data, 200 - 196)[0]
            # Created at prio-168
            created = struct.unpack_from('<I', rec_data, 200 - 168)[0]
            # Start at prio-156
            start = struct.unpack_from('<I', rec_data, 200 - 156)[0]
            # Finish at prio-152
            finish = struct.unpack_from('<I', rec_data, 200 - 152)[0]

            from datetime import datetime, timedelta
            def decode_date(p):
                if p == 0xFFFFFFFF: return "NA"
                d = p >> 16
                m = (p & 0xFFFF) // 10
                if d < 1 or d > 50000 or m >= 1440: return f"inv(0x{p:08x})"
                dt = datetime(1984,1,1) + timedelta(days=d-1)
                return f"{dt.strftime('%Y-%m-%d')} {m//60:02d}:{m%60:02d}"

            print(f"    Duration*10: {dur} ({dur/10:.0f} min)")
            print(f"    Created: {decode_date(created)}")
            print(f"    Start: {decode_date(start)}")
            print(f"    Finish: {decode_date(finish)}")

    # Restore
    t3.Priority = 500
    time.sleep(0.3)
    print(f"Task 3 Priority restored to {t3.Priority}")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
