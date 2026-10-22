#!/usr/bin/env python3
"""Phase 2d: Find the byte_offset field within PropInfoData.

We know these fields have specific TASKNODE offsets from getter2 analysis:
  field_id 0x0B400011 (slot=3)  → getter2 reads at offset 0x730
  field_id 0x0B400015 (slot=6)  → getter2 reads at offset 0x740
  field_id 0x0B400019 (slot=2)  → getter2 reads at offset 0x910
  field_id 0x0B4000B4 (slot=20) → getter2 reads at offset 0x74A
  field_id 0x0B4000BA (slot=20) → getter2 reads at offset 0x74C
  field_id 0x0B40005D (slot=4)  → getter2 reads at offset 0x7C4

If PropInfoData contains the byte_offset, one of its undecoded fields
should match these values.

Also resolves getter1 thunks to find the real implementation.
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
        # Read Task descriptor cache
        desc_va = mod_base + 0xf8bda0
        desc_data = rpm(handle, desc_va, 0x10)
        cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]

        # Known offsets from getter2 analysis
        known_offsets = {
            0x0B400011: 0x730,   # i16, slot=3
            0x0B400015: 0x740,   # i32, slot=6
            0x0B400019: 0x910,   # i16, slot=2
            0x0B4000B4: 0x74A,   # i16, slot=20
            0x0B4000BA: 0x74C,   # i16, slot=20
            0x0B40005D: 0x7C4,   # date32, slot=4
        }

        # =====================================================================
        # PART 1: Dump FULL 0xA8 bytes for entries with known offsets
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 1: FULL CACHE ENTRY DUMP FOR FIELDS WITH KNOWN OFFSETS")
        print(f"{'='*80}")

        # Build index: field_id -> cache_index
        field_idx_map = {}
        for idx in range(1444):
            entry_va = cache_ptr + idx * 0xA8
            entry_data = rpm(handle, entry_va, 0xA8)
            if not entry_data or len(entry_data) < 0xA8:
                continue
            field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
            field_idx_map[field_id] = idx

        for field_id, expected_offset in sorted(known_offsets.items()):
            idx = field_idx_map.get(field_id)
            if idx is None:
                print(f"\n  field_id 0x{field_id:08x}: NOT FOUND in cache!")
                continue

            entry_va = cache_ptr + idx * 0xA8
            entry_data = rpm(handle, entry_va, 0xA8)

            type_code = struct.unpack_from('<I', entry_data, 0x0C)[0]
            slot_idx = struct.unpack_from('<I', entry_data, 0x18)[0]
            flags = struct.unpack_from('<I', entry_data, 0x1C)[0]

            print(f"\n  field_id 0x{field_id:08x} (idx={idx}, type={type_code}, slot={slot_idx}, flags=0x{flags:08x})")
            print(f"  Expected TASKNODE offset: 0x{expected_offset:x}")

            # Dump all bytes looking for the expected offset
            print(f"  Full 0xA8 hex dump:")
            for j in range(0, 0xA8, 16):
                row = entry_data[j:j+16]
                hex_str = ' '.join(f'{b:02x}' for b in row)

                # Check if expected_offset appears anywhere in this row
                markers = []
                for k in range(min(16, len(row))):
                    # Check as i16
                    if k + 2 <= len(row):
                        val16 = struct.unpack_from('<H', row, k)[0]
                        if val16 == expected_offset:
                            markers.append(f"  <-- u16 at +0x{j+k:02x} = 0x{expected_offset:x}!")
                    # Check as i32
                    if k + 4 <= len(row):
                        val32 = struct.unpack_from('<I', row, k)[0]
                        if val32 == expected_offset:
                            markers.append(f"  <-- u32 at +0x{j+k:02x} = 0x{expected_offset:x}!")

                mark = markers[0] if markers else ""
                print(f"    +0x{j:02x}: {hex_str}{mark}")

            # Also print all 16/32-bit values > 0x100 that could be offsets
            print(f"  Candidate offset values (>0x100):")
            for off in range(0, 0xA8 - 1, 2):
                val16 = struct.unpack_from('<H', entry_data, off)[0]
                if 0x100 < val16 < 0x2000:
                    print(f"    +0x{off:02x} (u16): 0x{val16:04x} ({val16})")
            for off in range(0, 0xA8 - 3, 4):
                val32 = struct.unpack_from('<I', entry_data, off)[0]
                if 0x100 < val32 < 0x2000:
                    print(f"    +0x{off:02x} (u32): 0x{val32:08x} ({val32})")

        # =====================================================================
        # PART 2: Compare specific fields at same offsets to find correlations
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 2: BYTE-BY-BYTE COMPARISON OF ENTRIES WITH KNOWN OFFSETS")
        print(f"{'='*80}")

        entries = {}
        for field_id in known_offsets:
            idx = field_idx_map.get(field_id)
            if idx is not None:
                entry_va = cache_ptr + idx * 0xA8
                entries[field_id] = rpm(handle, entry_va, 0xA8)

        if len(entries) >= 2:
            print(f"\n  Comparing {len(entries)} entries side by side:")
            print(f"  Looking for position where value matches expected TASKNODE offset\n")

            # For each byte offset in the entry, check correlation
            for off in range(0, 0xA8 - 1, 2):
                values = {}
                for fid, data in entries.items():
                    val16 = struct.unpack_from('<H', data, off)[0]
                    values[fid] = val16

                # Check if any value matches the expected offset
                matches = sum(1 for fid, val in values.items() if val == known_offsets[fid])
                if matches > 0:
                    vals_str = ', '.join(f"0x{fid:08x}:{val:04x}(expect {known_offsets[fid]:04x}){'*' if val == known_offsets[fid] else ''}"
                                        for fid, val in sorted(values.items()))
                    print(f"  +0x{off:02x} (u16): {matches}/{len(entries)} match! {vals_str}")

            for off in range(0, 0xA8 - 3, 4):
                values = {}
                for fid, data in entries.items():
                    val32 = struct.unpack_from('<I', data, off)[0]
                    values[fid] = val32

                matches = sum(1 for fid, val in values.items() if val == known_offsets[fid])
                if matches > 0:
                    vals_str = ', '.join(f"0x{fid:08x}:{val:08x}(expect {known_offsets[fid]:04x}){'*' if val == known_offsets[fid] else ''}"
                                        for fid, val in sorted(values.items()))
                    print(f"  +0x{off:02x} (u32): {matches}/{len(entries)} match! {vals_str}")

        # =====================================================================
        # PART 3: Resolve getter1 thunk chain
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 3: RESOLVING GETTER1 THUNK AT RVA 0x373670")
        print(f"{'='*80}")

        # Read the thunk
        thunk_rva = 0x373670
        thunk_bytes = rpm(handle, mod_base + thunk_rva, 6)
        if thunk_bytes and thunk_bytes[0] == 0xFF and thunk_bytes[1] == 0x25:
            disp = struct.unpack_from('<i', thunk_bytes, 2)[0]
            ptr_rva = thunk_rva + 6 + disp
            print(f"  JMP [RIP+0x{disp:x}] -> pointer at RVA 0x{ptr_rva:x}")

            # Read the pointer
            ptr_data = rpm(handle, mod_base + ptr_rva, 8)
            if ptr_data:
                target_va = struct.unpack_from('<Q', ptr_data, 0)[0]
                target_rva = target_va - mod_base
                print(f"  Pointer value: 0x{target_va:x} (RVA 0x{target_rva:x})")

                # Dump the real function
                real_code = rpm(handle, target_va, 256)
                if real_code:
                    print(f"\n  Real getter1 implementation at RVA 0x{target_rva:x}:")
                    for j in range(0, min(256, len(real_code)), 16):
                        row = real_code[j:min(j+16, len(real_code))]
                        hex_str = ' '.join(f'{b:02x}' for b in row)
                        print(f"    +0x{j:03x}: {hex_str}")
                        if any(b == 0xC3 for b in row):
                            break

        # Also check the other common thunk at RVA 0x3736b0
        thunk2_rva = 0x3736B0
        thunk2_bytes = rpm(handle, mod_base + thunk2_rva, 6)
        if thunk2_bytes and thunk2_bytes[0] == 0xFF and thunk2_bytes[1] == 0x25:
            disp = struct.unpack_from('<i', thunk2_bytes, 2)[0]
            ptr_rva = thunk2_rva + 6 + disp
            ptr_data = rpm(handle, mod_base + ptr_rva, 8)
            if ptr_data:
                target_va = struct.unpack_from('<Q', ptr_data, 0)[0]
                target_rva = target_va - mod_base
                print(f"\n  Thunk at 0x3736B0 → RVA 0x{target_rva:x}")
                real_code = rpm(handle, target_va, 256)
                if real_code:
                    print(f"  Code at RVA 0x{target_rva:x}:")
                    for j in range(0, min(256, len(real_code)), 16):
                        row = real_code[j:min(j+16, len(real_code))]
                        hex_str = ' '.join(f'{b:02x}' for b in row)
                        print(f"    +0x{j:03x}: {hex_str}")
                        if any(b == 0xC3 for b in row):
                            break

        # =====================================================================
        # PART 4: Survey ALL unique getter1 RVAs and resolve thunks
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 4: ALL UNIQUE GETTER1 RVAS AND THUNK RESOLUTION")
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

            getter1 = struct.unpack_from('<Q', entry_data, 0x58)[0]
            g1_rva = getter1 - mod_base if getter1 > mod_base else 0

            field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
            type_code = struct.unpack_from('<I', entry_data, 0x0C)[0]

            if g1_rva not in getter1_map:
                getter1_map[g1_rva] = {'fields': [], 'types': set()}
            getter1_map[g1_rva]['fields'].append(field_id)
            getter1_map[g1_rva]['types'].add(type_code)

        type_names = {0:'null', 2:'i16', 3:'i32', 5:'i64', 8:'text', 9:'obj_ref',
                      11:'bool', 19:'date32', 20:'comp_dur', 21:'order_id', 29:'virtual', 72:'guid'}

        print(f"\n  Total unique getter1 RVAs: {len(getter1_map)}")

        for g1_rva in sorted(getter1_map.keys()):
            info = getter1_map[g1_rva]
            types_str = ', '.join(type_names.get(t, str(t)) for t in sorted(info['types']))
            count = len(info['fields'])

            # Read first few bytes to check for thunk
            code = rpm(handle, mod_base + g1_rva, 16)
            if not code:
                print(f"  RVA 0x{g1_rva:x}: UNREADABLE ({count} fields, [{types_str}])")
                continue

            hex_head = ' '.join(f'{b:02x}' for b in code[:8])

            resolved = ""
            if code[0] == 0xFF and code[1] == 0x25:
                disp = struct.unpack_from('<i', code, 2)[0]
                ptr_rva = g1_rva + 6 + disp
                ptr_data = rpm(handle, mod_base + ptr_rva, 8)
                if ptr_data:
                    target_va = struct.unpack_from('<Q', ptr_data, 0)[0]
                    target_rva = target_va - mod_base
                    resolved = f" -> thunk to RVA 0x{target_rva:x}"
            elif code[0] == 0xE9:
                disp = struct.unpack_from('<i', code, 1)[0]
                target_rva = g1_rva + 5 + disp
                resolved = f" -> JMP to RVA 0x{target_rva:x}"

            print(f"  RVA 0x{g1_rva:x}: [{hex_head}] {count} fields, [{types_str}]{resolved}")

        # =====================================================================
        # PART 5: Detailed dump of undecoded tail bytes for first 20 entries
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 5: UNDECODED FIELDS IN FIRST 20 TASK ENTRIES")
        print(f"{'='*80}")

        print(f"\n  {'idx':>4s} {'field_id':>10s} {'type':>5s} {'slot':>4s} {'flags':>10s} | +10      +20-2F                            | +40-57                            +60      +70-A7")
        print(f"  {'-'*4} {'-'*10} {'-'*5} {'-'*4} {'-'*10} | {'-'*8} {'-'*33} | {'-'*33} {'-'*8} {'-'*40}")

        for idx in range(20):
            entry_va = cache_ptr + idx * 0xA8
            entry_data = rpm(handle, entry_va, 0xA8)
            if not entry_data or len(entry_data) < 0xA8:
                continue

            vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
            vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
            if vtable_rva not in (0x1050670, 0x0fb4a80):
                continue

            field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
            type_code = struct.unpack_from('<I', entry_data, 0x0C)[0]
            slot_idx = struct.unpack_from('<I', entry_data, 0x18)[0]
            flags = struct.unpack_from('<I', entry_data, 0x1C)[0]

            # Undecoded: +0x10(8), +0x20(32), +0x40(24), +0x60(8), +0x70(56)
            unk10 = ' '.join(f'{b:02x}' for b in entry_data[0x10:0x18])
            unk20 = ' '.join(f'{b:02x}' for b in entry_data[0x20:0x30])
            unk30 = ' '.join(f'{b:02x}' for b in entry_data[0x30:0x40])
            unk40 = ' '.join(f'{b:02x}' for b in entry_data[0x40:0x58])
            unk60 = ' '.join(f'{b:02x}' for b in entry_data[0x60:0x68])
            unk70 = ' '.join(f'{b:02x}' for b in entry_data[0x70:0x80])

            tn = type_names.get(type_code, str(type_code))
            print(f"  {idx:4d} 0x{field_id:08x} {tn:>5s} {slot_idx:4d} 0x{flags:08x} | {unk10} {unk20}")
            print(f"       {'':>10s} {'':>5s} {'':>4s} {'':>10s} | {'':>8s} {unk30}")
            print(f"       tail: {unk40}")
            print(f"             {unk60}")
            print(f"             {unk70}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
