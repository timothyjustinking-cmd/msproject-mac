#!/usr/bin/env python3
"""Phase 2d: Full TASKNODE decode using the proven index-table approach.

From prior runs we discovered:
- TASKNODE has an 8-byte header, then N index entries (8 bytes each),
  then a tightly-packed data area where variable-length strings and
  fixed-size numerics are packed without padding.
- Index entries: [field_id_32LE, offset_16LE, ??_16LE]
- Offsets are from the TASKNODE base; data starts at base + 8 + N*8.

This script sets many probe values, finds the TASKNODE, reads the header,
decodes all entries, and validates by reading values.
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
                type_code = int(parts[4])
                field_size = int(parts[7])
                names[fid] = (name or f'unnamed_{parts[1]}', type_name, storage, type_code, field_size)
    return names


def find_index_table(block, name_pos, field_names):
    """Find the field index table by scanning for 0x0b40 markers before the task name."""
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
            if mpos + 2 < len(block) and field_id in field_names:
                offset_16 = struct.unpack_from('<H', block, mpos + 2)[0]
                # Additional 2 bytes after offset
                extra_16 = struct.unpack_from('<H', block, mpos + 4)[0] if mpos + 4 < len(block) else 0
                entries.append((mpos - 2, field_id, offset_16, extra_16))
        pos = mpos + 1

    # Find the largest contiguous cluster at 8-byte stride
    if not entries:
        return []

    clusters = []
    cur = [entries[0]]
    for e in entries[1:]:
        if e[0] - cur[-1][0] == 8:
            cur.append(e)
        elif e[0] - cur[-1][0] <= 12:
            cur.append(e)
        else:
            clusters.append(cur)
            cur = [e]
    clusters.append(cur)

    # Return the largest cluster
    best = max(clusters, key=len)
    return best


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

    # Set many probe values across different storage classes/types
    probes = {
        # Text fields (class 5/baseline, variable-length strings)
        188743731: ("Text1", "XYZZY_TEXT1_PROBE"),
        188743734: ("Text2", "ABCDE_TEXT2_PROBE"),
        188743740: ("Text4", "TEXT4_PROBE_ZZZ"),
        188743743: ("Text5", "TEXT5_PROBE_QQQ"),
        # Numeric fields
        188743767: ("Number1", "77777"),
        188743770: ("Number2", "88888"),
        # Cost fields
        188743786: ("Cost1", "$12345.67"),
        188743789: ("Cost2", "$98765.43"),
        # Other types
        188743705: ("Priority", "777"),
        188743712: ("PercentComplete", "42"),
        188743694: ("Duration1", "10d"),
    }

    print("\nSetting probe values...")
    for fid, (name, val) in probes.items():
        try:
            task.SetField(fid, val)
            print(f"  OK: {name} = {val}")
        except Exception as e:
            print(f"  FAIL: {name} -> {str(e)[:60]}")
    time.sleep(0.5)

    # Scan heap for TASKNODE
    print("\nScanning for TASKNODE...")
    regions = scan_heap_regions(handle)
    print(f"  {len(regions)} heap regions")

    text1_pattern = "XYZZY_TEXT1_PROBE".encode('utf-16-le')
    task_name_pattern = task.Name.encode('utf-16-le')

    text1_hits = find_pattern(handle, regions, text1_pattern, 20)
    print(f"  Text1 pattern hits: {len(text1_hits)}")

    # For each hit, read a large block and look for the index table
    best_result = None

    for t1_addr in text1_hits:
        block_start = t1_addr - 0x3000
        block = read_mem(handle, block_start, 0x8000)
        if not block:
            continue

        name_pos = block.find(task_name_pattern)
        if name_pos < 0:
            continue

        # Look for index table before name
        idx_table = find_index_table(block, name_pos, field_names)
        if len(idx_table) < 5:
            continue

        # Verify: Name entry should point to where we found the name
        name_entry = None
        for off, fid, data_off, extra in idx_table:
            if fid == 0x0b40000e:
                name_entry = (off, fid, data_off, extra)
                break

        if name_entry:
            tasknode_base_in_block = name_pos - name_entry[2]
            # Verify other entries
            text1_pos = block.find(text1_pattern)
            text1_entry = None
            for off, fid, data_off, extra in idx_table:
                if fid == 0x0b400033:
                    text1_entry = (off, fid, data_off, extra)
                    break

            if text1_entry and text1_pos >= 0:
                predicted = tasknode_base_in_block + text1_entry[2]
                if abs(predicted - text1_pos) <= 2:
                    print(f"\n  VALIDATED TASKNODE at block+{tasknode_base_in_block:#06x} "
                          f"(abs {block_start + tasknode_base_in_block:#x})")
                    print(f"    Index table: {len(idx_table)} entries, "
                          f"starts at base+{idx_table[0][0] - tasknode_base_in_block:#06x}")
                    best_result = (block, block_start, tasknode_base_in_block, idx_table)
                    break

    if not best_result:
        print("ERROR: Could not validate TASKNODE")
        CloseHandle(handle)
        sys.exit(1)

    block, block_start, base_off, idx_table = best_result
    tasknode_abs = block_start + base_off

    # Read and decode header
    print(f"\n{'='*100}")
    print(f"TASKNODE HEADER")
    print(f"{'='*100}")

    hdr = block[base_off:base_off + 8]
    print(f"  Base address: {tasknode_abs:#x}")
    print(f"  Header (8 bytes): {hdr.hex()}")
    for i in range(8):
        print(f"    byte[{i}]: {hdr[i]:#04x} ({hdr[i]:3d})")

    # Also try interpreting as pointer or count
    hdr_u32a, hdr_u32b = struct.unpack_from('<II', hdr, 0)
    hdr_u16s = struct.unpack_from('<HHHH', hdr, 0)
    hdr_u64 = struct.unpack_from('<Q', hdr, 0)[0]
    print(f"  As 2×u32: {hdr_u32a:#010x}, {hdr_u32b:#010x}")
    print(f"  As 4×u16: {', '.join(f'{v:#06x}' for v in hdr_u16s)}")
    print(f"  As u64: {hdr_u64:#018x}")
    print(f"  Index entry count: {len(idx_table)}")
    print(f"  Index table size: {len(idx_table) * 8} bytes ({len(idx_table) * 8:#x})")
    idx_start = idx_table[0][0] - base_off
    print(f"  Index starts at base+{idx_start:#06x}")
    data_start = idx_start + len(idx_table) * 8
    print(f"  Data should start at base+{data_start:#06x}")

    # Show 32 bytes before the header too
    pre = block[base_off - 32:base_off]
    print(f"\n  32 bytes before TASKNODE base:")
    for off in range(0, 32, 8):
        chunk = pre[off:off+8]
        val = struct.unpack_from('<Q', chunk, 0)[0] if len(chunk) == 8 else 0
        print(f"    base-{32-off:02d}: {chunk.hex()} ({val:#018x})")

    # Decode all index entries with field values
    print(f"\n{'='*100}")
    print(f"FIELD INDEX TABLE + DATA ({len(idx_table)} entries)")
    print(f"{'='*100}")

    print(f"  {'#':>3s} {'IdxOff':>7s} {'Field ID':>12s} {'DataOff':>8s} {'Extra':>6s} "
          f"{'Name':<32s} {'Type':<8s} {'Class':<10s} {'Size':>5s} {'Value'}")
    print(f"  {'-'*3} {'-'*7} {'-'*12} {'-'*8} {'-'*6} "
          f"{'-'*32} {'-'*8} {'-'*10} {'-'*5} {'-'*50}")

    output_rows = []

    for i, (blk_off, fid, data_off, extra) in enumerate(idx_table):
        info = field_names.get(fid, (f'unknown_{fid:#x}', '?', '?', 0, 0))
        name, type_name, storage, type_code, field_size = info

        # Calculate size from gap to next entry's offset (or end of block)
        if i + 1 < len(idx_table):
            next_off = idx_table[i + 1][2]
            entry_size = next_off - data_off
        else:
            entry_size = 0

        # Read value at predicted data position
        abs_off = base_off + data_off
        val_str = ""

        if abs_off >= 0 and abs_off + 64 <= len(block):
            raw = block[abs_off:abs_off + 64]

            if type_name == 'text':
                # Inline UTF-16LE string
                try:
                    # Find null terminator
                    end = entry_size if entry_size > 0 else 256
                    text_data = block[abs_off:abs_off + min(end, 256)]
                    nul_pos = 0
                    while nul_pos < len(text_data) - 1:
                        if text_data[nul_pos] == 0 and text_data[nul_pos + 1] == 0:
                            break
                        nul_pos += 2
                    decoded = text_data[:nul_pos].decode('utf-16-le', errors='replace')
                    val_str = f'"{decoded[:50]}"'
                except:
                    val_str = f"raw={raw[:16].hex()}"

            elif type_name == 'i64':
                i64 = struct.unpack_from('<q', raw, 0)[0]
                dbl = struct.unpack_from('<d', raw, 0)[0]
                if storage in ('baseline', 'unk_2') and 'Cost' in name:
                    val_str = f"${dbl/100:.2f} (raw_dbl={dbl:.2f})"
                elif abs(dbl) < 1e15 and abs(dbl) > 0.001:
                    val_str = f"dbl={dbl:.4f} i64={i64}"
                else:
                    val_str = f"i64={i64} ({raw[:8].hex()})"

            elif type_name == 'bool':
                v = struct.unpack_from('<I', raw, 0)[0]
                val_str = f"{'True' if v else 'False'} ({v:#x})"

            elif type_name == 'i16':
                v = struct.unpack_from('<h', raw, 0)[0]
                val_str = f"i16={v}"

            elif type_name == 'i32':
                v = struct.unpack_from('<i', raw, 0)[0]
                val_str = f"i32={v}"

            elif type_name == 'date32':
                v = struct.unpack_from('<I', raw, 0)[0]
                val_str = f"date={v:#010x} ({v})"

            elif type_name == 'double':
                dbl = struct.unpack_from('<d', raw, 0)[0]
                val_str = f"dbl={dbl:.4f}"

            else:
                val_str = f"raw={raw[:8].hex()}"

        idx_rel = blk_off - base_off
        print(f"  {i:3d} {idx_rel:#07x} {fid:#012x} {data_off:#08x} {extra:#06x} "
              f"{name:<32s} {type_name:<8s} {storage:<10s} {entry_size:5d} {val_str}")

        output_rows.append({
            'idx': i, 'field_id': fid, 'data_offset': data_off,
            'extra': extra, 'name': name, 'type': type_name,
            'storage': storage, 'entry_size': entry_size, 'value': val_str
        })

    # Hex dump of the full TASKNODE structure
    print(f"\n{'='*100}")
    print(f"TASKNODE HEX DUMP (header + index + first 0x100 of data)")
    print(f"{'='*100}")

    total_idx_size = 8 + len(idx_table) * 8
    dump_size = total_idx_size + 0x200
    dump_start = base_off
    dump_end = min(len(block), dump_start + dump_size)

    for off in range(dump_start, dump_end, 16):
        chunk = block[off:min(off + 16, dump_end)]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        rel = off - base_off

        # Annotate
        annotation = ""
        if rel == 0:
            annotation = " << HEADER"
        elif 8 <= rel < total_idx_size:
            entry_idx = (rel - 8) // 8
            if entry_idx < len(idx_table) and (rel - 8) % 8 == 0:
                e = idx_table[entry_idx]
                info = field_names.get(e[1], ('?',))
                annotation = f" << IDX[{entry_idx}] {info[0]}"

        for e_off, e_fid, e_data_off, _ in idx_table:
            if rel == e_data_off and rel >= total_idx_size:
                info = field_names.get(e_fid, ('?',))
                annotation = f" << DATA: {info[0]}"

        print(f"  base+{rel:04x}: {hex_str:<48s} {ascii_str}  {annotation}")

    # Validate: cross-check with COM reads
    print(f"\n{'='*100}")
    print(f"COM VALIDATION")
    print(f"{'='*100}")

    for fid, (probe_name, probe_val) in probes.items():
        try:
            com_val = task.GetField(fid)
            # Find in index table
            found = False
            for _, e_fid, _, _ in idx_table:
                if e_fid == fid:
                    found = True
                    break
            status = "IN INDEX" if found else "NOT IN INDEX"
            print(f"  {probe_name:<20s}: COM={str(com_val)[:30]:<30s} {status}")
        except Exception as e:
            print(f"  {probe_name:<20s}: COM ERROR: {str(e)[:40]}")

    # Save decoded TASKNODE to TSV
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
    out_path = os.path.join(out_dir, 'tasknode_decoded.tsv')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write("idx\tfield_id_hex\tfield_id_dec\tdata_offset\textra\tname\ttype\tstorage\tentry_size\tvalue\n")
        for r in output_rows:
            f.write(f"{r['idx']}\t{r['field_id']:#010x}\t{r['field_id']}\t"
                    f"{r['data_offset']:#06x}\t{r['extra']:#06x}\t{r['name']}\t{r['type']}\t"
                    f"{r['storage']}\t{r['entry_size']}\t{r['value']}\n")
    print(f"\n  Saved {len(output_rows)} decoded entries to {out_path}")

    # Clean up
    print(f"\n  Cleaning up probe values...")
    for fid, (name, _) in probes.items():
        try:
            if "Text" in name:
                task.SetField(fid, "")
            elif name == "Duration1":
                task.SetField(fid, "0d")
            else:
                task.SetField(fid, "0")
        except Exception:
            pass

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
