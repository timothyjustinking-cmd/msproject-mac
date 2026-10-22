#!/usr/bin/env python3
"""Phase 2d: Find the task object by tracing pointers TO the TASKNODE sparse block.

Strategy:
1. Find the TASKNODE sparse block (we know how - set probes, scan for text)
2. Search all of memory for pointers TO the TASKNODE address
3. Each pointer-holder is a candidate for the task object
4. Check each candidate for class-1 field values (Priority, etc.)
5. Map the direct-storage field layout
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

    # Set probes to find TASKNODE
    task.SetField(188743731, "TASKNODE_FINDER_XYZ")  # Text1
    task.SetField(188743705, "987")  # Priority = 987 (unusual)
    task.SetField(188743712, "73")   # PercentComplete = 73%
    time.sleep(0.5)

    # Find TASKNODE sparse block
    print("\nFinding TASKNODE sparse block...")
    regions = scan_heap_regions(handle)
    print(f"  {len(regions)} heap regions")

    text1_pattern = "TASKNODE_FINDER_XYZ".encode('utf-16-le')
    task_name_pattern = task.Name.encode('utf-16-le')

    text1_hits = find_pattern(handle, regions, text1_pattern, 20)
    print(f"  Text1 hits: {len(text1_hits)}")

    tasknode_addr = None

    for t1_addr in text1_hits:
        block_start = t1_addr - 0x3000
        block = read_mem(handle, block_start, 0x8000)
        if not block:
            continue

        name_pos = block.find(task_name_pattern)
        if name_pos < 0:
            continue

        idx_table = find_index_table(block, name_pos, field_names)
        if len(idx_table) < 5:
            continue

        name_entry = None
        for off, fid, data_off in idx_table:
            if fid == 0x0b40000e:
                name_entry = (off, fid, data_off)
                break

        if name_entry:
            base_off = name_pos - name_entry[2]
            tasknode_addr = block_start + base_off
            print(f"  TASKNODE at {tasknode_addr:#x}")
            break

    if not tasknode_addr:
        print("ERROR: TASKNODE not found")
        CloseHandle(handle)
        sys.exit(1)

    # Now search all memory for pointers TO the TASKNODE address
    print(f"\n{'='*100}")
    print(f"SEARCHING FOR POINTERS TO TASKNODE ({tasknode_addr:#x})")
    print(f"{'='*100}")

    ptr_pattern = struct.pack('<Q', tasknode_addr)
    ptr_hits = find_pattern(handle, regions, ptr_pattern, 30)
    print(f"  Found {len(ptr_hits)} pointers to TASKNODE")

    for ptr_addr in ptr_hits:
        print(f"    {ptr_addr:#x}")

    # For each pointer holder, read a large block and analyze
    print(f"\n{'='*100}")
    print(f"ANALYZING POINTER HOLDERS")
    print(f"{'='*100}")

    prio_pattern_i16 = struct.pack('<h', 987)
    prio_pattern_i32 = struct.pack('<i', 987)
    pct_pattern_i32 = struct.pack('<i', 73)
    uid_pattern = struct.pack('<i', task.UniqueID)
    id_pattern = struct.pack('<i', task.ID)

    for ptr_addr in ptr_hits:
        # Read a block centered on the pointer
        obj_start = ptr_addr - 0x400
        obj_block = read_mem(handle, obj_start, 0x1000)
        if not obj_block:
            continue

        ptr_off = ptr_addr - obj_start

        # Check for class-1 values
        results = {}
        for pat_name, pat in [('Priority_i32', prio_pattern_i32),
                               ('Priority_i16', prio_pattern_i16),
                               ('PctComplete_i32', pct_pattern_i32),
                               ('UniqueID', uid_pattern),
                               ('TaskName', task_name_pattern)]:
            pos = obj_block.find(pat)
            if pos >= 0:
                delta = pos - ptr_off
                results[pat_name] = (pos, delta)

        if len(results) >= 2:
            print(f"\n  CANDIDATE holder at {ptr_addr:#x}:")
            print(f"    TASKNODE ptr at +{ptr_off:#06x}")
            for name, (pos, delta) in sorted(results.items(), key=lambda x: x[1][0]):
                print(f"    {name} at +{pos:#06x} (delta from ptr: {delta:+d})")

            # Check if there's a vtable pointer (points into .text section)
            # The object likely starts with a vtable pointer
            # Search backward from the TASKNODE ptr for a vtable pointer
            for search_off in range(ptr_off, max(ptr_off - 0x400, 0), -8):
                qword = struct.unpack_from('<Q', obj_block, search_off)[0]
                if IMAGE_BASE < qword < IMAGE_BASE + 0x2000000:
                    rva = qword - IMAGE_BASE
                    if 0xE00000 < rva < 0x1200000:  # .rdata section roughly
                        print(f"    Possible vtable at +{search_off:#06x}: {qword:#x} (RVA {rva:#x})")
                        # Read the vtable to check first entry
                        vt_data = read_mem(handle, qword, 0x10)
                        if vt_data:
                            first_entry = struct.unpack_from('<Q', vt_data, 0)[0]
                            if IMAGE_BASE < first_entry < IMAGE_BASE + 0x2000000:
                                first_rva = first_entry - IMAGE_BASE
                                if first_rva < 0xE00000:
                                    print(f"      -> vtable[0] = {first_entry:#x} (RVA {first_rva:#x}) LOOKS LIKE CODE")
                                    # This is likely the start of the task object
                                    obj_base_off = search_off
                                    obj_base_addr = obj_start + search_off

                                    # Hex dump of the task object
                                    print(f"\n    TASK OBJECT from {obj_base_addr:#x}:")
                                    dump_end = min(len(obj_block), obj_base_off + 0x200)
                                    for off in range(obj_base_off, dump_end, 16):
                                        chunk = obj_block[off:min(off + 16, dump_end)]
                                        hex_str = ' '.join(f'{b:02x}' for b in chunk)
                                        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                                        rel = off - obj_base_off

                                        ann = ""
                                        if off == search_off:
                                            ann = " << VTABLE PTR"
                                        if off <= ptr_off < off + 16:
                                            ann += " << TASKNODE PTR"
                                        for rname, (rpos, _) in results.items():
                                            if off <= rpos < off + 16:
                                                ann += f" << {rname}"

                                        print(f"      obj+{rel:04x}: {hex_str:<48s} {ascii_str}  {ann}")

                                    break  # Found vtable, stop searching

    # Differential approach: change Priority and scan for changed locations
    print(f"\n{'='*100}")
    print(f"DIFFERENTIAL SCAN: Changing Priority 987 -> 543")
    print(f"{'='*100}")

    # First snapshot Priority=987 hits near pointer holders
    old_prio = struct.pack('<i', 987)
    new_prio_val = 543
    new_prio = struct.pack('<i', new_prio_val)

    # Record old Priority locations
    old_hits = set()
    for ptr_addr in ptr_hits:
        block_start = ptr_addr - 0x800
        block = read_mem(handle, block_start, 0x1200)
        if not block:
            continue
        offset = 0
        while True:
            pos = block.find(old_prio, offset)
            if pos == -1:
                break
            old_hits.add(block_start + pos)
            offset = pos + 1

    print(f"  Old Priority (987) locations near ptrs: {len(old_hits)}")

    # Change Priority
    task.SetField(188743705, str(new_prio_val))
    time.sleep(0.3)

    # Check which locations changed
    changed = []
    for addr in old_hits:
        data = read_mem(handle, addr, 4)
        if data and data == new_prio:
            changed.append(addr)
            print(f"  CHANGED: {addr:#x} now contains {new_prio_val}")

    if not changed:
        # Search more broadly
        print("  No changes near pointer holders, searching broadly...")
        new_hits = find_pattern(handle, regions, new_prio, 50)
        # Intersect with old_prio regions
        for addr in new_hits:
            # Check if this was previously 987
            # We can't check retroactively, but check if it's near a pointer holder
            for ptr_addr in ptr_hits:
                if abs(addr - ptr_addr) < 0x1000:
                    print(f"  NEW VALUE at {addr:#x} (near ptr holder {ptr_addr:#x}, delta {addr - ptr_addr:+d})")
                    changed.append(addr)

    if changed:
        print(f"\n  Found {len(changed)} confirmed Priority storage location(s)")
        for addr in changed:
            # Read context
            ctx = read_mem(handle, addr - 0x40, 0xC0)
            if ctx:
                print(f"\n  Context around Priority at {addr:#x}:")
                for off in range(0, 0xC0, 16):
                    chunk = ctx[off:off + 16]
                    hex_str = ' '.join(f'{b:02x}' for b in chunk)
                    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                    abs_addr = addr - 0x40 + off
                    ann = " << PRIORITY" if off == 0x40 else ""
                    print(f"    {abs_addr:#014x}: {hex_str:<48s} {ascii_str}  {ann}")

    # Clean up
    print(f"\n  Cleaning up...")
    task.SetField(188743731, "")  # Text1
    task.SetField(188743705, "500")  # Priority back to default
    task.SetField(188743712, "42")   # Keep PercentComplete

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
