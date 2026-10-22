#!/usr/bin/env python3
"""Examine the TASKNODE containing the Name string.

Found Name at 0x23b6384bda8 inside a TASKNODE record.
Need to find the TASKNODE header (size, count) and index entries
to understand how text fields are stored and indexed.

TASKNODE format: header[size:u32, count:u32] + N×8 index[field_id:u32, offset:u32] + packed data
"""

import ctypes
import struct
import sys
import time

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


def try_utf16(data, max_chars=100):
    """Try to decode as null-terminated UTF-16LE."""
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

    # Set distinctive values for all text fields
    task.Name = "XYZZY_NAME_42"
    task.WBS = "WBS_777"
    task.Contact = "CONTACT_XYZ"
    time.sleep(0.3)

    # Known: Name string found inline at 0x23b6384bda8
    name_addr = 0x23b6384bda8
    prio_addr = 0x23b7ef6dd3c

    # Read a large chunk BEFORE the name string to find the TASKNODE header
    # TASKNODE header should be: [size:u32][count:u32] then count×8 index entries
    # The name is somewhere in the packed data section
    # Read 2 KB before the name to find the header
    read_start = name_addr - 2048
    chunk = read_mem(handle, read_start, 4096)
    if not chunk:
        print("ERROR: Can't read")
        CloseHandle(handle)
        return

    name_offset_in_chunk = 2048

    # Strategy: scan backwards for the TASKNODE header
    # The header has [size][count] where size matches the total record size
    # and count is reasonable (< 200 for index entries)

    print("=== Scanning for TASKNODE header ===")

    # Try to find the header by looking for (size, count) pairs
    # where count * 8 + 8 + data_size ≈ size
    for scan_off in range(0, 2000, 4):
        size_val = struct.unpack_from('<I', chunk, scan_off)[0]
        count_val = struct.unpack_from('<I', chunk, scan_off + 4)[0]

        if 100 < size_val < 0x10000 and 2 < count_val < 200:
            # Check: header(8) + index(count*8) + data fits in size
            idx_end = scan_off + 8 + count_val * 8
            if idx_end > len(chunk): continue

            # The name string should be within the data section
            record_end = scan_off + size_val
            name_rel = name_offset_in_chunk - scan_off

            if 0 < name_rel < size_val:
                # Verify: check if the index entries look like field_id, offset pairs
                valid_index = True
                for e in range(min(count_val, 5)):
                    fid = struct.unpack_from('<I', chunk, scan_off + 8 + e * 8)[0]
                    foff = struct.unpack_from('<I', chunk, scan_off + 8 + e * 8 + 4)[0]
                    # field_id should be in 0x0b40XXXX range for task fields
                    if (fid >> 16) != 0x0b40 and fid != 0:
                        valid_index = False
                        break
                    if foff > size_val:
                        valid_index = False
                        break

                if valid_index:
                    abs_addr = read_start + scan_off
                    print(f"\n*** CANDIDATE HEADER at 0x{abs_addr:012x} (name at +{name_rel}) ***")
                    print(f"  size={size_val} (0x{size_val:x}), count={count_val}")

                    # Print first few index entries
                    print(f"\n  Index entries:")
                    for e in range(min(count_val, 30)):
                        fid = struct.unpack_from('<I', chunk, scan_off + 8 + e * 8)[0]
                        foff = struct.unpack_from('<I', chunk, scan_off + 8 + e * 8 + 4)[0]
                        lower = fid & 0xFFFF
                        print(f"    [{e:3d}] field_id=0x{fid:08x} (lower=0x{lower:04x}={lower}) offset=0x{foff:04x}")

                    if count_val > 30:
                        print(f"    ... {count_val - 30} more entries")

                    # Find which index entry points to (or near) the name string
                    data_start = scan_off + 8 + count_val * 8
                    name_data_offset = name_offset_in_chunk - data_start

                    print(f"\n  Data section starts at record+0x{8 + count_val * 8:x}")
                    print(f"  Name string at data_offset=0x{name_data_offset:x}")

                    # Look for index entry pointing to name_data_offset
                    for e in range(count_val):
                        fid = struct.unpack_from('<I', chunk, scan_off + 8 + e * 8)[0]
                        foff = struct.unpack_from('<I', chunk, scan_off + 8 + e * 8 + 4)[0]
                        if abs(foff - name_data_offset) < 4:
                            lower = fid & 0xFFFF
                            print(f"  ==> Index entry {e}: field_id=0x{fid:08x} (0x{lower:04x}) -> offset 0x{foff:x} MATCHES NAME!")

                    # Also scan data for WBS and Contact strings
                    for search_str in ["WBS_777", "CONTACT_XYZ"]:
                        needle = search_str.encode('utf-16-le')
                        idx = chunk.find(needle, data_start, scan_off + size_val)
                        if idx >= 0:
                            str_data_off = idx - data_start
                            print(f"  '{search_str}' at data_offset=0x{str_data_off:x}")
                            # Find matching index entry
                            for e2 in range(count_val):
                                fid2 = struct.unpack_from('<I', chunk, scan_off + 8 + e2 * 8)[0]
                                foff2 = struct.unpack_from('<I', chunk, scan_off + 8 + e2 * 8 + 4)[0]
                                if abs(foff2 - str_data_off) < 4:
                                    lower2 = fid2 & 0xFFFF
                                    print(f"    ==> Index entry {e2}: field_id=0x{fid2:08x} (0x{lower2:04x})")

                    # Show the data around the name string
                    print(f"\n  Data around name string:")
                    for off in range(max(0, name_offset_in_chunk - 64), min(len(chunk), name_offset_in_chunk + 128), 16):
                        line_data = chunk[off:off+16]
                        hex_str = ' '.join(f'{b:02x}' for b in line_data)
                        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in line_data)
                        rel = off - scan_off
                        marker = " <<<NAME" if off <= name_offset_in_chunk < off + 16 else ""
                        print(f"    rec+0x{rel:04x}: {hex_str:<48s} |{ascii_str}|{marker}")

                    break  # Found it

    # Also look at the counted string at 0x23b7291cad4 (from earlier)
    # This might be a different string storage mechanism
    counted_addr = 0x23b7291cad4
    cdata = read_mem(handle, counted_addr - 48, 128)
    if cdata:
        print(f"\n=== Counted string at 0x{counted_addr:012x} ===")
        for off in range(0, min(128, len(cdata)), 16):
            line_data = cdata[off:off+16]
            hex_str = ' '.join(f'{b:02x}' for b in line_data)
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in line_data)
            abs_a = counted_addr - 48 + off
            marker = " <<<STRING" if off <= 48 < off + 16 else ""
            print(f"  0x{abs_a:012x}: {hex_str:<48s} |{ascii_str}|{marker}")

        # Parse the header
        hash_val = struct.unpack_from('<I', cdata, 32)[0]
        char_count = struct.unpack_from('<I', cdata, 36)[0]
        byte_len = struct.unpack_from('<I', cdata, 40)[0]
        char_count2 = struct.unpack_from('<I', cdata, 44)[0]
        print(f"  hash=0x{hash_val:08x}, char_count={char_count}, byte_len={byte_len}, char_count2={char_count2}")

        # The string is at cdata+48
        s = try_utf16(cdata[48:])
        if s:
            print(f"  String: \"{s}\"")

    # Reset
    task.Name = "Task 1"
    task.WBS = ""
    task.Contact = ""
    time.sleep(0.3)
    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
