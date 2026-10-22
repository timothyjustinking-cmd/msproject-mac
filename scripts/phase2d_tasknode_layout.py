#!/usr/bin/env python3
"""Phase 2d: Map TASKNODE byte offsets to field semantics.

Known offsets from getter2 analysis:
  0x01a0 ( 416) - vtable pointer for something (accessed in date Start/Finish getters)
  0x0300 ( 768) - vtable dispatch slot (accessed in date Start/Finish getters)
  0x0638 (1592) - LEA address calc (pointer to sub-object in date getters)
  0x0730 (1840) - ConstraintType (i16)
  0x0740 (1856) - FreeSlack/TotalSlack/Duration/variances (i32, multiplied by 3 for some)
  0x074a (1866) - Duration units i16 (accessed by many duration-related getters)
  0x074c (1868) - Work/Cost units i16 (class 13 "units" fields)
  0x07b4 (1972) - date32 accessed in Start getter (used in comparison)
  0x07c4 (1988) - date32: Created, ResumeNoEarlierThan (9 fields)
  0x0910 (2320) - Priority (i16, with complex bit manipulation)
  0x0b60 (2912) - pointer to something (accessed via R14/ESI in date getters)

This script reads a live TASKNODE from the running process to see the actual
values at these offsets, helping confirm what they store.
"""

import ctypes
import ctypes.wintypes as wt
import struct
import sys

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
        return None
    return buf.raw[:br.value]

def format_val(data, offset, size):
    if offset + size > len(data):
        return "OUT OF RANGE"
    if size == 2:
        return f"0x{struct.unpack_from('<H', data, offset)[0]:04x} ({struct.unpack_from('<h', data, offset)[0]})"
    elif size == 4:
        val = struct.unpack_from('<I', data, offset)[0]
        ival = struct.unpack_from('<i', data, offset)[0]
        return f"0x{val:08x} ({ival})"
    elif size == 8:
        val = struct.unpack_from('<Q', data, offset)[0]
        return f"0x{val:016x}"
    return ""


