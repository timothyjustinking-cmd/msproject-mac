#!/usr/bin/env python3
"""Phase 2d: Disassemble vfunc[158] at runtime to trace the field lookup path.

We know:
- vfunc[158] is at RVA 0x239380 (TTaskTable/TAssnTable shared)
- It does binary search + SHL RDX, 6
- But the actual cache stride is 0xA8, not 0x40
- Need to find the ACTUAL addressing after SHL 6

Dumps raw bytes and does a simple x86-64 disassembly of the key section.
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
        raise OSError(f"RPM fail 0x{address:x}: err {ctypes.get_last_error()}")
    return buf.raw[:br.value]


def simple_disasm(code, base_va, mod_base):
    """Basic x86-64 disassembler for the instructions we care about.
    Focuses on SHL, LEA, MOV, ADD, IMUL, and REX prefixes.
    Returns list of (offset, hex, description) tuples.
    """
    lines = []
    i = 0
    while i < len(code):
        start = i
        va = base_va + i
        rva = va - mod_base

        # Read up to 15 bytes for one instruction
        raw = code[i:min(i+15, len(code))]
        if not raw:
            break

        # Parse prefixes
        rex = 0
        has_rex = False
        prefix_66 = False
        prefix_f2 = False
        prefix_f3 = False

        j = 0
        while j < len(raw):
            b = raw[j]
            if b == 0x66:
                prefix_66 = True; j += 1
            elif b == 0xF2:
                prefix_f2 = True; j += 1
            elif b == 0xF3:
                prefix_f3 = True; j += 1
            elif 0x40 <= b <= 0x4F:
                rex = b; has_rex = True; j += 1
            else:
                break

        if j >= len(raw):
            lines.append((rva, raw[:1].hex(), "???"))
            i += 1
            continue

        opcode = raw[j]
        desc = ""
        inst_len = 0

        regs64 = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI',
                   'R8','R9','R10','R11','R12','R13','R14','R15']
        regs32 = ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI',
                   'R8D','R9D','R10D','R11D','R12D','R13D','R14D','R15D']

        def modrm_reg(modrm, rex_r=False):
            r = (modrm >> 3) & 7
            if rex_r or (rex & 4):
                r += 8
            return r

        def modrm_rm(modrm, rex_b=False):
            rm = modrm & 7
            if rex_b or (rex & 1):
                rm += 8
            return rm

        # SHL reg, imm8 (C1 /4)
        if opcode == 0xC1 and j+2 < len(raw):
            modrm = raw[j+1]
            op_ext = (modrm >> 3) & 7
            if op_ext == 4:  # SHL
                rm = modrm_rm(modrm)
                imm = raw[j+2]
                rname = regs64[rm] if (rex & 8) else regs32[rm]
                desc = f"SHL {rname}, 0x{imm:x}"
                inst_len = j + 3

        # LEA with RIP-relative (8D + ModRM with mod=00, rm=5)
        elif opcode == 0x8D and j+1 < len(raw):
            modrm = raw[j+1]
            mod = (modrm >> 6) & 3
            rm = modrm & 7
            reg = modrm_reg(modrm)
            rname = regs64[reg + (8 if rex & 4 else 0)]
            if mod == 0 and rm == 5 and j+6 <= len(raw):  # RIP-relative
                disp = struct.unpack_from('<i', raw, j+2)[0]
                target = va + j + 6 + disp
                target_rva = target - mod_base
                desc = f"LEA {rname}, [RIP+0x{disp:x}]  ; VA 0x{target:x} (RVA 0x{target_rva:x})"
                inst_len = j + 6
            elif mod == 2 and j+6 <= len(raw):  # [reg+disp32]
                disp = struct.unpack_from('<i', raw, j+2)[0]
                base_r = regs64[modrm_rm(modrm)]
                desc = f"LEA {rname}, [{base_r}+0x{disp:x}]"
                inst_len = j + 6
            elif mod == 1 and j+3 <= len(raw):  # [reg+disp8]
                disp = struct.unpack_from('<b', raw, j+2)[0]
                base_r = regs64[modrm_rm(modrm)]
                desc = f"LEA {rname}, [{base_r}+0x{disp & 0xff:x}]"
                inst_len = j + 3
            elif mod == 0 and rm == 4 and j+3 <= len(raw):  # SIB
                sib = raw[j+2]
                scale = 1 << ((sib >> 6) & 3)
                idx = (sib >> 3) & 7
                if rex & 2: idx += 8
                base_sib = sib & 7
                if rex & 1: base_sib += 8
                idx_name = regs64[idx] if idx != 4 else "none"
                base_name = regs64[base_sib]
                desc = f"LEA {rname}, [{base_name}+{idx_name}*{scale}]"
                inst_len = j + 3

        # MOV reg, [rip+disp32]  (8B + modrm mod=00 rm=5)
        elif opcode == 0x8B and j+1 < len(raw):
            modrm = raw[j+1]
            mod = (modrm >> 6) & 3
            rm = modrm & 7
            reg = modrm_reg(modrm)
            rname = regs64[reg + (8 if rex & 4 else 0)] if (rex & 8) else regs32[reg + (8 if rex & 4 else 0)]
            if mod == 0 and rm == 5 and j+6 <= len(raw):
                disp = struct.unpack_from('<i', raw, j+2)[0]
                target = va + j + 6 + disp
                target_rva = target - mod_base
                desc = f"MOV {rname}, [RIP+0x{disp:x}]  ; VA 0x{target:x} (RVA 0x{target_rva:x})"
                inst_len = j + 6

        # ADD reg, reg or similar
        elif opcode == 0x03 and j+1 < len(raw):
            modrm = raw[j+1]
            reg = modrm_reg(modrm)
            rm = modrm_rm(modrm)
            rname = regs64[reg + (8 if rex & 4 else 0)]
            rm_name = regs64[rm]
            desc = f"ADD {rname}, {rm_name}"
            inst_len = j + 2

        # IMUL reg, reg, imm (69 or 6B)
        elif opcode == 0x69 and j+6 <= len(raw):
            modrm = raw[j+1]
            reg = modrm_reg(modrm)
            rm = modrm_rm(modrm)
            imm = struct.unpack_from('<i', raw, j+2)[0]
            rname = regs64[reg + (8 if rex & 4 else 0)]
            rm_name = regs64[rm]
            desc = f"IMUL {rname}, {rm_name}, 0x{imm:x}"
            inst_len = j + 6
        elif opcode == 0x6B and j+3 <= len(raw):
            modrm = raw[j+1]
            reg = modrm_reg(modrm)
            rm = modrm_rm(modrm)
            imm = struct.unpack_from('<b', raw, j+2)[0]
            rname = regs64[reg + (8 if rex & 4 else 0)]
            rm_name = regs64[rm]
            desc = f"IMUL {rname}, {rm_name}, 0x{imm & 0xff:x}"
            inst_len = j + 3

        # RET
        elif opcode == 0xC3:
            desc = "RET"
            inst_len = j + 1

        # CALL rel32
        elif opcode == 0xE8 and j+5 <= len(raw):
            disp = struct.unpack_from('<i', raw, j+1)[0]
            target = va + j + 5 + disp
            target_rva = target - mod_base
            desc = f"CALL 0x{target:x} (RVA 0x{target_rva:x})"
            inst_len = j + 5

        # JMP rel32
        elif opcode == 0xE9 and j+5 <= len(raw):
            disp = struct.unpack_from('<i', raw, j+1)[0]
            target = va + j + 5 + disp
            desc = f"JMP 0x{target:x}"
            inst_len = j + 5

        # Jcc rel32 (0F 8x)
        elif opcode == 0x0F and j+1 < len(raw) and 0x80 <= raw[j+1] <= 0x8F and j+6 <= len(raw):
            cc = raw[j+1] - 0x80
            cc_names = ['O','NO','B','NB','Z','NZ','BE','A','S','NS','PE','PO','L','GE','LE','G']
            disp = struct.unpack_from('<i', raw, j+2)[0]
            target = va + j + 6 + disp
            desc = f"J{cc_names[cc]} 0x{target:x}"
            inst_len = j + 6

        # Jcc rel8 (7x)
        elif 0x70 <= opcode <= 0x7F and j+2 <= len(raw):
            cc = opcode - 0x70
            cc_names = ['O','NO','B','NB','Z','NZ','BE','A','S','NS','PE','PO','L','GE','LE','G']
            disp = struct.unpack_from('<b', raw, j+1)[0]
            target = va + j + 2 + disp
            desc = f"J{cc_names[cc]} 0x{target:x}"
            inst_len = j + 2

        # CMP (39, 3B, 3D, 81/7)
        elif opcode == 0x39 and j+2 <= len(raw):
            modrm = raw[j+1]
            reg = modrm_reg(modrm)
            rm = modrm_rm(modrm)
            rname = regs64[reg + (8 if rex & 4 else 0)] if (rex & 8) else regs32[reg + (8 if rex & 4 else 0)]
            rm_name = regs64[rm] if (rex & 8) else regs32[rm]
            desc = f"CMP {rm_name}, {rname}"
            inst_len = j + 2

        # TEST (85)
        elif opcode == 0x85 and j+2 <= len(raw):
            modrm = raw[j+1]
            reg = modrm_reg(modrm)
            rm = modrm_rm(modrm)
            rname = regs64[reg + (8 if rex & 4 else 0)] if (rex & 8) else regs32[reg + (8 if rex & 4 else 0)]
            rm_name = regs64[rm] if (rex & 8) else regs32[rm]
            desc = f"TEST {rm_name}, {rname}"
            inst_len = j + 2

        if not desc:
            # Unknown — just show hex byte
            desc = f"DB 0x{raw[0]:02x}"
            inst_len = 1

        hex_bytes = code[start:start + inst_len].hex()
        lines.append((rva, hex_bytes, desc))
        i = start + inst_len

    return lines


def main():
    print("Phase 2d: vfunc[158] Disassembly")
    print("=" * 80)

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
        # vfunc[158] at RVA 0x239380
        func_rva = 0x239380
        func_va = mod_base + func_rva
        print(f"vfunc[158] at VA 0x{func_va:x} (RVA 0x{func_rva:x})")

        # Read enough code (function is ~2733 bytes per summary)
        code = rpm(handle, func_va, 3000)
        print(f"Read {len(code)} bytes of code")

        # First, search for SHL instructions (C1 xx 06)
        print(f"\n{'='*80}")
        print("SEARCHING FOR SHL AND IMUL INSTRUCTIONS")
        print("-" * 60)

        for i in range(len(code) - 3):
            # SHL reg, 6 = C1 (modrm with /4) 06
            # Also check with REX prefix (48 C1 xx 06, 49 C1 xx 06)
            if code[i] == 0xC1 and code[i+2] == 0x06:
                modrm = code[i+1]
                if ((modrm >> 3) & 7) == 4:  # /4 = SHL
                    rm = modrm & 7
                    rva = func_rva + i
                    # Check for REX prefix
                    rex_w = False
                    prefix_start = i
                    if i > 0 and 0x40 <= code[i-1] <= 0x4F:
                        rex_w = bool(code[i-1] & 8)
                        prefix_start = i - 1
                    reg_name = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI'][rm]
                    context_start = max(0, prefix_start - 20)
                    context_end = min(len(code), i + 20)
                    context_hex = code[context_start:context_end].hex()
                    print(f"  SHL {reg_name}, 6 at offset +0x{i:x} (RVA 0x{rva:x})")
                    print(f"    Context: ...{context_hex}...")

            # IMUL with 0xA8 immediate (check for IMUL r, r/m, 0xA8)
            # 6B = IMUL r, r/m, imm8; 69 = IMUL r, r/m, imm32
            if code[i] == 0x6B and i+3 <= len(code) and code[i+2] == 0xA8:
                rva = func_rva + i
                print(f"  IMUL ..., 0xA8 at offset +0x{i:x} (RVA 0x{rva:x})")
            if code[i] == 0x69 and i+6 <= len(code):
                imm = struct.unpack_from('<i', code, i+2)[0]
                if imm == 0xA8:
                    rva = func_rva + i
                    print(f"  IMUL ..., 0xA8 (32-bit) at offset +0x{i:x} (RVA 0x{rva:x})")

        # Also search for the SHL with REX prefix
        for i in range(len(code) - 4):
            if 0x40 <= code[i] <= 0x4F and code[i+1] == 0xC1 and code[i+3] == 0x06:
                modrm = code[i+2]
                if ((modrm >> 3) & 7) == 4:
                    rm = (modrm & 7) + (8 if code[i] & 1 else 0)
                    regs = ['RAX','RCX','RDX','RBX','RSP','RBP','RSI','RDI',
                            'R8','R9','R10','R11','R12','R13','R14','R15']
                    rva = func_rva + i
                    context_start = max(0, i - 20)
                    context_end = min(len(code), i + 25)
                    context_hex = code[context_start:context_end].hex()
                    print(f"  REX+SHL {regs[rm]}, 6 at offset +0x{i:x} (RVA 0x{rva:x})")
                    print(f"    Context: ...{context_hex}...")

        # Disassemble a window around each SHL site
        print(f"\n{'='*80}")
        print("DISASSEMBLY AROUND SHL SITES")
        print("-" * 60)

        shl_offsets = []
        for i in range(len(code) - 4):
            if 0x40 <= code[i] <= 0x4F and code[i+1] == 0xC1 and code[i+3] == 0x06:
                modrm = code[i+2]
                if ((modrm >> 3) & 7) == 4:
                    shl_offsets.append(i)
            elif code[i] == 0xC1 and i+2 < len(code) and code[i+2] == 0x06:
                modrm = code[i+1]
                if ((modrm >> 3) & 7) == 4:
                    shl_offsets.append(i)

        for shl_off in shl_offsets:
            window_start = max(0, shl_off - 40)
            window_end = min(len(code), shl_off + 60)
            window = code[window_start:window_end]
            window_va = func_va + window_start

            print(f"\n  --- Around SHL at offset +0x{shl_off:x} ---")
            lines = simple_disasm(window, window_va, mod_base)
            for rva, hexbytes, desc in lines:
                marker = " <<<" if abs(rva - (func_rva + shl_off)) < 5 else ""
                print(f"    0x{rva:08x}: {hexbytes:<30s} {desc}{marker}")

        # Also dump the first 200 bytes for the function prologue
        print(f"\n{'='*80}")
        print("FUNCTION PROLOGUE (first 200 bytes)")
        print("-" * 60)
        prologue = code[:200]
        lines = simple_disasm(prologue, func_va, mod_base)
        for rva, hexbytes, desc in lines:
            print(f"    0x{rva:08x}: {hexbytes:<30s} {desc}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
