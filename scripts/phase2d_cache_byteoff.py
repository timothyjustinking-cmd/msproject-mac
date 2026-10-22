#!/usr/bin/env python3
"""Dump full 0xA8-byte cache entries for fields with known byte offsets.

Compare the raw bytes to find which field in the cache entry stores the
byte offset that maps to the flat buffer position.
"""

import ctypes
import ctypes.wintypes as wt
import struct
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

REAL_STRIDE = 0xA8  # 168 bytes

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)

class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [
        ('dwSize', wt.DWORD), ('cntUsage', wt.DWORD),
        ('th32ProcessID', wt.DWORD),
        ('th32DefaultHeapID', ctypes.POINTER(ctypes.c_ulong)),
        ('th32ModuleID', wt.DWORD), ('cntThreads', wt.DWORD),
        ('th32ParentProcessID', wt.DWORD),
        ('pcPriClassBase', ctypes.c_long), ('dwFlags', wt.DWORD),
        ('szExeFile', ctypes.c_char * 260),
    ]

class MODULEENTRY32(ctypes.Structure):
    _fields_ = [
        ('dwSize', wt.DWORD), ('th32ModuleID', wt.DWORD),
        ('th32ProcessID', wt.DWORD), ('GlblcntUsage', wt.DWORD),
        ('ProccntUsage', wt.DWORD),
        ('modBaseAddr', ctypes.POINTER(ctypes.c_byte)),
        ('modBaseSize', wt.DWORD), ('hModule', wt.HMODULE),
        ('szModule', ctypes.c_char * 256),
        ('szExePath', ctypes.c_char * 260),
    ]

def find_pid():
    snap = kernel32.CreateToolhelp32Snapshot(0x2, 0)
    pe = PROCESSENTRY32()
    pe.dwSize = ctypes.sizeof(pe)
    pids = []
    if kernel32.Process32First(snap, ctypes.byref(pe)):
        while True:
            if b'WINPROJ' in pe.szExeFile.upper():
                pids.append(pe.th32ProcessID)
            if not kernel32.Process32Next(snap, ctypes.byref(pe)):
                break
    kernel32.CloseHandle(snap)
    return pids

def get_base(pid):
    snap = kernel32.CreateToolhelp32Snapshot(0x8 | 0x10, pid)
    me = MODULEENTRY32()
    me.dwSize = ctypes.sizeof(me)
    base = None
    if kernel32.Module32First(snap, ctypes.byref(me)):
        while True:
            if b'WINPROJ' in me.szModule.upper():
                base = ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value
                break
            if not kernel32.Module32Next(snap, ctypes.byref(me)):
                break
    kernel32.CloseHandle(snap)
    return base

