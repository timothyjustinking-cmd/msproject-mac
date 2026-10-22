#!/usr/bin/env python3
"""Phase 2d: Full TASKNODE decode — header, index table, data area.

From phase2d_tasknode_structure.py we discovered:
- Field index table at 8-byte stride with (field_id_32, offset_16) entries
- Data area starts immediately after the index entries
- All offsets relative to a common TASKNODE base

This script validates that model and dumps the full structure.
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

    # Set many probe values to populate more index entries
    probes = {
        188743731: ("Text1", "XYZZY_TEXT1_PROBE"),
        188743734: ("Text2", "ABCDE_TEXT2_PROBE"),
        188743767: ("Number1", "77777"),
        188743770: ("Number2", "88888"),
        188743786: ("Cost1", "$12345.67"),
        188743705: ("Priority", "777"),
        188743712: ("PercentComplete", "42"),
        188743694: ("Duration1", "10d"),
        188743740: ("Text4", "TEXT4_MARKER_ZZZ"),
        188743743: ("Text5", "TEXT5_MARKER_QQQ"),
    }

    print("\nSetting probe values...")
    set_ok = []
    for fid, (name, val) in probes.items():
        try:
            task.SetField(fid, val)
            set_ok.append(fid)
            print(f"  OK: {name} = {val}")
        except Exception as e:
            print(f"  FAIL: {name} = {val} -> {str(e)[:60]}")
    time.sleep(0.5)

    # Scan for TASKNODE
    print("\nScanning for TASKNODE...")
    regions = scan_heap_regions(handle)
    text1_pattern = "XYZZY_TEXT1_PROBE".encode('utf-16-le')
    text2_pattern = "ABCDE_TEXT2_PROBE".encode('utf-16-le')
    task_name_pattern = task.Name.encode('utf-16-le')

    text1_hits = find_pattern(handle, regions, text1_pattern, 20)
    print(f"  Text1 hits: {len(text1_hits)}")

    best_block = None
    best_addr = None

    for t1_addr in text1_hits:
        block_start = t1_addr - 0x2000
        block = read_mem(handle, block_start, 0x6000)
        if not block:
            continue
        name_pos = block.find(task_name_pattern)
        t2_pos = block.find(text2_pattern)
        if name_pos >= 0 and t2_pos >= 0:
            best_block = block
            best_addr = block_start
            break
        elif name_pos >= 0 and not best_block:
            best_block = block
            best_addr = block_start

    if not best_block:
        print("ERROR: TASKNODE not found")
        CloseHandle(handle)
        sys.exit(1)

    block = best_block
    block_start = best_addr

    # Find the field index table
    marker = b'\x40\x0b'
    name_pos = block.find(task_name_pattern)
    name_abs = block_start + name_pos

    # Collect all valid index entries in the region before task name
    search_start = max(0, name_pos - 0x200)
    entries = []
    pos = search_start
    while pos < name_pos:
        mpos = block.find(marker, pos, name_pos)
        if mpos == -1:
            break
        if mpos >= 2:
            low16 = struct.unpack_from('<H', block, mpos - 2)[0]
            field_id = 0x0b400000 + low16
            if mpos + 2 < len(block):
                offset_16 = struct.unpack_from('<H', block, mpos + 2)[0]
                if field_id in field_names:
                    entries.append((mpos - 2, field_id, offset_16))
        pos = mpos + 1

    # Find the largest contiguous cluster (the real index table)
    clusters = []
    if entries:
        cur = [entries[0]]
        for e in entries[1:]:
            if e[0] - cur[-1][0] <= 10:
                cur.append(e)
            else:
                clusters.append(cur)
                cur = [e]
        clusters.append(cur)

    # Pick the largest cluster
    idx_table = max(clusters, key=len) if clusters else []

    print(f"\n{'='*100}")
    print(f"TASKNODE INDEX TABLE ({len(idx_table)} entries)")
    print(f"{'='*100}")

    if not idx_table:
        print("  No index table found!")
        CloseHandle(handle)
        sys.exit(1)

    # Calculate the TASKNODE base
    # Name entry offset should point to where we found Name in the block
    name_entry = None
    for off, fid, data_off in idx_table:
        if fid == 0x0b40000e:  # Name field_id
            name_entry = (off, fid, data_off)
            break

    if name_entry:
        # base = name_abs_pos - name_data_offset
        tasknode_base = (block_start + name_pos) - name_entry[2]
        print(f"  Name in block at +{name_pos:#06x} (abs {block_start + name_pos:#x})")
        print(f"  Name index entry offset: {name_entry[2]:#06x}")
        print(f"  => TASKNODE base: {tasknode_base:#x}")
        print(f"  => Index table starts at base + {(block_start + idx_table[0][0]) - tasknode_base:#06x}")
    else:
        # Fallback: infer base from first entry
        tasknode_base = block_start + idx_table[0][0] - 8  # assume 8-byte header
        print(f"  (No Name entry found, estimating base at {tasknode_base:#x})")

    base_in_block = tasknode_base - block_start

    # Dump the header (8 bytes before index table)
    print(f"\n  HEADER (8 bytes before first index entry):")
    hdr_off = idx_table[0][0] - 8
    if hdr_off >= 0 and hdr_off + 8 <= len(block):
        hdr = block[hdr_off:hdr_off + 8]
        vals = struct.unpack_from('<BBHHH', hdr, 0)
        print(f"    Raw hex: {hdr.hex()}")
        print(f"    As bytes: {' '.join(f'{b:02x}' for b in hdr)}")
        # Try different interpretations
        v32a, v32b = struct.unpack_from('<II', hdr, 0)
        v16a, v16b, v16c, v16d = struct.unpack_from('<HHHH', hdr, 0)
        v64 = struct.unpack_from('<Q', hdr, 0)[0]
        print(f"    As 2×u32: {v32a:#010x}, {v32b:#010x}")
        print(f"    As 4×u16: {v16a:#06x}, {v16b:#06x}, {v16c:#06x}, {v16d:#06x}")
        print(f"    As u64:   {v64:#018x}")
        print(f"    Entry count = {len(idx_table)}, stored as u16 = {len(idx_table):#06x}")

    # Also dump 16 bytes BEFORE the header
    pre_hdr_off = idx_table[0][0] - 24
    if pre_hdr_off >= 0:
        pre = block[pre_hdr_off:pre_hdr_off + 24]
        print(f"\n  24 bytes before index table:")
        for i in range(0, 24, 8):
            chunk = pre[i:i+8]
            val = struct.unpack_from('<Q', chunk, 0)[0] if len(chunk) == 8 else 0
            print(f"    base-{24-i:02d}: {chunk.hex()} ({val:#018x})")

    # Dump index table with decoded field values
    print(f"\n  INDEX TABLE ENTRIES:")
    print(f"  {'#':>3s} {'Blk Off':>8s} {'Field ID':>12s} {'Data Off':>8s} {'Name':<35s} {'Type':<8s} {'Class':<10s} {'Value'}")
    print(f"  {'-'*3} {'-'*8} {'-'*12} {'-'*8} {'-'*35} {'-'*8} {'-'*10} {'-'*40}")

    for i, (blk_off, fid, data_off) in enumerate(idx_table):
        info = field_names.get(fid, (f'unknown_{fid:#x}', '?', '?', 0, 0))
        name, type_name, storage, type_code, field_size = info

        # Read the value at the data offset
        val_abs_off = base_in_block + data_off
        val_str = ""

        if val_abs_off >= 0 and val_abs_off + 16 <= len(block):
            raw = block[val_abs_off:val_abs_off + 16]

            if type_name == 'text':
                # Text: pointer to UTF-16LE string — or inline?
                # Try reading as pointer first
                ptr = struct.unpack_from('<Q', raw, 0)[0]
                if 0x100000 < ptr < 0x7FFFFFFFFFFF:
                    str_data = read_mem(handle, ptr, 512)
                    if str_data:
                        try:
                            nul = str_data.find(b'\x00\x00')
                            if nul >= 0 and nul % 2 == 0:
                                val_str = str_data[:nul].decode('utf-16-le', errors='replace')[:60]
                            else:
                                val_str = str_data[:64].decode('utf-16-le', errors='replace')[:60]
                        except:
                            val_str = f"ptr={ptr:#x}"
                    else:
                        val_str = f"ptr={ptr:#x} (unreadable)"
                else:
                    # Try inline UTF-16LE
                    try:
                        nul = raw.find(b'\x00\x00')
                        if nul > 0:
                            val_str = raw[:nul].decode('utf-16-le', errors='replace')[:30]
                        else:
                            val_str = f"raw={raw[:8].hex()}"
                    except:
                        val_str = f"raw={raw[:8].hex()}"

            elif type_name in ('i64', 'double'):
                # Could be i64 or double
                i64 = struct.unpack_from('<q', raw, 0)[0]
                dbl = struct.unpack_from('<d', raw, 0)[0]
                i32 = struct.unpack_from('<i', raw, 0)[0]
                if abs(dbl) < 1e15 and dbl != 0:
                    val_str = f"i64={i64} dbl={dbl:.2f} (cents={dbl/100:.2f})"
                else:
                    val_str = f"i64={i64} i32={i32}"

            elif type_name == 'bool':
                b = struct.unpack_from('<I', raw, 0)[0]
                val_str = f"bool={b} ({raw[:4].hex()})"

            elif type_name == 'i16':
                v = struct.unpack_from('<h', raw, 0)[0]
                val_str = f"i16={v}"

            elif type_name == 'i32':
                v = struct.unpack_from('<i', raw, 0)[0]
                val_str = f"i32={v}"

            elif type_name == 'date32':
                v = struct.unpack_from('<I', raw, 0)[0]
                val_str = f"date32={v:#010x}"

            else:
                val_str = f"raw={raw[:8].hex()}"

        print(f"  {i:3d} {blk_off:#08x} {fid:#012x} {data_off:#08x} {name:<35s} {type_name:<8s} {storage:<10s} {val_str}")

    # Verify: check if our probe values appear at the predicted offsets
    print(f"\n{'='*100}")
    print(f"PROBE VALUE VERIFICATION")
    print(f"{'='*100}")

    text1_in_block = block.find(text1_pattern)
    text2_in_block = block.find(text2_pattern)

    if text1_in_block >= 0:
        predicted_off = None
        for _, fid, data_off in idx_table:
            if fid == 0x0b400033:  # Text1
                predicted_off = base_in_block + data_off
                break
        if predicted_off is not None:
            # The offset points to a pointer to the string
            ptr_data = block[predicted_off:predicted_off + 8]
            ptr = struct.unpack_from('<Q', ptr_data, 0)[0]
            print(f"  Text1: index says offset {data_off:#06x} -> abs ptr at block+{predicted_off:#06x}")
            print(f"         pointer value: {ptr:#018x}")
            print(f"         actual string at block+{text1_in_block:#06x} (abs {block_start + text1_in_block:#x})")
            if ptr == block_start + text1_in_block:
                print(f"         ** POINTER MATCHES! Text stored via pointer, not inline.")
            else:
                print(f"         mismatch: ptr={ptr:#x}, actual={block_start + text1_in_block:#x}")
                print(f"         delta: {(block_start + text1_in_block) - ptr:+d}")

    # Dump a hex region around the data area
    print(f"\n{'='*100}")
    print(f"DATA AREA HEX DUMP (first 0x200 bytes from TASKNODE base)")
    print(f"{'='*100}")

    dump_start = base_in_block
    dump_end = min(len(block), base_in_block + 0x200)
    if dump_start >= 0:
        for off in range(dump_start, dump_end, 16):
            chunk = block[off:off + 16]
            hex_str = ' '.join(f'{b:02x}' for b in chunk)
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
            rel = off - base_in_block
            print(f"  base+{rel:04x}: {hex_str:<48s} {ascii_str}")

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
