#!/usr/bin/env python3
"""Phase 2d: Find task object by reading BACKWARD from TASKNODE.

The TASKNODE sparse block may be EMBEDDED in the task object (not separately
allocated). If so, the direct-storage fields and vtable are at negative offsets
from the TASKNODE base.

Strategy:
1. Find TASKNODE (proven method)
2. Read backward from TASKNODE looking for vtable pointer
3. Use differential scan (change Priority) to find exact direct-field offsets
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


def get_image_base(pid):
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    hProcess = OpenProcess(0x0410 | 0x0010, False, pid)
    if not hProcess:
        return None
    hMods = (ctypes.c_void_p * 1024)()
    cbNeeded = ctypes.c_ulong()
    if not psapi.EnumProcessModulesEx(hProcess, ctypes.byref(hMods), ctypes.sizeof(hMods),
                                       ctypes.byref(cbNeeded), 0x03):
        CloseHandle(hProcess)
        return None
    base = hMods[0]
    CloseHandle(hProcess)
    return base


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


def find_index_table(block, name_pos, field_names):
    marker = b'\x40\x0b'
    entries = []
    search_start = max(0, name_pos - 0x200)
    pos = search_start
    while pos < name_pos:
        mpos = block.find(marker, pos, name_pos)
        if mpos == -1:
            break
        if mpos >= 2:
            low16 = struct.unpack_from('<H', block, mpos - 2)[0]
            field_id = 0x0b400000 + low16
            if mpos + 4 < len(block) and field_id in field_names:
                offset_32 = struct.unpack_from('<I', block, mpos + 2)[0]
                entries.append((mpos - 2, field_id, offset_32))
        pos = mpos + 1

    if not entries:
        return []

    clusters = []
    cur = [entries[0]]
    for e in entries[1:]:
        if e[0] - cur[-1][0] <= 10:
            cur.append(e)
        else:
            clusters.append(cur)
            cur = [e]
    clusters.append(cur)
    return max(clusters, key=len)


def load_field_names():
    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
    path = os.path.join(base_dir, 'field_definitive_map.tsv')
    names = {}
    with open(path, 'r', encoding='utf-8') as f:
        f.readline()
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 18 and parts[0] == 'Task':
                fid = int(parts[2])
                name = parts[3]
                type_name = parts[5]
                storage = parts[10]
                names[fid] = (name or f'unnamed_{parts[1]}', type_name, storage)
    return names


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")
    field_names = load_field_names()

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    if task.Name in ('0d', '10d', ''):
        task.Name = "Task 1"
    print(f"Task: ID={task.ID} Name='{task.Name}' UniqueID={task.UniqueID}")

    # Set probes: a text field to find TASKNODE, and a unique Priority value
    task.SetField(188743731, "TASKNODE_LOCATOR_V2")  # Text1
    task.SetField(188743705, "987")  # Priority
    time.sleep(0.3)

    # Find TASKNODE
    print("\nFinding TASKNODE...")
    regions = scan_heap_regions(handle)
    text1_pattern = "TASKNODE_LOCATOR_V2".encode('utf-16-le')
    task_name_pattern = task.Name.encode('utf-16-le')

    text1_hits = find_pattern(handle, regions, text1_pattern, 20)

    tasknode_addr = None
    tasknode_block = None
    tasknode_block_start = None

    for t1_addr in text1_hits:
        block_start = t1_addr - 0x4000
        block = read_mem(handle, block_start, 0xA000)
        if not block:
            continue
        name_pos = block.find(task_name_pattern)
        if name_pos < 0:
            continue
        idx_table = find_index_table(block, name_pos, field_names)
        if len(idx_table) < 5:
            continue
        for off, fid, data_off in idx_table:
            if fid == 0x0b40000e:
                base_off = name_pos - data_off
                tasknode_addr = block_start + base_off
                tasknode_block = block
                tasknode_block_start = block_start
                break
        if tasknode_addr:
            break

    if not tasknode_addr:
        print("ERROR: TASKNODE not found")
        CloseHandle(handle)
        sys.exit(1)

    print(f"  TASKNODE at {tasknode_addr:#x}")

    # Read backward from TASKNODE looking for vtable and heap header
    print(f"\n{'='*100}")
    print(f"READING BACKWARD FROM TASKNODE")
    print(f"{'='*100}")

    # Read 0x2000 bytes before TASKNODE
    pre_data = read_mem(handle, tasknode_addr - 0x2000, 0x2000)
    if not pre_data:
        print("  Can't read data before TASKNODE")
        CloseHandle(handle)
        sys.exit(1)

    # Scan for vtable pointers (values that point into .rdata section of WINPROJ.EXE)
    vtable_candidates = []
    for off in range(len(pre_data) - 8, -1, -8):
        qword = struct.unpack_from('<Q', pre_data, off)[0]
        if IMAGE_BASE < qword < IMAGE_BASE + 0x2000000:
            rva = qword - IMAGE_BASE
            if 0xE00000 < rva < 0x1200000:
                # Verify: first vtable entry should be a code pointer
                vt_data = read_mem(handle, qword, 8)
                if vt_data:
                    first_entry = struct.unpack_from('<Q', vt_data, 0)[0]
                    if IMAGE_BASE < first_entry < IMAGE_BASE + 0x2000000:
                        first_rva = first_entry - IMAGE_BASE
                        if first_rva < 0xE00000:
                            neg_offset = off - len(pre_data)
                            abs_addr = tasknode_addr + neg_offset
                            vtable_candidates.append((neg_offset, abs_addr, rva, first_rva))

    print(f"  Found {len(vtable_candidates)} vtable candidates before TASKNODE:")
    for neg_off, abs_addr, rva, first_rva in vtable_candidates[-20:]:
        print(f"    TASKNODE{neg_off:+d} ({abs_addr:#x}): vtable RVA={rva:#x}, [0] RVA={first_rva:#x}")

    # Use differential scan to find Priority
    print(f"\n{'='*100}")
    print(f"DIFFERENTIAL SCAN FOR PRIORITY")
    print(f"{'='*100}")

    # Snapshot the region around TASKNODE BEFORE changing Priority
    scan_start = tasknode_addr - 0x2000
    scan_size = 0x4000
    before_data = read_mem(handle, scan_start, scan_size)
    if not before_data:
        print("  Can't read scan region")
        CloseHandle(handle)
        sys.exit(1)

    # Find all locations with current Priority value (987 = 0x03DB)
    prio_val_old = 987
    prio_val_new = 543
    old_pattern = struct.pack('<i', prio_val_old)
    old_locations = []
    pos = 0
    while True:
        p = before_data.find(old_pattern, pos)
        if p == -1:
            break
        old_locations.append(p)
        pos = p + 1

    print(f"  Priority=987 locations in scan region: {len(old_locations)}")

    # Change Priority
    task.SetField(188743705, str(prio_val_new))
    time.sleep(0.3)

    # Re-read and check which locations changed
    after_data = read_mem(handle, scan_start, scan_size)
    if not after_data:
        print("  Can't re-read scan region")
    else:
        changed = []
        for loc in old_locations:
            new_val = struct.unpack_from('<i', after_data, loc)[0]
            if new_val == prio_val_new:
                abs_addr = scan_start + loc
                rel_tasknode = abs_addr - tasknode_addr
                changed.append((loc, abs_addr, rel_tasknode))
                print(f"  CHANGED: offset {loc:#x} -> abs {abs_addr:#x} (TASKNODE{rel_tasknode:+d})")

        if not changed:
            print("  No locations changed to new value in scan region")
            # Broader scan: also check i16
            old_i16 = struct.pack('<h', prio_val_old)
            new_i16 = struct.pack('<h', prio_val_new)
            old_i16_locs = []
            p = 0
            while True:
                pp = before_data.find(old_i16, p)
                if pp == -1:
                    break
                old_i16_locs.append(pp)
                p = pp + 1

            print(f"  Priority=987 as i16 locations: {len(old_i16_locs)}")
            for loc in old_i16_locs:
                old_val = struct.unpack_from('<h', before_data, loc)[0]
                new_val = struct.unpack_from('<h', after_data, loc)[0]
                if old_val == prio_val_old and new_val == prio_val_new:
                    abs_addr = scan_start + loc
                    rel_tasknode = abs_addr - tasknode_addr
                    changed.append((loc, abs_addr, rel_tasknode))
                    print(f"  CHANGED (i16): offset {loc:#x} -> abs {abs_addr:#x} (TASKNODE{rel_tasknode:+d})")

        # Also do a full diff to find ANY changed bytes
        if not changed:
            print(f"\n  Full byte-level diff of scan region:")
            diff_count = 0
            for i in range(min(len(before_data), len(after_data))):
                if before_data[i] != after_data[i]:
                    abs_addr = scan_start + i
                    rel_tn = abs_addr - tasknode_addr
                    if diff_count < 40:
                        print(f"    offset {i:#06x} (TASKNODE{rel_tn:+d}): "
                              f"{before_data[i]:02x} -> {after_data[i]:02x}")
                    diff_count += 1
            print(f"  Total changed bytes: {diff_count}")

            if diff_count > 0 and diff_count < 100:
                # Group contiguous changes
                changes = []
                start_i = None
                for i in range(min(len(before_data), len(after_data))):
                    if before_data[i] != after_data[i]:
                        if start_i is None:
                            start_i = i
                    else:
                        if start_i is not None:
                            changes.append((start_i, i))
                            start_i = None
                if start_i is not None:
                    changes.append((start_i, min(len(before_data), len(after_data))))

                print(f"\n  Contiguous change regions:")
                for start, end in changes:
                    size = end - start
                    abs_start = scan_start + start
                    rel_tn = abs_start - tasknode_addr
                    old_bytes = before_data[start:end]
                    new_bytes = after_data[start:end]

                    old_vals = ""
                    new_vals = ""
                    if size == 2:
                        old_vals = f"i16={struct.unpack_from('<h', old_bytes, 0)[0]}"
                        new_vals = f"i16={struct.unpack_from('<h', new_bytes, 0)[0]}"
                    elif size == 4:
                        old_vals = f"i32={struct.unpack_from('<i', old_bytes, 0)[0]}"
                        new_vals = f"i32={struct.unpack_from('<i', new_bytes, 0)[0]}"
                    elif size == 8:
                        old_vals = f"i64={struct.unpack_from('<q', old_bytes, 0)[0]}"
                        new_vals = f"i64={struct.unpack_from('<q', new_bytes, 0)[0]}"

                    print(f"    TASKNODE{rel_tn:+d} ({abs_start:#x}): {size} bytes "
                          f"[{old_bytes.hex()} -> {new_bytes.hex()}]  {old_vals} -> {new_vals}")

    # Clean up
    print(f"\n  Cleaning up...")
    task.SetField(188743731, "")
    task.SetField(188743705, "500")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
