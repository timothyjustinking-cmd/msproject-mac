#!/usr/bin/env python3
"""Phase 2d: Scan raw cache data for ALL PropInfoData vtable occurrences.

The cache entries might not be at 0x40-byte alignment. Scan every 8 bytes
for the vtable pattern, then determine the TRUE entry stride.
"""

import ctypes
import ctypes.wintypes as wt
import struct
import sys
from pathlib import Path

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x00000002
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010

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
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32()
    pe.dwSize = ctypes.sizeof(pe)
    pids = []
    if kernel32.Process32First(snap, ctypes.byref(pe)):
        while True:
            name = pe.szExeFile.decode('utf-8', errors='replace').lower()
            if 'winproj' in name:
                pids.append((pe.th32ProcessID, name))
            if not kernel32.Process32Next(snap, ctypes.byref(pe)):
                break
    kernel32.CloseHandle(snap)
    return pids


def get_base(pid):
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    me = MODULEENTRY32()
    me.dwSize = ctypes.sizeof(me)
    base = None
    if kernel32.Module32First(snap, ctypes.byref(me)):
        while True:
            name = me.szModule.decode('utf-8', errors='replace').lower()
            if 'winproj' in name:
                base = ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value
                break
            if not kernel32.Module32Next(snap, ctypes.byref(me)):
                break
    kernel32.CloseHandle(snap)
    return base


def rpm(handle, address, size):
    buf = ctypes.create_string_buffer(size)
    bytes_read = ctypes.c_size_t(0)
    ok = kernel32.ReadProcessMemory(
        handle, ctypes.c_void_p(address), buf, size, ctypes.byref(bytes_read))
    if not ok:
        raise OSError(f"RPM failed at 0x{address:x}: error {ctypes.get_last_error()}")
    return buf.raw[:bytes_read.value]


