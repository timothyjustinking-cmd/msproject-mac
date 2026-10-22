#!/usr/bin/env python3
"""Phase 2d: Master dump of all field metadata with decoded tail layout.

Produces a comprehensive TSV with ALL decoded fields from the PropInfoData cache:
  field_id, record_type, type_code, type_name, slot_index, flags,
  storage_class, class_slot, dirty_bit, field_size, sub_type,
  secondary_idx, linked_field, getter1_rva, getter2_rva,
  byte_offset (from getter2 analysis where available)

Also extracts byte offsets by disassembling all unique getter2 functions
that perform actual record reads.
"""

import ctypes
import ctypes.wintypes as wt
import struct
import sys
import os

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


def extract_byte_offset_from_getter2(handle, mod_base, g2_rva):
    """Analyze a getter2 function to find hardcoded record byte offsets."""
    code = rpm(handle, mod_base + g2_rva, 256)
    if not code:
        return None

    # Find the function end (first RET)
    func_end = min(200, len(code))
    for i in range(len(code)):
        if code[i] == 0xC3:
            func_end = i + 1
            break

    # Only analyze functions that call the dynamic_cast helper (E8 xx B5 B8 FF pattern)
    has_call = False
    for i in range(func_end - 5):
        if code[i] == 0xE8:
            disp = struct.unpack_from('<i', code, i+1)[0]
            target = g2_rva + i + 5 + disp
            if 0x369000 < target < 0x36A000:
                has_call = True
                break

    if not has_call:
        return None

    offsets = []

    for i in range(func_end):
        # MOV EAX, [RAX+disp32]: 8B 80 xx xx xx xx
        if i + 6 <= func_end and code[i] == 0x8B and code[i+1] == 0x80:
            disp = struct.unpack_from('<i', code, i+2)[0]
            if 0x100 < disp < 0x2000:
                offsets.append((disp, 4, 'i32'))

        # MOVZX EAX, WORD [RAX+disp32]: 0F B7 80 xx xx xx xx
        if i + 7 <= func_end and code[i] == 0x0F and code[i+1] == 0xB7 and code[i+2] == 0x80:
            disp = struct.unpack_from('<i', code, i+3)[0]
            if 0x100 < disp < 0x2000:
                offsets.append((disp, 2, 'i16'))

        # MOV EAX, [RCX+disp32]: 8B 81 xx xx xx xx
        if i + 6 <= func_end and code[i] == 0x8B and code[i+1] == 0x81:
            disp = struct.unpack_from('<i', code, i+2)[0]
            if 0x100 < disp < 0x2000:
                offsets.append((disp, 4, 'i32'))

        # MOV RAX, [RAX+disp32]: 48 8B 80 xx xx xx xx
        if i + 7 <= func_end and code[i] == 0x48 and code[i+1] == 0x8B and code[i+2] == 0x80:
            disp = struct.unpack_from('<i', code, i+3)[0]
            if 0x100 < disp < 0x2000:
                offsets.append((disp, 8, 'i64'))

        # MOVZX ECX, WORD [RAX+disp32]: 0F B7 88 xx xx xx xx
        if i + 7 <= func_end and code[i] == 0x0F and code[i+1] == 0xB7 and code[i+2] == 0x88:
            disp = struct.unpack_from('<i', code, i+3)[0]
            if 0x100 < disp < 0x2000:
                offsets.append((disp, 2, 'i16'))

    # Return the FIRST offset found (the primary data access)
    if offsets:
        return offsets[0]
    return None


# Record type descriptors: (rva, count, name)
DESCRIPTORS = [
    (0xF8BDA0, 1444, "Task"),
    (0xF8BDB0, 883,  "Resource"),
    (0xF8BDC0, 777,  "Assignment"),
    (0xF8BDD0, 30,   "Link"),
    (0xF8BDE0, 14,   "Calendar"),
    (0xF8BDF0, 9,    "Project"),
    (0xF8BE00, 16,   "OutlineCode"),
    (0xF8BE10, 34,   "Task34"),
]

TYPE_NAMES = {
    0:'null', 2:'i16', 3:'i32', 5:'i64', 8:'text', 9:'obj_ref',
    11:'bool', 19:'date32', 20:'comp_dur', 21:'order_id', 29:'virtual', 72:'guid',
    100:'bool_ex', 101:'i64_ex'
}

