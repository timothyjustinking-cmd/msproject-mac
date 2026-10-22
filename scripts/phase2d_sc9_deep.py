#!/usr/bin/env python3
"""Deep SC9 hyperlink analysis: differential test + structure decode.

Goal: Find authoritative copy, decode compound record header,
and trace back to containing data structure.
"""

import ctypes
import ctypes.wintypes
import struct
import sys
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

try:
    import win32com.client
except ImportError:
    print("ERROR: pywin32 required"); sys.exit(1)

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
OpenProcess = k32.OpenProcess
ReadProcessMemory = k32.ReadProcessMemory
CloseHandle = k32.CloseHandle
VirtualQueryEx = k32.VirtualQueryEx

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", ctypes.wintypes.DWORD),
        ("RegionSize", ctypes.c_size_t),
        ("State", ctypes.wintypes.DWORD),
        ("Protect", ctypes.wintypes.DWORD),
        ("Type", ctypes.wintypes.DWORD),
    ]

MEM_COMMIT = 0x1000

def get_pid():
    import subprocess
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq WINPROJ.EXE', '/FO', 'CSV', '/NH'],
                       capture_output=True, text=True)
    for line in r.stdout.strip().split('\n'):
        if 'WINPROJ' in line:
            return int(line.split(',')[1].strip('"'))
    return None

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]

def search_memory(handle, needle_bytes, max_results=30):
    results = []
    addr = 0
    mbi = MEMORY_BASIC_INFORMATION()
    mbi_size = ctypes.sizeof(mbi)
    while addr < 0x7FFFFFFFFFFF:
        ret = VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), mbi_size)
        if ret == 0: break
        base = mbi.BaseAddress or 0
        size = mbi.RegionSize or 0
        if mbi.State == MEM_COMMIT and size > 0 and size < 0x10000000:
            protect = mbi.Protect & 0xFF
            if protect not in (0x01, 0x00):
                data = read_mem(handle, base, size)
                if data:
                    offset = 0
                    while True:
                        idx = data.find(needle_bytes, offset)
                        if idx == -1: break
                        results.append(base + idx)
                        if len(results) >= max_results: return results
                        offset = idx + 1
        next_addr = base + max(size, 0x1000)
        if next_addr <= addr: break
        addr = next_addr
    return results

