#!/usr/bin/env python3
"""Dump the full task record at the two candidate addresses.

Known record offsets from getter2 disassembly:
  +0x730: ConstraintType raw (i16)
  +0x740: Duration raw (i32, ×10 = COM value)
  +0x74a: DurationUnits raw (i16)
  +0x7c4: Created date (date32)
  +0x1a0: used by Start getter (complex)
  +0x638: used by Start getter (LEA offset in complex code)

Also check: vtable at +0, pointers, structure.
"""

import ctypes
import struct
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
    if not hProcess: return None
    hMods = (ctypes.c_void_p * 1024)()
    cbNeeded = ctypes.c_ulong()
    if not psapi.EnumProcessModulesEx(hProcess, ctypes.byref(hMods), ctypes.sizeof(hMods),
                                       ctypes.byref(cbNeeded), 0x03):
        CloseHandle(hProcess); return None
    base = hMods[0]
    CloseHandle(hProcess)
    return base

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")

    # The two candidate record bases
    candidates = [0x23b75edc834, 0x23b75edd644]

    for idx, record_base in enumerate(candidates):
        # Read the full record (0xA00 bytes should be enough to cover all byte_offsets up to 0x910+)
        data = read_mem(handle, record_base, 0xA00)
        if not data or len(data) < 0xA00:
            print(f"\nCandidate {idx}: FAILED to read at {record_base:#x}")
            continue

        print(f"\n{'='*100}")
        print(f"CANDIDATE {idx}: record_base = {record_base:#x}")
        print(f"{'='*100}")

        # Check vtable at +0
        vtable_ptr = struct.unpack_from('<Q', data, 0)[0]
        is_vtable = IMAGE_BASE < vtable_ptr < IMAGE_BASE + 0x2000000
        if is_vtable:
            vtable_rva = vtable_ptr - IMAGE_BASE
            print(f"  +0x000: VTABLE ptr = {vtable_ptr:#x} (RVA {vtable_rva:#x})")
        else:
            print(f"  +0x000: {vtable_ptr:#x} (not a vtable)")

        # All known byte_offsets
        print(f"\n  GETTER BYTE_OFFSET VALUES:")
        offsets = [
            (0x1a0, 'Start area'),
            (0x730, 'ConstraintType'),
            (0x740, 'Duration'),
            (0x74a, 'DurationUnits'),
            (0x7c4, 'Created'),
            (0x910, 'Priority/vtable_slot'),
        ]
        for off, label in offsets:
            i32 = struct.unpack_from('<i', data, off)[0]
            u32 = struct.unpack_from('<I', data, off)[0]
            i16 = struct.unpack_from('<h', data, off)[0]
            u64 = struct.unpack_from('<Q', data, off)[0]
            print(f"  +{off:#06x} ({label:>20s}): i32={i32:10d}  u32={u32:#010x}  i16={i16:6d}  u64={u64:#018x}")

        # Duration should be 2400 × 10 = 24000 (5 days)
        dur_raw = struct.unpack_from('<i', data, 0x740)[0]
        print(f"\n  Duration: raw={dur_raw}, ×10={dur_raw*10}, ÷480={dur_raw/480:.1f} days")

        # Scan the entire record for date32 values (0x3d1xxxxx range)
        print(f"\n  DATE VALUES in record:")
        for off in range(0, len(data) - 4, 4):
            val = struct.unpack_from('<I', data, off)[0]
            if 0x3d100000 <= val <= 0x3d200000:
                print(f"    +{off:#06x}: {val:#010x}")

        # Scan for pointers to code/vtables
        print(f"\n  CODE POINTERS in record:")
        ptr_count = 0
        for off in range(0, len(data) - 8, 8):
            val = struct.unpack_from('<Q', data, off)[0]
            if IMAGE_BASE < val < IMAGE_BASE + 0x2000000:
                rva = val - IMAGE_BASE
                print(f"    +{off:#06x}: {val:#x} (RVA {rva:#x})")
                ptr_count += 1
                if ptr_count > 20:
                    print(f"    ... (truncated)")
                    break

        # Scan for heap pointers (high address range like 0x23bxxxxxxxx)
        print(f"\n  HEAP POINTERS in record:")
        heap_count = 0
        for off in range(0, len(data) - 8, 8):
            val = struct.unpack_from('<Q', data, off)[0]
            if 0x200000000000 < val < 0x800000000000 and val != struct.unpack_from('<Q', data, 0)[0]:
                print(f"    +{off:#06x}: {val:#x}")
                heap_count += 1
                if heap_count > 20:
                    print(f"    ... (truncated)")
                    break

        # Full hex dump of key areas
        areas = [
            (0x000, 0x020, "Record header"),
            (0x190, 0x1C0, "Start area (0x1a0)"),
            (0x620, 0x650, "Start LEA area (0x638)"),
            (0x720, 0x760, "Duration/ConstraintType area"),
            (0x7B0, 0x7E0, "Created area"),
            (0x900, 0x930, "0x910 area"),
        ]
        for start, end, label in areas:
            print(f"\n  HEX: {label} (+{start:#x} to +{end:#x}):")
            for j in range(start, min(end, len(data)), 16):
                c = data[j:j+16]
                hex_str = ' '.join(f'{b:02x}' for b in c)
                # Annotate values
                i32_vals = []
                for k in range(0, min(16, len(c)), 4):
                    v = struct.unpack_from('<i', c, k)[0]
                    i32_vals.append(str(v))
                print(f"    +{j:#06x}: {hex_str}  i32=[{', '.join(i32_vals)}]")

    # Also check what's at the Priority address we found before
    prio_addr = 0x23b7ef6dd3c
    print(f"\n{'='*100}")
    print(f"COMPARISON: Previously found Priority area at {prio_addr:#x}")
    print(f"{'='*100}")

    # What's the distance from each candidate?
    for idx, rb in enumerate(candidates):
        diff = prio_addr - rb
        print(f"  Candidate {idx}: prio_addr - record_base = {diff:#x} ({diff} bytes)")

    # Is the Priority address near these record bases?
    # For candidate 0: diff = 0x23b7ef6dd3c - 0x23b75edc834 = 0x9010508
    # That's ~150 MB apart — different allocations entirely

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
