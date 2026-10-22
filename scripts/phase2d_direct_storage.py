#!/usr/bin/env python3
"""Phase 2d: Locate class 1 (direct) field storage.

We know the TASKNODE sparse block stores class 3/5/unk_2/unk_11 fields.
Class 1 (direct) fields like Priority, PercentComplete, UniqueID are stored
at fixed offsets in a different part of the task object.

Strategy:
1. Set distinctive values to several class 1 fields via COM
2. Scan process memory for the values
3. Find a block where multiple class-1 values co-locate
4. Map the fixed offsets of direct-storage fields
"""

import ctypes
import struct
import os
import sys
import time

try:
    import win32com.client
except ImportError:
    print("ERROR: pywin32 required"); sys.exit(1)

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
OpenProcess = k32.OpenProcess
ReadProcessMemory = k32.ReadProcessMemory
CloseHandle = k32.CloseHandle


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
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok:
        return None
    return buf.raw[:n.value]


def scan_heap_regions(handle):
    regions = []
    MEM_COMMIT = 0x1000
    READABLE = {0x02, 0x04, 0x20, 0x40}

    class MBI(ctypes.Structure):
        _fields_ = [
            ("BaseAddress", ctypes.c_void_p),
            ("AllocationBase", ctypes.c_void_p),
            ("AllocationProtect", ctypes.c_ulong),
            ("PartitionId", ctypes.c_ushort),
            ("RegionSize", ctypes.c_size_t),
            ("State", ctypes.c_ulong),
            ("Protect", ctypes.c_ulong),
            ("Type", ctypes.c_ulong),
        ]

    addr = 0
    mbi = MBI()
    while addr < 0x7FFFFFFFFFFF:
        result = k32.VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
        if result == 0:
            break
        if mbi.State == MEM_COMMIT and mbi.Protect in READABLE and 0x10000 <= mbi.RegionSize <= 0x4000000:
            regions.append((mbi.BaseAddress, mbi.RegionSize))
        addr = (mbi.BaseAddress or 0) + mbi.RegionSize
        if addr <= (mbi.BaseAddress or 0):
            break
    return regions


def find_pattern(handle, regions, pattern, max_hits=30):
    hits = []
    for base, size in regions:
        data = read_mem(handle, base, size)
        if not data:
            continue
        offset = 0
        while True:
            pos = data.find(pattern, offset)
            if pos == -1:
                break
            hits.append(base + pos)
            offset = pos + 1
            if len(hits) >= max_hits:
                return hits
    return hits


