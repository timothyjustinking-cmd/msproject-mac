#!/usr/bin/env python3
"""Phase 2d: Extract ALL byte offsets from ALL 27 unique getter2 functions.

Improved over phase2d_dump_getter.py:
- Wider instruction pattern matching (REX prefixes, all base registers)
- Also catches LEA instructions
- Correlates each getter2's offset with the storage_class of its fields
- Produces a complete getter2->offset mapping
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


REG64 = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI',
         'R8','R9','R10','R11','R12','R13','R14','R15']
REG32 = ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI',
         'R8D','R9D','R10D','R11D','R12D','R13D','R14D','R15D']

def extract_record_offsets(code, max_len=None):
    """Extract all [REG+disp32] memory access patterns from x86-64 code.

    Returns list of (code_offset, disp, access_size, description)
    """
    if max_len is None:
        max_len = len(code)
    offsets = []
    seen = set()

    i = 0
    while i < min(len(code), max_len) - 4:
        # Stop at first RET
        if code[i] == 0xC3 and (i == 0 or code[i-1] not in (0x0F, 0x66)):
            break

        rex = 0
        pos = i

        # Check for REX prefix (0x40-0x4F)
        if 0x40 <= code[pos] <= 0x4F:
            rex = code[pos]
            pos += 1

        if pos >= min(len(code), max_len) - 4:
            i += 1
            continue

        opcode = code[pos]

        # MOV r32/r64, [base+disp32] — opcode 8B, ModR/M=10,reg,base (base!=4,5 for simple)
        if opcode == 0x8B and pos + 5 < len(code):
            modrm = code[pos + 1]
            mod = (modrm >> 6) & 3
            reg = (modrm >> 3) & 7
            rm = modrm & 7

            if mod == 2 and rm != 4:  # mod=10 (disp32), not SIB
                disp = struct.unpack_from('<i', code, pos + 2)[0]
                if 0x40 < disp < 0x3000:
                    base_idx = rm + (8 if rex & 1 else 0)  # REX.B extends rm
                    dst_idx = reg + (8 if rex & 4 else 0)   # REX.R extends reg
                    sz = 8 if rex & 8 else 4  # REX.W = 64-bit
                    desc = f"MOV {REG64[dst_idx] if sz==8 else REG32[dst_idx]}, [{REG64[base_idx]}+0x{disp:x}]"
                    key = (disp, desc)
                    if key not in seen:
                        seen.add(key)
                        offsets.append((i, disp, sz, desc))

        # MOVZX r32, WORD [base+disp32] — 0F B7, ModR/M
        if opcode == 0x0F and pos + 1 < len(code) and code[pos + 1] == 0xB7 and pos + 6 < len(code):
            modrm = code[pos + 2]
            mod = (modrm >> 6) & 3
            reg = (modrm >> 3) & 7
            rm = modrm & 7

            if mod == 2 and rm != 4:
                disp = struct.unpack_from('<i', code, pos + 3)[0]
                if 0x40 < disp < 0x3000:
                    base_idx = rm + (8 if rex & 1 else 0)
                    dst_idx = reg + (8 if rex & 4 else 0)
                    desc = f"MOVZX {REG32[dst_idx]}, WORD [{REG64[base_idx]}+0x{disp:x}]"
                    key = (disp, desc)
                    if key not in seen:
                        seen.add(key)
                        offsets.append((i, disp, 2, desc))

        # MOVZX r32, BYTE [base+disp32] — 0F B6, ModR/M
        if opcode == 0x0F and pos + 1 < len(code) and code[pos + 1] == 0xB6 and pos + 6 < len(code):
            modrm = code[pos + 2]
            mod = (modrm >> 6) & 3
            reg = (modrm >> 3) & 7
            rm = modrm & 7

            if mod == 2 and rm != 4:
                disp = struct.unpack_from('<i', code, pos + 3)[0]
                if 0x40 < disp < 0x3000:
                    base_idx = rm + (8 if rex & 1 else 0)
                    dst_idx = reg + (8 if rex & 4 else 0)
                    desc = f"MOVZX {REG32[dst_idx]}, BYTE [{REG64[base_idx]}+0x{disp:x}]"
                    key = (disp, desc)
                    if key not in seen:
                        seen.add(key)
                        offsets.append((i, disp, 1, desc))

        # LEA r64, [base+disp32] — opcode 8D, needs REX.W
        if opcode == 0x8D and (rex & 8) and pos + 5 < len(code):
            modrm = code[pos + 1]
            mod = (modrm >> 6) & 3
            reg = (modrm >> 3) & 7
            rm = modrm & 7

            if mod == 2 and rm != 4:
                disp = struct.unpack_from('<i', code, pos + 2)[0]
                if 0x40 < disp < 0x3000:
                    base_idx = rm + (8 if rex & 1 else 0)
                    dst_idx = reg + (8 if rex & 4 else 0)
                    desc = f"LEA {REG64[dst_idx]}, [{REG64[base_idx]}+0x{disp:x}]"
                    key = (disp, desc)
                    if key not in seen:
                        seen.add(key)
                        offsets.append((i, disp, 0, desc))  # 0 = address calc

        i += 1

    return offsets


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

    TYPE_NAMES = {0:'null', 2:'i16', 3:'i32', 5:'i64', 8:'text', 9:'obj_ref',
                  11:'bool', 19:'date32', 20:'comp_dur', 21:'order_id', 29:'virtual', 72:'guid'}

    try:
        # Read Task descriptor cache
        desc_va = mod_base + 0xf8bda0
        desc_data = rpm(handle, desc_va, 0x10)
        cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]
        print(f"Task cache ptr: 0x{cache_ptr:x}")

        # Collect all getter2 info from cache entries
        getter2_map = {}  # rva -> {types, storage_classes, fields, field_ids}

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
            type_code = struct.unpack_from('<I', entry_data, 0x0C)[0]
            slot_idx = struct.unpack_from('<I', entry_data, 0x18)[0]
            storage_class = struct.unpack_from('<I', entry_data, 0x48)[0]
            class_slot = struct.unpack_from('<I', entry_data, 0x4C)[0]

            getter2_va = struct.unpack_from('<Q', entry_data, 0x68)[0]
            g2_rva = getter2_va - mod_base if getter2_va > mod_base else 0

            if g2_rva < 0x1000:
                continue

            if g2_rva not in getter2_map:
                getter2_map[g2_rva] = {
                    'types': set(), 'storage_classes': set(),
                    'fields': [], 'count': 0, 'slots': set()
                }
            getter2_map[g2_rva]['types'].add(type_code)
            getter2_map[g2_rva]['storage_classes'].add(storage_class)
            getter2_map[g2_rva]['slots'].add(slot_idx)
            getter2_map[g2_rva]['count'] += 1
            getter2_map[g2_rva]['fields'].append({
                'field_id': field_id, 'type_code': type_code,
                'slot': slot_idx, 'storage_class': storage_class,
                'class_slot': class_slot
            })

        print(f"\nTotal unique getter2 RVAs: {len(getter2_map)}")

        # Load COM names for display
        com_names = {}
        com_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis', 'field_id_map.tsv')
        if os.path.exists(com_path):
            with open(com_path, 'r', encoding='utf-8') as f:
                f.readline()
                for line in f:
                    parts = line.strip().split('\t')
                    if len(parts) >= 9:
                        try:
                            fid = int(parts[4], 16) if parts[4].startswith('0x') else int(parts[4])
                            if parts[7] == 'get' and parts[8]:
                                com_names[fid] = parts[8]
                        except ValueError:
                            pass

        print(f"\n{'='*80}")
        print(f"GETTER2 ANALYSIS: ALL {len(getter2_map)} UNIQUE FUNCTIONS")
        print(f"{'='*80}")

        all_getter_offsets = {}  # rva -> list of (disp, size, desc)

        for g2_rva in sorted(getter2_map.keys()):
            info = getter2_map[g2_rva]
            types_str = ','.join(TYPE_NAMES.get(t, str(t)) for t in sorted(info['types']))
            sc_str = ','.join(str(s) for s in sorted(info['storage_classes']))

            print(f"\n{'-'*80}")
            print(f"Getter2 RVA 0x{g2_rva:x}: {info['count']} fields, types=[{types_str}], storage=[{sc_str}]")

            # Read and decode function
            func_code = rpm(handle, mod_base + g2_rva, 1024)
            if not func_code:
                print(f"  Cannot read function!")
                continue

            # Find function end
            func_end = len(func_code)
            for j in range(len(func_code)):
                if func_code[j] == 0xC3 and (j == 0 or func_code[j-1] not in (0x0F, 0x66)):
                    func_end = j + 1
                    break

            print(f"  Function size: {func_end} bytes")

            # Hex dump (first 256 bytes or function end)
            dump_len = min(func_end + 16, 256)
            for j in range(0, dump_len, 16):
                row = func_code[j:min(j+16, len(func_code))]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                marker = " <RET" if any(func_code[j+k] == 0xC3 for k in range(min(16, len(row))) if j+k < func_end) else ""
                print(f"    +0x{j:03x}: {hex_str:<48s}{marker}")

            # Extract offsets
            offsets = extract_record_offsets(func_code, func_end)
            all_getter_offsets[g2_rva] = offsets

            if offsets:
                print(f"  Record offsets found:")
                for code_off, disp, sz, desc in offsets:
                    print(f"    +0x{code_off:03x}: {desc} (size={sz})")
            else:
                print(f"  No record offsets found (may be trampoline/memset)")

            # Check for CALL/JMP to dynamic_cast helper
            has_dyncast = False
            for j in range(min(func_end, len(func_code)) - 5):
                if func_code[j] == 0xE8:  # CALL
                    disp = struct.unpack_from('<i', func_code, j+1)[0]
                    target_rva = g2_rva + j + 5 + disp
                    if target_rva == 0x369437:
                        has_dyncast = True
                        break
                elif func_code[j] == 0xFF and j+1 < len(func_code) and func_code[j+1] == 0x25:
                    # JMP [rip+disp32] — thunk
                    pass

            if has_dyncast:
                print(f"  Calls __RTDynamicCast (helper at 0x369437)")

            # Show sample fields
            samples = info['fields'][:5]
            print(f"  Sample fields:")
            for f in samples:
                name = com_names.get(f['field_id'], f"0x{f['field_id']:08x}")
                print(f"    {name:<35s} type={TYPE_NAMES.get(f['type_code'],'?'):<8s} sc={f['storage_class']} cs={f['class_slot']}")

        # Summary: all discovered offsets
        print(f"\n{'='*80}")
        print(f"SUMMARY: ALL DISCOVERED RECORD BYTE OFFSETS")
        print(f"{'='*80}")

        unique_offsets = {}  # disp -> set of getter2 RVAs
        for g2_rva, offsets in all_getter_offsets.items():
            for code_off, disp, sz, desc in offsets:
                if disp not in unique_offsets:
                    unique_offsets[disp] = set()
                unique_offsets[disp].add(g2_rva)

        print(f"\nTotal unique offsets: {len(unique_offsets)}")
        for disp in sorted(unique_offsets.keys()):
            rvas = unique_offsets[disp]
            # Count fields using this offset
            field_count = sum(getter2_map[rva]['count'] for rva in rvas)
            sc_set = set()
            for rva in rvas:
                sc_set.update(getter2_map[rva]['storage_classes'])
            print(f"  0x{disp:04x} ({disp:>5d}): {field_count:>4d} fields via {len(rvas)} getter2(s), storage_classes={sorted(sc_set)}")

        # Write TSV mapping: field_id -> getter2_rva -> byte_offsets
        out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis', 'getter2_offsets.tsv')
        with open(out_path, 'w', encoding='utf-8') as fout:
            fout.write('getter2_rva\tfield_count\tstorage_classes\toffsets_found\n')
            for g2_rva in sorted(all_getter_offsets.keys()):
                offsets = all_getter_offsets[g2_rva]
                info = getter2_map[g2_rva]
                sc = ','.join(str(s) for s in sorted(info['storage_classes']))
                offs = ';'.join(f"0x{d:x}({sz})" for _, d, sz, _ in offsets) if offsets else 'none'
                fout.write(f"0x{g2_rva:x}\t{info['count']}\t{sc}\t{offs}\n")

        print(f"\nSaved to: {out_path}")

    finally:
        kernel32.CloseHandle(handle)


if __name__ == '__main__':
    main()
