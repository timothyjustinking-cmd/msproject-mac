#!/usr/bin/env python3
"""Phase 2d: Dump ALL runtime field caches from a running MS Project process.

Discovered descriptors and their caches:
  Task       (0x0b4): 1444 fields, cache RVA 0x130be10
  Resource   (0x0c4):  883 fields, cache RVA 0x12e0d30
  Assignment (0x0f4):  777 fields, cache RVA 0x12c0f30
  Links      (0x0e4):   30 fields, cache RVA 0x1378be0
  Calendar   (0x3c4):   14 fields, cache RVA 0x1381ae0
  Project    (0x074):    9 fields, cache RVA 0x1382aa0
  OutlineCode(0x3e4):   16 fields, cache RVA 0x1384990
  Task-34    (0x164):   34 fields, cache RVA 0x137e9a0

Cache entry layout (0x40 bytes):
  +0x00: vtable ptr (8 bytes) - polymorphic field object type
  +0x08: field_id (u32)
  +0x0C: type_code (u32) - PjFieldType
  +0x10: padding (8 bytes)
  +0x18: record_offset (u32) - byte offset into in-memory record
  +0x1C: flags (u32)
  +0x20: hash/key (8 bytes)
  +0x28: extra data (8 bytes)
  +0x30: size (u32) - field size in bytes
  +0x34: padding (12 bytes)

Uses Win32 ReadProcessMemory API via ctypes.
Run with MS Project open and a .mpp file loaded.
"""

import ctypes
import ctypes.wintypes as wt
import struct
import csv
import sys
from pathlib import Path
from collections import Counter

BASE = Path(r"C:\Users\timot\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile")
ANALYSIS = BASE / "analysis"

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x00000002
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010

ENTRY_SIZE = 0x40

# All discovered caches: (name, field_id_prefix, cache_rva, entry_count)
CACHES = [
    ('Task',        '0x0b4', 0x130be10, 1444),
    ('Resource',    '0x0c4', 0x12e0d30,  883),
    ('Assignment',  '0x0f4', 0x12c0f30,  777),
    ('Links',       '0x0e4', 0x1378be0,   30),
    ('Calendar',    '0x3c4', 0x1381ae0,   14),
    ('Project',     '0x074', 0x1382aa0,    9),
    ('OutlineCode', '0x3e4', 0x1384990,   16),
    ('Task34',      '0x164', 0x137e9a0,   34),
]

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)


class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [
        ('dwSize', wt.DWORD),
        ('cntUsage', wt.DWORD),
        ('th32ProcessID', wt.DWORD),
        ('th32DefaultHeapID', ctypes.POINTER(ctypes.c_ulong)),
        ('th32ModuleID', wt.DWORD),
        ('cntThreads', wt.DWORD),
        ('th32ParentProcessID', wt.DWORD),
        ('pcPriClassBase', ctypes.c_long),
        ('dwFlags', wt.DWORD),
        ('szExeFile', ctypes.c_char * 260),
    ]


class MODULEENTRY32(ctypes.Structure):
    _fields_ = [
        ('dwSize', wt.DWORD),
        ('th32ModuleID', wt.DWORD),
        ('th32ProcessID', wt.DWORD),
        ('GlblcntUsage', wt.DWORD),
        ('ProccntUsage', wt.DWORD),
        ('modBaseAddr', ctypes.POINTER(ctypes.c_byte)),
        ('modBaseSize', wt.DWORD),
        ('hModule', wt.HMODULE),
        ('szModule', ctypes.c_char * 256),
        ('szExePath', ctypes.c_char * 260),
    ]


def find_winproj_pid():
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap == -1:
        raise OSError("CreateToolhelp32Snapshot failed")
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


def get_module_base(pid, module_name):
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    if snap == -1:
        raise OSError(f"CreateToolhelp32Snapshot(module) failed: {ctypes.get_last_error()}")
    me = MODULEENTRY32()
    me.dwSize = ctypes.sizeof(me)
    base = None
    if kernel32.Module32First(snap, ctypes.byref(me)):
        while True:
            name = me.szModule.decode('utf-8', errors='replace').lower()
            if module_name.lower() in name:
                base = ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value
                break
            if not kernel32.Module32Next(snap, ctypes.byref(me)):
                break
    kernel32.CloseHandle(snap)
    return base


def read_process_memory(handle, address, size):
    buf = ctypes.create_string_buffer(size)
    bytes_read = ctypes.c_size_t(0)
    ok = kernel32.ReadProcessMemory(
        handle, ctypes.c_void_p(address), buf, size, ctypes.byref(bytes_read))
    if not ok:
        err = ctypes.get_last_error()
        raise OSError(f"ReadProcessMemory failed at 0x{address:x}: error {err}")
    return buf.raw[:bytes_read.value]


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


def parse_cache(cache_data, entry_count, field_map):
    entries = []
    populated = 0
    for i in range(entry_count):
        off = i * ENTRY_SIZE
        entry = cache_data[off:off + ENTRY_SIZE]

        vtable = struct.unpack_from('<Q', entry, 0)[0]
        field_id = struct.unpack_from('<I', entry, 8)[0]
        type_code = struct.unpack_from('<I', entry, 12)[0]
        rec_offset = struct.unpack_from('<I', entry, 0x18)[0]
        flags = struct.unpack_from('<I', entry, 0x1C)[0]
        size = struct.unpack_from('<I', entry, 0x30)[0]

        if vtable != 0 or field_id != 0:
            populated += 1
            entries.append({
                'index': i,
                'vtable': vtable,
                'field_id': field_id,
                'type_code': type_code,
                'rec_offset': rec_offset,
                'flags': flags,
                'size': size,
                'name': field_map.get(field_id, ''),
            })

    return entries, populated


