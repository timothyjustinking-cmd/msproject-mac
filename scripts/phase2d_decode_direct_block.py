#!/usr/bin/env python3
"""Phase 2d: Decode the direct-storage sparse block.

We found Priority at 0x23b7f585e48 and PctComplete at 0x23b7f585d30.
The region around them contains 0x0b40xxxx field_id patterns at 40-byte stride.
This appears to be ANOTHER sparse storage block (like the TASKNODE extended block).

This script reads and decodes the full direct-storage block using the same
differential-scan approach to locate it reliably.
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


def scan_all_regions(handle):
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
        if mbi.State == MEM_COMMIT and mbi.Protect in READABLE:
            if 0x1000 <= mbi.RegionSize <= 0x10000000:
                regions.append((mbi.BaseAddress, mbi.RegionSize))
        addr = (mbi.BaseAddress or 0) + mbi.RegionSize
        if addr <= (mbi.BaseAddress or 0):
            break
    return regions


def find_all_pattern(handle, regions, pattern):
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
                type_code = int(parts[4])
                field_size = int(parts[7])
                names[fid] = (name or f'unnamed_{parts[1]}', type_name, storage, type_code, field_size)
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
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    # Step 1: Locate Priority via differential scan
    print("\nLocating Priority via differential scan...")
    regions = scan_all_regions(handle)

    task.SetField(188743705, "987")
    time.sleep(0.3)
    hits_a = set(find_all_pattern(handle, regions, struct.pack('<i', 987)))

    task.SetField(188743705, "543")
    time.sleep(0.3)
    hits_b = set(find_all_pattern(handle, regions, struct.pack('<i', 543)))

    prio_addrs = sorted(hits_a & hits_b)
    print(f"  Priority confirmed at {len(prio_addrs)} location(s)")
    if not prio_addrs:
        print("ERROR: Can't find Priority")
        CloseHandle(handle)
        sys.exit(1)

    prio_addr = prio_addrs[0]
    print(f"  Using: {prio_addr:#x}")

    # Step 2: Read a large block around Priority and scan for field_ids
    print(f"\n{'='*100}")
    print(f"SCANNING FOR DIRECT-STORAGE BLOCK STRUCTURE")
    print(f"{'='*100}")

    block_start = prio_addr - 0x2000
    block = read_mem(handle, block_start, 0x4000)
    if not block:
        print("ERROR: Can't read block")
        CloseHandle(handle)
        sys.exit(1)

    # Scan for 0x0b40 field_id patterns
    marker = b'\x40\x0b'
    field_entries = []
    pos = 0
    while pos < len(block):
        mpos = block.find(marker, pos)
        if mpos == -1:
            break
        if mpos >= 2:
            low16 = struct.unpack_from('<H', block, mpos - 2)[0]
            field_id = 0x0b400000 + low16
            if field_id in field_names:
                abs_addr = block_start + mpos - 2
                field_entries.append((mpos - 2, field_id, abs_addr))
        pos = mpos + 1

    print(f"  Found {len(field_entries)} field_id occurrences in block")

    # Find clusters with consistent stride
    if not field_entries:
        print("  No field_ids found!")
        CloseHandle(handle)
        sys.exit(1)

    # Compute strides between consecutive entries
    stride_counts = {}
    for i in range(1, len(field_entries)):
        stride = field_entries[i][0] - field_entries[i-1][0]
        if 8 <= stride <= 64:
            stride_counts[stride] = stride_counts.get(stride, 0) + 1

    print(f"  Stride distribution:")
    for stride, count in sorted(stride_counts.items()):
        print(f"    stride={stride} (0x{stride:02x}): {count} occurrences")

    # Find the cluster near Priority
    prio_off = prio_addr - block_start
    near_prio = [(off, fid, addr) for off, fid, addr in field_entries
                 if abs(off - prio_off) < 0x400]

    print(f"\n  Field entries near Priority ({len(near_prio)} entries):")

    # Determine the dominant stride
    if len(near_prio) >= 3:
        strides = [near_prio[i+1][0] - near_prio[i][0] for i in range(len(near_prio)-1)]
        from collections import Counter
        stride_dist = Counter(strides)
        dominant_stride = stride_dist.most_common(1)[0][0]
        print(f"  Dominant stride: {dominant_stride} (0x{dominant_stride:02x})")

    # Decode each entry
    print(f"\n{'='*100}")
    print(f"DIRECT-STORAGE BLOCK ENTRIES")
    print(f"{'='*100}")

    output_rows = []

    for i, (off, fid, abs_addr) in enumerate(near_prio):
        info = field_names.get(fid, (f'unknown', '?', '?', 0, 0))
        name, type_name, storage, type_code, field_size = info

        # Read the 40 bytes of this entry
        entry = block[off:off + 48] if off + 48 <= len(block) else block[off:]

        # Interpret fields within the entry
        entry_hex = entry[:40].hex() if len(entry) >= 40 else entry.hex()

        # The entry appears to be:
        # [0:4] field_id
        # [4:8] padding/flags
        # [8:16] value_1 (possibly default)
        # [16:24] metadata
        # [24:32] value_2 (possibly current)
        # [32:40] metadata

        rel_prio = abs_addr - prio_addr
        vals = []
        if len(entry) >= 40:
            v1 = struct.unpack_from('<q', entry, 8)[0]
            v2 = struct.unpack_from('<q', entry, 16)[0]
            v3 = struct.unpack_from('<q', entry, 24)[0]
            v4 = struct.unpack_from('<q', entry, 32)[0]

            # Show different interpretations
            val_str = f"[+8]={v1} [+16]={v2} [+24]={v3} [+32]={v4}"

            if type_name in ('i16', 'i32'):
                vi16_1 = struct.unpack_from('<h', entry, 8)[0]
                vi16_2 = struct.unpack_from('<h', entry, 24)[0]
                vi32_1 = struct.unpack_from('<i', entry, 8)[0]
                vi32_2 = struct.unpack_from('<i', entry, 24)[0]
                val_str = f"i16_a={vi16_1} i32_a={vi32_1} | i16_b={vi16_2} i32_b={vi32_2}"
            elif type_name == 'i64':
                dbl1 = struct.unpack_from('<d', entry, 8)[0]
                dbl2 = struct.unpack_from('<d', entry, 24)[0]
                if abs(dbl1) < 1e15:
                    val_str = f"dbl_a={dbl1:.2f} | dbl_b={dbl2:.2f}"
                else:
                    val_str = f"i64_a={v1} | i64_b={v3}"
            elif type_name == 'date32':
                d1 = struct.unpack_from('<I', entry, 8)[0]
                d2 = struct.unpack_from('<I', entry, 24)[0]
                val_str = f"date_a={d1:#x} | date_b={d2:#x}"
            elif type_name == 'bool':
                b1 = struct.unpack_from('<I', entry, 8)[0]
                b2 = struct.unpack_from('<I', entry, 24)[0]
                val_str = f"bool_a={b1} | bool_b={b2}"
        else:
            val_str = f"(entry too short: {len(entry)} bytes)"

        is_prio = " << PRIORITY" if fid == 188743705 else ""
        print(f"  [{i:3d}] {abs_addr:#x} (prio{rel_prio:+5d}) {fid:#010x} "
              f"{name:<30s} {type_name:<8s} {storage:<10s} | {val_str}{is_prio}")

        output_rows.append({
            'field_id': fid, 'name': name, 'type': type_name,
            'storage': storage, 'val_str': val_str
        })

    # Hex dump of the block structure around Priority
    print(f"\n{'='*100}")
    print(f"HEX DUMP: 10 entries around Priority")
    print(f"{'='*100}")

    # Find the entry containing Priority
    prio_entry_idx = None
    for i, (off, fid, _) in enumerate(near_prio):
        if fid == 188743705:
            prio_entry_idx = i
            break

    if prio_entry_idx is not None:
        start_idx = max(0, prio_entry_idx - 5)
        end_idx = min(len(near_prio), prio_entry_idx + 5)

        for i in range(start_idx, end_idx):
            off, fid, abs_addr = near_prio[i]
            info = field_names.get(fid, ('?', '?', '?'))
            name = info[0]

            # Entry hex dump
            entry_data = block[off:off + 40]
            print(f"\n  Entry {i}: {abs_addr:#x} field={fid:#x} ({name})")
            for j in range(0, min(40, len(entry_data)), 8):
                chunk = entry_data[j:j+8]
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                val = struct.unpack_from('<Q', chunk, 0)[0] if len(chunk) == 8 else 0
                labels = {0: 'field_id+flags', 8: 'slot_a', 16: 'meta_a',
                          24: 'slot_b', 32: 'meta_b'}
                label = labels.get(j, '')
                print(f"    +{j:02x}: {hex_str:<24s} ({val:#018x}) {label}")

    # Cross-reference: read field values via COM and see which slot matches
    print(f"\n{'='*100}")
    print(f"COM VALUE CROSS-REFERENCE")
    print(f"{'='*100}")

    for i, (off, fid, abs_addr) in enumerate(near_prio):
        info = field_names.get(fid, ('?', '?', '?', 0, 0))
        name, type_name = info[0], info[1]

        try:
            com_val = task.GetField(fid)
            com_str = str(com_val)[:40]
        except:
            com_str = "ERROR"

        entry = block[off:off + 40]
        if len(entry) >= 32:
            if type_name in ('i16', 'i32'):
                v_a = struct.unpack_from('<i', entry, 8)[0]
                v_b = struct.unpack_from('<i', entry, 24)[0]
            elif type_name == 'i64':
                v_a = struct.unpack_from('<d', entry, 8)[0]
                v_b = struct.unpack_from('<d', entry, 24)[0]
            elif type_name == 'date32':
                v_a = struct.unpack_from('<I', entry, 8)[0]
                v_b = struct.unpack_from('<I', entry, 24)[0]
            elif type_name == 'bool':
                v_a = struct.unpack_from('<I', entry, 8)[0]
                v_b = struct.unpack_from('<I', entry, 24)[0]
            else:
                v_a = v_b = '?'

            print(f"  {name:<30s} COM={com_str:<30s} slot_a={v_a} slot_b={v_b}")

    # Save results
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            '..', 'analysis', 'direct_storage_decoded.tsv')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write("field_id_hex\tfield_id_dec\tname\ttype\tstorage\tvalues\n")
        for r in output_rows:
            f.write(f"{r['field_id']:#010x}\t{r['field_id']}\t{r['name']}\t"
                    f"{r['type']}\t{r['storage']}\t{r['val_str']}\n")
    print(f"\n  Saved to {out_path}")

    # Clean up
    task.SetField(188743705, "500")
    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
