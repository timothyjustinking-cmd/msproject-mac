#!/usr/bin/env python3
"""Phase 2d: Dump the runtime field cache from a running MS Project process.

The 0x40-byte field entry cache at VA 0x14130be10 is zero-initialized in the
binary and populated at runtime. We need to read it from a live process.

Cache entry layout (from entry 0):
  +0x00: vtable ptr (8 bytes)
  +0x08: field_id (u32) | type_code (u32)
  +0x10: 0 (padding/null)
  +0x18: record_offset (u32) | flags (u32)
  +0x20: hash (u64)
  +0x28: extra data (u64)
  +0x30: size (u32) | 0 (u32)
  +0x38: 0 (padding)

Uses ctypes to call ReadProcessMemory via Win32 API.
"""

import ctypes
import ctypes.wintypes as wt
import struct
import csv
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
ANALYSIS = BASE / "analysis"

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x00000002
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010

# Cache parameters
CACHE_RVA = 0x14130be10 - 0x140000000  # RVA = 0x130be10
ENTRY_SIZE = 0x40
ENTRY_COUNT = 1444
CACHE_SIZE = ENTRY_SIZE * ENTRY_COUNT

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
    """Find WINPROJ.EXE process ID."""
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
    """Get the base address of a module in a process."""
    snap = kernel32.CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    if snap == -1:
        raise OSError(f"CreateToolhelp32Snapshot(module) failed: {ctypes.get_last_error()}")

    me = MODULEENTRY32()
    me.dwSize = ctypes.sizeof(me)

    base = None
    if kernel32.Module32First(snap, ctypes.byref(me)):
        while True:
            name = me.szModule.decode('utf-8', errors='replace').lower()
            if module_name.lower() in name:
                base = ctypes.addressof(me.modBaseAddr.contents)
                # Actually need the pointer value, not the address of the pointer
                base = ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value
                break
            if not kernel32.Module32Next(snap, ctypes.byref(me)):
                break

    kernel32.CloseHandle(snap)
    return base


def read_process_memory(handle, address, size):
    """Read memory from a process."""
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
    print("Phase 2d: Dumping Runtime Field Cache from WINPROJ.EXE")
    print("=" * 70)

    field_map = load_field_id_map()

    # Find WINPROJ.EXE process
    pids = find_winproj_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE is not running!")
        print("Please start Microsoft Project and try again.")
        sys.exit(1)

    pid, name = pids[0]
    print(f"Found process: {name} (PID {pid})")

    # Get module base address
    base = get_module_base(pid, 'winproj.exe')
    if base is None:
        print("ERROR: Could not find WINPROJ.EXE module base address")
        sys.exit(1)
    print(f"Module base: 0x{base:x}")

    # Calculate cache address
    cache_addr = base + CACHE_RVA
    print(f"Cache address: 0x{cache_addr:x} (base + 0x{CACHE_RVA:x})")
    print(f"Expected: 0x14130be10 if base is 0x140000000")

    # Open process for reading
    handle = kernel32.OpenProcess(
        PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        err = ctypes.get_last_error()
        print(f"ERROR: OpenProcess failed: error {err}")
        print("Try running as Administrator.")
        sys.exit(1)

    try:
        # Read the cache
        print(f"\nReading {CACHE_SIZE} bytes ({ENTRY_COUNT} entries x 0x{ENTRY_SIZE:x})...")
        cache_data = read_process_memory(handle, cache_addr, CACHE_SIZE)
        print(f"Read {len(cache_data)} bytes successfully!")

        # Parse entries
        entries = []
        populated = 0
        for i in range(ENTRY_COUNT):
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
                name = field_map.get(field_id, '')
                entries.append({
                    'index': i,
                    'vtable': vtable,
                    'field_id': field_id,
                    'type_code': type_code,
                    'rec_offset': rec_offset,
                    'flags': flags,
                    'size': size,
                    'name': name,
                    'raw': entry,
                })

        print(f"\nPopulated entries: {populated}/{ENTRY_COUNT}")

        if populated == 0:
            print("WARNING: All entries are zero! Cache may not be initialized yet.")
            print("Make sure a project file is open in MS Project.")
            # Still continue to check

        # Show first 20 populated entries
        print(f"\n{'='*100}")
        print(f"{'Idx':>5} {'FieldID':>12} {'Type':>6} {'Offset':>8} {'Size':>6} {'Flags':>10} {'Name'}")
        print(f"{'='*100}")
        for e in entries[:50]:
            print(f"{e['index']:5d} 0x{e['field_id']:08x} {e['type_code']:6d} "
                  f"0x{e['rec_offset']:04x} {e['size']:6d} 0x{e['flags']:08x}  {e['name']}")

        # Check a few known entries to validate
        print(f"\n{'='*70}")
        print("VALIDATION: Looking for known fields")
        print("-" * 50)
        known_fields = {
            0x0b400000: 'Work',
            0x0b400017: 'ID',
            0x0b400002: 'ActualWork',
            0x0b40001d: 'Duration',
            0x0b400005: 'Cost',
            0x0b400018: 'Milestone',
            0x0b400015: 'FreeSlack',
            0x0b400014: 'LevelingDelay',
        }
        for fid, expected_name in known_fields.items():
            found = [e for e in entries if e['field_id'] == fid]
            if found:
                e = found[0]
                print(f"  0x{fid:08x} ({expected_name}): offset=0x{e['rec_offset']:04x}, "
                      f"size={e['size']}, type={e['type_code']}, flags=0x{e['flags']:08x}")
            else:
                print(f"  0x{fid:08x} ({expected_name}): NOT FOUND")

        # Write full dump
        out_path = ANALYSIS / "runtime_cache_dump.tsv"
        with open(out_path, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f, delimiter='\t')
            w.writerow(['index', 'field_id_hex', 'type_code', 'record_offset_hex',
                         'record_offset_dec', 'size', 'flags_hex', 'vtable_hex',
                         'property_name'])
            for e in entries:
                w.writerow([
                    e['index'],
                    f"0x{e['field_id']:08x}",
                    e['type_code'],
                    f"0x{e['rec_offset']:04x}",
                    e['rec_offset'],
                    e['size'],
                    f"0x{e['flags']:08x}",
                    f"0x{e['vtable']:016x}",
                    e['name'],
                ])
        print(f"\nWrote {len(entries)} entries to {out_path}")

        # Also dump raw cache for later analysis
        raw_path = ANALYSIS / "runtime_cache_raw.bin"
        with open(raw_path, 'wb') as f:
            f.write(cache_data)
        print(f"Wrote raw cache to {raw_path}")

        # Analyze offset distribution
        if populated > 10:
            offsets = [e['rec_offset'] for e in entries if e['rec_offset'] > 0]
            types = [e['type_code'] for e in entries if e['type_code'] > 0]
            sizes = [e['size'] for e in entries if e['size'] > 0]

            print(f"\n{'='*70}")
            print("STATISTICS")
            print("-" * 50)
            if offsets:
                print(f"  Offsets: {len(offsets)} non-zero, range {min(offsets)}-{max(offsets)} "
                      f"(0x{min(offsets):x}-0x{max(offsets):x})")
                print(f"  Unique offsets: {len(set(offsets))}")
            if types:
                from collections import Counter
                type_dist = Counter(types)
                print(f"  Type codes: {dict(type_dist.most_common(20))}")
            if sizes:
                size_dist = Counter(sizes)
                print(f"  Sizes: {dict(size_dist.most_common(20))}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone.")


if __name__ == '__main__':
    main()
