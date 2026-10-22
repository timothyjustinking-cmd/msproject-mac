#!/usr/bin/env python3
"""Phase 2d: Mass field prober.

Sets distinctive values on ~100 custom fields + key built-in fields,
then scans memory to find where each one lands in TASKNODE and direct-storage blocks.

Goal: empirically map which storage class each field actually uses at runtime
and validate the two-block storage model across a large sample.
"""

import ctypes
import struct
import os
import sys
import time
import traceback

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
                    'storage_class': int(parts[9]) if parts[9] else -1,
                    'storage_name': parts[10],
                    'slot': int(parts[6]),
                    'size': int(parts[7]),
                    'secondary_idx': int(parts[15]) if parts[15] and parts[15] != '-1' else -1,
                }
    return fields


# ── Probe value generators ──────────────────────────────────────

def gen_text_probe(name, idx):
    return f"PROBE_{name}_{idx:03d}"

def gen_number_probe(idx):
    return str(10000 + idx * 111)

def gen_cost_probe(idx):
    return str(20000 + idx * 222)

def gen_flag_probe():
    return "Yes"

def gen_date_probe(idx):
    day = 1 + (idx % 28)
    month = 1 + (idx % 12)
    return f"{month}/{day}/2025"

def gen_duration_probe(idx):
    return f"{idx + 1}d"


# ── TASKNODE finder ──────────────────────────────────────────────

def find_tasknode(handle, regions, field_map, task):
    probe_text = "MASS_PROBE_ANCHOR_TN"
    task.SetField(188743731, probe_text)  # Text1
    time.sleep(0.3)

    text_pattern = probe_text.encode('utf-16-le')
    name_pattern = task.Name.encode('utf-16-le')
    text_hits = find_all_pattern(handle, regions, text_pattern)

    for t1_addr in text_hits:
        block_start = t1_addr - 0x8000
        block = read_mem(handle, block_start, 0x14000)
        if not block:
            continue

        name_pos = block.find(name_pattern)
        if name_pos < 0:
            continue

        marker = b'\x40\x0b'
        entries = []
        search_start = max(0, name_pos - 0x800)
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

        for off, fid, data_off in idx_table:
            if fid == 0x0b40000e:  # Name
                base_off = name_pos - data_off
                tn_addr = block_start + base_off
                return tn_addr, block, block_start, idx_table

    return None, None, None, None


# ── Direct-storage block finder ──────────────────────────────────

def find_direct_block(handle, regions, task):
    task.SetField(188743705, "987")
    time.sleep(0.3)
    hits_a = set(find_all_pattern(handle, regions, struct.pack('<i', 987)))

    task.SetField(188743705, "543")
    time.sleep(0.3)
    hits_b = set(find_all_pattern(handle, regions, struct.pack('<i', 543)))

    task.SetField(188743705, "500")
    time.sleep(0.2)

    prio_addrs = sorted(hits_a & hits_b)
    if not prio_addrs:
        return None, None, []

    prio_addr = prio_addrs[0]
    prio_entry = prio_addr - 0x18

    scan_start = prio_entry - 0x4000
    block = read_mem(handle, scan_start, 0xC000)
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


# ── Value decoders ──────────────────────────────────────────────

def decode_tasknode_value(data, base_off, offset, field_type):
    try:
        abs_off = offset  # offset is from TASKNODE base
        rel = abs_off - (base_off - 0)  # relative to data buffer... actually offset is already from TN base
        # We need: data position = (tn_addr - block_start) + offset = base_off_in_block + offset
        # But we pass data as the full block, and base_off is tn_addr - block_start
        pos = base_off + offset  # position in the block buffer

        if field_type == 'text':
            if pos < 2 or pos > len(data):
                return None
            char_count = struct.unpack_from('<H', data, pos - 2)[0]
            if char_count > 5000:
                return None
            str_bytes = char_count * 2
            if pos + str_bytes > len(data):
                return None
            return data[pos:pos + str_bytes].decode('utf-16-le', errors='replace')
        elif field_type == 'i64':
            if pos + 8 > len(data):
                return None
            return struct.unpack_from('<d', data, pos)[0]
        elif field_type in ('i32', 'date32'):
            if pos + 4 > len(data):
                return None
            return struct.unpack_from('<i', data, pos)[0]
        elif field_type in ('i16', 'bool'):
            if pos + 4 > len(data):
                return None
            return struct.unpack_from('<i', data, pos)[0]
        else:
            return None
    except:
        return None


