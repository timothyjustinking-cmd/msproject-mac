#!/usr/bin/env python3
"""Phase 2d: Find Resource and Assignment descriptors.

The Task descriptor at 0x140f8bda0 has a known structure. Resource and
Assignment descriptors should follow the same pattern:
  +0x00: RTTI ptr (.data)
  +0x08: runtime cache ptr (.data)
  +0x10: field count (u16), field count (u16)
  +0x24: field_id marker (0x0c404000 for Resource, 0x0f404000 for Assignment)
  +0x60: dispatcher function (.text)
  +0x68: accessor function (.text)

Strategy: scan .rdata for the field_id markers 0x0c404000 and 0x0f404000.
"""

import struct
import csv
from pathlib import Path

IMAGE_BASE = 0x140000000
WINPROJ = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
BASE = Path(r"C:\Users\timot\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile")
ANALYSIS = BASE / "analysis"


def parse_pe_sections(data):
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    coff_offset = e_lfanew + 4
    num_sections = struct.unpack_from('<H', data, coff_offset + 2)[0]
    opt_header_size = struct.unpack_from('<H', data, coff_offset + 16)[0]
    section_offset = coff_offset + 20 + opt_header_size
    sections = []
    for i in range(num_sections):
        off = section_offset + i * 40
        name = data[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
        sections.append({
            'name': name,
            'virt_addr': struct.unpack_from('<I', data, off + 12)[0],
            'virt_size': struct.unpack_from('<I', data, off + 8)[0],
            'raw_ptr': struct.unpack_from('<I', data, off + 20)[0],
            'raw_size': struct.unpack_from('<I', data, off + 16)[0],
        })
    return sections


def va_to_file(va, sections):
    rva = va - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            off = rva - s['virt_addr'] + s['raw_ptr']
            if off < s['raw_ptr'] + s['raw_size']:
                return off
    return None


def section_for_va(va, sections):
    rva = va - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return s['name']
    return '?'


def is_pointer(val, sections):
    if val < IMAGE_BASE or val > IMAGE_BASE + 0x20000000:
        return False
    rva = val - IMAGE_BASE
    for s in sections:
        if s['virt_addr'] <= rva < s['virt_addr'] + s['virt_size']:
            return True
    return False


def load_field_id_map():
    fmap = {}
    path = ANALYSIS / "field_id_map.tsv"
    if not path.exists():
        return fmap
    with open(path, 'r') as f:
        for row in csv.DictReader(f, delimiter='\t'):
            fid_str = row['field_id']
            fid = int(fid_str, 16) if fid_str.startswith('0x') else int(fid_str)
            prop = row['property_name']
            disp = row.get('dispatcher', '')
            if disp == 'get' or fid not in fmap:
                fmap[fid] = prop
    return fmap


def dump_descriptor(data, sections, desc_va, desc_off, field_map, label):
    """Dump a descriptor structure similar to the Task descriptor at 0x140f8bda0."""
    print(f"\n{'='*70}")
    print(f"DESCRIPTOR: {label} at VA 0x{desc_va:x}")
    print("=" * 70)

    # Read 0x80 bytes
    for i in range(16):
        off = desc_off + i * 8
        v = struct.unpack_from('<Q', data, off)[0]
        sec = section_for_va(v, sections) if is_pointer(v, sections) else ''
        v32a = struct.unpack_from('<I', data, off)[0]
        v32b = struct.unpack_from('<I', data, off + 4)[0]

        label_str = ''
        if i == 0 and is_pointer(v, sections):
            label_str = 'RTTI/type_info'
        elif i == 1 and is_pointer(v, sections):
            label_str = 'Runtime cache'
        elif i == 2:
            count_a = v32a & 0xFFFF
            count_b = v32a >> 16
            label_str = f'counts: {count_a}, {count_b}'
        elif i == 4 and (v32a & 0xFF000000) in (0x0b000000, 0x0c000000, 0x0f000000):
            label_str = f'field_id_marker: 0x{v32a:08x}'
        elif is_pointer(v, sections) and section_for_va(v, sections) == '.text':
            label_str = '.text function'
        elif is_pointer(v, sections):
            label_str = sec

        print(f"  +0x{i*8:02x}: 0x{v:016x}  (u32: 0x{v32a:08x}, 0x{v32b:08x})  {label_str}")

    # Extract key fields
    rtti_ptr = struct.unpack_from('<Q', data, desc_off)[0]
    cache_ptr = struct.unpack_from('<Q', data, desc_off + 8)[0]
    count = struct.unpack_from('<H', data, desc_off + 0x10)[0]
    field_marker = struct.unpack_from('<I', data, desc_off + 0x24)[0]
    dispatcher = struct.unpack_from('<Q', data, desc_off + 0x60)[0]
    accessor = struct.unpack_from('<Q', data, desc_off + 0x68)[0]

    print(f"\n  Summary:")
    print(f"    RTTI ptr:       0x{rtti_ptr:x} ({section_for_va(rtti_ptr, sections)})")
    print(f"    Cache ptr:      0x{cache_ptr:x} ({section_for_va(cache_ptr, sections)})")
    print(f"    Field count:    {count}")
    print(f"    Field marker:   0x{field_marker:08x}")
    if is_pointer(dispatcher, sections):
        print(f"    Dispatcher:     0x{dispatcher:x} ({section_for_va(dispatcher, sections)})")
    if is_pointer(accessor, sections):
        print(f"    Accessor:       0x{accessor:x} ({section_for_va(accessor, sections)})")

    # Category boundaries
    bounds = []
    for j in range(8):
        b = struct.unpack_from('<H', data, desc_off + 0x14 + j*2)[0]
        if b > 0 and b <= count:
            bounds.append(b)
    bounds = sorted(set(bounds))
    if bounds:
        print(f"    Category bounds: {bounds}")

    # Cache entry analysis
    if is_pointer(cache_ptr, sections):
        cache_off = va_to_file(cache_ptr, sections)
        if cache_off:
            # Check first cache entry
            vtable = struct.unpack_from('<Q', data, cache_off)[0]
            fid0 = struct.unpack_from('<I', data, cache_off + 8)[0]
            tc0 = struct.unpack_from('<I', data, cache_off + 12)[0]
            off0 = struct.unpack_from('<I', data, cache_off + 0x18)[0]
            sz0 = struct.unpack_from('<I', data, cache_off + 0x30)[0]

            if vtable != 0 or fid0 != 0:
                name = field_map.get(fid0, '')
                print(f"\n    Cache entry 0:")
                print(f"      vtable: 0x{vtable:x}")
                print(f"      field_id: 0x{fid0:08x} ({name})")
                print(f"      type_code: {tc0}")
                print(f"      offset: 0x{off0:04x}")
                print(f"      size: {sz0}")
            else:
                print(f"\n    Cache entry 0: all zeros (runtime-populated)")

    return {
        'va': desc_va,
        'rtti': rtti_ptr,
        'cache': cache_ptr,
        'count': count,
        'marker': field_marker,
        'dispatcher': dispatcher,
        'accessor': accessor,
        'bounds': bounds,
    }


def main():
    print("Phase 2d: Resource & Assignment Descriptor Search")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # Known Task descriptor
    task_desc_va = 0x140f8bda0
    task_desc_off = va_to_file(task_desc_va, sections)
    task_info = dump_descriptor(data, sections, task_desc_va, task_desc_off, field_map, "TASK (known)")

    # Search for Resource and Assignment descriptors
    # Strategy 1: Look for field_id markers 0x0c404000 (Resource) and 0x0f404000 (Assignment)
    rdata = None
    for s in sections:
        if s['name'] == '.rdata':
            rdata = s
            break

    if not rdata:
        print("ERROR: .rdata section not found")
        return

    print(f"\n{'='*70}")
    print("SEARCHING FOR RESOURCE/ASSIGNMENT DESCRIPTORS")
    print("=" * 70)

    target_markers = {
        0x0c404000: 'Resource',
        0x0f404000: 'Assignment',
        0x3c404000: 'Calendar',
        0x07404000: 'Project',
        0x3e404000: 'OutlineCode',
    }

    rdata_off = rdata['raw_ptr']
    rdata_size = rdata['raw_size']
    rdata_va = IMAGE_BASE + rdata['virt_addr']

    for marker, rtype in target_markers.items():
        print(f"\n  Searching for marker 0x{marker:08x} ({rtype})...")
        found = []
        for pos in range(0, rdata_size - 4, 4):
            val = struct.unpack_from('<I', data, rdata_off + pos)[0]
            if val == marker:
                va = rdata_va + pos
                # Check if this is at the expected +0x24 offset of a descriptor
                desc_start_va = va - 0x24
                desc_start_off = va_to_file(desc_start_va, sections)
                if desc_start_off:
                    # Validate: +0x08 should be a .data pointer (cache)
                    cache_ptr = struct.unpack_from('<Q', data, desc_start_off + 8)[0]
                    count = struct.unpack_from('<H', data, desc_start_off + 0x10)[0]
                    if is_pointer(cache_ptr, sections) and 0 < count < 10000:
                        found.append((desc_start_va, desc_start_off, count))
                        print(f"    MATCH at VA 0x{desc_start_va:x}: count={count}, cache=0x{cache_ptr:x}")

        for va, off, count in found:
            dump_descriptor(data, sections, va, off, field_map, rtype)

    # Strategy 2: Also search .data for similar markers (in case descriptors are in .data)
    print(f"\n{'='*70}")
    print("SEARCHING .DATA SECTION")
    print("-" * 50)

    data_sec = None
    for s in sections:
        if s['name'] == '.data':
            data_sec = s
            break

    if data_sec:
        for marker, rtype in target_markers.items():
            for pos in range(0, min(data_sec['raw_size'], 0x400000) - 4, 4):
                val = struct.unpack_from('<I', data, data_sec['raw_ptr'] + pos)[0]
                if val == marker:
                    va = IMAGE_BASE + data_sec['virt_addr'] + pos
                    desc_start_va = va - 0x24
                    desc_start_off = va_to_file(desc_start_va, sections)
                    if desc_start_off:
                        cache_ptr = struct.unpack_from('<Q', data, desc_start_off + 8)[0]
                        count = struct.unpack_from('<H', data, desc_start_off + 0x10)[0]
                        if is_pointer(cache_ptr, sections) and 0 < count < 10000:
                            print(f"  .data MATCH: {rtype} at VA 0x{desc_start_va:x}: count={count}")
                            dump_descriptor(data, sections, desc_start_va, desc_start_off, field_map, f"{rtype} (.data)")

    # Strategy 3: The FUN_140226e2c indexer takes (base, typeIndex).
    # The "base" is likely an array of descriptor pointers.
    # typeIndex: 0=Task, 1=Resource, 3=Assignment
    # So there's an array [task_desc_ptr, resource_desc_ptr, ??, assignment_desc_ptr]
    # Look for this array by finding 0x140f8bda0 (Task desc VA) as a u64 in .rdata/.data
    print(f"\n{'='*70}")
    print("SEARCHING FOR DESCRIPTOR POINTER ARRAY")
    print("-" * 50)

    task_desc_ptr = task_desc_va
    # Search all sections for this pointer value
    for s in sections:
        if s['name'] not in ('.rdata', '.data'):
            continue
        for pos in range(0, s['raw_size'] - 8, 8):
            val = struct.unpack_from('<Q', data, s['raw_ptr'] + pos)[0]
            if val == task_desc_ptr:
                va = IMAGE_BASE + s['virt_addr'] + pos
                print(f"  Found Task descriptor ptr at VA 0x{va:x} ({s['name']})")

                # Check surrounding pointers — typeIndex 1=Resource should follow
                for di in range(-4, 8):
                    adj_off = s['raw_ptr'] + pos + di * 8
                    if adj_off < s['raw_ptr'] or adj_off + 8 > s['raw_ptr'] + s['raw_size']:
                        continue
                    adj_val = struct.unpack_from('<Q', data, adj_off)[0]
                    adj_va = va + di * 8
                    sec = section_for_va(adj_val, sections) if is_pointer(adj_val, sections) else ''
                    marker_check = ''
                    if is_pointer(adj_val, sections):
                        adj_foff = va_to_file(adj_val, sections)
                        if adj_foff:
                            m = struct.unpack_from('<I', data, adj_foff + 0x24)[0]
                            if m in target_markers:
                                marker_check = f' -> {target_markers[m]} descriptor!'
                            elif m == 0x0b404000:
                                marker_check = ' -> Task descriptor'
                    is_self = ' <-- Task desc' if adj_val == task_desc_ptr else ''
                    print(f"    [{di:+d}] VA 0x{adj_va:x}: 0x{adj_val:016x}  {sec}{is_self}{marker_check}")

    print(f"\n{'='*70}")
    print("DONE")
    print("=" * 70)


if __name__ == '__main__':
    main()
