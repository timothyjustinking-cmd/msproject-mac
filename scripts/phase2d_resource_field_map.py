#!/usr/bin/env python3
"""Extract resource field map using the byte-offset formula.

Same approach as task field mapping: read resource descriptor cache entries,
apply byte_off_raw/32 for dispatch_type=1 fields.
"""

import ctypes
import ctypes.wintypes as wt
import struct
import csv
import sys
from pathlib import Path
from collections import Counter, defaultdict

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

BASE = Path(__file__).resolve().parents[1]
ANALYSIS = BASE / "analysis"

REAL_STRIDE = 0xA8
PROPINFO_ENTRY_SIZE = 0x40
PROPINFO_VT_RVA = 0x1050670
CUSTOM_VT_RVA = 0x0fb4a80

# Resource cache - need to find the right RVA
# From field_metadata files: task cache at 0x130be10
# Resource cache should be nearby
# Let's check the known cache RVAs from the descriptor table layout
RESOURCE_CACHE_RVA = 0x12e0d30  # resource cache BEFORE task cache
RESOURCE_COUNT = 883  # from scan

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

TYPE_NAMES = {
    2: 'i16', 3: 'u32/dur', 5: 'Work', 8: 'String', 9: 'Cost',
    11: 'bool', 19: 'date', 29: 'type29',
}

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
        return None
    return buf.raw[:n.value]

def load_field_id_map():
    fmap = {}
    path = ANALYSIS / "field_id_map.tsv"
    if not path.exists():
        return fmap
    with open(path, 'r') as f:
        for row in csv.DictReader(f, delimiter='\t'):
            fid_str = row['field_id']
            fid = int(fid_str, 16) if fid_str.startswith('0x') else int(fid_str)
            prop = row['property_name']
            disp = row.get('dispatcher', '')
            if disp == 'get' or fid not in fmap:
                fmap[fid] = prop
    return fmap


