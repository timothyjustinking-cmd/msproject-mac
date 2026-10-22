#!/usr/bin/env python3
"""Phase 2d: Comprehensive task field reader.

Reads ALL field values from BOTH storage mechanisms:
1. TASKNODE extended sparse block (class 3/5/unk_2/unk_11): [field_id:u32, offset:u32] index + packed data
2. Direct-storage 2D block (mixed classes): 40-byte entries [field_id:u32][pad:u32][val_a:u64][sz_a:u64][val_b:u64][sz_b:u64]

Validates against COM GetField for every readable field.
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

def load_field_map():
    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
    path = os.path.join(base_dir, 'field_definitive_map.tsv')
    fields = {}
    with open(path, 'r', encoding='utf-8') as f:
        f.readline()
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 18 and parts[0] == 'Task':
                fid = int(parts[2])
                fields[fid] = {
                    'name': parts[3] or f'unnamed_{fid:#x}',
                    'type': parts[5],
                    'storage': parts[10],
                    'slot': int(parts[6]),
                    'size': int(parts[7]),
                    'type_code': int(parts[4]),
                }
    return fields


def find_tasknode(handle, regions, field_map, task):
    """Find the TASKNODE extended sparse block via text probe."""
    task.SetField(188743731, "COMPREHENSIVE_READER_TN")  # Text1
    time.sleep(0.3)

    text_pattern = "COMPREHENSIVE_READER_TN".encode('utf-16-le')
    name_pattern = task.Name.encode('utf-16-le')
    text_hits = find_all_pattern(handle, regions, text_pattern)

    for t1_addr in text_hits:
        block_start = t1_addr - 0x4000
        block = read_mem(handle, block_start, 0xA000)
        if not block:
            continue

        name_pos = block.find(name_pattern)
        if name_pos < 0:
            continue

        # Find index table before name
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
                fid = 0x0b400000 + low16
                if fid in field_map:
                    offset_32 = struct.unpack_from('<I', block, mpos + 2)[0]
                    entries.append((mpos - 2, fid, offset_32))
            pos = mpos + 1

        if len(entries) < 5:
            continue

        # Cluster entries
        clusters = []
        cur = [entries[0]]
        for e in entries[1:]:
            if e[0] - cur[-1][0] <= 10:
                cur.append(e)
            else:
                clusters.append(cur)
                cur = [e]
        clusters.append(cur)
        idx_table = max(clusters, key=len)

        # Find base from Name field offset
        for off, fid, data_off in idx_table:
            if fid == 0x0b40000e:  # Name
                # data_off points to the string bytes (NOT the char_count prefix)
                base_off = name_pos - data_off
                tn_addr = block_start + base_off
                return tn_addr, block, block_start, idx_table

    return None, None, None, None


def find_direct_block(handle, regions, task):
    """Find the direct-storage 2D block via Priority differential scan."""
    task.SetField(188743705, "987")
    time.sleep(0.3)
    hits_a = set(find_all_pattern(handle, regions, struct.pack('<i', 987)))

    task.SetField(188743705, "543")
    time.sleep(0.3)
    hits_b = set(find_all_pattern(handle, regions, struct.pack('<i', 543)))

    task.SetField(188743705, "500")  # restore default
    time.sleep(0.2)

    prio_addrs = sorted(hits_a & hits_b)
    if not prio_addrs:
        return None, None, []

    prio_addr = prio_addrs[0]
    prio_entry = prio_addr - 0x18  # val_b is at entry+0x18

    # Read region and walk entries
    scan_start = prio_entry - 0x2000
    block = read_mem(handle, scan_start, 0x4000)
    if not block:
        return None, None, []

    known_off = prio_entry - scan_start
    STRIDE = 40

    first_off = known_off
    while first_off >= STRIDE:
        off = first_off - STRIDE
        fid = struct.unpack_from('<I', block, off)[0]
        if (fid & 0xFFFF0000) == 0x0b400000:
            first_off = off
        else:
            break

    last_off = known_off
    while last_off + 2 * STRIDE <= len(block):
        off = last_off + STRIDE
        fid = struct.unpack_from('<I', block, off)[0]
        if (fid & 0xFFFF0000) == 0x0b400000:
            last_off = off
        else:
            break

    entries = []
    for i in range((last_off - first_off) // STRIDE + 1):
        off = first_off + i * STRIDE
        fid = struct.unpack_from('<I', block, off)[0]
        sz_a = struct.unpack_from('<Q', block, off + 16)[0]
        sz_b = struct.unpack_from('<Q', block, off + 32)[0]
        val_a_raw = block[off + 8:off + 16]
        val_b_raw = block[off + 24:off + 32]
        entries.append((fid, val_a_raw, sz_a, val_b_raw, sz_b))

    return scan_start + first_off, block[first_off:last_off + STRIDE], entries


def decode_tasknode_value(data, offset, field_type, field_size):
    """Decode a value from the TASKNODE packed data area."""
    try:
        if field_type == 'text':
            # data_offset points to string data; char_count u16 is at offset-2
            if offset < 2 or offset > len(data):
                return None
            char_count = struct.unpack_from('<H', data, offset - 2)[0]
            if char_count > 5000:
                return None
            str_bytes = char_count * 2
            if offset + str_bytes > len(data):
                return None
            return data[offset:offset + str_bytes].decode('utf-16-le', errors='replace')
        elif field_type in ('i64',):
            if offset + 8 > len(data):
                return None
            return struct.unpack_from('<d', data, offset)[0]
        elif field_type in ('i32', 'date32'):
            if offset + 4 > len(data):
                return None
            return struct.unpack_from('<i', data, offset)[0]
        elif field_type in ('i16', 'bool'):
            if offset + 4 > len(data):
                return None
            return struct.unpack_from('<i', data, offset)[0]
        else:
            return None
    except:
        return None


def decode_direct_value(val_raw, sz):
    """Decode a value from the direct-storage block."""
    if sz == 0:
        return None
    if sz == 2:
        return struct.unpack_from('<i', val_raw, 0)[0]
    elif sz == 4:
        v = struct.unpack_from('<I', val_raw, 0)[0]
        if v == 0xFFFFFFFF:
            return "NO_DATE"
        return struct.unpack_from('<i', val_raw, 0)[0]
    elif sz == 8:
        return struct.unpack_from('<d', val_raw, 0)[0]
    else:
        return struct.unpack_from('<Q', val_raw, 0)[0]


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    field_map = load_field_map()
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

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    regions = scan_all_regions(handle)
    print(f"  {len(regions)} regions, {sum(s for _, s in regions) / 1024 / 1024:.0f} MB")

    # Find TASKNODE
    print("\n--- Finding TASKNODE sparse block ---")
    tn_addr, tn_block, tn_block_start, tn_index = find_tasknode(handle, regions, field_map, task)
    task.SetField(188743731, "")  # clear probe

    tn_fields = {}
    if tn_addr:
        tn_base_off = tn_addr - tn_block_start
        header = tn_block[tn_base_off:tn_base_off + 8]
        total_size = struct.unpack_from('<I', header, 0)[0]
        entry_count = struct.unpack_from('<I', header, 4)[0]
        print(f"  TASKNODE at {tn_addr:#x}: size={total_size}, entries={entry_count}")

        for idx_off, fid, data_off in tn_index:
            info = field_map.get(fid, {'type': '?', 'name': f'unknown_{fid:#x}'})
            abs_data_off = tn_base_off + 8 + len(tn_index) * 8 + data_off  # approximate
            # Actually, data starts after header + index
            data_start = tn_base_off + 8 + entry_count * 8
            val = decode_tasknode_value(tn_block, tn_base_off + data_off, info['type'], info.get('size', 4))
            tn_fields[fid] = val
    else:
        print("  TASKNODE not found!")

    # Find direct-storage block
    print("\n--- Finding direct-storage block ---")
    db_addr, db_data, db_entries = find_direct_block(handle, regions, task)

    db_fields = {}
    if db_addr:
        print(f"  Direct block at {db_addr:#x}: {len(db_entries)} entries")
        for fid, val_a_raw, sz_a, val_b_raw, sz_b in db_entries:
            val_b = decode_direct_value(val_b_raw, sz_b)
            val_a = decode_direct_value(val_a_raw, sz_a)
            db_fields[fid] = (val_a, val_b)
    else:
        print("  Direct block not found!")

    # Comprehensive comparison
    print(f"\n{'='*140}")
    print(f"COMPREHENSIVE FIELD VALUE COMPARISON")
    print(f"{'='*140}")
    print(f"{'FieldID':<12s} {'Name':<28s} {'Type':<8s} {'Storage':<10s} "
          f"{'COM Value':<25s} {'TASKNODE':<20s} {'Direct(cur)':<20s} {'Source':8s}")
    print(f"{'-'*12:<12s} {'-'*28:<28s} {'-'*8:<8s} {'-'*10:<10s} "
          f"{'-'*25:<25s} {'-'*20:<20s} {'-'*20:<20s} {'-'*8:8s}")

    results = []
    tn_match = 0
    db_match = 0
    total = 0

    for fid in sorted(field_map.keys()):
        info = field_map[fid]
        name = info['name']

        # Get COM value
        try:
            com_val = task.GetField(fid)
            com_str = str(com_val)[:25]
        except:
            continue

        if not com_str or not com_str.strip():
            continue

        # Get TASKNODE value
        tn_val = tn_fields.get(fid)
        tn_str = ""
        if tn_val is not None:
            if isinstance(tn_val, float):
                tn_str = f"{tn_val:.6g}"
            else:
                tn_str = str(tn_val)[:20]

        # Get direct-block value
        db_val = db_fields.get(fid)
        db_str = ""
        if db_val is not None:
            _, cur = db_val
            if cur is not None:
                if isinstance(cur, float):
                    db_str = f"{cur:.6g}"
                else:
                    db_str = str(cur)[:20]

        source = ""
        if tn_str:
            source = "TN"
        if db_str:
            source = "DB" if not source else "BOTH"

        if source:
            total += 1
            # Simple numeric match check
            try:
                clean = com_str.replace('%', '').replace('$', '').replace(',', '').replace(' days', '').replace(' hrs', '').strip()
                if clean in ('Yes', 'No'):
                    com_num = 1 if clean == 'Yes' else 0
                else:
                    com_num = float(clean)

                if tn_val is not None and isinstance(tn_val, (int, float)):
                    if info['type'] == 'i64' and 'Cost' in name:
                        if abs(tn_val / 100 - com_num) < 1:
                            tn_match += 1
                    elif abs(tn_val - com_num) < 1:
                        tn_match += 1

                if db_val is not None:
                    _, cur = db_val
                    if cur is not None and cur != "NO_DATE" and isinstance(cur, (int, float)):
                        if info['type'] == 'i64' and 'Cost' in name:
                            if abs(cur / 100 - com_num) < 1:
                                db_match += 1
                        elif abs(cur - com_num) < 1:
                            db_match += 1
            except (ValueError, TypeError):
                pass

            print(f"{fid:#010x}  {name:<28s} {info['type']:<8s} {info['storage']:<10s} "
                  f"{com_str:<25s} {tn_str:<20s} {db_str:<20s} {source:8s}")

            results.append({
                'field_id': fid, 'name': name, 'type': info['type'],
                'storage': info['storage'], 'com': com_str,
                'tasknode': tn_str, 'direct': db_str, 'source': source,
            })

    print(f"\n  Fields with memory data: {total}")
    print(f"  TASKNODE matches: {tn_match}")
    print(f"  Direct-block matches: {db_match}")
    print(f"  Fields in TASKNODE only: {sum(1 for r in results if r['source'] == 'TN')}")
    print(f"  Fields in direct-block only: {sum(1 for r in results if r['source'] == 'DB')}")
    print(f"  Fields in BOTH: {sum(1 for r in results if r['source'] == 'BOTH')}")

    # Save results
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            '..', 'analysis', 'comprehensive_field_read.tsv')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write("field_id\tname\ttype\tstorage\tcom_value\ttasknode_value\tdirect_value\tsource\n")
        for r in results:
            f.write(f"{r['field_id']:#010x}\t{r['name']}\t{r['type']}\t{r['storage']}\t"
                    f"{r['com']}\t{r['tasknode']}\t{r['direct']}\t{r['source']}\n")
    print(f"\n  Saved to {out_path}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
