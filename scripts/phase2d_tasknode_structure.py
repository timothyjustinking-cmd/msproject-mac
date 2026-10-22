#!/usr/bin/env python3
"""Phase 2d: Decode TASKNODE internal structure.

We found that at address 0x23b72d42f78 the task name "Task 1" and
our probe values (Text1, Cost1) are laid out sequentially. There's
an index table before the data area with (field_id, offset) entries.

This script reads the full TASKNODE block and decodes its structure.
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
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    # Set distinctive probe values
    probes = {
        # Class 5 (baseline) fields
        188743731: ("Text1", "XYZZY_TASKNODE_TEXT1"),
        188743734: ("Text2", "QWERTY_TEXT2_MARKER"),
        188743767: ("Number1", "77777"),
        188743786: ("Cost1", "$12345.67"),
        # Class 1 (direct) fields
        188743705: ("Priority", "777"),
        188743712: ("PercentComplete", "42"),
    }

    print("\nSetting probe values...")
    for fid, (name, val) in probes.items():
        try:
            print(f"  {name} = {val}")
            task.SetField(fid, val)
        except Exception as e:
            print(f"    ERROR: {str(e)[:60]}")
    time.sleep(0.5)

    # Scan for task name to find TASKNODE
    print("\nScanning for TASKNODE...")
    regions = scan_heap_regions(handle)
    print(f"  {len(regions)} heap regions")

    task_name_pattern = task.Name.encode('utf-16-le')
    text1_pattern = "XYZZY_TASKNODE_TEXT1".encode('utf-16-le')
    text2_pattern = "QWERTY_TEXT2_MARKER".encode('utf-16-le')

    text1_hits = find_pattern(handle, regions, text1_pattern, 20)
    print(f"  Text1 hits: {len(text1_hits)}")

    best_block_addr = None
    best_block_data = None

    for t1_addr in text1_hits:
        block_start = t1_addr - 0x2000
        block = read_mem(handle, block_start, 0x6000)
        if not block:
            continue

        t2_pos = block.find(text2_pattern)
        name_pos = block.find(task_name_pattern)

        found = sum(1 for p in [t2_pos, name_pos] if p >= 0)
        if found >= 1:
            t1_off = t1_addr - block_start
            print(f"\n  TASKNODE CANDIDATE at block {block_start:#x}:")
            if name_pos >= 0: print(f"    Name at +{name_pos:#06x} (abs {block_start+name_pos:#x})")
            print(f"    Text1 at +{t1_off:#06x} (abs {t1_addr:#x})")
            if t2_pos >= 0: print(f"    Text2 at +{t2_pos:#06x} (abs {block_start+t2_pos:#x})")

            if name_pos >= 0 and t2_pos >= 0:
                best_block_addr = block_start
                best_block_data = block
                print(f"    ** BEST MATCH (has name + text1 + text2)")
                break
            elif not best_block_data and name_pos >= 0:
                best_block_addr = block_start
                best_block_data = block

    if not best_block_data:
        print("ERROR: Could not locate TASKNODE block")
        CloseHandle(handle)
        sys.exit(1)

    block = best_block_data
    block_start = best_block_addr

    # Find the actual TASKNODE base by looking for the index table
    # The index entries are 8-byte records: [pad16][field_id_32LE][offset_16LE]
    # or possibly 10-byte records
    name_pos = block.find(task_name_pattern)
    name_addr = block_start + name_pos

    # Read backward from task name looking for field_id patterns (0x0b40xxxx)
    print(f"\n{'='*100}")
    print(f"SEARCHING FOR FIELD INDEX TABLE")
    print(f"{'='*100}")

    # Scan the block for 0x0b40 patterns (field_id high bytes)
    marker = b'\x40\x0b'  # little-endian 0x0b40
    idx_entries = []

    search_start = max(0, name_pos - 0x1000)
    search_end = name_pos
    pos = search_start

    while pos < search_end:
        mpos = block.find(marker, pos, search_end)
        if mpos == -1:
            break
        if mpos >= 2:
            # Try to decode as field_id: [low16][0x0b40]
            low16 = struct.unpack_from('<H', block, mpos - 2)[0]
            field_id = 0x0b400000 + low16
            # Read the next 2 or 4 bytes as potential offset
            if mpos + 2 < len(block):
                offset_16 = struct.unpack_from('<H', block, mpos + 2)[0]
                if field_id in field_names:
                    name, type_name, storage = field_names[field_id]
                    idx_entries.append((mpos - 2, field_id, offset_16, name, type_name, storage))
        pos = mpos + 1

    print(f"  Found {len(idx_entries)} potential field index entries before task name:")
    for entry_off, fid, offset, name, type_name, storage in idx_entries[-30:]:
        print(f"    block+{entry_off:#06x}: field_id={fid:#010x} offset={offset:#06x} "
              f"{name:<35s} type={type_name:<8s} class={storage}")

    # Now look for the same pattern AFTER the task name (the data area)
    # to find where the index table ends and data begins
    print(f"\n{'='*100}")
    print(f"FULL BLOCK FIELD INDEX SCAN (all entries)")
    print(f"{'='*100}")

    all_entries = []
    pos = 0
    while pos < len(block):
        mpos = block.find(marker, pos)
        if mpos == -1:
            break
        if mpos >= 2:
            low16 = struct.unpack_from('<H', block, mpos - 2)[0]
            field_id = 0x0b400000 + low16
            if field_id in field_names and mpos + 2 < len(block):
                offset_val = struct.unpack_from('<H', block, mpos + 2)[0]
                name, type_name, storage = field_names[field_id]
                all_entries.append((mpos - 2, field_id, offset_val, name, type_name, storage))
        pos = mpos + 1

    print(f"  Total entries: {len(all_entries)}")

    # Find clusters of entries (index table should be contiguous)
    if all_entries:
        clusters = []
        current_cluster = [all_entries[0]]
        for e in all_entries[1:]:
            if e[0] - current_cluster[-1][0] < 20:
                current_cluster.append(e)
            else:
                clusters.append(current_cluster)
                current_cluster = [e]
        clusters.append(current_cluster)

        print(f"  Found {len(clusters)} clusters")
        for ci, cluster in enumerate(clusters):
            if len(cluster) >= 3:
                first = cluster[0]
                last = cluster[-1]
                print(f"\n  Cluster {ci}: {len(cluster)} entries, "
                      f"block+{first[0]:#06x} to +{last[0]:#06x}")

                stride = (last[0] - first[0]) / max(len(cluster) - 1, 1)
                print(f"    Stride: {stride:.1f} bytes")

                for e in cluster[:20]:
                    print(f"      +{e[0]:#06x}: {e[1]:#010x} off={e[2]:#06x} "
                          f"{e[3]:<35s} {e[4]:<8s} {e[5]}")
                if len(cluster) > 20:
                    print(f"      ... ({len(cluster) - 20} more)")
                    for e in cluster[-5:]:
                        print(f"      +{e[0]:#06x}: {e[1]:#010x} off={e[2]:#06x} "
                              f"{e[3]:<35s} {e[4]:<8s} {e[5]}")

    # Clean up
    print(f"\n  Cleaning up probe values...")
    for fid, (name, _) in probes.items():
        try:
            task.SetField(fid, "" if "Text" in name else "0")
        except Exception:
            pass

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
