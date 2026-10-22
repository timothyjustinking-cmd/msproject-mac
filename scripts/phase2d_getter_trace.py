#!/usr/bin/env python3
"""Trace how getter2 functions access the flat buffer.

18 fields have byte_offset -> read from getter record at `base + byte_offset`
86 fields have NO byte_offset -> read from flat buffer via class_slot

Key getter2 RVAs for class 1 fields without byte_offset:
  0x1ecb50: 49 boolean fields (flag bits)
  0x7ddf30: 10 cost/work fields (doubles)
  0x4b2b00: 8 date fields
  0x7de4b0: 3 duration fields (no byte_offset)
  0x7ddea0: 3 i16 fields (PercentComplete, PercentWorkComplete, Type)
  0x7ddd80: 2 i16 fields (FixedCostAccrual, BaselineFixedCostAccrual)

For each getter: disassemble to understand how class_slot is used to compute address.
Also: look at the getter record to understand its structure.
"""

import ctypes
import struct
import sys
import time
import csv

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
OpenProcess = k32.OpenProcess
ReadProcessMemory = k32.ReadProcessMemory
CloseHandle = k32.CloseHandle

def get_pid():
    import subprocess
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq WINPROJ.EXE', '/FO', 'CSV', '/NH'],
                       capture_output=True, text=True)
    for line in r.stdout.strip().split('\n'):
        if 'WINPROJ' in line:
            return int(line.split(',')[1].strip('"'))
    return None

def get_image_base(pid):
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    hProcess = OpenProcess(0x0410 | 0x0010, False, pid)
    if not hProcess: return None
    hMods = (ctypes.c_void_p * 1024)()
    cbNeeded = ctypes.c_ulong()
    if not psapi.EnumProcessModulesEx(hProcess, ctypes.byref(hMods), ctypes.sizeof(hMods),
                                       ctypes.byref(cbNeeded), 0x03):
        CloseHandle(hProcess); return None
    base = hMods[0]
    CloseHandle(hProcess)
    return base

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]


