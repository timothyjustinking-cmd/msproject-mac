#!/usr/bin/env python3
"""Phase 2d: Trace storage class dispatching for class 3 (unk_3, 671 Task fields).

Disassembles sub_4A1610 (storage class dispatcher) to find the handler for class 3,
then traces that handler to understand how slot_index maps to byte positions.
Also reads TASKNODE memory via a live task to validate the mapping.
"""

import ctypes
import ctypes.wintypes as wt
import struct
import os
import sys

try:
    import win32com.client
    import win32process
    import win32api
    import win32con
except ImportError:
    print("ERROR: pywin32 required"); sys.exit(1)

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

STORAGE_DISPATCHER_RVA = 0x4A1610
VTABLE158_RVA = 0x239380

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
    """Get actual image base of WINPROJ.EXE using EnumProcessModulesEx."""
    import ctypes
    psapi = ctypes.WinDLL('psapi', use_last_error=True)

    hProcess = OpenProcess(0x0410 | 0x0010, False, pid)
    if not hProcess:
        return None

    hMods = (ctypes.c_void_p * 1024)()
    cbNeeded = ctypes.c_ulong()

    if not psapi.EnumProcessModulesEx(hProcess, ctypes.byref(hMods), ctypes.sizeof(hMods),
                                       ctypes.byref(cbNeeded), 0x03):
        CloseHandle(hProcess)
        return None

    base = hMods[0]
    CloseHandle(hProcess)
    return base

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    addr_p = ctypes.c_void_p(addr)
    ok = ReadProcessMemory(handle, addr_p, buf, size, ctypes.byref(n))
    if not ok:
        err = ctypes.get_last_error()
        print(f"  ReadProcessMemory failed at {addr:#x} size={size}: error {err}")
        return None
    return buf.raw[:n.value]