def find_resource_cache(handle, mod_base, propinfo_vt):
    """Search for the resource cache by scanning for propinfo vtable pointers."""
    # Resource field_ids start with 0x0c40xxxx
    # The cache should be at a fixed RVA. Let's scan a few candidates.

    candidates = [
        0x130c270,  # expected: after task cache
        0x130c2d0,
        0x130c330,
        0x130c010,
        0x130be10 + 1444 * 0xA8,  # right after task cache
    ]

    # Also scan a region after the task cache
    task_end = 0x130be10 + 1444 * REAL_STRIDE

    for rva in candidates:
        addr = mod_base + rva
        data = rpm(handle, addr, 16)
        if data:
            vt = struct.unpack_from('<Q', data, 0)[0]
            fid = struct.unpack_from('<I', data, 8)[0]
            if vt == propinfo_vt and (fid & 0xFF000000) == 0x0c000000:
                return rva

    # Scan from after task cache
    print(f"Scanning from RVA 0x{task_end:x} for resource cache...")
    scan_size = 0x10000
    scan_data = rpm(handle, mod_base + task_end, scan_size)
    if scan_data:
        for off in range(0, scan_size - 16, 8):
            vt = struct.unpack_from('<Q', scan_data, off)[0]
            if vt == propinfo_vt:
                fid = struct.unpack_from('<I', scan_data, off + 8)[0]
                if (fid & 0xFF000000) == 0x0c000000:
                    rva = task_end + off
                    # Verify a few more entries at 0xA8 stride
                    ok = True
                    for i in range(1, 5):
                        entry_off = off + i * REAL_STRIDE
                        if entry_off + 16 > len(scan_data):
                            ok = False
                            break
                        vt2 = struct.unpack_from('<Q', scan_data, entry_off)[0]
                        if vt2 != propinfo_vt:
                            ok = False
                            break
                    if ok:
                        print(f"  Found resource cache at RVA 0x{rva:x}, first field_id=0x{fid:08x}")
                        return rva

    return None


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

    field_map = load_field_id_map()
    propinfo_vt = mod_base + PROPINFO_VT_RVA
    custom_vt = mod_base + CUSTOM_VT_RVA

    resource_rva = RESOURCE_CACHE_RVA
    print(f"Resource cache at RVA 0x{resource_rva:x}")
    cache_addr = mod_base + resource_rva

    # Count entries by scanning until we hit non-propinfo vtable
    max_entries = 2000
    read_size = max_entries * REAL_STRIDE + PROPINFO_ENTRY_SIZE
    cache_data = rpm(handle, cache_addr, read_size)
    if not cache_data:
        print("ERROR: Could not read resource cache"); sys.exit(1)

    entry_count = 0
    for i in range(max_entries):
        off = i * REAL_STRIDE
        if off + 16 > len(cache_data):
            break
        vt = struct.unpack_from('<Q', cache_data, off)[0]
        if vt != propinfo_vt and vt != custom_vt:
            break
        entry_count += 1

    print(f"Resource cache: {entry_count} entries")

    # Extract all entries
    entries = []
    for i in range(entry_count):
        off = i * REAL_STRIDE
        vt = struct.unpack_from('<Q', cache_data, off)[0]
        fid = struct.unpack_from('<I', cache_data, off + 8)[0]
        tc = struct.unpack_from('<I', cache_data, off + 12)[0]
        rec_off = struct.unpack_from('<I', cache_data, off + 0x18)[0]
        flags = struct.unpack_from('<I', cache_data, off + 0x1C)[0]
        size = struct.unpack_from('<I', cache_data, off + 0x30)[0]
        byte_off_raw = struct.unpack_from('<H', cache_data, off + 0x40)[0]
        dispatch_type = struct.unpack_from('<I', cache_data, off + 0x48)[0]
        class_slot = struct.unpack_from('<I', cache_data, off + 0x4C)[0]

        name = field_map.get(fid, '')

        byte_pos = byte_off_raw / 32.0 if dispatch_type == 1 else None
        entries.append({
            'index': i,
            'field_id': fid,
            'name': name,
            'type_code': tc,
            'rec_offset': rec_off,
            'flags': flags,
            'size': size,
            'byte_off_raw': byte_off_raw,
            'dispatch_type': dispatch_type,
            'class_slot': class_slot,
            'byte_pos': byte_pos,
        })

    # Count dispatch types
    dt_counts = Counter(e['dispatch_type'] for e in entries)
    print(f"Dispatch types: {dict(dt_counts.most_common())}")

    # Field ID range check
    fid_ranges = Counter()
    for e in entries:
        prefix = (e['field_id'] >> 24) & 0xFF
        fid_ranges[prefix] += 1
    print(f"Field ID prefixes: {dict(fid_ranges.most_common())}")

    # Show SC1 (dispatch_type=1) fields
    main_entries = sorted(
        [e for e in entries if e['dispatch_type'] == 1],
        key=lambda e: e['byte_pos'] if e['byte_pos'] is not None else 9999
    )
    print(f"\nSC1 fields (dispatch_type=1): {len(main_entries)}")

    if main_entries:
        min_pos = min(e['byte_pos'] for e in main_entries if e['byte_pos'] is not None)
        max_pos = max(e['byte_pos'] for e in main_entries if e['byte_pos'] is not None)
        print(f"Byte range: {min_pos:.0f} to {max_pos:.0f}")

        print(f"\n{'BytePos':>7s} {'Type':>5s} {'Size':>4s} {'Slot':>4s} "
              f"{'RawOff':>6s} {'FieldID':>12s} {'Name'}")
        print("-" * 90)

        for e in main_entries:
            bp = f"{e['byte_pos']:7.1f}" if e['byte_pos'] is not None else "   ???"
            tn = TYPE_NAMES.get(e['type_code'], str(e['type_code']))
            print(f"{bp} {tn:>5s} {e['size']:>4d} {e['class_slot']:>4d} "
                  f"{e['byte_off_raw']:>6d} 0x{e['field_id']:08x} {e['name']}")

    # Show alternative dispatch summary
    alt_entries = [e for e in entries if e['dispatch_type'] != 1]
    if alt_entries:
        print(f"\n{'='*80}")
        print(f"ALTERNATIVE DISPATCH TYPES")
        print(f"{'='*80}\n")
        alt_by_dt = defaultdict(list)
        for e in alt_entries:
            alt_by_dt[e['dispatch_type']].append(e)

        for dt in sorted(alt_by_dt.keys()):
            elist = alt_by_dt[dt]
            print(f"  Dispatch type {dt}: {len(elist)} fields")
            for e in elist[:10]:
                tn = TYPE_NAMES.get(e['type_code'], str(e['type_code']))
                print(f"    [{e['index']:4d}] 0x{e['field_id']:08x} {e['name']:30s} "
                      f"tc={tn:>5s} slot={e['class_slot']:3d} raw={e['byte_off_raw']:#06x}")
            if len(elist) > 10:
                print(f"    ... and {len(elist)-10} more")

    # Write TSV output
    out_path = ANALYSIS / "sc1_resource_field_map.tsv"
    with open(out_path, 'w', newline='') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['byte_pos', 'type_code', 'type_name', 'size',
                     'class_slot', 'field_id_hex', 'field_name', 'dispatch_type',
                     'rec_offset', 'flags_hex', 'byte_off_raw'])
        for e in main_entries:
            w.writerow([
                f"{e['byte_pos']:.0f}" if e['byte_pos'] is not None else '',
                e['type_code'],
                TYPE_NAMES.get(e['type_code'], ''),
                e['size'],
                e['class_slot'],
                f"0x{e['field_id']:08x}",
                e['name'],
                e['dispatch_type'],
                e['rec_offset'],
                f"0x{e['flags']:08x}",
                e['byte_off_raw'],
            ])
    print(f"\nWrote {len(main_entries)} SC1 entries to {out_path}")

    kernel32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
