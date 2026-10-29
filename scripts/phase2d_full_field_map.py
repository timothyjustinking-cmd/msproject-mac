#!/usr/bin/env python3
"""Extract full SC1 field-to-byte-offset mapping using the formula:
    byte_position_in_record = cache_entry[0x40] / 32
    prio_offset = byte_position - 236

BREAKTHROUGH: The u16 at offset 0x40 in each 0xA8-stride cache entry,
divided by 32, gives the absolute byte position within the 576-byte record.
Priority is at byte 236.

Only applies when cache_entry[0x48] == 1 (main dispatch path).
Fields with [0x48] != 1 use alternative dispatch.
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

REAL_STRIDE = 0xA8  # 168 bytes between entries
PROPINFO_ENTRY_SIZE = 0x40
PROPINFO_VT_RVA = 0x1050670
CUSTOM_VT_RVA = 0x0fb4a80

TASK_CACHE_RVA = 0x130be10
TASK_COUNT = 1444

PRIORITY_BYTE_POS = 236  # Priority is at byte 236 within the 576-byte record

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
        raise OSError(f"ReadProcessMemory failed at 0x{addr:x}")
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
    if not handle:
        print("ERROR: OpenProcess failed"); sys.exit(1)

    field_map = load_field_id_map()
    propinfo_vt = mod_base + PROPINFO_VT_RVA
    custom_vt = mod_base + CUSTOM_VT_RVA

    cache_addr = mod_base + TASK_CACHE_RVA
    read_size = TASK_COUNT * REAL_STRIDE + PROPINFO_ENTRY_SIZE
    cache_data = rpm(handle, cache_addr, read_size)

    # Extract all entries
    entries = []
    for i in range(TASK_COUNT):
        off = i * REAL_STRIDE
        vt = struct.unpack_from('<Q', cache_data, off)[0]
        if vt != propinfo_vt and vt != custom_vt:
            continue

        fid = struct.unpack_from('<I', cache_data, off + 8)[0]
        tc = struct.unpack_from('<I', cache_data, off + 12)[0]
        rec_off = struct.unpack_from('<I', cache_data, off + 0x18)[0]
        flags = struct.unpack_from('<I', cache_data, off + 0x1C)[0]
        size = struct.unpack_from('<I', cache_data, off + 0x30)[0]

        # Tail fields
        byte_off_raw = struct.unpack_from('<H', cache_data, off + 0x40)[0]
        dispatch_type = struct.unpack_from('<I', cache_data, off + 0x48)[0]
        class_slot = struct.unpack_from('<I', cache_data, off + 0x4C)[0]

        name = field_map.get(fid, '')

        byte_pos = byte_off_raw / 32.0 if dispatch_type == 1 else None
        prio_off = int(byte_pos - PRIORITY_BYTE_POS) if byte_pos is not None and byte_pos == int(byte_pos) else None

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
            'prio_off': prio_off,
        })

    print(f"\nTotal entries: {len(entries)}")

    # Count dispatch types
    dt_counts = Counter(e['dispatch_type'] for e in entries)
    print(f"Dispatch types: {dict(dt_counts.most_common())}")

    # Show all entries with dispatch_type == 1 (main path)
    main_entries = [e for e in entries if e['dispatch_type'] == 1]
    print(f"\nMain dispatch (type 1): {len(main_entries)} fields")
    print(f"Alternative dispatch: {len(entries) - len(main_entries)} fields")

    # Group by type_code
    by_type = defaultdict(list)
    for e in main_entries:
        by_type[e['type_code']].append(e)

    print(f"\n{'='*100}")
    print("SC1 FIELD MAP — sorted by byte position within 576-byte record")
    print(f"{'='*100}")
    print(f"Formula: prio_offset = byte_off_raw / 32 - {PRIORITY_BYTE_POS}")
    print(f"Record: 576 bytes, Priority at byte {PRIORITY_BYTE_POS}\n")

    # Sort all main entries by byte position
    sorted_entries = sorted(main_entries, key=lambda e: e['byte_pos'] if e['byte_pos'] is not None else 9999)

    print(f"{'BytePos':>7s} {'PrioOff':>8s} {'Type':>5s} {'Size':>4s} {'Slot':>4s} "
          f"{'RawOff':>6s} {'FieldID':>12s} {'Name'}")
    print("-" * 100)

    for e in sorted_entries:
        bp = f"{e['byte_pos']:7.1f}" if e['byte_pos'] is not None else "   ???"
        po = f"{e['prio_off']:+8d}" if e['prio_off'] is not None else "     ???"
        tn = TYPE_NAMES.get(e['type_code'], str(e['type_code']))
        print(f"{bp} {po} {tn:>5s} {e['size']:>4d} {e['class_slot']:>4d} "
              f"{e['byte_off_raw']:>6d} 0x{e['field_id']:08x} {e['name']}")

    # Now show alternative-dispatch entries
    alt_entries = [e for e in entries if e['dispatch_type'] != 1]
    if alt_entries:
        alt_by_dt = defaultdict(list)
        for e in alt_entries:
            alt_by_dt[e['dispatch_type']].append(e)

        print(f"\n{'='*100}")
        print("ALTERNATIVE DISPATCH FIELDS (dispatch_type != 1)")
        print(f"{'='*100}\n")

        for dt in sorted(alt_by_dt.keys()):
            elist = alt_by_dt[dt]
            print(f"  Dispatch type {dt}: {len(elist)} fields")
            for e in elist[:20]:
                tn = TYPE_NAMES.get(e['type_code'], str(e['type_code']))
                print(f"    [{e['index']:4d}] 0x{e['field_id']:08x} {e['name']:30s} "
                      f"tc={tn:>5s} slot={e['class_slot']:3d} raw={e['byte_off_raw']:#06x}")
            if len(elist) > 20:
                print(f"    ... and {len(elist)-20} more")

    # Summary: byte position ranges by type_code
    print(f"\n{'='*100}")
    print("BYTE POSITION RANGES BY TYPE")
    print(f"{'='*100}\n")
    for tc in sorted(by_type.keys()):
        elist = by_type[tc]
        positions = [e['byte_pos'] for e in elist if e['byte_pos'] is not None]
        if positions:
            tn = TYPE_NAMES.get(tc, str(tc))
            print(f"  type_code={tc:2d} ({tn:>6s}): byte {min(positions):5.0f}-{max(positions):5.0f} "
                  f"(prio {min(positions)-236:+.0f} to {max(positions)-236:+.0f}), "
                  f"{len(positions)} fields")

    # Write TSV output
    out_path = ANALYSIS / "sc1_field_map.tsv"
    with open(out_path, 'w', newline='') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['byte_pos', 'prio_offset', 'type_code', 'type_name', 'size',
                     'class_slot', 'field_id_hex', 'field_name', 'dispatch_type',
                     'rec_offset', 'flags_hex', 'byte_off_raw'])
        for e in sorted_entries:
            w.writerow([
                f"{e['byte_pos']:.0f}" if e['byte_pos'] is not None else '',
                e['prio_off'] if e['prio_off'] is not None else '',
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
    print(f"\nWrote {len(sorted_entries)} entries to {out_path}")

    kernel32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