def decode_direct_value(val_raw, sz, field_type):
    if sz == 0:
        return None
    if sz == 2:
        return struct.unpack_from('<h', val_raw, 0)[0]
    elif sz == 4:
        v = struct.unpack_from('<I', val_raw, 0)[0]
        if v == 0xFFFFFFFF:
            return -1
        return struct.unpack_from('<i', val_raw, 0)[0]
    elif sz == 8:
        return struct.unpack_from('<d', val_raw, 0)[0]
    elif sz == 24:
        return f"ptr={struct.unpack_from('<Q', val_raw, 0)[0]:#x}"
    else:
        return f"blob(sz={sz},ptr={struct.unpack_from('<Q', val_raw, 0)[0]:#x})"


# ── Main ──────────────────────────────────────────────────────

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

    # ── Phase 1: Set distinctive probe values ──
    print(f"\n{'='*100}")
    print("PHASE 1: Setting probe values on custom fields")
    print(f"{'='*100}")

    probe_fields = {}  # fid -> (expected_value, field_type, set_value_str)

    # Text1-30
    for i in range(1, 31):
        fid_name = f"Text{i}"
        fid = None
        for f, info in field_map.items():
            if info['name'] == fid_name:
                fid = f
                break
        if fid:
            val = gen_text_probe("TXT", i)
            try:
                task.SetField(fid, val)
                probe_fields[fid] = (val, 'text', val)
            except Exception as e:
                print(f"  WARN: Can't set {fid_name}: {e}")

    # Number1-20
    for i in range(1, 21):
        fid_name = f"Number{i}"
        fid = None
        for f, info in field_map.items():
            if info['name'] == fid_name:
                fid = f
                break
        if fid:
            val = gen_number_probe(i)
            try:
                task.SetField(fid, val)
                probe_fields[fid] = (float(val), 'i64', val)
            except Exception as e:
                print(f"  WARN: Can't set {fid_name}: {e}")

    # Cost1-10
    for i in range(1, 11):
        fid_name = f"Cost{i}"
        fid = None
        for f, info in field_map.items():
            if info['name'] == fid_name:
                fid = f
                break
        if fid:
            val = gen_cost_probe(i)
            try:
                task.SetField(fid, val)
                probe_fields[fid] = (float(val), 'i64', val)
            except Exception as e:
                print(f"  WARN: Can't set {fid_name}: {e}")

    # Flag1-20
    for i in range(1, 21):
        fid_name = f"Flag{i}"
        fid = None
        for f, info in field_map.items():
            if info['name'] == fid_name:
                fid = f
                break
        if fid:
            try:
                task.SetField(fid, "Yes")
                probe_fields[fid] = (1, 'bool', "Yes")
            except Exception as e:
                print(f"  WARN: Can't set {fid_name}: {e}")

    # Date1-10
    for i in range(1, 11):
        fid_name = f"Date{i}"
        fid = None
        for f, info in field_map.items():
            if info['name'] == fid_name:
                fid = f
                break
        if fid:
            val = gen_date_probe(i)
            try:
                task.SetField(fid, val)
                probe_fields[fid] = (val, 'date32', val)
            except Exception as e:
                print(f"  WARN: Can't set {fid_name}: {e}")

    # Duration1-10
    for i in range(1, 11):
        fid_name = f"Duration{i}"
        fid = None
        for f, info in field_map.items():
            if info['name'] == fid_name:
                fid = f
                break
        if fid:
            val = gen_duration_probe(i)
            try:
                task.SetField(fid, val)
                probe_fields[fid] = (i, 'i32', val)
            except Exception as e:
                print(f"  WARN: Can't set {fid_name}: {e}")

    # Start1-10, Finish1-10
    for prefix in ('Start', 'Finish'):
        for i in range(1, 11):
            fid_name = f"{prefix}{i}"
            fid = None
            for f, info in field_map.items():
                if info['name'] == fid_name:
                    fid = f
                    break
            if fid:
                val = gen_date_probe(i + (10 if prefix == 'Finish' else 0))
                try:
                    task.SetField(fid, val)
                    probe_fields[fid] = (val, 'date32', val)
                except Exception as e:
                    print(f"  WARN: Can't set {fid_name}: {e}")

    # Built-in direct fields: Priority, PercentComplete, Marked, FixedCost, LevelingDelay
    builtin_probes = [
        (188743705, "750", 750, 'i16'),      # Priority
        (188743712, "42", 42, 'i16'),          # PercentComplete
        (188743751, "Yes", 1, 'bool'),         # Marked
        (188743688, "12345", 12345.0, 'i64'),  # FixedCost
    ]
    for fid, set_val, expected, ftype in builtin_probes:
        try:
            task.SetField(fid, set_val)
            probe_fields[fid] = (expected, ftype, set_val)
        except Exception as e:
            name = field_map.get(fid, {}).get('name', f'{fid:#x}')
            print(f"  WARN: Can't set {name}: {e}")

    time.sleep(0.5)
    print(f"  Set {len(probe_fields)} probe fields")

    # ── Phase 2: Scan memory for both blocks ──
    print(f"\n{'='*100}")
    print("PHASE 2: Scanning memory for storage blocks")
    print(f"{'='*100}")

    regions = scan_all_regions(handle)
    total_mb = sum(s for _, s in regions) / 1024 / 1024
    print(f"  {len(regions)} regions, {total_mb:.0f} MB")

    # Find TASKNODE
    print("\n  Finding TASKNODE...")
    tn_addr, tn_block, tn_block_start, tn_index = find_tasknode(handle, regions, field_map, task)

    tn_fields = {}
    if tn_addr:
        tn_base_off = tn_addr - tn_block_start
        header = tn_block[tn_base_off:tn_base_off + 8]
        if len(header) >= 8:
            total_size = struct.unpack_from('<I', header, 0)[0]
            entry_count = struct.unpack_from('<I', header, 4)[0]
            print(f"  TASKNODE at {tn_addr:#x}: size={total_size}, entries={entry_count}")

            for idx_off, fid, data_off in tn_index:
                info = field_map.get(fid, {'type': '?', 'name': f'unknown_{fid:#x}'})
                val = decode_tasknode_value(tn_block, tn_base_off, data_off, info['type'])
                if val is not None:
                    tn_fields[fid] = val
    else:
        print("  TASKNODE not found!")

    # Find direct-storage block
    print("\n  Finding direct-storage block...")
    db_addr, db_data, db_entries = find_direct_block(handle, regions, task)

    db_fields = {}
    if db_addr:
        print(f"  Direct block at {db_addr:#x}: {len(db_entries)} entries")
        for fid, val_a_raw, sz_a, val_b_raw, sz_b in db_entries:
            info = field_map.get(fid, {'type': '?'})
            val_b = decode_direct_value(val_b_raw, sz_b, info['type'])
            val_a = decode_direct_value(val_a_raw, sz_a, info['type'])
            db_fields[fid] = {'val_a': val_a, 'val_b': val_b, 'sz_a': sz_a, 'sz_b': sz_b}
    else:
        print("  Direct block not found!")

    # ── Phase 3: Cross-reference everything ──
    print(f"\n{'='*100}")
    print("PHASE 3: Cross-reference probe values against memory")
    print(f"{'='*100}")

    results = []
    tn_found = 0
    db_found = 0
    both_found = 0
    neither = 0

    for fid in sorted(probe_fields.keys()):
        expected, ftype, set_val = probe_fields[fid]
        info = field_map.get(fid, {'name': f'unknown_{fid:#x}', 'storage_class': -1, 'storage_name': '?', 'secondary_idx': -1})
        name = info['name']
        sc = info['storage_class']
        sname = info['storage_name']
        sec_idx = info['secondary_idx']

        # Check COM value
        try:
            com_val = str(task.GetField(fid))[:30]
        except:
            com_val = "ERROR"

        # Check TASKNODE
        tn_val = tn_fields.get(fid)
        tn_str = ""
        tn_match = False
        if tn_val is not None:
            if isinstance(tn_val, float):
                tn_str = f"{tn_val:.6g}"
            elif isinstance(tn_val, str):
                tn_str = tn_val[:30]
            else:
                tn_str = str(tn_val)
            # Check match
            if ftype == 'text' and isinstance(tn_val, str):
                tn_match = str(expected) in tn_val
            elif ftype == 'i64' and isinstance(tn_val, (int, float)):
                if 'Cost' in name:
                    tn_match = abs(tn_val / 100 - float(expected)) < 1
                else:
                    tn_match = abs(tn_val - float(expected)) < 1
            elif ftype == 'bool' and isinstance(tn_val, (int, float)):
                tn_match = tn_val != 0  # bitmask: any nonzero means at least one flag set
            elif ftype == 'date32' and isinstance(tn_val, (int, float)):
                tn_match = tn_val > 900000000 and tn_val < 1100000000  # plausible date range
            elif ftype == 'i32' and isinstance(tn_val, (int, float)):
                # Duration: expect minutes. "N days" = N * 4800 minutes
                tn_match = tn_val > 0 and tn_val < 500000  # plausible duration range
            elif isinstance(tn_val, (int, float)):
                try:
                    tn_match = abs(tn_val - float(expected)) < 10
                except:
                    pass

        # Check direct-storage block
        db_info = db_fields.get(fid)
        db_str = ""
        db_match = False
        if db_info is not None:
            v = db_info['val_b']
            if v is not None:
                if isinstance(v, float):
                    db_str = f"{v:.6g}"
                else:
                    db_str = str(v)[:30]
                # Check match
                if ftype == 'i64' and isinstance(v, (int, float)):
                    if 'Cost' in name:
                        db_match = abs(v / 100 - float(expected)) < 1
                    else:
                        db_match = abs(v - float(expected)) < 1
                elif ftype == 'bool' and isinstance(v, (int, float)):
                    db_match = v in (1, 2)
                elif ftype in ('i16', 'i32') and isinstance(v, (int, float)):
                    try:
                        db_match = abs(v - float(expected)) < 10
                    except:
                        pass
                elif ftype == 'date32' and isinstance(v, (int, float)):
                    db_match = v > 900000000 and v < 1100000000
                elif isinstance(v, str) and (v.startswith('ptr=') or v.startswith('blob(')):
                    db_match = True  # pointer to text/blob — present

        source = ""
        if tn_match:
            source = "TN"
            tn_found += 1
        if db_match:
            source = "DB" if not source else "BOTH"
            db_found += 1
        if source == "BOTH":
            both_found += 1
        if not source and (tn_str or db_str):
            source = "TN?" if tn_str else "DB?"
        if not source:
            source = "NONE"
            neither += 1

        results.append({
            'fid': fid, 'name': name, 'type': ftype,
            'storage_class': sc, 'storage_name': sname,
            'secondary_idx': sec_idx,
            'set_val': set_val, 'com_val': com_val,
            'tn_val': tn_str, 'db_val': db_str,
            'tn_match': tn_match, 'db_match': db_match,
            'source': source,
        })

    # Print results
    print(f"\n{'FID':<12s} {'Name':<22s} {'Type':<6s} {'SC':>3s} {'SecIdx':>6s} "
          f"{'COM':<22s} {'TN Value':<22s} {'DB Value':<22s} {'Match':8s}")
    print("-" * 140)

    for r in results:
        mark = ""
        if r['tn_match']:
            mark += "TN"
        if r['db_match']:
            mark += "+DB" if mark else "DB"
        if not mark:
            mark = "NONE"

        print(f"{r['fid']:#010x}  {r['name']:<22s} {r['type']:<6s} {r['storage_class']:3d} {r['secondary_idx']:6d} "
              f"{r['com_val']:<22s} {r['tn_val']:<22s} {r['db_val']:<22s} {mark:8s}")

    # Summary
    print(f"\n{'='*100}")
    print(f"SUMMARY")
    print(f"{'='*100}")
    print(f"  Total probed: {len(probe_fields)}")
    print(f"  Found in TASKNODE: {tn_found}")
    print(f"  Found in direct-block: {db_found}")
    print(f"  Found in BOTH: {both_found}")
    print(f"  Not found: {neither}")
    print(f"  TASKNODE entries: {len(tn_index) if tn_index else 0}")
    print(f"  Direct-block entries: {len(db_entries)}")

    # Storage class breakdown
    sc_tn = {}
    sc_db = {}
    sc_none = {}
    for r in results:
        sc = r['storage_class']
        if r['tn_match']:
            sc_tn[sc] = sc_tn.get(sc, 0) + 1
        if r['db_match']:
            sc_db[sc] = sc_db.get(sc, 0) + 1
        if r['source'] == 'NONE':
            sc_none[sc] = sc_none.get(sc, 0) + 1

    print(f"\n  Storage class -> block mapping:")
    all_scs = sorted(set(list(sc_tn.keys()) + list(sc_db.keys()) + list(sc_none.keys())))
    for sc in all_scs:
        tn_c = sc_tn.get(sc, 0)
        db_c = sc_db.get(sc, 0)
        none_c = sc_none.get(sc, 0)
        sname = ""
        for r in results:
            if r['storage_class'] == sc:
                sname = r['storage_name']
                break
        print(f"    class {sc:2d} ({sname:<12s}): TN={tn_c:3d}  DB={db_c:3d}  NONE={none_c:3d}")

    # Save results
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            '..', 'analysis', 'mass_probe_results.tsv')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write("field_id\tname\ttype\tstorage_class\tstorage_name\tsecondary_idx\t"
                "set_value\tcom_value\ttasknode_value\tdirect_value\t"
                "tn_match\tdb_match\tsource\n")
        for r in results:
            f.write(f"{r['fid']:#010x}\t{r['name']}\t{r['type']}\t{r['storage_class']}\t"
                    f"{r['storage_name']}\t{r['secondary_idx']}\t{r['set_val']}\t{r['com_val']}\t"
                    f"{r['tn_val']}\t{r['db_val']}\t{r['tn_match']}\t{r['db_match']}\t{r['source']}\n")
    print(f"\n  Saved to {out_path}")

    # ── Phase 4: Clean up probe values ──
    print(f"\n{'='*100}")
    print("PHASE 4: Cleaning up probe values")
    print(f"{'='*100}")

    for fid in probe_fields:
        info = field_map.get(fid, {'type': '?', 'name': '?'})
        try:
            if info['type'] == 'text':
                task.SetField(fid, "")
            elif info['type'] == 'bool':
                task.SetField(fid, "No")
            elif info['type'] == 'date32':
                task.SetField(fid, "NA")
            elif info['type'] == 'i64':
                task.SetField(fid, "0")
            elif info['type'] in ('i32', 'i16'):
                if fid == 188743705:  # Priority
                    task.SetField(fid, "500")
                elif fid == 188743712:  # PercentComplete
                    task.SetField(fid, "0")
                else:
                    task.SetField(fid, "0")
        except:
            pass

    CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
