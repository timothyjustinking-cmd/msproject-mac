#!/usr/bin/env python3
"""Phase 2d: Master dump v2 - fixes COM name loading and non-Task record types.

Fixes:
1. field_id_map.tsv format: tab-delimited with header, field_id in col[4], property_name in col[8]
2. Non-Task records: don't filter by vtable RVA, just validate first 8 bytes are a valid pointer
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


def is_valid_propinfo(entry_data, mod_base):
    """Check if an entry looks like a valid PropInfoData."""
    if not entry_data or len(entry_data) < 0xA8:
        return False
    vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
    vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
    # Valid vtable should be in .rdata section (RVA 0x0900000 - 0x1200000)
    if not (0x0800000 < vtable_rva < 0x1200000):
        return False
    # field_id should be in a known range
    field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
    prefix = (field_id >> 24) & 0xFF
    if prefix not in (0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x14, 0x15):
        return False
    return True


def extract_byte_offset_from_getter2(handle, mod_base, g2_rva):
    """Analyze a getter2 function to find hardcoded record byte offsets."""
    code = rpm(handle, mod_base + g2_rva, 256)
    if not code:
        return None

    func_end = min(200, len(code))
    for i in range(len(code)):
        if code[i] == 0xC3:
            func_end = i + 1
            break

    has_call = False
    for i in range(func_end - 5):
        if code[i] == 0xE8:
            disp = struct.unpack_from('<i', code, i+1)[0]
            target = g2_rva + i + 5 + disp
            if 0x360000 < target < 0x370000:
                has_call = True
                break

    if not has_call:
        return None

    offsets = []
    for i in range(func_end):
        if i + 6 <= func_end and code[i] == 0x8B and code[i+1] == 0x80:
            disp = struct.unpack_from('<i', code, i+2)[0]
            if 0x40 < disp < 0x4000:
                offsets.append((disp, 4, 'i32'))
        if i + 7 <= func_end and code[i] == 0x0F and code[i+1] == 0xB7 and code[i+2] == 0x80:
            disp = struct.unpack_from('<i', code, i+3)[0]
            if 0x40 < disp < 0x4000:
                offsets.append((disp, 2, 'i16'))
        if i + 6 <= func_end and code[i] == 0x8B and code[i+1] == 0x81:
            disp = struct.unpack_from('<i', code, i+2)[0]
            if 0x40 < disp < 0x4000:
                offsets.append((disp, 4, 'i32'))
        if i + 7 <= func_end and code[i] == 0x48 and code[i+1] == 0x8B and code[i+2] == 0x80:
            disp = struct.unpack_from('<i', code, i+3)[0]
            if 0x40 < disp < 0x4000:
                offsets.append((disp, 8, 'i64'))
        if i + 7 <= func_end and code[i] == 0x0F and code[i+1] == 0xB7 and code[i+2] == 0x88:
            disp = struct.unpack_from('<i', code, i+3)[0]
            if 0x40 < disp < 0x4000:
                offsets.append((disp, 2, 'i16'))

    if offsets:
        return offsets[0]
    return None


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
    0:'special', 1:'direct', 2:'unk_2', 3:'unk_3', 4:'text',
    5:'baseline', 6:'unk_6', 7:'virtual', 8:'dyn_text', 9:'unk_9',
    10:'computed', 11:'unk_11', 12:'unk_12', 13:'unk_13', 14:'unk_14', 15:'unk_15'
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
        # STEP 1: Load COM field names from field_id_map.tsv
        # =====================================================================
        print("Step 1: Loading COM field names...")

        field_names = {}
        name_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis', 'field_id_map.tsv')
        if os.path.exists(name_file):
            with open(name_file, 'r', encoding='utf-8') as f:
                header = f.readline()
                for line in f:
                    parts = line.strip().split('\t')
                    if len(parts) >= 9:
                        fid_str = parts[4]  # field_id column
                        name = parts[8]     # property_name column
                        dispatcher = parts[7] if len(parts) > 7 else ''
                        try:
                            fid = int(fid_str, 16) if fid_str.startswith('0x') else int(fid_str)
                            if name and dispatcher == 'get':
                                field_names[fid] = name
                        except ValueError:
                            pass
            print(f"  Loaded {len(field_names)} unique field names (get dispatchers)")
        else:
            print(f"  WARNING: {name_file} not found, trying alternate path")
            name_file2 = r"C:\Users\timot\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile\analysis\field_id_map.tsv"
            if os.path.exists(name_file2):
                with open(name_file2, 'r', encoding='utf-8') as f:
                    header = f.readline()
                    for line in f:
                        parts = line.strip().split('\t')
                        if len(parts) >= 9:
                            fid_str = parts[4]
                            name = parts[8]
                            dispatcher = parts[7] if len(parts) > 7 else ''
                            try:
                                fid = int(fid_str, 16) if fid_str.startswith('0x') else int(fid_str)
                                if name and dispatcher == 'get':
                                    field_names[fid] = name
                            except ValueError:
                                pass
                print(f"  Loaded {len(field_names)} from alternate path")

        # =====================================================================
        # STEP 2: First pass - discover vtable RVAs per record type
        # =====================================================================
        print("\nStep 2: Discovering vtable RVAs per record type...")

        for desc_rva, count, rec_name in DESCRIPTORS:
            desc_va = mod_base + desc_rva
            desc_data = rpm(handle, desc_va, 0x10)
            if not desc_data:
                print(f"  {rec_name}: cannot read descriptor")
                continue

            cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]
            if cache_ptr == 0:
                print(f"  {rec_name}: null cache pointer")
                continue

            # Read first entry to find vtable
            entry_data = rpm(handle, cache_ptr, 0xA8)
            if not entry_data:
                print(f"  {rec_name}: cannot read first entry at 0x{cache_ptr:x}")
                continue

            vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
            vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
            field_id = struct.unpack_from('<I', entry_data, 0x08)[0]

            print(f"  {rec_name}: cache=0x{cache_ptr:x}, vtable_rva=0x{vtable_rva:x}, first_field_id=0x{field_id:08x}")

            # Check entry 1 too
            if count > 1:
                e1 = rpm(handle, cache_ptr + 0xA8, 0xA8)
                if e1:
                    vt1 = struct.unpack_from('<Q', e1, 0x00)[0]
                    vt1_rva = vt1 - mod_base if vt1 > mod_base else 0
                    fid1 = struct.unpack_from('<I', e1, 0x08)[0]
                    print(f"         entry[1] vtable_rva=0x{vt1_rva:x}, field_id=0x{fid1:08x}")

        # =====================================================================
        # STEP 3: Extract all getter2 byte offsets across all record types
        # =====================================================================
        print("\nStep 3: Extracting getter2 byte offsets...")

        all_getter2_rvas = set()
        for desc_rva, count, rec_name in DESCRIPTORS:
            desc_va = mod_base + desc_rva
            desc_data = rpm(handle, desc_va, 0x10)
            if not desc_data:
                continue
            cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]
            if cache_ptr == 0:
                continue

            for idx in range(count):
                entry_data = rpm(handle, cache_ptr + idx * 0xA8, 0xA8)
                if not is_valid_propinfo(entry_data, mod_base):
                    continue
                getter2 = struct.unpack_from('<Q', entry_data, 0x68)[0]
                g2_rva = getter2 - mod_base if getter2 > mod_base else 0
                if g2_rva > 0x1000:
                    all_getter2_rvas.add(g2_rva)

        print(f"  Total unique getter2 RVAs: {len(all_getter2_rvas)}")

        getter2_offsets = {}
        for g2_rva in sorted(all_getter2_rvas):
            result = extract_byte_offset_from_getter2(handle, mod_base, g2_rva)
            if result:
                getter2_offsets[g2_rva] = result

        print(f"  Getter2 functions with byte offsets: {len(getter2_offsets)}")

        # =====================================================================
        # STEP 4: Produce master TSV
        # =====================================================================
        print("\nStep 4: Producing master TSV...")

        out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
        os.makedirs(out_dir, exist_ok=True)
        out_path = os.path.join(out_dir, 'field_master_decoded.tsv')

        total_fields = 0
        total_with_offset = 0
        total_with_name = 0

        with open(out_path, 'w', encoding='utf-8') as fout:
            header = '\t'.join([
                'record_type', 'cache_idx', 'field_id_hex', 'field_id_dec',
                'com_name', 'type_code', 'type_name', 'slot_index', 'flags_hex',
                'storage_class', 'storage_name', 'class_slot', 'dirty_bit_pos',
                'field_size', 'sub_type', 'secondary_idx', 'linked_field_hex',
                'getter2_rva', 'byte_offset_hex', 'byte_offset_dec',
                'vtable_rva'
            ])
            fout.write(header + '\n')

            for desc_rva, count, rec_name in DESCRIPTORS:
                desc_va = mod_base + desc_rva
                desc_data = rpm(handle, desc_va, 0x10)
                if not desc_data:
                    continue
                cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]
                if cache_ptr == 0:
                    continue

                rec_fields = 0
                rec_with_offset = 0
                rec_with_name = 0

                for idx in range(count):
                    entry_data = rpm(handle, cache_ptr + idx * 0xA8, 0xA8)
                    if not is_valid_propinfo(entry_data, mod_base):
                        continue

                    vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
                    vtable_rva = vtable_ptr - mod_base

                    field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
                    type_code = struct.unpack_from('<I', entry_data, 0x0C)[0]
                    slot_idx = struct.unpack_from('<I', entry_data, 0x18)[0]
                    flags = struct.unpack_from('<I', entry_data, 0x1C)[0]

                    dirty_bit = struct.unpack_from('<I', entry_data, 0x40)[0]
                    storage_class = struct.unpack_from('<I', entry_data, 0x48)[0]
                    class_slot = struct.unpack_from('<I', entry_data, 0x4C)[0]
                    field_size = struct.unpack_from('<I', entry_data, 0x84)[0]
                    sub_type = struct.unpack_from('<I', entry_data, 0x88)[0]
                    secondary_idx = struct.unpack_from('<i', entry_data, 0x78)[0]
                    linked_fid = struct.unpack_from('<I', entry_data, 0x60)[0]

                    getter2 = struct.unpack_from('<Q', entry_data, 0x68)[0]
                    g2_rva = getter2 - mod_base if getter2 > mod_base else 0

                    byte_offset = getter2_offsets.get(g2_rva)
                    byte_offset_hex = f"0x{byte_offset[0]:x}" if byte_offset else ""
                    byte_offset_dec = str(byte_offset[0]) if byte_offset else ""

                    com_name = field_names.get(field_id, "")
                    type_name = TYPE_NAMES.get(type_code, f"unk_{type_code}")
                    storage_name = STORAGE_CLASS_NAMES.get(storage_class, f"unk_{storage_class}")
                    linked_hex = f"0x{linked_fid:08x}" if linked_fid != 0xFFFFFFFF else ""

                    row = '\t'.join([
                        rec_name, str(idx), f"0x{field_id:08x}", str(field_id),
                        com_name, str(type_code), type_name, str(slot_idx), f"0x{flags:08x}",
                        str(storage_class), storage_name, str(class_slot), str(dirty_bit),
                        str(field_size), str(sub_type), str(secondary_idx), linked_hex,
                        f"0x{g2_rva:x}" if g2_rva else "",
                        byte_offset_hex, byte_offset_dec,
                        f"0x{vtable_rva:x}"
                    ])
                    fout.write(row + '\n')

                    rec_fields += 1
                    if byte_offset:
                        rec_with_offset += 1
                    if com_name:
                        rec_with_name += 1

                total_fields += rec_fields
                total_with_offset += rec_with_offset
                total_with_name += rec_with_name
                print(f"  {rec_name}: {rec_fields} fields, {rec_with_name} named, {rec_with_offset} with byte offsets")

        print(f"\n  TOTAL: {total_fields} fields, {total_with_name} named, {total_with_offset} with byte offsets")
        print(f"  Output: {out_path}")

        # =====================================================================
        # STEP 5: Summary statistics
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"SUMMARY")
        print(f"{'='*80}")

        # Count by storage class across all types
        class_counts = {}
        for desc_rva, count, rec_name in DESCRIPTORS:
            desc_va = mod_base + desc_rva
            desc_data = rpm(handle, desc_va, 0x10)
            if not desc_data:
                continue
            cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]
            if cache_ptr == 0:
                continue

            for idx in range(count):
                entry_data = rpm(handle, cache_ptr + idx * 0xA8, 0xA8)
                if not is_valid_propinfo(entry_data, mod_base):
                    continue
                sc = struct.unpack_from('<I', entry_data, 0x48)[0]
                key = (rec_name, sc)
                class_counts[key] = class_counts.get(key, 0) + 1

        print(f"\nStorage class distribution:")
        print(f"  {'Record':<12s} {'Class':>5s} {'Name':<12s} {'Count':>6s}")
        for (rn, sc), cnt in sorted(class_counts.items()):
            sn = STORAGE_CLASS_NAMES.get(sc, f"unk_{sc}")
            print(f"  {rn:<12s} {sc:>5d} {sn:<12s} {cnt:>6d}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