def main():
    print("Phase 2d: Full Vtable Pattern Scan")
    print("=" * 70)

    pids = find_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE not running")
        sys.exit(1)

    pid, _ = pids[0]
    mod_base = get_base(pid)
    print(f"Module base: 0x{mod_base:x}")

    propinfo_vt = mod_base + 0x1050670
    custom_vt = mod_base + 0x0fb4a80
    vt_bytes = struct.pack('<Q', propinfo_vt)
    custom_bytes = struct.pack('<Q', custom_vt)

    handle = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        print(f"ERROR: OpenProcess failed")
        sys.exit(1)

    try:
        # Read Task cache (1444 * 0x40 = 0x16900 bytes, but let's read more to be safe)
        cache_addr = mod_base + 0x130be10
        # Read 2x the expected size to look beyond
        read_size = 1444 * 0x40 * 2
        cache_data = rpm(handle, cache_addr, read_size)
        print(f"Read {len(cache_data)} bytes from Task cache area (0x{cache_addr:x})")

        # =====================================================================
        # 1. Find ALL PropInfoData vtable occurrences (at 8-byte alignment)
        # =====================================================================
        print(f"\n1. ALL PropInfoData VTABLE OCCURRENCES (Task cache region)")
        print("-" * 60)

        vtable_positions = []
        for off in range(0, len(cache_data) - 8, 8):
            val = struct.unpack_from('<Q', cache_data, off)[0]
            if val == propinfo_vt:
                vtable_positions.append(off)
            elif val == custom_vt:
                vtable_positions.append(off)  # mark custom too

        print(f"  Found {len(vtable_positions)} vtable occurrences")

        # Show first 30
        for i, off in enumerate(vtable_positions[:30]):
            vt = struct.unpack_from('<Q', cache_data, off)[0]
            fid = struct.unpack_from('<I', cache_data, off + 8)[0] if off + 12 <= len(cache_data) else 0
            tc = struct.unpack_from('<I', cache_data, off + 12)[0] if off + 16 <= len(cache_data) else 0
            roff = struct.unpack_from('<I', cache_data, off + 0x18)[0] if off + 0x1C <= len(cache_data) else 0
            sz = struct.unpack_from('<I', cache_data, off + 0x30)[0] if off + 0x34 <= len(cache_data) else 0

            vtype = "PropInfo" if vt == propinfo_vt else "Custom"
            print(f"  [{i:3d}] byte_offset=0x{off:06x} (entry_idx={off/0x40:.2f}) "
                  f"{vtype} fid=0x{fid:08x} type={tc} off=0x{roff:x} sz={sz}")

        if len(vtable_positions) > 30:
            print(f"  ... ({len(vtable_positions) - 30} more)")

        # =====================================================================
        # 2. Compute strides between consecutive vtable entries
        # =====================================================================
        print(f"\n{'='*70}")
        print("2. STRIDE ANALYSIS")
        print("-" * 60)

        if len(vtable_positions) > 1:
            strides = []
            for i in range(1, min(100, len(vtable_positions))):
                stride = vtable_positions[i] - vtable_positions[i-1]
                strides.append(stride)

            from collections import Counter
            stride_counts = Counter(strides)
            print(f"  Strides between consecutive vtable entries:")
            for stride, count in stride_counts.most_common(10):
                print(f"    0x{stride:x} ({stride}) bytes: {count} occurrences")

            # Most common stride
            most_common = stride_counts.most_common(1)[0][0]
            print(f"\n  Most common stride: 0x{most_common:x} ({most_common}) bytes")
            print(f"  That's {most_common / 8} QWORDs or {most_common / 0x40:.2f} 'entries'")

        # =====================================================================
        # 3. Decode entries at actual vtable positions
        # =====================================================================
        print(f"\n{'='*70}")
        print("3. DECODED ENTRIES AT TRUE POSITIONS (first 20)")
        print("-" * 60)

        TYPE_NAMES = {
            0: 'Unknown', 1: 'Currency', 2: 'Date', 3: 'Duration', 4: 'Finish',
            5: 'Work', 6: 'Number', 7: 'Text', 8: 'Boolean', 9: 'Start',
            10: 'Resource', 11: 'Cost', 12: 'Percent', 13: 'Enumerated',
            19: 'Image', 29: 'type_29',
        }

        entries = []
        for off in vtable_positions:
            if off + 0x40 > len(cache_data):
                break
            e = {
                'byte_offset': off,
                'vtable': struct.unpack_from('<Q', cache_data, off)[0],
                'field_id': struct.unpack_from('<I', cache_data, off + 8)[0],
                'type_code': struct.unpack_from('<I', cache_data, off + 12)[0],
                'rec_offset': struct.unpack_from('<I', cache_data, off + 0x18)[0],
                'flags': struct.unpack_from('<I', cache_data, off + 0x1C)[0],
                'size': struct.unpack_from('<I', cache_data, off + 0x30)[0],
            }
            entries.append(e)

        print(f"  {'#':>4} {'ByteOff':>10} {'FieldID':>12} {'Type':>10} {'Offset':>8} {'Size':>6}")
        for i, e in enumerate(entries[:20]):
            tname = TYPE_NAMES.get(e['type_code'], f"t_{e['type_code']}")
            print(f"  {i:4d} 0x{e['byte_offset']:08x} 0x{e['field_id']:08x} {tname:>10s} "
                  f"0x{e['rec_offset']:04x} {e['size']:6d}")

        # =====================================================================
        # 4. Verify field_ids are valid task IDs
        # =====================================================================
        print(f"\n{'='*70}")
        print("4. FIELD ID VALIDATION")
        print("-" * 60)

        task_prefix = 0x0b4
        valid_fids = sum(1 for e in entries if (e['field_id'] >> 20) == task_prefix)
        print(f"  Total entries found: {len(entries)}")
        print(f"  With valid Task prefix (0x0b4): {valid_fids}")

        # Check if field_ids are in sorted order
        fids = [e['field_id'] for e in entries if (e['field_id'] >> 20) == task_prefix]
        sorted_check = all(fids[i] <= fids[i+1] for i in range(len(fids)-1))
        print(f"  Field IDs in sorted order: {sorted_check}")

        if fids:
            print(f"  Range: 0x{fids[0]:08x} - 0x{fids[-1]:08x}")
            print(f"  First 20 field_ids: {['0x{:08x}'.format(f) for f in fids[:20]]}")

        # =====================================================================
        # 5. Check offsets sanity
        # =====================================================================
        print(f"\n{'='*70}")
        print("5. OFFSET SANITY CHECK")
        print("-" * 60)

        offsets = [e['rec_offset'] for e in entries if (e['field_id'] >> 20) == task_prefix]
        valid_offsets = [o for o in offsets if 0 < o < 0x10000]
        print(f"  Total offsets: {len(offsets)}")
        print(f"  In range 1-65535: {len(valid_offsets)}")
        if valid_offsets:
            print(f"  Range: 0x{min(valid_offsets):x} - 0x{max(valid_offsets):x}")
            print(f"  Max record size needed: {max(valid_offsets)} bytes")

        # Offset distribution
        offset_counts = Counter(valid_offsets)
        print(f"  Unique offsets: {len(offset_counts)}")
        print(f"  Most common: {offset_counts.most_common(10)}")

    finally:
        kernel32.CloseHandle(handle)

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