def rpm(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = kernel32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok:
        raise OSError(f"ReadProcessMemory failed at 0x{addr:x}")
    return buf.raw[:n.value]


def main():
    pids = find_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    pid = pids[0]
    mod_base = get_base(pid)
    print(f"Module base: 0x{mod_base:x}")

    handle = kernel32.OpenProcess(0x0010 | 0x0400, False, pid)
    if not handle:
        print("ERROR: OpenProcess failed"); sys.exit(1)

    # Task cache at RVA 0x130be10, 1444 entries
    cache_rva = 0x130be10
    cache_addr = mod_base + cache_rva

    # Fields with known byte offsets (index in cache, field name, prio_offset)
    # Index from field_metadata_task.tsv
    known_fields = [
        (25, 'Priority',         0,    'u16', 2),    # rec_off=2
        (17, 'ConstraintType',  -104,  'u16', 3),    # rec_off=3
        (32, 'PercentComplete',  2,    'u16', 5),    # rec_off=5
        (200,'FixedCostAccrual', -4,   'u16', 13),   # rec_off=13
        (85, 'OutlineLevel',    -216,  'u16', 20),   # rec_off=20
        (128,'Type',             6,    'u16', 29),   # rec_off=29
        (29, 'Duration',        -196,  'u32', 6),    # rec_off=6
        (28, 'ActualDuration',  -188,  'u32', 6),    # rec_off=6
        (31, 'RemainingDuration',-184, 'u32', 6),    # rec_off=6
        (20, 'LevelingDelay',   -180,  'u32', 28),   # rec_off=28
        (21, 'FreeSlack',       -96,   'u32', 6),    # rec_off=6
        (22, 'TotalSlack',      -100,  'u32', 6),    # rec_off=6
    ]

    print(f"\n{'='*80}")
    print("Full 0xA8 cache entry dumps for fields with known byte offsets")
    print(f"{'='*80}\n")

    for idx, name, prio_off, dtype, rec_off in known_fields:
        entry_addr = cache_addr + idx * REAL_STRIDE
        try:
            data = rpm(handle, entry_addr, REAL_STRIDE)
        except OSError as e:
            print(f"ERROR reading {name}: {e}")
            continue

        # Verify field_id and type_code
        vt = struct.unpack_from('<Q', data, 0)[0]
        fid = struct.unpack_from('<I', data, 8)[0]
        tc = struct.unpack_from('<I', data, 12)[0]
        ro = struct.unpack_from('<I', data, 0x18)[0]
        flags = struct.unpack_from('<I', data, 0x1C)[0]
        size = struct.unpack_from('<I', data, 0x30)[0]

        print(f"--- {name} (index={idx}, prio_off={prio_off:+d}, rec_off={rec_off}) ---")
        print(f"  field_id=0x{fid:08x} type_code={tc} rec_offset={ro} "
              f"flags=0x{flags:08x} size={size}")

        # Hex dump of full entry
        print(f"  Full 0xA8 hex dump:")
        for off in range(0, REAL_STRIDE, 16):
            hex_bytes = ' '.join(f'{data[off+j]:02x}' for j in range(min(16, REAL_STRIDE - off)))
            print(f"    +{off:03x}: {hex_bytes}")

        # Extract all u16 and u32 values and check if any match prio_off
        print(f"  Scanning for prio_off={prio_off:+d} or byte_offset value:")
        for off in range(0, REAL_STRIDE, 2):
            u16 = struct.unpack_from('<H', data, off)[0]
            if off + 2 < REAL_STRIDE:
                i16 = struct.unpack_from('<h', data, off)[0]
            else:
                i16 = 0
            if u16 == (prio_off & 0xFFFF) or (prio_off < 0 and i16 == prio_off):
                print(f"    +{off:03x}: u16={u16} (i16={i16}) MATCH!")
        for off in range(0, REAL_STRIDE, 4):
            u32 = struct.unpack_from('<I', data, off)[0]
            i32 = struct.unpack_from('<i', data, off)[0]
            if u32 == (prio_off & 0xFFFFFFFF) or i32 == prio_off:
                if off not in (8, 0x18, 0x1C, 0x30):  # skip known fields
                    print(f"    +{off:03x}: u32={u32} (i32={i32}) MATCH!")
        print()

    # Now look at specific offsets across all known fields
    print(f"\n{'='*80}")
    print("Cross-field comparison at specific cache entry offsets")
    print(f"{'='*80}\n")

    # Re-read all entries
    entries_data = {}
    for idx, name, prio_off, dtype, rec_off in known_fields:
        entry_addr = cache_addr + idx * REAL_STRIDE
        try:
            entries_data[name] = rpm(handle, entry_addr, REAL_STRIDE)
        except:
            pass

    # For each offset in the entry, compare values across fields
    print(f"{'Offset':>6s} | ", end='')
    for _, name, _, _, _ in known_fields[:8]:
        print(f"{name[:12]:>12s} ", end='')
    print()
    print("-" * 120)

    for off in range(0, 0x40, 4):
        print(f"+{off:04x}   | ", end='')
        for _, name, prio_off, dtype, rec_off in known_fields[:8]:
            if name in entries_data:
                val = struct.unpack_from('<I', entries_data[name], off)[0]
                print(f"  {val:10d} ", end='')
            else:
                print(f"  {'?':>10s} ", end='')
        print(f"  (prio_off: {', '.join(str(p) for _, _, p, _, _ in known_fields[:8])})")

    # Show the tail section (0x40 to 0xA8)
    print(f"\nTail section (0x40-0xA8):")
    print(f"{'Offset':>6s} | ", end='')
    for _, name, _, _, _ in known_fields[:8]:
        print(f"{name[:12]:>12s} ", end='')
    print()
    print("-" * 120)

    for off in range(0x40, REAL_STRIDE, 4):
        print(f"+{off:04x}   | ", end='')
        for _, name, prio_off, dtype, rec_off in known_fields[:8]:
            if name in entries_data:
                val = struct.unpack_from('<I', entries_data[name], off)[0]
                print(f"  {val:10d} ", end='')
            else:
                print(f"  {'?':>10s} ", end='')
        print()

    # Also show as i16 for the tail
    print(f"\nTail section as i16:")
    for off in range(0x40, REAL_STRIDE, 2):
        vals = []
        all_same = True
        first = None
        for _, name, _, _, _ in known_fields[:8]:
            if name in entries_data:
                val = struct.unpack_from('<h', entries_data[name], off)[0]
                vals.append(val)
                if first is None:
                    first = val
                elif val != first:
                    all_same = False
            else:
                vals.append(None)
        if not all_same:
            print(f"  +{off:03x}: {vals}")

    kernel32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