def disasm_bytes(data, base_addr):
    """Simple x86-64 byte dump with basic instruction hints."""
    lines = []
    i = 0
    while i < len(data):
        addr = base_addr + i
        # Just show hex bytes in groups
        chunk = data[i:i+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        lines.append(f"  {addr:#x}: {hex_str}")
        i += 16
    return lines


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")

    # Key getter2 RVAs for flat buffer fields (no byte_offset)
    getter2_rvas = {
        0x7ddf30: ("double_getter", "10 cost/work fields (Work, Cost, etc.)"),
        0x7ddea0: ("i16_getter", "3 i16 fields (PercentComplete, PercentWorkComplete, Type)"),
        0x7ddd80: ("i16_getter2", "2 i16 fields (FixedCostAccrual)"),
        0x1ecb50: ("bool_getter", "49 boolean fields (flag bits)"),
        0x4b2b00: ("date_getter", "8 date fields (ActualStart, ActualFinish, etc.)"),
        0x7de4b0: ("duration_getter", "3 duration fields (ActualDuration, RemainingDuration, LevelingDelay)"),
        0x486e60: ("unknown_getter", "5 fields"),
        0x4ae5e0: ("unknown_getter2", "2 fields"),
        0x4b01e0: ("unknown_getter3", "1 field"),
        0x7ddd40: ("unknown_getter4", "1 field"),
    }

    # Also get getter2 RVAs for fields WITH byte_offset (getter record access)
    getter2_with_offset = {
        0x7ddfb0: ("record_i32_getter", "Duration, FreeSlack, etc. with byte_offset=0x740"),
        0x7de2d0: ("record_start_date", "Start, EarlyStart, LateStart with byte_offset=0x1a0"),
        0x7de090: ("record_finish_date", "Finish, EarlyFinish, LateFinish with byte_offset=0x1a0"),
        0x7de230: ("record_priority", "Priority with byte_offset=0x910"),
        0x7ddec0: ("record_constraint", "ConstraintType with byte_offset=0x730"),
        0x7ddf50: ("record_created", "Created with byte_offset=0x7c4"),
        0x7de010: ("record_i16", "2 unnamed i16 with byte_offset=0x74a"),
    }

    # Dump first 128 bytes of each getter
    print("\n=== GETTER2 FUNCTIONS (flat buffer, no byte_offset) ===")
    for rva, (name, desc) in sorted(getter2_rvas.items()):
        addr = IMAGE_BASE + rva
        data = read_mem(handle, addr, 128)
        if data:
            print(f"\n  [{name}] RVA {rva:#x} ({addr:#x}): {desc}")
            # Show raw hex in lines of 16
            for off in range(0, min(64, len(data)), 16):
                chunk = data[off:off+16]
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                print(f"    +{off:#04x}: {hex_str}")

    print("\n=== GETTER2 FUNCTIONS (getter record, with byte_offset) ===")
    for rva, (name, desc) in sorted(getter2_with_offset.items()):
        addr = IMAGE_BASE + rva
        data = read_mem(handle, addr, 128)
        if data:
            print(f"\n  [{name}] RVA {rva:#x} ({addr:#x}): {desc}")
            for off in range(0, min(64, len(data)), 16):
                chunk = data[off:off+16]
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                print(f"    +{off:#04x}: {hex_str}")

    # Now let's try a different approach: for the i16 getter at 0x7ddea0,
    # look at what class_slot values it handles and try to map them
    # We know: PercentComplete (slot 17) -> prio+2, Type (slot 35) -> prio+6
    # So slot 17 -> offset +2, slot 35 -> offset +6
    # PercentWorkComplete (slot 18) -> prio+4
    # So: slot_17=+2, slot_18=+4, slot_35=+6
    # This suggests: offset = (slot - 17) * 2 + 2??? No: 17->2, 18->4, 35->6
    # 17->2: 2 = 2*(17-16) = 2
    # 18->4: 4 = 2*(18-16) = 4
    # 35->6: 6 = 2*(35-16) = 38??? No.
    #
    # Maybe there's a LOOKUP TABLE indexed by class_slot
    # Or: the i16 region at prio-4 to prio+6 has 6 i16 values:
    #   prio-4: FixedCostAccrual (slot 48)
    #   prio-2: unknown (slot ?)
    #   prio+0: Priority (slot 13) — BUT Priority has byte_offset!
    #   prio+2: PercentComplete (slot 17)
    #   prio+4: PercentWorkComplete (slot 18)
    #   prio+6: Type (slot 35)
    #
    # Within the i16 getter group (0x7ddea0): slots 17, 18, 35
    # Their positions: +2, +4, +6. Sequential, starting at +2.
    # So within this getter's range, field [0]=slot17 at +2, field[1]=slot18 at +4, field[2]=slot35 at +6
    #
    # The secondary_idx might be the key:
    # PercentComplete: secondary_idx=25
    # PercentWorkComplete: secondary_idx=26  (25+1)
    # Type: secondary_idx=35??? need to check

    # Load field data to get secondary_idx for these fields
    tsv_path = "analysis/field_master_decoded.tsv"
    with open(tsv_path, 'r') as f:
        reader = csv.DictReader(f, delimiter='\t')
        fields = list(reader)

    print("\n\n=== CLASS 1 FIELDS SORTED BY GETTER2 + SECONDARY_IDX ===")
    c1_fields = [f for f in fields
                 if f['record_type'] == 'Task' and f['storage_class'] == '1']

    # Group by getter2_rva
    from collections import defaultdict
    by_getter = defaultdict(list)
    for f in c1_fields:
        by_getter[f['getter2_rva']].append(f)

    for g_rva in sorted(by_getter.keys()):
        group = by_getter[g_rva]
        has_offset = any(f['byte_offset_hex'].strip() for f in group)
        label = " [HAS byte_offset]" if has_offset else " [flat buffer]"
        name = getter2_rvas.get(int(g_rva.replace('0x', ''), 16), ("unknown", ""))[0] if not has_offset else \
               getter2_with_offset.get(int(g_rva.replace('0x', ''), 16), ("unknown", ""))[0]
        print(f"\n  Getter2 {g_rva} ({name}){label}: {len(group)} fields")
        group.sort(key=lambda f: int(f['secondary_idx']))
        for f in group:
            name_str = f['com_name'] or f"unnamed_{f['cache_idx']}"
            off = f"byte_off={f['byte_offset_hex']}" if f['byte_offset_hex'].strip() else ""
            print(f"    slot={f['class_slot']:4s} sec_idx={f['secondary_idx']:4s} "
                  f"{name_str:30s} type={f['type_code']} size={f['field_size']} {off}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