def disasm(data, base_addr, count=100):
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    lines = []
    for insn in md.disasm(data, base_addr):
        lines.append(insn)
        if len(lines) >= count:
            break
    return lines


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
    if not IMAGE_BASE:
        print("ERROR: Can't get image base"); sys.exit(1)

    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")

    # Read and disassemble the storage class dispatcher
    disp_addr = IMAGE_BASE + STORAGE_DISPATCHER_RVA
    disp_data = read_mem(handle, disp_addr, 0x400)
    if not disp_data:
        print("ERROR: Can't read dispatcher"); sys.exit(1)

    print(f"\n{'='*90}")
    print(f"STORAGE CLASS DISPATCHER (sub_4A1610) at {disp_addr:#x}")
    print(f"{'='*90}")

    insns = disasm(disp_data, disp_addr, 200)

    # Find CMP instructions that check storage class values
    cmp_targets = {}
    branch_targets = {}
    current_cmp = None

    for i, insn in enumerate(insns):
        line = f"  {insn.address:#x}: {insn.mnemonic:8s} {insn.op_str}"

        # Track CMP with immediate values (storage class checks)
        if insn.mnemonic == 'cmp' and '#' not in insn.op_str:
            # Extract immediate from op_str like "ecx, 0xa"
            parts = insn.op_str.split(',')
            if len(parts) == 2:
                try:
                    val = int(parts[1].strip(), 0)
                    if 0 <= val <= 25:
                        current_cmp = val
                        cmp_targets[val] = insn.address
                except ValueError:
                    pass

        # Track conditional jumps after CMP
        if insn.mnemonic.startswith('j') and insn.mnemonic != 'jmp' and current_cmp is not None:
            try:
                target = int(insn.op_str, 0)
                branch_targets[current_cmp] = (insn.mnemonic, target)
            except ValueError:
                pass
            current_cmp = None
        elif insn.mnemonic == 'jmp':
            current_cmp = None

    # Print all instructions
    for insn in insns[:150]:
        marker = ''
        for cls, addr in cmp_targets.items():
            if addr == insn.address:
                marker = f'  <-- CMP storage_class == {cls}'
        print(f"  {insn.address:#x}: {insn.mnemonic:8s} {insn.op_str}{marker}")

    print(f"\n  Storage class check points:")
    for cls in sorted(cmp_targets.keys()):
        addr = cmp_targets[cls]
        branch = branch_targets.get(cls, ('?', 0))
        print(f"    class {cls:2d}: CMP at {addr:#x}, {branch[0]} -> {branch[1]:#x}")

    # Now trace vtable[158] to find where it accesses storage class 3
    print(f"\n{'='*90}")
    print(f"VTABLE[158] STORAGE CLASS 3 PATH")
    print(f"{'='*90}")

    vt158_addr = IMAGE_BASE + VTABLE158_RVA
    vt158_data = read_mem(handle, vt158_addr, 0x1200)
    if not vt158_data:
        print("ERROR: Can't read vtable[158]"); sys.exit(1)

    insns158 = disasm(vt158_data, vt158_addr, 600)

    # Find all references to storage_class offset (+0x4A in PropInfoData)
    # and slot_index offset (+0x28)
    print(f"\n  References to PropInfoData offsets in vtable[158]:")
    for insn in insns158:
        op = insn.op_str
        if '+0x4a]' in op.lower() or '+ 0x4a]' in op.lower():
            print(f"    {insn.address:#x}: {insn.mnemonic:8s} {op}  <-- storage_class")
        if '+0x28]' in op.lower() or '+ 0x28]' in op.lower():
            print(f"    {insn.address:#x}: {insn.mnemonic:8s} {op}  <-- slot_index")
        if '+0x48]' in op.lower() or '+ 0x48]' in op.lower():
            print(f"    {insn.address:#x}: {insn.mnemonic:8s} {op}  <-- type_code")

    # Look for the getter functions called from the default path
    # (class 3 should fall through to default in the storage dispatcher)
    print(f"\n  CALL targets in vtable[158] (first 30):")
    call_targets = []
    for insn in insns158:
        if insn.mnemonic == 'call':
            try:
                target = int(insn.op_str, 0)
                call_targets.append((insn.address, target))
                rva = target - IMAGE_BASE
                print(f"    {insn.address:#x}: call {target:#x} (RVA {rva:#x})")
            except ValueError:
                if 'r' in insn.op_str.lower() or 'qword' in insn.op_str.lower():
                    print(f"    {insn.address:#x}: call {insn.op_str} (indirect)")
            if len(call_targets) >= 30:
                break

    # Now let's trace what happens for a specific class-3 field
    # Read a class-3 field's PropInfoData from memory to understand the getter chain
    print(f"\n{'='*90}")
    print(f"SAMPLE CLASS-3 FIELDS FROM DEFINITIVE MAP")
    print(f"{'='*90}")

    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
    map_path = os.path.join(base_dir, 'field_definitive_map.tsv')

    class3_fields = []
    with open(map_path, 'r', encoding='utf-8') as f:
        f.readline()
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 18 and parts[0] == 'Task' and parts[10] == 'unk_3':
                class3_fields.append({
                    'field_id_hex': parts[1],
                    'field_id_dec': int(parts[2]),
                    'com_name': parts[3],
                    'type_code': int(parts[4]),
                    'type_name': parts[5],
                    'slot_index': int(parts[6]),
                    'field_size': int(parts[7]),
                    'storage_class': int(parts[9]),
                    'byte_offset_hex': parts[12],
                })

    print(f"  Total class-3 fields: {len(class3_fields)}")

    # Show slot_index distribution
    slots = [f['slot_index'] for f in class3_fields]
    print(f"  Slot index range: {min(slots)} - {max(slots)}")

    # Group by type
    by_type = {}
    for f in class3_fields:
        t = f['type_name']
        if t not in by_type:
            by_type[t] = []
        by_type[t].append(f)

    print(f"\n  By engine type:")
    for t in sorted(by_type.keys()):
        flds = by_type[t]
        slots = sorted(set(f['slot_index'] for f in flds))
        named = sum(1 for f in flds if f['com_name'])
        print(f"    {t:<10s}: {len(flds):3d} fields, {named:3d} named, slots {min(slots)}-{max(slots)}")

    # Show some named class-3 fields with their slot_index to understand the pattern
    print(f"\n  Named class-3 fields (first 30):")
    named_c3 = [f for f in class3_fields if f['com_name']]
    named_c3.sort(key=lambda x: x['slot_index'])
    for f in named_c3[:30]:
        print(f"    slot={f['slot_index']:3d} type={f['type_name']:<8s} size={f['field_size']} "
              f"offset={f['byte_offset_hex']:<8s} {f['com_name']}")

    # Check if slot_index directly maps to an array position
    # For class-3, if there's a base pointer + slot_index * element_size pattern
    print(f"\n  Slot index pattern analysis:")
    named_with_offsets = [(f['slot_index'], f['byte_offset_hex'], f['field_size'], f['com_name'])
                          for f in named_c3 if f['byte_offset_hex'] and f['byte_offset_hex'] != '0x0000']

    if named_with_offsets:
        for slot, offset_hex, size, name in named_with_offsets[:20]:
            try:
                offset = int(offset_hex, 16)
                print(f"    slot={slot:3d} offset={offset:#06x} size={size} ratio={offset/max(slot,1):.1f} {name}")
            except ValueError:
                print(f"    slot={slot:3d} offset={offset_hex} size={size} {name}")

    # Try to read a COM field value and correlate with raw TASKNODE bytes
    print(f"\n{'='*90}")
    print(f"COM VALUE vs RAW MEMORY CORRELATION")
    print(f"{'='*90}")

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    if not proj:
        print("ERROR: No project open")
        CloseHandle(handle)
        sys.exit(1)

    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    if not task:
        print("ERROR: No tasks")
        CloseHandle(handle)
        sys.exit(1)

    print(f"  Task: ID={task.ID} Name='{task.Name}'")

    # Read some class-3 fields via COM
    test_fields = [f for f in named_c3 if f['com_name'] in
                   ['Baseline1Start', 'Baseline1Finish', 'Baseline1Duration',
                    'Baseline1Work', 'Baseline1Cost',
                    'Baseline2Start', 'Baseline2Finish', 'Baseline2Duration',
                    'Baseline3Start', 'Baseline3Finish',
                    'EnterpriseText1', 'EnterpriseText2',
                    'EnterpriseNumber1', 'EnterpriseNumber2',
                    'EnterpriseDuration1', 'EnterpriseDuration2',
                    'EnterpriseDate1', 'EnterpriseDate2',
                    'EnterpriseCost1', 'EnterpriseCost2']]

    if not test_fields:
        test_fields = named_c3[:20]

    print(f"\n  Reading {len(test_fields)} class-3 fields via COM:")
    for f in test_fields:
        try:
            val = task.GetField(f['field_id_dec'])
            val_str = str(val)[:40].encode('ascii', errors='replace').decode('ascii')
            print(f"    {f['com_name']:<30s} slot={f['slot_index']:3d} = {val_str}")
        except Exception as e:
            print(f"    {f['com_name']:<30s} slot={f['slot_index']:3d} ERROR: {str(e)[:40]}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