STORAGE_CLASS_NAMES = {
    0: 'special',
    1: 'direct',
    4: 'text',
    5: 'baseline',
    7: 'virtual',
    8: 'dyn_text',
    10: 'computed',
}


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
        # =====================================================================
        # STEP 1: Extract byte offsets from ALL unique getter2 functions
        # =====================================================================
        print("Step 1: Extracting byte offsets from getter2 functions...")

        # Collect ALL getter2 RVAs across all record types
        all_getter2_rvas = set()

        for desc_rva, count, rec_name in DESCRIPTORS:
            desc_va = mod_base + desc_rva
            desc_data = rpm(handle, desc_va, 0x10)
            if not desc_data:
                continue
            cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]

            for idx in range(count):
                entry_va = cache_ptr + idx * 0xA8
                entry_data = rpm(handle, entry_va, 0xA8)
                if not entry_data or len(entry_data) < 0xA8:
                    continue

                vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
                vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
                if vtable_rva not in (0x1050670, 0x0fb4a80):
                    continue

                getter2 = struct.unpack_from('<Q', entry_data, 0x68)[0]
                g2_rva = getter2 - mod_base if getter2 > mod_base else 0
                if g2_rva > 0x1000:
                    all_getter2_rvas.add(g2_rva)

        print(f"  Found {len(all_getter2_rvas)} unique getter2 RVAs")

        # Analyze each getter2 for byte offsets
        getter2_offsets = {}  # g2_rva -> (byte_offset, size, type)
        for g2_rva in sorted(all_getter2_rvas):
            result = extract_byte_offset_from_getter2(handle, mod_base, g2_rva)
            if result:
                getter2_offsets[g2_rva] = result

        print(f"  Found byte offsets in {len(getter2_offsets)} getter2 functions")
        for g2_rva, (off, sz, tn) in sorted(getter2_offsets.items()):
            print(f"    0x{g2_rva:x} -> offset 0x{off:x} ({sz} bytes, {tn})")

        # =====================================================================
        # STEP 2: Load COM field names
        # =====================================================================
        print("\nStep 2: Loading COM field names...")

        field_names = {}
        name_file = os.path.join(os.path.dirname(__file__), '..', 'analysis', 'field_id_map.tsv')
        if os.path.exists(name_file):
            with open(name_file, 'r') as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('field_id'):
                        continue
                    parts = line.split('\t')
                    if len(parts) >= 2:
                        try:
                            fid = int(parts[0], 0)
                            name = parts[1]
                            field_names[fid] = name
                        except ValueError:
                            pass
            print(f"  Loaded {len(field_names)} field names")
        else:
            print(f"  WARNING: {name_file} not found")

        # =====================================================================
        # STEP 3: Produce master dump for ALL record types
        # =====================================================================
        print("\nStep 3: Producing master dump...")

        out_dir = os.path.join(os.path.dirname(__file__), '..', 'analysis')
        os.makedirs(out_dir, exist_ok=True)

        total_fields = 0
        total_with_offset = 0

        out_path = os.path.join(out_dir, 'field_master_decoded.tsv')
        with open(out_path, 'w', encoding='utf-8') as fout:
            header = '\t'.join([
                'record_type', 'cache_idx', 'field_id', 'field_id_hex',
                'com_name', 'type_code', 'type_name', 'slot_index', 'flags_hex',
                'storage_class', 'storage_name', 'class_slot', 'dirty_bit',
                'field_size', 'sub_type', 'secondary_idx', 'linked_field_hex',
                'getter1_rva', 'getter2_rva', 'byte_offset_hex', 'byte_offset_dec',
                'vtable_class'
            ])
            fout.write(header + '\n')

            for desc_rva, count, rec_name in DESCRIPTORS:
                desc_va = mod_base + desc_rva
                desc_data = rpm(handle, desc_va, 0x10)
                if not desc_data:
                    continue
                cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]

                rec_fields = 0
                rec_with_offset = 0

                for idx in range(count):
                    entry_va = cache_ptr + idx * 0xA8
                    entry_data = rpm(handle, entry_va, 0xA8)
                    if not entry_data or len(entry_data) < 0xA8:
                        continue

                    vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
                    vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
                    if vtable_rva not in (0x1050670, 0x0fb4a80):
                        continue

                    vtable_class = 'PropInfoData' if vtable_rva == 0x1050670 else 'CustomPropInfoData'

                    field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
                    type_code = struct.unpack_from('<I', entry_data, 0x0C)[0]
                    slot_idx = struct.unpack_from('<I', entry_data, 0x18)[0]
                    flags = struct.unpack_from('<I', entry_data, 0x1C)[0]

                    # Tail fields
                    dirty_bit = struct.unpack_from('<I', entry_data, 0x40)[0]
                    storage_class = struct.unpack_from('<I', entry_data, 0x48)[0]
                    class_slot = struct.unpack_from('<I', entry_data, 0x4C)[0]
                    field_size = struct.unpack_from('<I', entry_data, 0x84)[0]
                    sub_type = struct.unpack_from('<I', entry_data, 0x88)[0]
                    secondary_idx = struct.unpack_from('<i', entry_data, 0x78)[0]

                    linked_fid = struct.unpack_from('<I', entry_data, 0x60)[0]

                    getter1 = struct.unpack_from('<Q', entry_data, 0x58)[0]
                    getter2 = struct.unpack_from('<Q', entry_data, 0x68)[0]
                    g1_rva = getter1 - mod_base if getter1 > mod_base else 0
                    g2_rva = getter2 - mod_base if getter2 > mod_base else 0

                    # Look up byte offset from getter2 analysis
                    byte_offset = getter2_offsets.get(g2_rva)
                    byte_offset_hex = f"0x{byte_offset[0]:x}" if byte_offset else ""
                    byte_offset_dec = str(byte_offset[0]) if byte_offset else ""

                    # COM name
                    com_name = field_names.get(field_id, "")

                    type_name = TYPE_NAMES.get(type_code, f"unk_{type_code}")
                    storage_name = STORAGE_CLASS_NAMES.get(storage_class, f"unk_{storage_class}")

                    linked_hex = f"0x{linked_fid:08x}" if linked_fid != 0xFFFFFFFF else ""

                    row = '\t'.join([
                        rec_name, str(idx), str(field_id), f"0x{field_id:08x}",
                        com_name, str(type_code), type_name, str(slot_idx), f"0x{flags:08x}",
                        str(storage_class), storage_name, str(class_slot), str(dirty_bit),
                        str(field_size), str(sub_type), str(secondary_idx), linked_hex,
                        f"0x{g1_rva:x}" if g1_rva else "", f"0x{g2_rva:x}" if g2_rva else "",
                        byte_offset_hex, byte_offset_dec,
                        vtable_class
                    ])
                    fout.write(row + '\n')

                    rec_fields += 1
                    if byte_offset:
                        rec_with_offset += 1

                total_fields += rec_fields
                total_with_offset += rec_with_offset
                print(f"  {rec_name}: {rec_fields} fields ({rec_with_offset} with byte offsets)")

        print(f"\n  TOTAL: {total_fields} fields, {total_with_offset} with byte offsets")
        print(f"  Output: {out_path}")

        # =====================================================================
        # STEP 4: Storage class statistics
        # =====================================================================
        print(f"\nStep 4: Storage class statistics...")

        # Re-read and collect stats
        class_counts = {}
        for desc_rva, count, rec_name in DESCRIPTORS:
            desc_va = mod_base + desc_rva
            desc_data = rpm(handle, desc_va, 0x10)
            if not desc_data:
                continue
            cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]

            for idx in range(count):
                entry_va = cache_ptr + idx * 0xA8
                entry_data = rpm(handle, entry_va, 0xA8)
                if not entry_data or len(entry_data) < 0xA8:
                    continue

                vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
                vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
                if vtable_rva not in (0x1050670, 0x0fb4a80):
                    continue

                sc = struct.unpack_from('<I', entry_data, 0x48)[0]
                key = (rec_name, sc)
                class_counts[key] = class_counts.get(key, 0) + 1

        print(f"\n  {'Record':<12s} {'Class':<10s} {'Name':<12s} {'Count':>6s}")
        print(f"  {'-'*12} {'-'*10} {'-'*12} {'-'*6}")
        for (rec_name, sc), cnt in sorted(class_counts.items()):
            sn = STORAGE_CLASS_NAMES.get(sc, f"unk_{sc}")
            print(f"  {rec_name:<12s} {sc:<10d} {sn:<12s} {cnt:>6d}")

        # =====================================================================
        # STEP 5: Summary of byte offset coverage
        # =====================================================================
        print(f"\n\nStep 5: Byte offset analysis...")

        offset_field_map = {}  # byte_offset -> list of (rec_name, field_id, com_name)
        for desc_rva, count, rec_name in DESCRIPTORS:
            desc_va = mod_base + desc_rva
            desc_data = rpm(handle, desc_va, 0x10)
            if not desc_data:
                continue
            cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]

            for idx in range(count):
                entry_va = cache_ptr + idx * 0xA8
                entry_data = rpm(handle, entry_va, 0xA8)
                if not entry_data or len(entry_data) < 0xA8:
                    continue

                vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
                vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
                if vtable_rva not in (0x1050670, 0x0fb4a80):
                    continue

                field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
                getter2 = struct.unpack_from('<Q', entry_data, 0x68)[0]
                g2_rva = getter2 - mod_base if getter2 > mod_base else 0

                bo = getter2_offsets.get(g2_rva)
                if bo:
                    com_name = field_names.get(field_id, "")
                    offset_field_map.setdefault(bo[0], []).append((rec_name, field_id, com_name))

        print(f"\n  Byte offset mapping (offset -> fields that share it):")
        for off in sorted(offset_field_map.keys()):
            fields = offset_field_map[off]
            sample = fields[:5]
            sample_str = ', '.join(f"0x{fid:08x}({name or '?'})" for _, fid, name in sample)
            more = f" +{len(fields)-5} more" if len(fields) > 5 else ""
            print(f"  0x{off:04x} ({off:5d}): {len(fields):3d} fields: {sample_str}{more}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
