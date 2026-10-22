#!/usr/bin/env python3
"""Phase 2d: Find the separate 0x40-stride and 0x78-stride arrays.

vfunc[158] uses TWO different strides with different base pointers:
  - SHL*0x40 via R13 (4th param) for PropInfoData type lookup
  - IMUL*0x78 via this->member_0x58 for record access

The 0xA8-stride cache has interleaved PropInfoData+tail. The code must
access separate packed arrays. This script finds them.
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
        # Read the Task descriptor
        desc_rva = 0xf8bda0
        desc_va = mod_base + desc_rva
        desc_data = rpm(handle, desc_va, 0x70)

        cache_ptr = struct.unpack_from('<Q', desc_data, 0x08)[0]
        field_count = struct.unpack_from('<H', desc_data, 0x10)[0]
        secondary_desc = struct.unpack_from('<Q', desc_data, 0x58)[0]

        print(f"Task descriptor at VA 0x{desc_va:x}")
        print(f"  Cache base (+0x08): 0x{cache_ptr:x} (RVA 0x{cache_ptr - mod_base:x})")
        print(f"  Field count (+0x10): {field_count}")
        print(f"  Secondary desc (+0x58): 0x{secondary_desc:x} (RVA 0x{secondary_desc - mod_base:x})")

        # =====================================================================
        # TEST 1: Read the secondary descriptor at +0x58
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"TEST 1: SECONDARY DESCRIPTOR AT +0x58")
        print(f"{'='*80}")

        sec_data = rpm(handle, secondary_desc, 0x200)
        if sec_data:
            print(f"\n  First 0x100 bytes of secondary descriptor:")
            for j in range(0, min(0x100, len(sec_data)), 16):
                row = sec_data[j:j+16]
                hex_str = ' '.join(f'{b:02x}' for b in row)
                vals = []
                for q in range(0, min(16, len(row)), 8):
                    if j + q + 8 <= len(sec_data):
                        v = struct.unpack_from('<Q', sec_data, j + q)[0]
                        rva = v - mod_base if mod_base < v < mod_base + 0x20000000 else None
                        if rva is not None:
                            vals.append(f"RVA 0x{rva:x}")
                        else:
                            vals.append(f"0x{v:x}")
                print(f"    +0x{j:02x}: {hex_str}  ({', '.join(vals)})")

        # =====================================================================
        # TEST 2: Check if the 0xA8 cache actually contains TWO interleaved arrays
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"TEST 2: VERIFY 0x40-STRIDE VS 0xA8-STRIDE FIELD IDS")
        print(f"{'='*80}")

        # Read enough cache for first 10 entries at 0xA8 stride
        cache_size = 10 * 0xA8 + 0x40
        cache_data = rpm(handle, cache_ptr, cache_size)

        print(f"\n  Expected field IDs at 0xA8 stride (entries 0-9):")
        for i in range(10):
            off = i * 0xA8 + 0x08  # field_id at +0x08 in PropInfoData
            tc_off = i * 0xA8 + 0x0C  # type_code at +0x0C
            if off + 4 <= len(cache_data) and tc_off + 4 <= len(cache_data):
                fid = struct.unpack_from('<I', cache_data, off)[0]
                tc = struct.unpack_from('<I', cache_data, tc_off)[0]
                print(f"    [{i}] field_id=0x{fid:08x} type_code={tc}")

        print(f"\n  What 0x40 stride sees (potential garbage):")
        for i in range(10):
            off = i * 0x40 + 0x08
            tc_off = i * 0x40 + 0x0C
            if off + 4 <= len(cache_data) and tc_off + 4 <= len(cache_data):
                fid = struct.unpack_from('<I', cache_data, off)[0]
                tc = struct.unpack_from('<I', cache_data, tc_off)[0]
                vtable = struct.unpack_from('<Q', cache_data, i * 0x40)[0]
                vtable_rva = vtable - mod_base if vtable > mod_base else 0
                is_propinfo = vtable_rva in (0x1050670, 0x0fb4a80)
                print(f"    [{i}] field_id=0x{fid:08x} type_code={tc} vtable_rva=0x{vtable_rva:x} {'YES PropInfoData' if is_propinfo else 'NOT PropInfoData'}")

        # =====================================================================
        # TEST 3: Search for a SEPARATE packed 0x40-stride array
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"TEST 3: SEARCH FOR SEPARATE 0x40-STRIDE PROPINFODATA ARRAY")
        print(f"{'='*80}")

        # We know entry 0 has field_id=0x0b400000, type=5, slot=8
        # Entry 1 has field_id=0x0b400001, type=5, slot=8
        # If there's a packed 0x40-stride copy, entry[1] at stride*0x40 should
        # also have field_id=0x0b400001 and type=5

        # Strategy: search .data section for the pattern:
        # At offset 0x08: 0x0b400000 (field_id of entry 0)
        # At offset 0x48 (0x40 + 0x08): 0x0b400001 (field_id of entry 1)
        # At offset 0x88 (0x80 + 0x08): 0x0b400002 (field_id of entry 2)

        # Search a wide range of .data
        search_ranges = [
            (0x1200000, 0x1500000, ".data region 1"),
            (0x0f00000, 0x1200000, ".rdata/.data boundary"),
        ]

        propinfo_vtable_va = mod_base + 0x1050670
        propinfo_vtable_bytes = struct.pack('<Q', propinfo_vtable_va)

        for start_rva, end_rva, label in search_ranges:
            print(f"\n  Scanning {label} (RVA 0x{start_rva:x} - 0x{end_rva:x})...")

            chunk_size = 0x100000  # 1MB chunks
            for chunk_start in range(start_rva, end_rva, chunk_size):
                chunk_end = min(chunk_start + chunk_size + 0x100, end_rva)
                data = rpm(handle, mod_base + chunk_start, chunk_end - chunk_start)
                if not data:
                    continue

                # Search for PropInfoData vtable at 0x40 stride
                for i in range(0, len(data) - 0xC0):
                    # Check if [i] and [i+0x40] both start with PropInfoData vtable
                    if data[i:i+8] == propinfo_vtable_bytes:
                        if i + 0x40 + 8 <= len(data) and data[i+0x40:i+0x40+8] == propinfo_vtable_bytes:
                            # Both have PropInfoData vtable at 0x40 stride!
                            # Verify a third entry
                            if i + 0x80 + 8 <= len(data) and data[i+0x80:i+0x80+8] == propinfo_vtable_bytes:
                                rva = chunk_start + i
                                fid0 = struct.unpack_from('<I', data, i + 0x08)[0]
                                fid1 = struct.unpack_from('<I', data, i + 0x40 + 0x08)[0]
                                fid2 = struct.unpack_from('<I', data, i + 0x80 + 0x08)[0]
                                tc0 = struct.unpack_from('<I', data, i + 0x0C)[0]
                                tc1 = struct.unpack_from('<I', data, i + 0x40 + 0x0C)[0]

                                # Check if this is at the 0xA8-stride cache (skip it)
                                cache_rva = cache_ptr - mod_base
                                if abs(rva - cache_rva) < 0x200000:
                                    # It's near the cache - check stride
                                    if i + 0xA8 + 8 <= len(data):
                                        also_a8 = data[i+0xA8:i+0xA8+8] == propinfo_vtable_bytes
                                    else:
                                        also_a8 = False
                                    stride_label = "0xA8-also" if also_a8 else "0x40-only"
                                else:
                                    stride_label = "NEW"

                                print(f"    FOUND at RVA 0x{rva:x} ({stride_label}): fid[0]=0x{fid0:08x} fid[1]=0x{fid1:08x} fid[2]=0x{fid2:08x} tc[0]={tc0} tc[1]={tc1}")

        # =====================================================================
        # TEST 4: Look at descriptor members for array pointers
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"TEST 4: DESCRIPTOR EXTENDED LAYOUT (first 0x100 bytes)")
        print(f"{'='*80}")

        ext_desc = rpm(handle, desc_va, 0x100)
        if ext_desc:
            for j in range(0, min(0x100, len(ext_desc)), 8):
                val = struct.unpack_from('<Q', ext_desc, j)[0]
                rva = val - mod_base if mod_base < val < mod_base + 0x20000000 else None
                section = ""
                if rva is not None:
                    if 0 < rva < 0x900000:
                        section = "(.text)"
                    elif 0x900000 < rva < 0x1100000:
                        section = "(.rdata)"
                    elif 0x1100000 < rva < 0x1600000:
                        section = "(.data)"
                    print(f"    +0x{j:02x}: 0x{val:016x}  RVA 0x{rva:x} {section}")
                else:
                    print(f"    +0x{j:02x}: 0x{val:016x}")

        # =====================================================================
        # TEST 5: Read the secondary descriptor structure
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"TEST 5: SECONDARY DESCRIPTOR DEEP DIVE")
        print(f"{'='*80}")

        if sec_data:
            # Check if secondary descriptor has its own pointer to an array
            for j in range(0, min(0x80, len(sec_data)), 8):
                val = struct.unpack_from('<Q', sec_data, j)[0]
                rva = val - mod_base if mod_base < val < mod_base + 0x20000000 else None
                if rva is not None:
                    if 0x1100000 < rva < 0x1600000:
                        # .data pointer - could be an array
                        # Read first 0x100 bytes from this target
                        target_data = rpm(handle, val, 0x100)
                        if target_data:
                            first_qword = struct.unpack_from('<Q', target_data, 0)[0]
                            first_rva = first_qword - mod_base if mod_base < first_qword < mod_base + 0x20000000 else None
                            is_propinfo = (first_rva == 0x1050670) if first_rva else False
                            print(f"    sec+0x{j:02x} -> RVA 0x{rva:x} (.data)")
                            print(f"      First qword: 0x{first_qword:x} {'= PropInfoData vtable!' if is_propinfo else ''}")
                            if is_propinfo:
                                # This might be a PropInfoData array!
                                fid = struct.unpack_from('<I', target_data, 0x08)[0]
                                tc = struct.unpack_from('<I', target_data, 0x0C)[0]
                                print(f"      field_id=0x{fid:08x} type_code={tc}")
                                # Check next entry at both strides
                                for stride in [0x40, 0x78, 0xA8]:
                                    if stride + 8 <= len(target_data):
                                        next_qword = struct.unpack_from('<Q', target_data, stride)[0]
                                        next_rva = next_qword - mod_base if mod_base < next_qword < mod_base + 0x20000000 else None
                                        is_pi = (next_rva == 0x1050670) if next_rva else False
                                        print(f"      At +0x{stride:x}: {'YES PropInfoData' if is_pi else 'NOT PropInfoData'} (0x{next_qword:x})")
                    elif 0 < rva < 0x900000:
                        print(f"    sec+0x{j:02x} -> RVA 0x{rva:x} (.text)")

        # =====================================================================
        # TEST 6: Maybe R13 actually IS the cache base and the SHL uses a
        # computed offset, not a pure index
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"TEST 6: BINARY SEARCH RESULT ANALYSIS")
        print(f"{'='*80}")

        # The sorted array for binary search: at RVA 0x12a9060
        sorted_rva = 0x12a9060
        sorted_va = mod_base + sorted_rva

        # Read sorted array structure
        sorted_header = rpm(handle, sorted_va, 0x20)
        if sorted_header:
            print(f"\n  Sorted array header at RVA 0x{sorted_rva:x}:")
            for j in range(0, 0x20, 8):
                val = struct.unpack_from('<Q', sorted_header, j)[0]
                print(f"    +0x{j:02x}: 0x{val:016x}")

        # The sorted array likely stores (field_id, some_data) pairs
        # Let's read the first few entries to understand the format
        sorted_entries = rpm(handle, sorted_va, 1444 * 8)
        if sorted_entries:
            print(f"\n  First 20 sorted array entries:")
            for i in range(20):
                val = struct.unpack_from('<Q', sorted_entries, i * 8)[0]
                lo = val & 0xFFFFFFFF
                hi = (val >> 32) & 0xFFFFFFFF
                print(f"    [{i:4d}] 0x{val:016x}  lo=0x{lo:08x} hi=0x{hi:08x}")

            # Check if this is field_id sorted
            print(f"\n  Checking sort order (by low 32 bits):")
            prev = 0
            sorted_ok = True
            for i in range(min(1444, len(sorted_entries) // 8)):
                val = struct.unpack_from('<Q', sorted_entries, i * 8)[0]
                lo = val & 0xFFFFFFFF
                if lo < prev:
                    sorted_ok = False
                    print(f"    Sort broken at [{i}]: 0x{lo:08x} < 0x{prev:08x}")
                    break
                prev = lo
            if sorted_ok:
                print(f"    Sorted OK (all {min(1444, len(sorted_entries)//8)} entries in ascending order)")

        # =====================================================================
        # TEST 7: Maybe the binary search returns a POSITION that when
        # multiplied by 0x40 happens to land at the PropInfoData within the
        # 0xA8-stride entry (i.e., position != sequential index)
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"TEST 7: RELATIONSHIP BETWEEN SORTED ARRAY AND CACHE")
        print(f"{'='*80}")

        # Read first 30 entries from both the sorted array and the 0xA8 cache
        print(f"\n  Sorted array entry vs cache field_id:")
        for i in range(30):
            # Sorted array
            sa_val = struct.unpack_from('<Q', sorted_entries, i * 8)[0]
            sa_lo = sa_val & 0xFFFFFFFF
            sa_hi = (sa_val >> 32) & 0xFFFFFFFF

            # Cache at 0xA8 stride
            entry_va = cache_ptr + i * 0xA8
            entry_data = rpm(handle, entry_va, 0x10)
            if entry_data:
                cache_fid = struct.unpack_from('<I', entry_data, 0x08)[0]
                cache_tc = struct.unpack_from('<I', entry_data, 0x0C)[0]
                match = "Y" if sa_lo == cache_fid else "N"
                print(f"    [{i:3d}] sorted=0x{sa_lo:08x} hi=0x{sa_hi:08x}  cache_fid=0x{cache_fid:08x} tc={cache_tc:2d}  {match}")

        # =====================================================================
        # TEST 8: Read the hi-word of sorted array entries
        # =====================================================================
        print(f"\n{'='*80}")
        print(f"TEST 8: SORTED ARRAY HI-WORD ANALYSIS")
        print(f"{'='*80}")

        # If hi contains an index or offset, it might resolve the dual-stride
        hi_values = []
        for i in range(min(1444, len(sorted_entries) // 8)):
            val = struct.unpack_from('<Q', sorted_entries, i * 8)[0]
            hi = (val >> 32) & 0xFFFFFFFF
            hi_values.append(hi)

        # Check if hi values are sequential
        sequential = all(hi_values[i] == i for i in range(len(hi_values)))
        print(f"  Hi values are sequential (0,1,2,...): {sequential}")

        if not sequential:
            # Check if they're field_ids or offsets
            print(f"  First 20 hi values: {[f'0x{h:x}' for h in hi_values[:20]]}")
            print(f"  Min: 0x{min(hi_values):x}, Max: 0x{max(hi_values):x}")

            # Maybe the sorted array is (field_id, cache_index) pairs
            # If hi = cache_index, then sorted[i].hi * 0xA8 would give the cache offset
            # OR hi * 0x40 might be the offset into a packed array

            # Check if hi * 0xA8 + cache_base has the matching field_id
            print(f"\n  Checking if hi = cache index (at 0xA8 stride):")
            for i in range(min(20, len(sorted_entries) // 8)):
                val = struct.unpack_from('<Q', sorted_entries, i * 8)[0]
                sa_fid = val & 0xFFFFFFFF
                hi = (val >> 32) & 0xFFFFFFFF

                if hi < 1500:
                    target_va = cache_ptr + hi * 0xA8
                    target_data = rpm(handle, target_va, 0x10)
                    if target_data:
                        target_fid = struct.unpack_from('<I', target_data, 0x08)[0]
                        match = "Y" if sa_fid == target_fid else "N"
                        print(f"    sorted[{i}] fid=0x{sa_fid:08x} hi={hi} -> cache[{hi}].fid=0x{target_fid:08x} {match}")

    finally:
        kernel32.CloseHandle(handle)

    print("\nDone!")


if __name__ == '__main__':
    main()
