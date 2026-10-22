#!/usr/bin/env python3
"""Phase 2d: Trace vfunc[87] (caller of vfunc[158]) to understand R9 parameter.

Also disassembles getter functions from the 0x68-byte tail to understand
how slot indices map to TASKNODE byte offsets.
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

def hex_line(data, offset, rva):
    """Format one line of hex dump."""
    row = data[offset:offset+16]
    hex_str = ' '.join(f'{b:02x}' for b in row)
    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
    return f"  0x{rva:06x}: {hex_str:<48s} {ascii_str}"

def find_call_through_vtable(code, vtable_offset, func_rva):
    """Search for CALL [reg + vtable_offset] patterns in code."""
    results = []
    target_bytes_le = struct.pack('<I', vtable_offset)

    for i in range(len(code) - 6):
        # FF 91 xx xx xx xx = CALL [RCX + disp32]  (vtable call through this ptr)
        # FF 92 xx xx xx xx = CALL [RDX + disp32]
        # FF 93 xx xx xx xx = CALL [RBX + disp32]
        # 41 FF 90-97 xx xx xx xx = CALL [R8-R15 + disp32]

        if code[i] == 0xFF and code[i+1] in range(0x90, 0x98):
            disp = struct.unpack_from('<i', code, i+2)[0]
            if disp == vtable_offset:
                reg_names = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI']
                reg = code[i+1] - 0x90
                results.append((i, f"CALL [{reg_names[reg]} + 0x{vtable_offset:x}]", func_rva + i))

        # With REX prefix (41 = REX.B for R8-R15 base)
        if i + 7 <= len(code) and code[i] == 0x41 and code[i+1] == 0xFF:
            if code[i+2] in range(0x90, 0x98):
                disp = struct.unpack_from('<i', code, i+3)[0]
                if disp == vtable_offset:
                    reg_names_ext = ['R8','R9','R10','R11','R12','R13','R14','R15']
                    reg = code[i+2] - 0x90
                    results.append((i, f"CALL [{reg_names_ext[reg]} + 0x{vtable_offset:x}]", func_rva + i))

        # REX.W prefix variants: 48 FF 9x = CALL [reg + disp32] with REX.W
        if i + 7 <= len(code) and code[i] == 0x48 and code[i+1] == 0xFF:
            if code[i+2] in range(0x90, 0x98):
                disp = struct.unpack_from('<i', code, i+3)[0]
                if disp == vtable_offset:
                    reg_names = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI']
                    reg = code[i+2] - 0x90
                    results.append((i, f"CALL [{reg_names[reg]} + 0x{vtable_offset:x}]", func_rva + i))

        # REX.WB (49 FF 9x) for R8-R15
        if i + 7 <= len(code) and code[i] == 0x49 and code[i+1] == 0xFF:
            if code[i+2] in range(0x90, 0x98):
                disp = struct.unpack_from('<i', code, i+3)[0]
                if disp == vtable_offset:
                    reg_names_ext = ['R8','R9','R10','R11','R12','R13','R14','R15']
                    reg = code[i+2] - 0x90
                    results.append((i, f"CALL [{reg_names_ext[reg]} + 0x{vtable_offset:x}]", func_rva + i))

    return results

def search_for_pattern(code, pattern, func_rva, label):
    """Search for a byte pattern in code."""
    results = []
    for i in range(len(code) - len(pattern)):
        if code[i:i+len(pattern)] == pattern:
            context = code[max(0,i-4):min(i+len(pattern)+8, len(code))]
            results.append((i, func_rva + i, context))
    return results

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
        # PART 1: Analyze vfunc[87] — the caller of vfunc[158]
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 1: ANALYZING VFUNC[87] (RVA 0x231f00) — caller of vfunc[158]")
        print(f"{'='*80}")

        vf87_rva = 0x231f00
        vf87_va = mod_base + vf87_rva
        vf87_code = rpm(handle, vf87_va, 4096)
        if not vf87_code:
            print("ERROR: Cannot read vfunc[87]"); sys.exit(1)

        # vtable slot 158 = offset 158 * 8 = 0x4F0
        vtable_off_158 = 158 * 8  # = 0x4F0
        print(f"\nSearching for CALL [reg + 0x{vtable_off_158:x}] (vtable slot 158)...")

        calls = find_call_through_vtable(vf87_code, vtable_off_158, vf87_rva)
        for off, desc, rva in calls:
            print(f"  Found at offset +0x{off:x} (RVA 0x{rva:x}): {desc}")
            # Show context: 40 bytes before the call
            start = max(0, off - 40)
            print(f"  Context (40 bytes before call):")
            for j in range(start, off + 8, 16):
                row = vf87_code[j:min(j+16, off+8)]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                print(f"    +0x{j:03x} (RVA 0x{vf87_rva+j:x}): {hex_str}")

        if not calls:
            print("  Not found with standard patterns. Trying broader search...")
            # Search for the displacement bytes 0xF0 0x04 0x00 0x00 (= 0x4F0)
            disp_bytes = struct.pack('<I', vtable_off_158)
            for i in range(len(vf87_code) - 6):
                if vf87_code[i+2:i+6] == disp_bytes or vf87_code[i+3:i+7] == disp_bytes:
                    rva = vf87_rva + i
                    context = vf87_code[i:min(i+10, len(vf87_code))]
                    hex_str = ' '.join(f'{b:02x}' for b in context)
                    print(f"  Possible at RVA 0x{rva:x}: {hex_str}")

        # Also search for indirect call through a loaded vtable pointer
        # Pattern: load vtable from [reg], then call [vtable + 0x4F0]
        # This would be: MOV RAX, [RCX] followed by CALL [RAX + 0x4F0]
        print(f"\n  Searching for 0x4F0 as displacement anywhere...")
        for i in range(len(vf87_code) - 4):
            val = struct.unpack_from('<I', vf87_code, i)[0]
            if val == vtable_off_158:
                rva = vf87_rva + i
                pre = vf87_code[max(0,i-6):i]
                post = vf87_code[i:min(i+6,len(vf87_code))]
                pre_hex = ' '.join(f'{b:02x}' for b in pre)
                post_hex = ' '.join(f'{b:02x}' for b in post)
                print(f"  0x4F0 at RVA 0x{rva:x}: [{pre_hex}] [{post_hex}]")

        # Dump the prologue of vfunc[87]
        print(f"\n  Prologue of vfunc[87] (first 64 bytes):")
        for j in range(0, 64, 16):
            row = vf87_code[j:j+16]
            hex_str = ' '.join(f'{b:02x}' for b in row)
            print(f"    +0x{j:03x} (RVA 0x{vf87_rva+j:x}): {hex_str}")

        # Search for R9 setup (4D 8B C? or 49 89 ?? or MOV R9, xxx)
        print(f"\n  Searching for R9 setup (MOV R9, xxx) before vtable call...")
        for i in range(min(len(vf87_code), 2000)):
            # 4D 8B Cx = MOV R8-R15, R8-R15 (need REX.WRB)
            # 49 8B C9 = MOV RCX, R9 (wrong direction)
            # 4C 8B 0D = MOV R9, [RIP+disp32]
            # 4C 8B 49 xx = MOV R9, [RCX+disp8]
            # 4C 8B 89 xx xx xx xx = MOV R9, [RCX+disp32]

            if i + 3 <= len(vf87_code):
                b0, b1, b2 = vf87_code[i], vf87_code[i+1], vf87_code[i+2]

                # 4C = REX.WR (sets REX.R, so reg field encodes R8-R15)
                # 8B = MOV reg, r/m
                # ModRM reg field = 1 means R9 (1+8=9 with REX.R)
                if b0 == 0x4C and b1 == 0x8B:
                    modrm = b2
                    reg = (modrm >> 3) & 7  # 1 = R9 (with REX.R)
                    mod = (modrm >> 6) & 3
                    rm = modrm & 7
                    if reg == 1:  # R9
                        rva = vf87_rva + i
                        context = vf87_code[i:min(i+10, len(vf87_code))]
                        hex_str = ' '.join(f'{b:02x}' for b in context)
                        print(f"  MOV R9, ... at RVA 0x{rva:x}: {hex_str} (mod={mod} rm={rm})")

                # 4D = REX.WRB — MOV R9, R-reg
                if b0 == 0x4D and b1 == 0x8B:
                    modrm = b2
                    reg = (modrm >> 3) & 7
                    if reg == 1:  # R9
                        rva = vf87_rva + i
                        context = vf87_code[i:min(i+10, len(vf87_code))]
                        hex_str = ' '.join(f'{b:02x}' for b in context)
                        print(f"  MOV R9, R... at RVA 0x{rva:x}: {hex_str}")

        # =====================================================================
        # PART 2: Analyze vfunc[100] — lazy initializer called before [158]
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 2: ANALYZING VFUNC[100] (RVA 0x231d90)")
        print(f"{'='*80}")

        vf100_rva = 0x231d90
        vf100_va = mod_base + vf100_rva
        vf100_code = rpm(handle, vf100_va, 1024)
        if vf100_code:
            print(f"\n  Prologue (first 128 bytes):")
            for j in range(0, min(128, len(vf100_code)), 16):
                row = vf100_code[j:j+16]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                print(f"    +0x{j:03x} (RVA 0x{vf100_rva+j:x}): {hex_str}")

            # Search for 0x5e8 offset (where result is cached on this)
            print(f"\n  Searching for 0x5e8 (cache offset on this)...")
            for i in range(len(vf100_code) - 4):
                val = struct.unpack_from('<I', vf100_code, i)[0]
                if val == 0x5e8:
                    rva = vf100_rva + i
                    pre = vf100_code[max(0,i-4):i]
                    post = vf100_code[i:min(i+8,len(vf100_code))]
                    hex_str = ' '.join(f'{b:02x}' for b in pre) + ' [' + ' '.join(f'{b:02x}' for b in post) + ']'
                    print(f"    At RVA 0x{rva:x}: {hex_str}")

        # =====================================================================
        # PART 3: Read the TTaskTable object to understand member layout
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 3: READING TTaskTable INSTANCE MEMBERS")
        print(f"{'='*80}")

        # The descriptor at VA contains the cache base at +0x08
        # We know the Task descriptor is at RVA 0xf8bda0
        desc_va = mod_base + 0xf8bda0
        desc_data = rpm(handle, desc_va, 0x70)
        if desc_data:
            cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]
            print(f"\n  Task descriptor at VA 0x{desc_va:x}")
            print(f"  Cache base from descriptor+0x08: 0x{cache_ptr:x}")

            # Read the first few entries at 0xA8 stride to get getter function pointers
            print(f"\n  Reading first 5 entries at 0xA8 stride for getter analysis:")
            for idx in range(5):
                entry_va = cache_ptr + idx * 0xA8
                entry_data = rpm(handle, entry_va, 0xA8)
                if entry_data:
                    vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
                    field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
                    type_code = struct.unpack_from('<I', entry_data, 0x0C)[0]
                    slot_idx = struct.unpack_from('<I', entry_data, 0x18)[0]
                    flags = struct.unpack_from('<I', entry_data, 0x1C)[0]
                    size = struct.unpack_from('<I', entry_data, 0x30)[0]

                    # Tail data
                    getter1 = struct.unpack_from('<Q', entry_data, 0x58)[0]
                    related_fid = struct.unpack_from('<I', entry_data, 0x60)[0]
                    getter2 = struct.unpack_from('<Q', entry_data, 0x68)[0]

                    getter1_rva = getter1 - mod_base if getter1 > mod_base else 0
                    getter2_rva = getter2 - mod_base if getter2 > mod_base else 0

                    print(f"\n  Entry {idx}: field_id=0x{field_id:08x} type={type_code} slot={slot_idx} size={size} flags=0x{flags:08x}")
                    print(f"    Getter1 VA=0x{getter1:x} (RVA 0x{getter1_rva:x})")
                    print(f"    Related field_id=0x{related_fid:08x}")
                    print(f"    Getter2 VA=0x{getter2:x} (RVA 0x{getter2_rva:x})")

                    # Dump full tail
                    print(f"    Full tail (bytes 0x40-0xA7):")
                    for t in range(0x40, 0xA8, 16):
                        row = entry_data[t:min(t+16, 0xA8)]
                        hex_str = ' '.join(f'{b:02x}' for b in row)
                        vals = []
                        for q in range(0, len(row), 8):
                            if t + q + 8 <= 0xA8:
                                v = struct.unpack_from('<Q', entry_data, t + q)[0]
                                vals.append(f"0x{v:x}")
                        print(f"      +0x{t:02x}: {hex_str}  ({', '.join(vals)})")

        # =====================================================================
        # PART 4: Disassemble getter functions
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 4: DISASSEMBLING GETTER FUNCTIONS FROM TAIL")
        print(f"{'='*80}")

        # Collect unique getter RVAs from first 20 entries
        getter_rvas = set()
        getter_map = {}  # rva -> [(field_id, type_code, slot_idx)]

        for idx in range(20):
            entry_va = cache_ptr + idx * 0xA8
            entry_data = rpm(handle, entry_va, 0xA8)
            if entry_data:
                field_id = struct.unpack_from('<I', entry_data, 0x08)[0]
                type_code = struct.unpack_from('<I', entry_data, 0x0C)[0]
                slot_idx = struct.unpack_from('<I', entry_data, 0x18)[0]
                getter1 = struct.unpack_from('<Q', entry_data, 0x58)[0]
                getter2 = struct.unpack_from('<Q', entry_data, 0x68)[0]

                g1_rva = getter1 - mod_base if getter1 > mod_base else 0
                g2_rva = getter2 - mod_base if getter2 > mod_base else 0

                getter_rvas.add(g1_rva)
                getter_rvas.add(g2_rva)

                if g1_rva not in getter_map:
                    getter_map[g1_rva] = []
                getter_map[g1_rva].append((field_id, type_code, slot_idx, 'getter1'))

                if g2_rva not in getter_map:
                    getter_map[g2_rva] = []
                getter_map[g2_rva].append((field_id, type_code, slot_idx, 'getter2'))

        print(f"\n  Unique getter RVAs from first 20 entries: {len(getter_rvas)}")
        for rva in sorted(getter_rvas):
            fields = getter_map.get(rva, [])
            field_str = ', '.join(f"0x{fid:08x}(type={tc},slot={si})" for fid,tc,si,role in fields[:3])
            print(f"    RVA 0x{rva:x}: used by {field_str}{'...' if len(fields) > 3 else ''}")

        # Disassemble each unique getter (first 256 bytes)
        for rva in sorted(getter_rvas):
            if rva < 0x1000:  # skip null/invalid
                continue
            va = mod_base + rva
            code = rpm(handle, va, 256)
            if not code:
                continue

            fields = getter_map.get(rva, [])
            field_str = ', '.join(f"0x{fid:08x}" for fid,tc,si,role in fields[:3])
            roles = set(role for _,_,_,role in fields)

            print(f"\n  --- Getter at RVA 0x{rva:x} ({','.join(roles)}) fields: {field_str} ---")

            # Dump first 128 bytes
            for j in range(0, min(128, len(code)), 16):
                row = code[j:j+16]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
                print(f"    +0x{j:03x} (RVA 0x{rva+j:06x}): {hex_str:<48s} {ascii_str}")

            # Look for key patterns in the getter:
            # - MUL or SHL for array indexing
            # - MOV with offsets into a record struct
            # - RET pattern to find function boundaries

            # Find first RET (C3)
            ret_off = None
            for i in range(len(code)):
                if code[i] == 0xC3:
                    ret_off = i
                    break

            if ret_off is not None and ret_off < 128:
                print(f"    Function size: ~{ret_off + 1} bytes (RET at +0x{ret_off:x})")

            # Search for interesting operations
            for i in range(min(len(code), 200) - 4):
                # IMUL with immediate
                if code[i] in (0x69, 0x6B):
                    context = code[i:min(i+8, len(code))]
                    hex_str = ' '.join(f'{b:02x}' for b in context)
                    print(f"    IMUL at +0x{i:x}: {hex_str}")

                # SHL
                if i + 3 <= len(code) and code[i] in (0x48, 0x49, 0x4C, 0x4D):
                    if code[i+1] == 0xC1:
                        modrm = code[i+2]
                        if (modrm >> 3) & 7 == 4:  # SHL
                            shift = code[i+3] if i+3 < len(code) else 0
                            reg = modrm & 7
                            context = code[i:min(i+5, len(code))]
                            hex_str = ' '.join(f'{b:02x}' for b in context)
                            print(f"    SHL r{reg}, {shift} at +0x{i:x}: {hex_str}")

        # =====================================================================
        # PART 5: Collect ALL unique getter RVAs across field types
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 5: GETTER FUNCTION SURVEY (by type_code)")
        print(f"{'='*80}")

        type_getters = {}  # type_code -> set of getter RVAs

        # Read ALL 1444 Task entries
        for idx in range(1444):
            entry_va = cache_ptr + idx * 0xA8
            entry_data = rpm(handle, entry_va, 0xA8)
            if not entry_data or len(entry_data) < 0xA8:
                continue

            vtable_ptr = struct.unpack_from('<Q', entry_data, 0x00)[0]
            propinfo_vtable_rva = vtable_ptr - mod_base if vtable_ptr > mod_base else 0
            # Check it's a valid PropInfoData
            if propinfo_vtable_rva not in (0x1050670, 0x0fb4a80):
                continue

            type_code = struct.unpack_from('<I', entry_data, 0x0C)[0]
            getter1 = struct.unpack_from('<Q', entry_data, 0x58)[0]
            getter2 = struct.unpack_from('<Q', entry_data, 0x68)[0]

            g1_rva = getter1 - mod_base if getter1 > mod_base else 0
            g2_rva = getter2 - mod_base if getter2 > mod_base else 0

            if type_code not in type_getters:
                type_getters[type_code] = {'g1': set(), 'g2': set()}
            type_getters[type_code]['g1'].add(g1_rva)
            type_getters[type_code]['g2'].add(g2_rva)

        type_names = {0:'null', 2:'i16', 3:'i32', 5:'i64', 8:'text', 9:'obj_ref',
                      11:'bool', 19:'date32', 20:'comp_dur', 21:'order_id', 29:'virtual', 72:'guid'}

        for tc in sorted(type_getters.keys()):
            tn = type_names.get(tc, f'unk_{tc}')
            g1s = type_getters[tc]['g1']
            g2s = type_getters[tc]['g2']
            print(f"\n  Type {tc} ({tn}):")
            print(f"    Getter1 RVAs ({len(g1s)}): {', '.join(f'0x{r:x}' for r in sorted(g1s))}")
            print(f"    Getter2 RVAs ({len(g2s)}): {', '.join(f'0x{r:x}' for r in sorted(g2s))}")

        # =====================================================================
        # PART 6: Detailed disassembly of representative getters per type
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"PART 6: REPRESENTATIVE GETTER DISASSEMBLY PER TYPE")
        print(f"{'='*80}")

        # Pick one getter1 per type and dump
        for tc in sorted(type_getters.keys()):
            if tc in (0, 29):  # skip null and virtual
                continue
            tn = type_names.get(tc, f'unk_{tc}')
            g1s = sorted(type_getters[tc]['g1'])
            if not g1s or g1s[0] < 0x1000:
                continue

            rva = g1s[0]
            va = mod_base + rva
            code = rpm(handle, va, 256)
            if not code:
                continue

            # Find RET
            func_len = len(code)
            for i in range(len(code)):
                if code[i] == 0xC3:
                    func_len = i + 1
                    break

            print(f"\n  --- Type {tc} ({tn}) getter1 at RVA 0x{rva:x} ({func_len} bytes) ---")
            for j in range(0, min(func_len + 16, len(code)), 16):
                row = code[j:min(j+16, len(code))]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                is_ret = any(code[j+k] == 0xC3 for k in range(min(16, len(row))) if j+k < func_len)
                marker = " <<<RET" if is_ret and j >= func_len - 16 else ""
                print(f"    +0x{j:03x} (RVA 0x{rva+j:06x}): {hex_str}{marker}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