def main():
    print("Phase 2d: Dumping ALL Runtime Field Caches from WINPROJ.EXE")
    print("=" * 70)

    field_map = load_field_id_map()

    pids = find_winproj_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE is not running!")
        print("Please start Microsoft Project with a .mpp file open and try again.")
        sys.exit(1)

    pid, name = pids[0]
    print(f"Found process: {name} (PID {pid})")

    base = get_module_base(pid, 'winproj.exe')
    if base is None:
        print("ERROR: Could not find WINPROJ.EXE module base address")
        sys.exit(1)
    print(f"Module base: 0x{base:x}")

    handle = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        err = ctypes.get_last_error()
        print(f"ERROR: OpenProcess failed: error {err}")
        print("Try running as Administrator.")
        sys.exit(1)

    try:
        all_entries = {}

        for cache_name, prefix, rva, count in CACHES:
            cache_addr = base + rva
            cache_size = ENTRY_SIZE * count

            print(f"\n{'='*70}")
            print(f"{cache_name} CACHE: {count} entries at 0x{cache_addr:x} (RVA 0x{rva:x})")
            print("-" * 50)

            try:
                cache_data = read_process_memory(handle, cache_addr, cache_size)
                print(f"  Read {len(cache_data)} bytes")

                entries, populated = parse_cache(cache_data, count, field_map)
                print(f"  Populated: {populated}/{count}")

                if populated > 0:
                    # Show first 10
                    print(f"\n  {'Idx':>5} {'FieldID':>12} {'Type':>6} {'Offset':>8} {'Size':>6} {'Flags':>10} Name")
                    for e in entries[:10]:
                        print(f"  {e['index']:5d} 0x{e['field_id']:08x} {e['type_code']:6d} "
                              f"0x{e['rec_offset']:04x} {e['size']:6d} 0x{e['flags']:08x}  {e['name']}")
                    if len(entries) > 10:
                        print(f"  ... ({len(entries) - 10} more)")

                    # Stats
                    offsets = [e['rec_offset'] for e in entries if e['rec_offset'] > 0]
                    types = Counter(e['type_code'] for e in entries)
                    sizes = Counter(e['size'] for e in entries if e['size'] > 0)
                    vtables = Counter(e['vtable'] for e in entries)

                    if offsets:
                        print(f"\n  Offsets: range 0x{min(offsets):x}-0x{max(offsets):x}, {len(set(offsets))} unique")
                    print(f"  Types: {dict(types.most_common(15))}")
                    print(f"  Sizes: {dict(sizes.most_common(10))}")
                    print(f"  Vtables: {len(vtables)} unique")

                    all_entries[cache_name] = entries

                    # Write per-cache dump
                    out_path = ANALYSIS / f"runtime_cache_{cache_name.lower()}.tsv"
                    with open(out_path, 'w', newline='', encoding='utf-8') as f:
                        w = csv.writer(f, delimiter='\t')
                        w.writerow(['index', 'field_id_hex', 'type_code', 'record_offset_hex',
                                     'record_offset_dec', 'size', 'flags_hex', 'vtable_hex',
                                     'property_name'])
                        for e in entries:
                            w.writerow([
                                e['index'], f"0x{e['field_id']:08x}", e['type_code'],
                                f"0x{e['rec_offset']:04x}", e['rec_offset'],
                                e['size'], f"0x{e['flags']:08x}",
                                f"0x{e['vtable']:016x}", e['name'],
                            ])
                    print(f"  Wrote to {out_path}")

                    # Also save raw binary
                    raw_path = ANALYSIS / f"runtime_cache_{cache_name.lower()}.bin"
                    with open(raw_path, 'wb') as f:
                        f.write(cache_data)

                else:
                    print("  WARNING: Cache empty! Ensure a project file is open.")

            except OSError as exc:
                print(f"  ERROR: {exc}")

        # Write combined summary
        print(f"\n{'='*70}")
        print("COMBINED SUMMARY")
        print("=" * 70)

        summary_path = ANALYSIS / "runtime_cache_summary.tsv"
        with open(summary_path, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f, delimiter='\t')
            w.writerow(['record_type', 'prefix', 'total_fields', 'populated',
                         'offset_range', 'unique_offsets', 'vtable_count'])
            for cache_name, prefix, rva, count in CACHES:
                if cache_name in all_entries:
                    entries = all_entries[cache_name]
                    offsets = [e['rec_offset'] for e in entries if e['rec_offset'] > 0]
                    vtables = len(set(e['vtable'] for e in entries))
                    off_range = f"0x{min(offsets):x}-0x{max(offsets):x}" if offsets else "n/a"
                    w.writerow([cache_name, prefix, count, len(entries),
                                off_range, len(set(offsets)), vtables])

        print(f"Wrote summary to {summary_path}")

        total = sum(len(e) for e in all_entries.values())
        print(f"\nTotal populated entries across all caches: {total}")
        print(f"Cache types dumped: {list(all_entries.keys())}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