def search_pointer(handle, target_addr, max_results=10):
    needle = struct.pack('<Q', target_addr)
    return search_memory(handle, needle, max_results)


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    prio_addr = 0x23b7ef6dd3c

    # Phase 1: Set marker A
    marker_a = "http://MARKER-ALPHA-11.test"
    task.Hyperlink = "LINK_ALPHA_11"
    task.HyperlinkAddress = marker_a
    task.HyperlinkSubAddress = "SUB_ALPHA"
    time.sleep(0.5)

    needle_a = marker_a.encode('utf-16-le')
    hits_a = search_memory(handle, needle_a)
    print(f"Phase 1 (ALPHA): {len(hits_a)} hits")
    for a in hits_a:
        print(f"  0x{a:012x} (prio{a-prio_addr:+d})")

    # Phase 2: Change to marker B
    marker_b = "http://MARKER-BRAVO-22.test"
    task.Hyperlink = "LINK_BRAVO_22"
    task.HyperlinkAddress = marker_b
    task.HyperlinkSubAddress = "SUB_BRAVO"
    time.sleep(0.5)

    needle_b = marker_b.encode('utf-16-le')
    hits_b = search_memory(handle, needle_b)
    hits_a2 = search_memory(handle, needle_a)

    print(f"\nPhase 2 (BRAVO): {len(hits_b)} hits, ALPHA still: {len(hits_a2)} hits")

    set_a = set(hits_a)
    set_b = set(hits_b)
    set_a2 = set(hits_a2)

    overwritten = set_a - set_a2  # Had ALPHA, now don't
    new_b = set_b - set_a  # Didn't exist for ALPHA
    survived_a = set_a & set_a2  # Still have ALPHA (stale)

    print(f"\nOverwritten (authoritative): {len(overwritten)}")
    for a in sorted(overwritten):
        # Check if BRAVO is nearby (within 100 bytes)
        found_b = False
        for b in hits_b:
            if abs(b - a) < 200:
                found_b = True
                break
        tag = " -> BRAVO nearby" if found_b else ""
        print(f"  0x{a:012x} (prio{a-prio_addr:+d}){tag}")

    print(f"\nNew BRAVO locations: {len(new_b)}")
    for b in sorted(new_b):
        print(f"  0x{b:012x} (prio{b-prio_addr:+d})")

    print(f"\nStale ALPHA (cache/undo): {len(survived_a)}")
    for a in sorted(survived_a):
        print(f"  0x{a:012x} (prio{a-prio_addr:+d})")

    # Phase 3: Deep decode of first overwritten address
    # Check all BRAVO hits that replaced ALPHA locations
    for b_addr in sorted(hits_b):
        # Find if this replaced an ALPHA location (within a few bytes for in-place overwrite)
        replaced = any(abs(b_addr - a) < 200 for a in overwritten)
        if not replaced:
            continue

        print(f"\n{'='*80}")
        print(f"AUTHORITATIVE compound record near 0x{b_addr:012x}")

        # Read 512 bytes before and 512 after
        ctx_start = b_addr - 512
        ctx = read_mem(handle, ctx_start, 1024)
        if not ctx:
            continue

        # Find the actual start of the compound record by scanning backwards
        # Look for the Hyperlink name "LINK_BRAVO_22" before the address
        hl_name = "LINK_BRAVO_22".encode('utf-16-le')
        hl_pos = ctx.find(hl_name)
        if hl_pos >= 0:
            record_start = ctx_start + hl_pos
            print(f"Record starts at 0x{record_start:012x} (Hyperlink name)")

            # Now look BEFORE the name for the record header
            if hl_pos >= 64:
                pre = ctx[hl_pos-64:hl_pos]
                print(f"\n64 bytes BEFORE Hyperlink name:")
                for off in range(0, 64, 16):
                    chunk = pre[off:off+16]
                    hex_str = ' '.join(f'{b:02x}' for b in chunk)
                    abs_addr = record_start - 64 + off
                    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                    print(f"  0x{abs_addr:012x}: {hex_str:<48s} |{ascii_str}|")

            # Decode the compound record
            rec_off = hl_pos
            # Find null terminator of Hyperlink name
            null_pos = ctx.find(b'\x00\x00', rec_off)
            while null_pos >= 0 and (null_pos - rec_off) % 2 != 0:
                null_pos = ctx.find(b'\x00\x00', null_pos + 1)
            if null_pos >= 0:
                name_end = null_pos + 2
                print(f"\nCompound record layout:")
                name_bytes = ctx[rec_off:name_end]
                try:
                    name_str = name_bytes.decode('utf-16-le').rstrip('\0')
                except:
                    name_str = "<decode error>"
                print(f"  [0x{record_start:012x}] Component 0 (Hyperlink): \"{name_str}\"")

                pos = name_end
                comp_idx = 1
                while pos + 12 < len(ctx):
                    byte_len = struct.unpack_from('<I', ctx, pos)[0]
                    comp_id = struct.unpack_from('<I', ctx, pos + 4)[0]
                    hdr_val = struct.unpack_from('<I', ctx, pos + 8)[0]

                    if byte_len > 0x10000 or comp_id > 10:
                        break

                    abs_pos = ctx_start + pos
                    print(f"  [0x{abs_pos:012x}] Header: byte_len={byte_len}, comp_id={comp_id}, hdr={hdr_val}")

                    str_start = pos + 12
                    if byte_len > 0 and str_start + byte_len <= len(ctx):
                        str_data = ctx[str_start:str_start + byte_len]
                        try:
                            s = str_data.decode('utf-16-le').rstrip('\0')
                        except:
                            s = f"<raw: {str_data[:20].hex()}>"
                        print(f"  [0x{ctx_start+str_start:012x}] Component {comp_id}: \"{s}\"")
                        pos = str_start + byte_len
                    elif byte_len == 0:
                        print(f"  Component {comp_id}: (empty)")
                        pos = str_start
                    else:
                        break

                    comp_idx += 1
                    if comp_idx > 6:
                        break

                # Show what follows the compound record
                end_pos = pos
                if end_pos + 32 < len(ctx):
                    print(f"\n  After compound record:")
                    for off in range(0, 32, 16):
                        chunk = ctx[end_pos+off:end_pos+off+16]
                        hex_str = ' '.join(f'{b:02x}' for b in chunk)
                        print(f"    0x{ctx_start+end_pos+off:012x}: {hex_str}")

        break  # Just analyze first authoritative copy

    # Phase 4: Search for pointers to the authoritative compound record
    if hits_b:
        first_auth = None
        for b_addr in sorted(hits_b):
            replaced = any(abs(b_addr - a) < 200 for a in overwritten)
            if replaced:
                first_auth = b_addr
                break

        if first_auth:
            # Search for the name start (approximate)
            hl_name = "LINK_BRAVO_22".encode('utf-16-le')
            name_hits = search_memory(handle, hl_name, 10)
            for nh in name_hits:
                if abs(nh - first_auth) < 200:
                    print(f"\n{'='*80}")
                    print(f"Searching for pointers TO compound record at 0x{nh:012x}...")
                    ptrs = search_pointer(handle, nh, 10)
                    print(f"Found {len(ptrs)} pointers")
                    for p in ptrs:
                        rel = p - prio_addr
                        print(f"  0x{p:012x} (prio{rel:+d})")
                        ctx = read_mem(handle, p - 32, 80)
                        if ctx:
                            for off in range(0, 80, 16):
                                chunk = ctx[off:off+16]
                                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                                tag = " <<< PTR" if off <= 32 < off + 16 else ""
                                print(f"    0x{p-32+off:012x}: {hex_str}{tag}")
                    break

    # Reset
    print("\n\nResetting...")
    task.Hyperlink = ""
    task.HyperlinkAddress = ""
    task.HyperlinkSubAddress = ""

    CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
