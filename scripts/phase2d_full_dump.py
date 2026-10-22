#!/usr/bin/env python3
"""Phase 2d: Full field metadata dump using correct 0xA8 stride.

BREAKTHROUGH: The cache entry stride is 0xA8 (168 bytes), NOT 0x40 (64 bytes).
Each entry is: 0x40 bytes PropInfoData + 0x68 bytes of additional metadata.

This script:
1. Dumps ALL entries for ALL 8 record types with the correct stride
2. Decodes the 0x68-byte "tail" between entries
3. Matches field_ids to COM property names
4. Produces the definitive field mapping TSV
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

PROPINFO_ENTRY_SIZE = 0x40
REAL_STRIDE = 0xA8  # 168 bytes between entries

PROPINFO_VT_RVA = 0x1050670
CUSTOM_VT_RVA = 0x0fb4a80

DESCRIPTORS = [
    ('Task',        0xf8bda0, 1444, 0x130be10),
    ('Resource',    0xf8bf60,  883, 0x12e0d30),
    ('Assignment',  0xf86410,  777, 0x12c0f30),
    ('Links',       0xf8c080,   30, 0x1378be0),
    ('Calendar',    0xf863b0,   14, 0x1381ae0),
    ('Project',     0xf85ff0,    9, 0x1382aa0),
    ('OutlineCode', 0xf86350,   16, 0x1384990),
    ('Task34',      0xf864d0,   34, 0x137e9a0),
]

TYPE_NAMES = {
    0: 'Unknown', 1: 'Currency', 2: 'Date', 3: 'Duration', 4: 'Finish',
    5: 'Work', 6: 'Number', 7: 'Text', 8: 'Boolean', 9: 'Start',
    10: 'Resource', 11: 'Cost', 12: 'Percent', 13: 'Enumerated',
    14: 'Mask', 15: 'Indicator', 16: 'IndCriteria', 17: 'Formula',
    18: 'Guid', 19: 'Image', 20: 'CompDuration', 21: 'Duration2',
    29: 'type_29',
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
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32(); pe.dwSize = ctypes.sizeof(pe)
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
    me = MODULEENTRY32(); me.dwSize = ctypes.sizeof(me)
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
    br = ctypes.c_size_t(0)
    ok = kernel32.ReadProcessMemory(handle, ctypes.c_void_p(address), buf, size, ctypes.byref(br))
    if not ok:
        raise OSError(f"RPM fail 0x{address:x}: err {ctypes.get_last_error()}")
    return buf.raw[:br.value]


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
    print("Phase 2d: Full Field Metadata Dump (0xA8 stride)")
    print("=" * 70)

    field_map = load_field_id_map()
    pids = find_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    pid, _ = pids[0]
    mod_base = get_base(pid)
    print(f"Module base: 0x{mod_base:x}")

    propinfo_vt = mod_base + PROPINFO_VT_RVA
    custom_vt = mod_base + CUSTOM_VT_RVA

    handle = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        print(f"ERROR: OpenProcess failed"); sys.exit(1)

    try:
        all_entries = {}
        grand_total = 0

        for cache_name, desc_rva, expected_count, cache_rva in DESCRIPTORS:
            print(f"\n{'='*70}")
            print(f"  {cache_name} ({expected_count} fields)")
            print("-" * 50)

            cache_addr = mod_base + cache_rva

            # Read enough for all entries at 0xA8 stride
            read_size = expected_count * REAL_STRIDE + PROPINFO_ENTRY_SIZE
            try:
                cache_data = rpm(handle, cache_addr, read_size)
            except OSError as e:
                print(f"  ERROR reading cache: {e}")
                continue

            entries = []
            valid = 0
            custom = 0
            invalid = 0

            for i in range(expected_count):
                off = i * REAL_STRIDE
                if off + PROPINFO_ENTRY_SIZE > len(cache_data):
                    break

                vt = struct.unpack_from('<Q', cache_data, off)[0]
                fid = struct.unpack_from('<I', cache_data, off + 8)[0]
                tc = struct.unpack_from('<I', cache_data, off + 12)[0]
                rec_off = struct.unpack_from('<I', cache_data, off + 0x18)[0]
                flags = struct.unpack_from('<I', cache_data, off + 0x1C)[0]
                size = struct.unpack_from('<I', cache_data, off + 0x30)[0]

                # Also read "tail" fields (0x40 through 0xA8)
                tail = {}
                if off + REAL_STRIDE <= len(cache_data):
                    for t in range(PROPINFO_ENTRY_SIZE, REAL_STRIDE, 8):
                        tail[t] = struct.unpack_from('<Q', cache_data, off + t)[0]

                vtype = None
                if vt == propinfo_vt:
                    vtype = 'PropInfoData'
                    valid += 1
                elif vt == custom_vt:
                    vtype = 'CustomPropInfoData'
                    custom += 1
                else:
                    invalid += 1
                    continue

                e = {
                    'index': i, 'vtable_type': vtype,
                    'field_id': fid, 'type_code': tc,
                    'rec_offset': rec_off, 'flags': flags, 'size': size,
                    'name': field_map.get(fid, ''),
                    'tail': tail,
                }
                entries.append(e)

            print(f"  Valid: {valid} PropInfoData + {custom} Custom = {valid + custom}")
            print(f"  Invalid/missing: {invalid}")
            grand_total += valid + custom

            if entries:
                types = Counter(e['type_code'] for e in entries)
                offsets = sorted(set(e['rec_offset'] for e in entries))
                print(f"  Types: {dict(types.most_common(15))}")
                if offsets:
                    print(f"  Offset range: 0x{offsets[0]:x} - 0x{offsets[-1]:x} ({len(offsets)} unique)")

                # Show first 20
                print(f"\n  {'#':>4} {'FieldID':>12} {'Name':<30s} {'Type':>10} {'Off':>6} {'Sz':>6}")
                for e in entries[:20]:
                    tname = TYPE_NAMES.get(e['type_code'], f"t{e['type_code']}")
                    nm = (e['name'][:28] + '..') if len(e['name']) > 30 else e['name']
                    print(f"  {e['index']:4d} 0x{e['field_id']:08x} {nm:<30s} {tname:>10s} 0x{e['rec_offset']:04x} {e['size']:6d}")
                if len(entries) > 20:
                    print(f"  ... ({len(entries) - 20} more)")

                all_entries[cache_name] = entries

                # Write TSV
                out_path = ANALYSIS / f"field_metadata_{cache_name.lower()}.tsv"
                with open(out_path, 'w', newline='', encoding='utf-8') as f:
                    w = csv.writer(f, delimiter='\t')
                    w.writerow(['index', 'field_id_hex', 'field_id_dec', 'property_name',
                                'vtable_type', 'type_code', 'type_name',
                                'record_offset', 'size', 'flags_hex'])
                    for e in entries:
                        tname = TYPE_NAMES.get(e['type_code'], f"type_{e['type_code']}")
                        w.writerow([
                            e['index'], f"0x{e['field_id']:08x}", e['field_id'],
                            e['name'], e['vtable_type'],
                            e['type_code'], tname,
                            e['rec_offset'], e['size'], f"0x{e['flags']:08x}",
                        ])
                print(f"  Wrote {out_path.name}")

        # =================================================================
        # Tail analysis (what's in the 0x68 bytes between entries)
        # =================================================================
        print(f"\n{'='*70}")
        print("TAIL STRUCTURE ANALYSIS (bytes 0x40-0xA7 of each entry)")
        print("-" * 50)

        if 'Task' in all_entries and len(all_entries['Task']) > 5:
            task_entries = all_entries['Task']
            # Show hex of tail for first 5 entries
            for e in task_entries[:5]:
                tail = e['tail']
                tname = TYPE_NAMES.get(e['type_code'], f"t{e['type_code']}")
                print(f"\n  Entry {e['index']}: fid=0x{e['field_id']:08x} ({e['name']}) type={tname}")
                for toff in sorted(tail.keys()):
                    val = tail[toff]
                    in_mod = (mod_base <= val < mod_base + 0x20000000)
                    rva = f"  (RVA 0x{val-mod_base:x})" if in_mod else ""
                    print(f"    +0x{toff:02x}: 0x{val:016x}{rva}")

            # Analyze tail[0x40] — first QWORD of tail
            tail40_vals = Counter()
            for e in task_entries:
                if 0x40 in e['tail']:
                    val = e['tail'][0x40]
                    in_mod = (mod_base <= val < mod_base + 0x20000000)
                    if in_mod:
                        tail40_vals[f"module+0x{val-mod_base:x}"] += 1
                    elif val == 0:
                        tail40_vals['NULL'] += 1
                    else:
                        tail40_vals[f'0x{val:x}'] += 1
            print(f"\n  Tail +0x40 value distribution (top 10):")
            for v, c in tail40_vals.most_common(10):
                print(f"    {v}: {c}")

            # Check if tail has function pointers (getter/setter?)
            tail48_vals = Counter()
            for e in task_entries:
                if 0x48 in e['tail']:
                    val = e['tail'][0x48]
                    in_mod = (mod_base <= val < mod_base + 0x20000000)
                    if in_mod:
                        rva = val - mod_base
                        if 0x100000 <= rva <= 0x900000:  # .text section
                            tail48_vals['code_ptr'] += 1
                        else:
                            tail48_vals[f'data_0x{rva:x}'] += 1
                    elif val == 0:
                        tail48_vals['NULL'] += 1
                    else:
                        tail48_vals[f'other'] += 1
            print(f"\n  Tail +0x48 distribution:")
            for v, c in tail48_vals.most_common(10):
                print(f"    {v}: {c}")

        # =================================================================
        # Summary
        # =================================================================
        print(f"\n{'='*70}")
        print("COMBINED SUMMARY")
        print("=" * 70)

        for cache_name, desc_rva, expected_count, cache_rva in DESCRIPTORS:
            if cache_name in all_entries:
                n = len(all_entries[cache_name])
                named = sum(1 for e in all_entries[cache_name] if e['name'])
                print(f"  {cache_name:<15s}: {n:5d} valid, {named:5d} named (of {expected_count} expected)")
            else:
                print(f"  {cache_name:<15s}: FAILED")

        print(f"\n  Grand total: {grand_total} valid entries")

        # Write combined TSV
        out_all = ANALYSIS / "field_metadata_all.tsv"
        with open(out_all, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f, delimiter='\t')
            w.writerow(['record_type', 'index', 'field_id_hex', 'field_id_dec',
                        'property_name', 'vtable_type', 'type_code', 'type_name',
                        'record_offset', 'size', 'flags_hex'])
            for cache_name in ['Task', 'Resource', 'Assignment', 'Links',
                               'Calendar', 'Project', 'OutlineCode', 'Task34']:
                if cache_name not in all_entries:
                    continue
                for e in all_entries[cache_name]:
                    tname = TYPE_NAMES.get(e['type_code'], f"type_{e['type_code']}")
                    w.writerow([
                        cache_name, e['index'], f"0x{e['field_id']:08x}", e['field_id'],
                        e['name'], e['vtable_type'],
                        e['type_code'], tname,
                        e['rec_offset'], e['size'], f"0x{e['flags']:08x}",
                    ])
        print(f"\n  Combined file: {out_all.name}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
