#!/usr/bin/env python3
"""Phase 2d: Dump runtime field caches — reads the LIVE descriptor to find actual cache addresses.

The static RVA cache addresses may not be where the scheduling engine stores data at runtime.
ASLR relocations contaminate the .data bytes at those addresses.

Strategy:
1. Read each descriptor from its RUNTIME address (static RVA + ASLR base)
2. Extract the actual cache pointer from descriptor+0x08 (may point to heap)
3. Read cache data from the REAL address
4. Validate entries have a valid PropInfoData vtable

Key: PropInfoData vtable at static VA 0x141050670 becomes (base + 0x1050670) at runtime.
"""

import ctypes
import ctypes.wintypes as wt
import struct
import csv
import sys
from pathlib import Path
from collections import Counter

BASE = Path(__file__).resolve().parents[1]
ANALYSIS = BASE / "analysis"

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x00000002
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010

ENTRY_SIZE = 0x40

# Descriptors: (name, prefix, descriptor_rva, expected_field_count, static_cache_rva)
DESCRIPTORS = [
    ('Task',        '0x0b4', 0xf8bda0, 1444, 0x130be10),
    ('Resource',    '0x0c4', 0xf8bf60,  883, 0x12e0d30),
    ('Assignment',  '0x0f4', 0xf86410,  777, 0x12c0f30),
    ('Links',       '0x0e4', 0xf8c080,   30, 0x1378be0),
    ('Calendar',    '0x3c4', 0xf863b0,   14, 0x1381ae0),
    ('Project',     '0x074', 0xf85ff0,    9, 0x1382aa0),
    ('OutlineCode', '0x3e4', 0xf86350,   16, 0x1384990),
    ('Task34',      '0x164', 0xf864d0,   34, 0x137e9a0),
]

# Static vtable RVA (image base 0x140000000 -> RVA = VA - 0x140000000)
PROPINFO_VT_RVA = 0x1050670
CUSTOM_VT_RVA = 0x0fb4a80

