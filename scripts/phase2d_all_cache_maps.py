#!/usr/bin/env python3
"""Extract SC1 field maps for ALL 6 descriptor caches."""

import ctypes
import ctypes.wintypes as wt
import struct
import csv
import sys
from pathlib import Path
from collections import Counter, defaultdict

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

BASE = Path(r"C:\Users\timot\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile")
ANALYSIS = BASE / "analysis"

REAL_STRIDE = 0xA8
PROPINFO_VT_RVA = 0x1050670
CUSTOM_VT_RVA = 0x0fb4a80

CACHES = [
    ('0x0a40', 'Unknown_0A', 0x1459350, 94),
    ('0x0b40', 'Task',       0x130be10, 1444),
    ('0x0c40', 'Resource',   0x12e0d30, 883),
    ('0x0d40', 'Unknown_0D', 0x13ad980, 40),
    ('0x0e40', 'Unknown_0E', 0x1378be0, 30),
    ('0x0f40', 'Assignment', 0x12c0f30, 777),
]

TYPE_NAMES = {
    2: 'i16', 3: 'u32/dur', 5: 'Work', 8: 'String', 9: 'Cost',
    11: 'bool', 19: 'date', 29: 'type29',
}

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


def main():
    pids = find_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    pid = pids[0]
    mod_base = get_base(pid)
    print(f"Module base: 0x{mod_base:x}")

    handle = kernel32.OpenProcess(0x0010 | 0x0400, False, pid)
    field_map = load_field_id_map()
    propinfo_vt = mod_base + PROPINFO_VT_RVA
    custom_vt = mod_base + CUSTOM_VT_RVA

    for prefix, name, rva, count in CACHES:
        if name == 'Task':
            continue  # Already have this

        print(f"\n{'='*90}")
        print(f"{name} CACHE (prefix {prefix}) — RVA 0x{rva:x}, {count} entries")
        print(f"{'='*90}")

        cache_addr = mod_base + rva
        read_size = count * REAL_STRIDE + 0x40
        cache_data = rpm(handle, cache_addr, read_size)
        if not cache_data:
            print("  ERROR: Could not read cache")
            continue

        entries = []
        for i in range(count):
            off = i * REAL_STRIDE
            if off + REAL_STRIDE > len(cache_data):
                break
            vt = struct.unpack_from('<Q', cache_data, off)[0]
            if vt != propinfo_vt and vt != custom_vt:
                break
            fid = struct.unpack_from('<I', cache_data, off + 8)[0]
            tc = struct.unpack_from('<I', cache_data, off + 12)[0]
            size = struct.unpack_from('<I', cache_data, off + 0x30)[0]
            byte_off_raw = struct.unpack_from('<H', cache_data, off + 0x40)[0]
            dispatch_type = struct.unpack_from('<I', cache_data, off + 0x48)[0]
            class_slot = struct.unpack_from('<I', cache_data, off + 0x4C)[0]
            field_name = field_map.get(fid, '')
            byte_pos = byte_off_raw / 32.0 if dispatch_type == 1 else None

            entries.append({
                'index': i, 'field_id': fid, 'name': field_name,
                'type_code': tc, 'size': size,
                'byte_off_raw': byte_off_raw, 'dispatch_type': dispatch_type,
                'class_slot': class_slot, 'byte_pos': byte_pos,
            })

        dt_counts = Counter(e['dispatch_type'] for e in entries)
        print(f"  Entries: {len(entries)}")
        print(f"  Dispatch types: {dict(dt_counts.most_common())}")

        # Show first 10 entries regardless of type (for identification)
        print(f"\n  First 10 entries:")
        for e in entries[:10]:
            tn = TYPE_NAMES.get(e['type_code'], str(e['type_code']))
            bp = f"{e['byte_pos']:.0f}" if e['byte_pos'] is not None else "n/a"
            print(f"    [{e['index']:3d}] 0x{e['field_id']:08x} {e['name']:30s} "
                  f"tc={tn:>5s} dt={e['dispatch_type']:2d} slot={e['class_slot']:3d} "
                  f"byte={bp}")

        # SC1 entries
        sc1 = sorted([e for e in entries if e['dispatch_type'] == 1],
                      key=lambda e: e['byte_pos'] if e['byte_pos'] is not None else 9999)
        if sc1:
            print(f"\n  SC1 (dispatch_type=1) fields: {len(sc1)}")
            print(f"  {'BytePos':>7s} {'Type':>5s} {'Size':>4s} {'Slot':>4s} {'FieldID':>12s} {'Name'}")
            print(f"  {'-'*80}")
            for e in sc1:
                bp = f"{e['byte_pos']:7.1f}" if e['byte_pos'] is not None else "   ???"
                tn = TYPE_NAMES.get(e['type_code'], str(e['type_code']))
                print(f"  {bp} {tn:>5s} {e['size']:>4d} {e['class_slot']:>4d} "
                      f"0x{e['field_id']:08x} {e['name']}")

            # Write TSV
            out_path = ANALYSIS / f"sc1_{name.lower()}_field_map.tsv"
            with open(out_path, 'w', newline='') as f:
                w = csv.writer(f, delimiter='\t')
                w.writerow(['byte_pos', 'type_code', 'type_name', 'size',
                             'class_slot', 'field_id_hex', 'field_name',
                             'dispatch_type', 'byte_off_raw'])
                for e in sc1:
                    w.writerow([
                        f"{e['byte_pos']:.0f}" if e['byte_pos'] is not None else '',
                        e['type_code'], TYPE_NAMES.get(e['type_code'], ''),
                        e['size'], e['class_slot'],
                        f"0x{e['field_id']:08x}", e['name'],
                        e['dispatch_type'], e['byte_off_raw'],
                    ])
            print(f"  Wrote {len(sc1)} entries to {out_path}")
        else:
            print(f"\n  No SC1 fields found")

    kernel32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
