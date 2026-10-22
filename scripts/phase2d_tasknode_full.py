#!/usr/bin/env python3
"""Dump all 92 index entries from the text TASKNODE and decode their data types.

TASKNODE header at 0x23b6384bad0:
  size=1490, count=92
  Data section at record+0x2E8 (= 8 + 92*8)
"""

import ctypes
import struct
import sys
import time
import csv

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

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
    if not ok: return None
    return buf.raw[:n.value]

def try_utf16(data, max_chars=200):
    try:
        for i in range(0, min(max_chars*2, len(data))-1, 2):
            if data[i] == 0 and data[i+1] == 0:
                if i == 0: return ""
                s = data[:i].decode('utf-16-le')
                if all(c.isprintable() or c in ' \t\n\r' for c in s):
                    return s
                return None
        return None
    except: return None

# Load field master to resolve field IDs
def load_field_names():
    """Map lower field_id (0x0b40XXXX & 0xFFFF) to COM name."""
    names = {}
    try:
        with open('analysis/field_master_decoded.tsv', 'r') as f:
            reader = csv.DictReader(f, delimiter='\t')
            for row in reader:
                if row['record_type'] == 'Task':
                    fid_hex = row.get('field_id', '')
                    if fid_hex:
                        try:
                            fid = int(fid_hex, 16) if fid_hex.startswith('0x') else int(fid_hex)
                            lower = fid & 0xFFFF
                            name = row.get('com_name', '') or f"unnamed_{row.get('cache_idx','?')}"
                            sc = row.get('storage_class', '?')
                            names[lower] = (name, sc)
                        except: pass
    except Exception as e:
        print(f"Warning: couldn't load field names: {e}")
    return names


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    task.Name = "XYZZY_NAME_42"
    task.WBS = "WBS_777"
    task.Contact = "CONTACT_XYZ"
    time.sleep(0.3)

    names = load_field_names()

    tn_addr = 0x23b6384bad0
    tn_size = 1490
    tn_count = 92

    # Read entire TASKNODE
    data = read_mem(handle, tn_addr, tn_size)
    if not data:
        print("ERROR: Can't read TASKNODE")
        CloseHandle(handle)
        return

    # Verify header
    size_check = struct.unpack_from('<I', data, 0)[0]
    count_check = struct.unpack_from('<I', data, 4)[0]
    print(f"TASKNODE at 0x{tn_addr:012x}: size={size_check}, count={count_check}")

    if size_check != tn_size or count_check != tn_count:
        print(f"WARNING: header mismatch! Expected size={tn_size}, count={tn_count}")

    data_start = 8 + tn_count * 8

    # Parse all index entries
    print(f"\n{'idx':>4s} {'field_id':>10s} {'lower':>6s} {'offset':>7s} {'size':>5s} {'name':30s} {'SC':>3s} {'data_preview'}")
    print("-" * 110)

    entries = []
    for i in range(tn_count):
        fid = struct.unpack_from('<I', data, 8 + i * 8)[0]
        foff = struct.unpack_from('<I', data, 8 + i * 8 + 4)[0]
        entries.append((i, fid, foff))

    for idx, (i, fid, foff) in enumerate(entries):
        lower = fid & 0xFFFF
        name_str, sc = names.get(lower, (f"???_{lower}", "?"))

        # Calculate data size (diff to next entry's offset, or to end)
        if idx + 1 < len(entries):
            next_off = entries[idx + 1][2]
            data_size = next_off - foff
        else:
            data_size = tn_size - foff

        # Read data at offset
        preview = ""
        if foff < tn_size:
            field_data = data[foff:foff + min(data_size, 64)]
            if len(field_data) >= 4:
                first_u32 = struct.unpack_from('<I', field_data, 0)[0]
                if first_u32 == 0xFFFFFFFF:
                    preview = "EMPTY (0xFFFFFFFF)"
                elif data_size >= 4 and data_size <= 8:
                    # Likely a small numeric value
                    if data_size == 4:
                        preview = f"u32=0x{first_u32:08x} ({first_u32})"
                    elif data_size == 8:
                        val64 = struct.unpack_from('<Q', field_data, 0)[0]
                        dbl = struct.unpack_from('<d', field_data, 0)[0]
                        preview = f"u64=0x{val64:016x} (dbl={dbl:.2f})"
                else:
                    # Try as string
                    s = try_utf16(field_data)
                    if s is not None:
                        if len(s) > 30:
                            s = s[:30] + "..."
                        preview = f'"{s}"'
                    else:
                        # Just show hex
                        preview = ' '.join(f'{b:02x}' for b in field_data[:16])

        print(f"{i:4d} 0x{fid:08x} 0x{lower:04x} 0x{foff:05x} {data_size:5d} {name_str:30s} {sc:>3s} {preview}")

    # Summary: count by storage class
    sc_counts = {}
    for i, fid, foff in entries:
        lower = fid & 0xFFFF
        _, sc = names.get(lower, ('?', '?'))
        sc_counts[sc] = sc_counts.get(sc, 0) + 1
    print(f"\nStorage class distribution in TASKNODE:")
    for sc, cnt in sorted(sc_counts.items()):
        print(f"  SC{sc}: {cnt} fields")

    # Reset
    task.Name = "Task 1"
    task.WBS = ""
    task.Contact = ""
    time.sleep(0.3)
    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