def main():
    pids = find_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    pid, _ = pids[0]
    mod_base = get_base(pid)
    print(f"Module base: 0x{mod_base:x}")

    handle = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        print("ERROR: OpenProcess failed"); sys.exit(1)

    try:
        # =================================================================
        # PART 1: Locate a TASKNODE via the prop container chain
        # =================================================================
        # TTaskTable is heap-allocated, reached via vfunc[87] on prop container
        # The prop container is at TTaskTable+0x5E8
        # Let's trace through COM to find a task record

        # Instead of going through COM, let's look at what the getter2 functions
        # do: they call dynamic_cast on their first parameter (RCX) which is
        # the prop container's vtable[158] self parameter.

        # Let's try to find tasknode through the getter2 flow:
        # getter2 functions receive RCX = some object, then call dynamic_cast
        # to get a TBkndRowset-derived object, then read at offsets.
        # The cast target chain includes TBkndDocPropPersistent.

        # For now, let's try reading what the getter2 returns WHEN we use COM
        # to get field values - that tells us the record pointer.

        # Alternative approach: scan for vtable patterns in the heap
        # OR use COM automation to read field values and cross-reference

        print(f"\n{'='*80}")
        print(f"PART 1: TASKNODE STRUCTURE ANALYSIS")
        print(f"{'='*80}")

        # Known byte offsets and their meanings from disassembly
        KNOWN_OFFSETS = [
            (0x01a0, 8, "vtable_ptr_a (date Start/Finish getter)"),
            (0x0300, 8, "vtable_dispatch_ptr (date Start/Finish getter)"),
            (0x0638, 8, "sub_object_ptr (LEA in date getter -> Start/Finish scheduling)"),
            (0x0730, 2, "ConstraintType (i16, storage=1, single field)"),
            (0x0740, 4, "FreeSlack/Duration/TotalSlack (i32, *3 in some getters)"),
            (0x074a, 2, "DurationUnits (i16, accessed by many getters for unit conversion)"),
            (0x074c, 2, "WorkUnits (i16, class 13 unit fields)"),
            (0x07b4, 4, "ScheduleCheck date32 (compared in Start getter)"),
            (0x07c4, 4, "Created/ResumeNoEarlierThan (date32)"),
            (0x0910, 8, "Priority dispatch table ptr (via CX+0x910)"),
            (0x0b60, 8, "Timeline sub-object ptr (date getters)"),
        ]

        print(f"\nKnown TASKNODE offsets from getter2 disassembly:")
        print(f"  {'Offset':<8s} {'Size':<5s} {'Description'}")
        print(f"  {'-'*8} {'-'*5} {'-'*50}")
        for off, sz, desc in KNOWN_OFFSETS:
            print(f"  0x{off:04x}   {sz:<5d} {desc}")

        # =================================================================
        # PART 2: Analyze the getter2 instruction patterns more carefully
        # =================================================================
        print(f"\n{'='*80}")
        print(f"PART 2: GETTER2 PATTERN CLASSIFICATION")
        print(f"{'='*80}")

        # Read the getter2 functions and classify them
        desc_va = mod_base + 0xf8bda0
        desc_data = rpm(handle, desc_va, 0x10)
        cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]

        # Collect getter2 patterns
        getter2_patterns = {}
        for idx in range(1444):
            entry_va = cache_ptr + idx * 0xA8
            entry_data = rpm(handle, entry_va, 0xA8)
            if not entry_data or len(entry_data) < 0xA8:
                continue

            vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
            vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
            if vtable_rva not in (0x1050670, 0x0fb4a80):
                continue

            getter2_va = struct.unpack_from('<Q', entry_data, 0x68)[0]
            g2_rva = getter2_va - mod_base if getter2_va > mod_base else 0

            if g2_rva < 0x1000:
                continue

            if g2_rva not in getter2_patterns:
                func_code = rpm(handle, mod_base + g2_rva, 32)
                if func_code:
                    # Classify by first few bytes
                    # Small functions (< 20 bytes to RET) = default value providers
                    func_end = 32
                    for j in range(len(func_code)):
                        if func_code[j] == 0xC3 and (j == 0 or func_code[j-1] not in (0x0F, 0x66)):
                            func_end = j + 1
                            break

                    if func_end <= 16:
                        pattern = "DEFAULT_VALUE"
                    elif func_end <= 30 and 0xE8 not in func_code[:func_end]:
                        pattern = "SIMPLE_DEFAULT"
                    else:
                        pattern = "RECORD_READER"

                    getter2_patterns[g2_rva] = {
                        'pattern': pattern,
                        'size': func_end,
                        'first_bytes': func_code[:min(16, func_end)].hex()
                    }

        pattern_counts = {}
        for rva, info in getter2_patterns.items():
            p = info['pattern']
            if p not in pattern_counts:
                pattern_counts[p] = 0
            pattern_counts[p] += 1

        print(f"\nGetter2 function patterns:")
        for p, cnt in sorted(pattern_counts.items(), key=lambda x: -x[1]):
            print(f"  {p:<20s}: {cnt} unique functions")

        record_readers = [(rva, info) for rva, info in getter2_patterns.items() if info['pattern'] == 'RECORD_READER']
        print(f"\nRECORD_READER functions ({len(record_readers)}):")
        for rva, info in sorted(record_readers):
            print(f"  RVA 0x{rva:x}: {info['size']} bytes, starts={info['first_bytes']}")

        # =================================================================
        # PART 3: Getter1 (default provider) analysis
        # =================================================================
        print(f"\n{'='*80}")
        print(f"PART 3: GETTER1 ANALYSIS (ALL UNIQUE)")
        print(f"{'='*80}")

        getter1_map = {}
        for idx in range(1444):
            entry_va = cache_ptr + idx * 0xA8
            entry_data = rpm(handle, entry_va, 0xA8)
            if not entry_data or len(entry_data) < 0xA8:
                continue

            vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
            vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
            if vtable_rva not in (0x1050670, 0x0fb4a80):
                continue

            field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
            storage_class = struct.unpack_from('<I', entry_data, 0x48)[0]
            getter1_va = struct.unpack_from('<Q', entry_data, 0x58)[0]
            g1_rva = getter1_va - mod_base if getter1_va > mod_base else 0

            if g1_rva < 0x1000:
                continue

            if g1_rva not in getter1_map:
                getter1_map[g1_rva] = {'count': 0, 'storage_classes': set()}
            getter1_map[g1_rva]['count'] += 1
            getter1_map[g1_rva]['storage_classes'].add(storage_class)

        print(f"\nTotal unique getter1 RVAs: {len(getter1_map)}")
        for rva in sorted(getter1_map.keys()):
            info = getter1_map[rva]
            sc_str = ','.join(str(s) for s in sorted(info['storage_classes']))

            # Read first 16 bytes to classify
            func_code = rpm(handle, mod_base + rva, 32)
            if func_code:
                hex_str = func_code[:16].hex()
                # Check if it's a JMP thunk (FF 25 = JMP [rip+disp32])
                if func_code[0] == 0xFF and func_code[1] == 0x25:
                    disp = struct.unpack_from('<i', func_code, 2)[0]
                    target_rip = rva + 6 + disp
                    target_va = mod_base + target_rip
                    target_ptr = rpm(handle, target_va, 8)
                    if target_ptr:
                        actual = struct.unpack_from('<Q', target_ptr, 0)[0]
                        actual_rva = actual - mod_base if actual > mod_base else 0
                        print(f"  0x{rva:x}: {info['count']} fields, sc=[{sc_str}] -> THUNK to [0x{target_rip:x}] = VA 0x{actual:x} (RVA 0x{actual_rva:x})")
                    else:
                        print(f"  0x{rva:x}: {info['count']} fields, sc=[{sc_str}] -> THUNK (unreadable)")
                else:
                    print(f"  0x{rva:x}: {info['count']} fields, sc=[{sc_str}] -> {hex_str[:32]}")

        # =================================================================
        # PART 4: How many fields have each getter pattern?
        # =================================================================
        print(f"\n{'='*80}")
        print(f"PART 4: FIELD COVERAGE BY GETTER2 PATTERN")
        print(f"{'='*80}")

        pattern_fields = {'DEFAULT_VALUE': 0, 'SIMPLE_DEFAULT': 0, 'RECORD_READER': 0, 'unknown': 0}
        for idx in range(1444):
            entry_va = cache_ptr + idx * 0xA8
            entry_data = rpm(handle, entry_va, 0xA8)
            if not entry_data or len(entry_data) < 0xA8:
                continue

            vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
            vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
            if vtable_rva not in (0x1050670, 0x0fb4a80):
                continue

            getter2_va = struct.unpack_from('<Q', entry_data, 0x68)[0]
            g2_rva = getter2_va - mod_base if getter2_va > mod_base else 0

            info = getter2_patterns.get(g2_rva, {})
            pattern = info.get('pattern', 'unknown')
            pattern_fields[pattern] += 1

        print(f"\n  {'Pattern':<20s} {'Fields':>6s} {'Pct':>5s}")
        print(f"  {'-'*20} {'-'*6} {'-'*5}")
        total = sum(pattern_fields.values())
        for p in ['RECORD_READER', 'DEFAULT_VALUE', 'SIMPLE_DEFAULT', 'unknown']:
            cnt = pattern_fields[p]
            pct = 100*cnt/total if total > 0 else 0
            print(f"  {p:<20s} {cnt:>6d} {pct:>4.1f}%")

    finally:
        kernel32.CloseHandle(handle)


if __name__ == '__main__':
    main()