def load_field_info():
    """Load class-1 (direct) fields from the definitive map."""
    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
    path = os.path.join(base_dir, 'field_definitive_map.tsv')
    fields = {}
    with open(path, 'r', encoding='utf-8') as f:
        header = f.readline()
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 18 and parts[0] == 'Task':
                fid = int(parts[2])
                name = parts[3]
                type_name = parts[5]
                storage = parts[10]
                slot_index = int(parts[6])
                field_size = int(parts[7])
                if storage == 'direct':
                    fields[fid] = {
                        'name': name or f'unnamed_{parts[1]}',
                        'type': type_name,
                        'slot': slot_index,
                        'size': field_size,
                    }
    return fields


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    print(f"PID: {pid}")

    direct_fields = load_field_info()
    print(f"Loaded {len(direct_fields)} class-1 (direct) Task fields")

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    # Fix task name if corrupted
    if task.Name in ('0d', '10d', ''):
        task.Name = "Task 1"
        print(f"  Fixed task name to 'Task 1'")

    print(f"Task: ID={task.ID} Name='{task.Name}' UniqueID={task.UniqueID}")

    # Set distinctive class-1 probe values
    # Use unusual values that are easy to find in memory
    probes = [
        (188743705, "Priority", "777", 'i16', struct.pack('<h', 777)),
        (188743712, "PercentComplete", "42", 'i16', struct.pack('<h', 42)),
        # PercentComplete might be stored as i16 with value 42 or as value*10=420
    ]

    # Also search for known values that should already be set
    known_values = [
        ("UniqueID", task.UniqueID, struct.pack('<i', task.UniqueID)),
        ("ID", task.ID, struct.pack('<i', task.ID)),
    ]

    print("\nSetting probe values...")
    for fid, name, val, _, _ in probes:
        try:
            task.SetField(fid, val)
            print(f"  OK: {name} = {val}")
        except Exception as e:
            print(f"  FAIL: {name} -> {str(e)[:60]}")
    time.sleep(0.5)

    # Also get current values of other class-1 fields
    print("\nReading current class-1 field values via COM:")
    for fid, info in sorted(direct_fields.items()):
        if info['name'] and not info['name'].startswith('unnamed'):
            try:
                val = task.GetField(fid)
                if val and str(val).strip() and str(val) != '0' and str(val) != 'No':
                    print(f"  {info['name']:<30s} slot={info['slot']:3d} type={info['type']:<8s} = {str(val)[:40]}")
            except:
                pass

    # Scan for co-located probe values
    print("\nScanning process memory...")
    regions = scan_heap_regions(handle)
    print(f"  {len(regions)} heap regions")

    # Search for Priority=777 as i16
    prio_pattern = struct.pack('<h', 777)  # 0x0309
    prio_hits = find_pattern(handle, regions, prio_pattern, 100)
    print(f"\n  Priority (0x0309) pattern hits: {len(prio_hits)}")

    # Also try as i32
    prio_i32 = struct.pack('<i', 777)
    prio_i32_hits = find_pattern(handle, regions, prio_i32, 50)
    print(f"  Priority as i32 hits: {len(prio_i32_hits)}")

    # Search for UniqueID=1 as i32 (too common, skip)
    # Instead search for task name to find the task object
    task_name_pattern = task.Name.encode('utf-16-le')
    name_hits = find_pattern(handle, regions, task_name_pattern, 30)
    print(f"\n  Task name '{task.Name}' hits: {len(name_hits)}")

    # For each Priority i32 hit, check if nearby we can find:
    # - PercentComplete (42 as i16/i32)
    # - UniqueID (1 as i32)
    # - ID (1 as i32)
    # - The task name pointer

    print(f"\n{'='*100}")
    print(f"CO-LOCATION ANALYSIS (Priority as i32 + nearby values)")
    print(f"{'='*100}")

    pct_i16 = struct.pack('<h', 42)
    pct_i32 = struct.pack('<i', 42)
    uid_i32 = struct.pack('<i', task.UniqueID)
    id_i32 = struct.pack('<i', task.ID)

    candidates = []

    for prio_addr in prio_i32_hits:
        block_start = prio_addr - 0x200
        block = read_mem(handle, block_start, 0x600)
        if not block:
            continue

        prio_off = prio_addr - block_start

        # Look for other values in the vicinity
        results = {}
        for pat_name, pat in [('pct_i32', pct_i32), ('uid_i32', uid_i32),
                               ('name_utf16', task_name_pattern)]:
            pos = block.find(pat)
            if pos >= 0:
                results[pat_name] = pos

        score = len(results)
        if score >= 2:
            print(f"\n  CANDIDATE at Priority={prio_addr:#x} (block {block_start:#x}):")
            print(f"    Priority at +{prio_off:#06x}")
            for name, pos in sorted(results.items(), key=lambda x: x[1]):
                delta = pos - prio_off
                print(f"    {name} at +{pos:#06x} (delta from Priority: {delta:+d})")
            candidates.append((block_start, block, prio_off, results))

    # Deep analysis of best candidate
    if candidates:
        candidates.sort(key=lambda x: len(x[3]), reverse=True)
        block_start, block, prio_off, results = candidates[0]

        print(f"\n{'='*100}")
        print(f"BEST CANDIDATE DEEP ANALYSIS")
        print(f"{'='*100}")

        # Hex dump around Priority
        dump_start = max(0, prio_off - 0x80)
        dump_end = min(len(block), prio_off + 0x180)

        print(f"  Hex dump around Priority at {block_start + prio_off:#x}:")
        for off in range(dump_start, dump_end, 16):
            chunk = block[off:min(off + 16, dump_end)]
            hex_str = ' '.join(f'{b:02x}' for b in chunk)
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
            addr = block_start + off

            annotation = ""
            if off == prio_off:
                annotation = " << PRIORITY=777"
            for rname, rpos in results.items():
                if off <= rpos < off + 16:
                    annotation += f" << {rname}"

            print(f"    {addr:#014x}: {hex_str:<48s} {ascii_str}  {annotation}")

        # Scan for ALL i16/i32 values that match known field values
        print(f"\n  Scanning for all class-1 field values in this block:")

        # Try reading some class-1 fields and see if their values appear
        test_fields = []
        for fid, info in direct_fields.items():
            if info['name'] and not info['name'].startswith('unnamed'):
                try:
                    val = task.GetField(fid)
                    if val and str(val).strip():
                        test_fields.append((fid, info, str(val)))
                except:
                    pass

        # For numeric class-1 fields, check if the value appears near Priority
        for fid, info, val_str in test_fields:
            try:
                num_val = int(val_str.replace('%', '').replace('$', '').replace(',', '').strip())
                if abs(num_val) > 1 and abs(num_val) < 100000:
                    for width, fmt in [(2, '<h'), (4, '<i')]:
                        try:
                            pattern = struct.pack(fmt, num_val)
                        except struct.error:
                            continue
                        pos = block.find(pattern, dump_start, dump_end)
                        if pos >= 0:
                            delta = pos - prio_off
                            print(f"    {info['name']:<25s} slot={info['slot']:3d} "
                                  f"val={num_val:6d} as {fmt} at +{pos:#06x} (delta={delta:+d})")
            except (ValueError, TypeError):
                pass
    else:
        print("\n  No strong co-location candidates found")

        # Fallback: try scanning around task name hits instead
        print(f"\n{'='*100}")
        print(f"FALLBACK: Scanning around task name hits")
        print(f"{'='*100}")

        for name_addr in name_hits[:5]:
            block_start = name_addr - 0x400
            block = read_mem(handle, block_start, 0x800)
            if not block:
                continue

            name_off = name_addr - block_start
            prio_pos = block.find(prio_i32)
            pct_pos = block.find(pct_i32)

            if prio_pos >= 0 or pct_pos >= 0:
                print(f"\n  Task name at {name_addr:#x}:")
                if prio_pos >= 0:
                    print(f"    Priority at +{prio_pos:#06x} (delta from name: {prio_pos - name_off:+d})")
                if pct_pos >= 0:
                    print(f"    PercentComplete at +{pct_pos:#06x} (delta from name: {pct_pos - name_off:+d})")

    # Clean up
    print(f"\n  Cleaning up...")
    for fid, name, _, _, _ in probes:
        try:
            task.SetField(fid, "0")
        except:
            pass

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
