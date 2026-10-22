#!/usr/bin/env python3
"""Probe SC7 Notes/RTF storage.

SC7 has 21 fields: Notes, Text1-30 (some mapped as SC7 not SC9).
Notes is the most distinctive — it stores RTF data.

Strategy: Set task Notes to a distinctive string, search memory for it.
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

def search_memory(handle, needle_bytes, max_results=20):
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
    tn_addr = 0x23b6384bad0

    # Set Notes via COM
    marker = "XYZZY_NOTES_77_TEST"
    print(f"Setting Notes to '{marker}'...")
    try:
        task.Notes = marker
        print(f"  Success. Read-back: '{task.Notes}'")
    except Exception as e:
        print(f"  FAILED: {e}")
        CloseHandle(handle)
        return

    time.sleep(0.5)

    # Search for plain text (UTF-16LE)
    needle_utf16 = marker.encode('utf-16-le')
    print(f"\nSearching for Notes as UTF-16LE...")
    hits_utf16 = search_memory(handle, needle_utf16)
    print(f"Found {len(hits_utf16)} UTF-16 hits")
    for addr in hits_utf16:
        print(f"  0x{addr:012x} (prio{addr-prio_addr:+d}, tn{addr-tn_addr:+d})")
        if tn_addr <= addr <= tn_addr + 1490:
            print(f"    -> INSIDE TASKNODE")
        ctx = read_mem(handle, addr - 32, 96)
        if ctx:
            for off in range(0, 96, 16):
                chunk = ctx[off:off+16]
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                tag = " <<<" if off <= 32 < off + 16 else ""
                print(f"    0x{addr-32+off:012x}: {hex_str:<48s} |{ascii_str}|{tag}")

    # Search for RTF-encoded version (Notes often stored as RTF internally)
    # RTF would be ASCII: {\rtf1\ansi... XYZZY_NOTES_77_TEST ...}
    needle_ascii = marker.encode('ascii')
    print(f"\nSearching for Notes as ASCII (RTF content)...")
    hits_ascii = search_memory(handle, needle_ascii)
    # Filter out UTF-16 hits (which also contain ASCII bytes)
    hits_rtf = []
    for addr in hits_ascii:
        is_utf16 = any(abs(addr - u) < 2 for u in hits_utf16)
        if not is_utf16:
            hits_rtf.append(addr)
    print(f"Found {len(hits_rtf)} ASCII/RTF hits (excluding UTF-16)")
    for addr in hits_rtf:
        print(f"  0x{addr:012x} (prio{addr-prio_addr:+d})")
        ctx = read_mem(handle, addr - 64, 192)
        if ctx:
            # Look for RTF header {\rtf
            rtf_start = ctx.find(b'{\\rtf')
            if rtf_start >= 0:
                print(f"    RTF header found at offset -{64-rtf_start}")
                rtf_data = ctx[rtf_start:rtf_start+120]
                print(f"    RTF: {rtf_data[:100]}")
            else:
                for off in range(0, 192, 16):
                    chunk = ctx[off:off+16]
                    hex_str = ' '.join(f'{b:02x}' for b in chunk)
                    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                    tag = " <<<" if off <= 64 < off + 16 else ""
                    print(f"    0x{addr-64+off:012x}: {hex_str:<48s} |{ascii_str}|{tag}")

    # Also try Text1 (SC7 in some configurations)
    print(f"\n{'='*80}")
    print("Setting Text1...")
    text1_marker = "TEXT1_SC7_PROBE_42"
    try:
        task.Text1 = text1_marker
        print(f"  Text1 = '{task.Text1}'")
    except Exception as e:
        print(f"  FAILED: {e}")

    time.sleep(0.3)

    needle_t1 = text1_marker.encode('utf-16-le')
    hits_t1 = search_memory(handle, needle_t1)
    print(f"Found {len(hits_t1)} UTF-16 hits for Text1")
    for addr in hits_t1:
        rel_tn = addr - tn_addr
        in_tn = " INSIDE_TN" if 0 <= rel_tn <= 1490 else ""
        print(f"  0x{addr:012x} (prio{addr-prio_addr:+d}, tn{rel_tn:+d}){in_tn}")

    # Differential test for Notes
    print(f"\n{'='*80}")
    print("Differential test for Notes...")
    marker_b = "PLUGH_NOTES_88_DIFF"
    task.Notes = marker_b
    time.sleep(0.3)

    needle_b = marker_b.encode('utf-16-le')
    hits_b_utf16 = search_memory(handle, needle_b)
    hits_a_still = search_memory(handle, needle_utf16)

    print(f"PLUGH hits: {len(hits_b_utf16)}, XYZZY still: {len(hits_a_still)}")

    overwritten = set(hits_utf16) - set(hits_a_still)
    print(f"Overwritten (authoritative): {len(overwritten)}")
    for a in sorted(overwritten):
        print(f"  0x{a:012x} (prio{a-prio_addr:+d})")

    # Check RTF for marker_b
    needle_b_ascii = marker_b.encode('ascii')
    hits_b_rtf = search_memory(handle, needle_b_ascii)
    hits_b_rtf = [a for a in hits_b_rtf if not any(abs(a - u) < 2 for u in hits_b_utf16)]

    needle_a_ascii = marker.encode('ascii')
    hits_a_rtf_still = search_memory(handle, needle_a_ascii)
    hits_a_rtf_still = [a for a in hits_a_rtf_still if not any(abs(a - u) < 2 for u in hits_a_still)]

    print(f"\nRTF: PLUGH hits: {len(hits_b_rtf)}, XYZZY still: {len(hits_a_rtf_still)}")
    overwritten_rtf = set(hits_rtf) - set(hits_a_rtf_still)
    print(f"Overwritten RTF (authoritative): {len(overwritten_rtf)}")
    for a in sorted(overwritten_rtf):
        print(f"  0x{a:012x}")

    # Reset
    print("\nResetting...")
    task.Notes = ""
    task.Text1 = ""
    CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
