#!/usr/bin/env python3
"""Phase 2d: Trace the actual field lookup path at runtime.

We know from static analysis that vfunc[158] does binary search + SHL RDX, 6.
But the cache data at the descriptor's cache_ptr is mostly garbage.

Hypothesis: the SHL RDX, 6 indexes into a DIFFERENT array, or the cache
entries are structured differently than assumed.

Strategy:
1. Read the RUNTIME sorted field ID array (from the descriptor)
2. Read the RUNTIME code at vfunc[158] to verify the lookup logic
3. Try calling GetFieldValue via COM to force cache population
4. Re-read the cache entries for fields we just accessed
5. Read more of the descriptor structure to find alternative array pointers
"""

import ctypes
import ctypes.wintypes as wt
import struct
import sys
import win32com.client
import time
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parents[1]

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
        err = ctypes.get_last_error()
        raise OSError(f"RPM failed at 0x{address:x}: error {err}")
    return buf.raw[:bytes_read.value]


def main():
    print("Phase 2d: Runtime Field Lookup Trace")
    print("=" * 70)

    pids = find_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE not running")
        sys.exit(1)

    pid, _ = pids[0]
    mod_base = get_base(pid)
    print(f"PID: {pid}, Module base: 0x{mod_base:x}")

    propinfo_vt = mod_base + 0x1050670
    custom_vt = mod_base + 0x0fb4a80

    handle = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        print(f"ERROR: OpenProcess failed: {ctypes.get_last_error()}")
        sys.exit(1)

    try:
        # =====================================================================
        # 1. Read full Task descriptor (0x70 bytes)
        # =====================================================================
        print("\n1. FULL TASK DESCRIPTOR (runtime)")
        print("-" * 60)

        desc_addr = mod_base + 0xf8bda0
        desc = rpm(handle, desc_addr, 0x70)

        fields = [
            (0x00, 'Q', 'type_info_ptr'),
            (0x08, 'Q', 'cache_ptr'),
            (0x10, 'H', 'field_count_lo'),
            (0x12, 'H', 'field_count_hi'),
            (0x14, 'H', 'cat_0'), (0x16, 'H', 'cat_1'),
            (0x18, 'H', 'cat_2'), (0x1A, 'H', 'cat_3'),
            (0x1C, 'H', 'cat_4'), (0x1E, 'H', 'cat_5'),
            (0x20, 'H', 'cat_6'), (0x22, 'H', 'cat_7'),
            (0x24, 'I', 'marker'),
            (0x28, 'I', 'flag_28'),
            (0x2C, 'I', 'flag_2C'),
            (0x30, 'I', 'val_30'),
            (0x34, 'I', 'val_34'),
            (0x38, 'I', 'col_count'),
            (0x3C, 'I', 'val_3C'),
            (0x40, 'Q', 'index_table'),
            (0x48, 'Q', 'packed_48'),
            (0x50, 'I', 'near_last_fid'),
            (0x54, 'I', 'val_54'),
            (0x58, 'Q', 'secondary_desc'),
            (0x60, 'Q', 'dispatcher'),
            (0x68, 'Q', 'accessor'),
        ]

        for off, fmt, name in fields:
            if fmt == 'Q':
                val = struct.unpack_from('<Q', desc, off)[0]
                in_module = (mod_base <= val < mod_base + 0x20000000) if val > mod_base else False
                rva_str = f" (RVA 0x{val - mod_base:x})" if in_module else ""
                print(f"  +0x{off:02x}: 0x{val:016x}{rva_str}  {name}")
            elif fmt == 'I':
                val = struct.unpack_from('<I', desc, off)[0]
                print(f"  +0x{off:02x}: 0x{val:08x} ({val:>10d})  {name}")
            elif fmt == 'H':
                val = struct.unpack_from('<H', desc, off)[0]
                print(f"  +0x{off:02x}: 0x{val:04x} ({val:>6d})     {name}")

        cache_ptr = struct.unpack_from('<Q', desc, 0x08)[0]
        field_count = struct.unpack_from('<H', desc, 0x10)[0]

        # =====================================================================
        # 2. Read the sorted field ID array
        # =====================================================================
        print(f"\n{'='*70}")
        print("2. SORTED FIELD ID ARRAY (runtime, first 30 entries)")
        print("-" * 60)

        # The sorted array is in .data at known RVA 0x12a9060
        sorted_addr = mod_base + 0x12a9060
        sorted_data = rpm(handle, sorted_addr, field_count * 4)

        sorted_ids = []
        for i in range(field_count):
            fid = struct.unpack_from('<I', sorted_data, i * 4)[0]
            sorted_ids.append(fid)

        for i in range(min(30, len(sorted_ids))):
            fid = sorted_ids[i]
            print(f"  [{i:4d}] 0x{fid:08x}")

        # Find some known fields
        print(f"\n  Searching for known field IDs in sorted array:")
        known = {
            0x0b40001d: 'Duration',
            0x0b400001: 'Start',
            0x0b400002: 'Finish',
            0x0b400003: 'Summary',
            0x0b40000e: 'Name',
            0x0b400008: 'FixedCost',
        }
        for fid, name in known.items():
            if fid in sorted_ids:
                idx = sorted_ids.index(fid)
                print(f"    {name} (0x{fid:08x}) -> sorted index {idx}")

        # =====================================================================
        # 3. Read cache entries for known fields
        # =====================================================================
        print(f"\n{'='*70}")
        print("3. CACHE ENTRIES FOR KNOWN FIELDS (at cache_ptr)")
        print("-" * 60)

        for fid, name in known.items():
            if fid not in sorted_ids:
                continue
            idx = sorted_ids.index(fid)
            entry_addr = cache_ptr + idx * 0x40
            try:
                entry = rpm(handle, entry_addr, 0x40)
                vtable = struct.unpack_from('<Q', entry, 0)[0]
                efid = struct.unpack_from('<I', entry, 8)[0]
                etype = struct.unpack_from('<I', entry, 12)[0]
                eoff = struct.unpack_from('<I', entry, 0x18)[0]
                eflags = struct.unpack_from('<I', entry, 0x1C)[0]
                esize = struct.unpack_from('<I', entry, 0x30)[0]

                valid = "VALID" if vtable == propinfo_vt else f"vt=0x{vtable:x}"
                print(f"  {name} (idx={idx}): {valid} fid=0x{efid:08x} type={etype} off=0x{eoff:x} sz={esize}")
            except OSError as e:
                print(f"  {name} (idx={idx}): ERROR {e}")

        # =====================================================================
        # 4. Force field access via COM then re-check
        # =====================================================================
        print(f"\n{'='*70}")
        print("4. FORCE FIELD ACCESS VIA COM")
        print("-" * 60)

        try:
            app = win32com.client.Dispatch("MSProject.Application")
            proj = app.ActiveProject
            if proj and proj.Tasks.Count > 0:
                task = proj.Tasks(1)
                print(f"  Accessing fields of '{task.Name}'...")

                # Access many fields to force cache population
                accessed = []
                for attr in ['Duration', 'Start', 'Finish', 'Work', 'Cost',
                             'PercentComplete', 'Name', 'ID', 'UniqueID',
                             'Summary', 'Milestone', 'Priority', 'FixedCost',
                             'WBS', 'Text1', 'Number1', 'Flag1', 'Baseline1Duration',
                             'ActualStart', 'ActualFinish', 'ActualWork', 'ActualCost',
                             'LateStart', 'LateFinish', 'FreeSlack', 'TotalSlack',
                             'Critical', 'Overallocated', 'OutlineLevel',
                             'Duration1', 'Duration2', 'Start1', 'Start2',
                             'Finish1', 'Finish2', 'Number2', 'Number3',
                             'Text2', 'Text3', 'Flag2', 'Flag3',
                             'Cost1', 'Cost2', 'Cost3',
                             'BaselineDuration', 'BaselineStart', 'BaselineFinish',
                             'BaselineWork', 'BaselineCost']:
                    try:
                        val = getattr(task, attr)
                        accessed.append(attr)
                    except:
                        pass

                print(f"  Accessed {len(accessed)} fields via COM")
                time.sleep(1)
            else:
                print("  No active project or no tasks")
        except Exception as e:
            print(f"  COM error: {e}")

        # =====================================================================
        # 5. Re-check cache entries after COM access
        # =====================================================================
        print(f"\n{'='*70}")
        print("5. RE-CHECK CACHE AFTER COM ACCESS")
        print("-" * 60)

        for fid, name in known.items():
            if fid not in sorted_ids:
                continue
            idx = sorted_ids.index(fid)
            entry_addr = cache_ptr + idx * 0x40
            try:
                entry = rpm(handle, entry_addr, 0x40)
                vtable = struct.unpack_from('<Q', entry, 0)[0]
                efid = struct.unpack_from('<I', entry, 8)[0]
                etype = struct.unpack_from('<I', entry, 12)[0]
                eoff = struct.unpack_from('<I', entry, 0x18)[0]
                eflags = struct.unpack_from('<I', entry, 0x1C)[0]
                esize = struct.unpack_from('<I', entry, 0x30)[0]

                valid = "VALID" if vtable == propinfo_vt else "CUSTOM" if vtable == custom_vt else f"vt=0x{vtable:x}"
                print(f"  {name} (idx={idx}): {valid} fid=0x{efid:08x} type={etype} off=0x{eoff:x} sz={esize}")
            except OSError as e:
                print(f"  {name} (idx={idx}): ERROR {e}")

        # =====================================================================
        # 6. Count ALL valid entries across full Task cache
        # =====================================================================
        print(f"\n{'='*70}")
        print("6. FULL TASK CACHE SCAN AFTER COM ACCESS")
        print("-" * 60)

        cache_data = rpm(handle, cache_ptr, field_count * 0x40)

        valid = 0
        custom = 0
        zero = 0
        garbage = 0
        for i in range(field_count):
            off = i * 0x40
            vt = struct.unpack_from('<Q', cache_data, off)[0]
            if vt == propinfo_vt:
                valid += 1
            elif vt == custom_vt:
                custom += 1
            elif vt == 0:
                fid = struct.unpack_from('<I', cache_data, off + 8)[0]
                if fid == 0:
                    zero += 1
                else:
                    garbage += 1
            else:
                garbage += 1

        print(f"  PropInfoData: {valid}")
        print(f"  CustomPropInfoData: {custom}")
        print(f"  Zero: {zero}")
        print(f"  Garbage: {garbage}")
        print(f"  Total: {valid + custom + zero + garbage}")

        # =====================================================================
        # 7. Hex dump of a few entries around known-good ones
        # =====================================================================
        print(f"\n{'='*70}")
        print("7. HEX DUMP: entries around index 0")
        print("-" * 60)

        for i in range(5):
            off = i * 0x40
            row = cache_data[off:off + 0x40]
            vt = struct.unpack_from('<Q', row, 0)[0]
            valid_marker = " <<< VALID" if vt in (propinfo_vt, custom_vt) else ""
            print(f"  Entry [{i}] (vtable=0x{vt:x}){valid_marker}:")
            for r in range(4):
                roff = r * 16
                hex_str = ' '.join(f'{row[roff+j]:02x}' for j in range(16))
                print(f"    +0x{roff:02x}: {hex_str}")

    finally:
        kernel32.CloseHandle(handle)

    print(f"\n{'='*70}")
    print("DONE")


if __name__ == '__main__':
    main()