TYPE_NAMES = {
    0: 'Unknown', 1: 'Currency', 2: 'Date', 3: 'Duration', 4: 'Finish',
    5: 'Work', 6: 'Number', 7: 'Text', 8: 'Boolean', 9: 'Start',
    10: 'Resource', 11: 'Cost', 12: 'Percent', 13: 'Enumerated',
    14: 'Mask', 15: 'Indicator', 16: 'IndCriteria', 17: 'Formula',
    18: 'Guid', 19: 'Image', 20: 'CompDuration', 21: 'Duration2',
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


def rpm(handle, address, size):
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


def main():
    print("Phase 2d: Runtime Cache Dump (Descriptor-Based)")
    print("=" * 70)

    field_map = load_field_id_map()

    pids = find_winproj_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE is not running!")
        sys.exit(1)

    pid, name = pids[0]
    print(f"Process: {name} (PID {pid})")

    mod_base = get_module_base(pid, 'winproj.exe')
    if mod_base is None:
        print("ERROR: Could not find module base")
        sys.exit(1)
    print(f"Module base: 0x{mod_base:x}")

    expected_propinfo_vt = mod_base + PROPINFO_VT_RVA
    expected_custom_vt = mod_base + CUSTOM_VT_RVA
    print(f"Expected PropInfoData vtable: 0x{expected_propinfo_vt:x}")
    print(f"Expected CustomPropInfoData vtable: 0x{expected_custom_vt:x}")

    handle = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        print(f"ERROR: OpenProcess failed: {ctypes.get_last_error()}")
        sys.exit(1)

    try:
        all_results = {}

        for cache_name, prefix, desc_rva, expected_count, static_cache_rva in DESCRIPTORS:
            print(f"\n{'='*70}")
            print(f"{cache_name} (prefix {prefix})")
            print("-" * 50)

            # Read descriptor from runtime address
            desc_addr = mod_base + desc_rva
            try:
                desc_data = rpm(handle, desc_addr, 0x70)
            except OSError as e:
                print(f"  ERROR reading descriptor: {e}")
                continue

            # Decode descriptor
            type_info_ptr = struct.unpack_from('<Q', desc_data, 0)[0]
            cache_ptr = struct.unpack_from('<Q', desc_data, 8)[0]
            field_count_lo = struct.unpack_from('<H', desc_data, 0x10)[0]
            field_count_hi = struct.unpack_from('<H', desc_data, 0x12)[0]
            marker = struct.unpack_from('<I', desc_data, 0x24)[0]
            col_count = struct.unpack_from('<I', desc_data, 0x38)[0]

            print(f"  Descriptor at 0x{desc_addr:x}:")
            print(f"    type_info:   0x{type_info_ptr:x}")
            print(f"    cache_ptr:   0x{cache_ptr:x}")
            print(f"    field_count: {field_count_lo} / {field_count_hi}")
            print(f"    marker:      0x{marker:08x}")
            print(f"    col_count:   {col_count}")

            static_cache_addr = mod_base + static_cache_rva
            if cache_ptr == static_cache_addr:
                print(f"    Cache at STATIC address (mod_base + 0x{static_cache_rva:x})")
            elif cache_ptr == 0:
                print(f"    Cache pointer is NULL — not initialized!")
                continue
            else:
                print(f"    Cache at DIFFERENT address (not static RVA!)")
                print(f"    Expected: 0x{static_cache_addr:x}")
                print(f"    Got:      0x{cache_ptr:x}")

            count = field_count_lo
            if count != expected_count:
                print(f"  WARNING: Expected {expected_count} fields, got {count}")

            # Read cache from ACTUAL runtime pointer
            cache_size = ENTRY_SIZE * count
            try:
                cache_data = rpm(handle, cache_ptr, cache_size)
            except OSError as e:
                print(f"  ERROR reading cache: {e}")
                continue

            print(f"  Read {len(cache_data)} bytes from cache")

            # Parse entries, validating vtable
            entries = []
            valid_propinfo = 0
            valid_custom = 0
            zero_entries = 0
            invalid = 0

            for i in range(count):
                off = i * ENTRY_SIZE
                entry = cache_data[off:off + ENTRY_SIZE]

                vtable = struct.unpack_from('<Q', entry, 0)[0]
                field_id = struct.unpack_from('<I', entry, 8)[0]
                type_code = struct.unpack_from('<I', entry, 12)[0]
                rec_offset = struct.unpack_from('<I', entry, 0x18)[0]
                flags = struct.unpack_from('<I', entry, 0x1C)[0]
                size = struct.unpack_from('<I', entry, 0x30)[0]

                if vtable == expected_propinfo_vt:
                    valid_propinfo += 1
                    entries.append({
                        'index': i, 'vtable_type': 'PropInfoData',
                        'field_id': field_id, 'type_code': type_code,
                        'rec_offset': rec_offset, 'flags': flags, 'size': size,
                        'name': field_map.get(field_id, ''),
                    })
                elif vtable == expected_custom_vt:
                    valid_custom += 1
                    entries.append({
                        'index': i, 'vtable_type': 'CustomPropInfoData',
                        'field_id': field_id, 'type_code': type_code,
                        'rec_offset': rec_offset, 'flags': flags, 'size': size,
                        'name': field_map.get(field_id, ''),
                    })
                elif vtable == 0 and field_id == 0:
                    zero_entries += 1
                else:
                    invalid += 1
                    if invalid <= 3:
                        print(f"  Invalid entry [{i}]: vtable=0x{vtable:x} fid=0x{field_id:08x}")

            print(f"\n  Results:")
            print(f"    Valid PropInfoData:      {valid_propinfo}")
            print(f"    Valid CustomPropInfoData: {valid_custom}")
            print(f"    Zero (uninitialized):    {zero_entries}")
            print(f"    Invalid/garbage:         {invalid}")
            print(f"    Total valid:             {valid_propinfo + valid_custom} / {count}")

            if entries:
                # Type distribution
                types = Counter(e['type_code'] for e in entries)
                sizes = Counter(e['size'] for e in entries if e['size'] > 0)
                offsets = sorted(set(e['rec_offset'] for e in entries if e['rec_offset'] > 0))

                print(f"\n  Type distribution: {dict(types.most_common(15))}")
                print(f"  Size distribution: {dict(sizes.most_common(10))}")
                if offsets:
                    print(f"  Offset range: 0x{offsets[0]:x} - 0x{offsets[-1]:x} ({len(offsets)} unique)")
                    if offsets[-1] > 0x10000:
                        print(f"  WARNING: Max offset > 64K — possible issue")

                named = sum(1 for e in entries if e['name'])
                print(f"  Named fields: {named}/{len(entries)}")

                # Show first 15
                print(f"\n  {'Idx':>5} {'FieldID':>12} {'Type':>12} {'Offset':>8} {'Size':>6} {'VtType':>18} Name")
                for e in entries[:15]:
                    tname = TYPE_NAMES.get(e['type_code'], f"type_{e['type_code']}")
                    print(f"  {e['index']:5d} 0x{e['field_id']:08x} {tname:>12s} "
                          f"0x{e['rec_offset']:04x} {e['size']:6d} {e['vtable_type']:>18s}  {e['name']}")
                if len(entries) > 15:
                    print(f"  ... ({len(entries) - 15} more)")

                all_results[cache_name] = entries

                # Write TSV
                out_path = ANALYSIS / f"runtime_cache_{cache_name.lower()}.tsv"
                with open(out_path, 'w', newline='', encoding='utf-8') as f:
                    w = csv.writer(f, delimiter='\t')
                    w.writerow(['index', 'field_id_hex', 'field_id_dec', 'property_name',
                                'vtable_type', 'type_code', 'type_name',
                                'record_offset_hex', 'record_offset_dec',
                                'size', 'flags_hex'])
                    for e in entries:
                        tname = TYPE_NAMES.get(e['type_code'], f"type_{e['type_code']}")
                        w.writerow([
                            e['index'], f"0x{e['field_id']:08x}", e['field_id'],
                            e['name'], e['vtable_type'],
                            e['type_code'], tname,
                            f"0x{e['rec_offset']:04x}", e['rec_offset'],
                            e['size'], f"0x{e['flags']:08x}",
                        ])
                print(f"\n  Wrote {out_path.name}")

                # Raw binary
                raw_path = ANALYSIS / f"runtime_cache_{cache_name.lower()}.bin"
                with open(raw_path, 'wb') as f:
                    f.write(cache_data)

        # =====================================================================
        # Summary
        # =====================================================================
        print(f"\n{'='*70}")
        print("COMBINED SUMMARY")
        print("=" * 70)

        total_valid = 0
        total_expected = 0
        for cache_name, prefix, desc_rva, expected_count, static_cache_rva in DESCRIPTORS:
            total_expected += expected_count
            if cache_name in all_results:
                n = len(all_results[cache_name])
                total_valid += n
                named = sum(1 for e in all_results[cache_name] if e['name'])
                print(f"  {cache_name:<15s}: {n:5d}/{expected_count:5d} valid, {named:5d} named")
            else:
                print(f"  {cache_name:<15s}: FAILED")

        print(f"\n  Total: {total_valid}/{total_expected} valid entries across all caches")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
