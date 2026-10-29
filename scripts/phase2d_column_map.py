#!/usr/bin/env python3
"""Phase 2d: Extract the UI column → field ID mapping table at 0x1413b0440.

Also does final analysis of the 115-entry "direct offset" index array and
produces the definitive category naming.
"""

import struct
import csv
from pathlib import Path
from collections import Counter

IMAGE_BASE = 0x140000000
WINPROJ = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
BASE = Path(__file__).resolve().parents[1]
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


def main():
    print("Phase 2d: Column Mapping & Final Analysis")
    print("=" * 70)

    with open(WINPROJ, 'rb') as f:
        data = f.read()

    sections = parse_pe_sections(data)
    field_map = load_field_id_map()

    # ===== 1. Extract column mapping table =====
    print("\n1. COLUMN ORDER -> FIELD ID MAPPING TABLE")
    print("-" * 50)
    map_va = 0x1413b0440
    map_off = va_to_file(map_va, sections)

    # First 2 u32 are header field IDs, then pairs of (column_ordinal, field_id)
    header_a = struct.unpack_from('<I', data, map_off)[0]
    header_b = struct.unpack_from('<I', data, map_off + 4)[0]
    print(f"  Header: 0x{header_a:08x} ({field_map.get(header_a, '?')}), "
          f"0x{header_b:08x} ({field_map.get(header_b, '?')})")

    # Read pairs starting at +8
    col_map = []
    for i in range(2048):
        off = map_off + 8 + i * 8
        col_idx = struct.unpack_from('<I', data, off)[0]
        fid = struct.unpack_from('<I', data, off + 4)[0]

        # Column index should have 0x1f4 prefix
        if (col_idx >> 12) != 0x1f400:
            # Check if it's still a valid field_id pair
            if not (0x0b400000 <= fid <= 0x0f4fffff):
                print(f"  End of table at entry {i}: col=0x{col_idx:08x}, fid=0x{fid:08x}")
                break
            # Might be two field IDs instead of col+fid
            if 0x0b400000 <= col_idx <= 0x0f4fffff:
                # Two field IDs — different table format
                pass

        col_num = col_idx & 0xFFFFF
        name = field_map.get(fid, '')
        col_map.append({
            'column': col_num,
            'column_raw': col_idx,
            'field_id': fid,
            'name': name,
        })

    print(f"  Extracted {len(col_map)} column mappings")

    # Show first 40
    print(f"\n  {'Col':>5} {'FieldID':>12} Name")
    print(f"  {'-'*5} {'-'*12} {'-'*30}")
    for entry in col_map[:50]:
        fid_hex = f"0x{entry['field_id']:08x}"
        print(f"  {entry['column']:5d} {fid_hex} {entry['name']}")

    # Check how many unique field IDs
    unique_fids = set(e['field_id'] for e in col_map)
    print(f"\n  Unique field IDs: {len(unique_fids)}")

    # Count by field type prefix
    prefix_counts = Counter()
    for e in col_map:
        prefix = (e['field_id'] >> 20) & 0xFFF
        prefix_counts[f"0x{prefix:03x}"] += 1
    print(f"  By prefix: {dict(prefix_counts)}")

    # Write to file
    out_path = ANALYSIS / "column_field_map.tsv"
    with open(out_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['column_index', 'column_raw_hex', 'field_id_hex', 'property_name'])
        for e in col_map:
            w.writerow([e['column'], f"0x{e['column_raw']:08x}",
                        f"0x{e['field_id']:08x}", e['name']])
    print(f"  Wrote to {out_path}")

    # ===== 2. 115-entry direct offset field analysis =====
    print(f"\n{'='*70}")
    print("2. DIRECT OFFSET FIELDS (115 entries at 0x1410fb248)")
    print("-" * 50)

    idx_va = 0x1410fb248
    idx_off = va_to_file(idx_va, sections)
    header = struct.unpack_from('<I', data, idx_off)[0]
    indices = [struct.unpack_from('<I', data, idx_off + 4 + i*4)[0] for i in range(115)]

    fid_off = va_to_file(0x1410e77a0, sections)
    task_fids = [struct.unpack_from('<I', data, fid_off + i*4)[0] for i in range(1444)]

    # Map indices to field info
    direct_fields = []
    for i, idx in enumerate(indices):
        fid = task_fids[idx] if idx < 1444 else 0
        name = field_map.get(fid, '')
        direct_fields.append({
            'array_pos': i,
            'field_index': idx,
            'field_id': fid,
            'name': name,
        })

    # Write to file
    out_path = ANALYSIS / "direct_offset_fields.tsv"
    with open(out_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['array_pos', 'field_index', 'field_id_hex', 'property_name'])
        for df in direct_fields:
            w.writerow([df['array_pos'], df['field_index'],
                        f"0x{df['field_id']:08x}", df['name']])
    print(f"  Wrote {len(direct_fields)} entries to {out_path}")
    print(f"  Named: {sum(1 for df in direct_fields if df['name'])}/115")

    # ===== 3. Definitive category naming =====
    print(f"\n{'='*70}")
    print("3. DEFINITIVE CATEGORY NAMING")
    print("-" * 50)

    categories_named = {
        'Standard':  (0, 1021, "Core task fields (standard PjField values)"),
        'Estimated': (1021, 1043, "Estimated duration fields, Calendar, Index, HyperlinkHREF"),
        'OutlineCodes':   (1043, 1046, "OutlineCode7-9"),
        'EnterpriseOC_1': (1046, 1069, "OutlineCode10 + EnterpriseOutlineCode1-20 + HyperlinkScreenTip"),
        'EnterpriseOC_2': (1069, 1082, "EnterpriseOutlineCode21-30"),
        'BaselineEst_1':  (1082, 1084, "Baseline4/5 DurationEstimated"),
        'BaselineEst_2':  (1084, 1134, "Baseline6-10 DurationEstimated + EnterpriseProjectOutlineCodes"),
        'Variance':  (1134, 1156, "Variance fields, CV, Project, Objects"),
        'Extended':  (1156, 1444, "Extended/enterprise/miscellaneous fields"),
    }

    for cat_name, (lo, hi, desc) in categories_named.items():
        fids = task_fids[lo:hi]
        named = sum(1 for fid in fids if fid in field_map)
        print(f"  {cat_name:20s}  [{lo:4d}-{hi-1:4d}] {hi-lo:3d} fields, {named:3d} named - {desc}")

    # Write updated category info
    out_path = ANALYSIS / "task_categories.tsv"
    with open(out_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['category', 'start_index', 'end_index', 'count', 'named_count', 'description'])
        for cat_name, (lo, hi, desc) in categories_named.items():
            fids = task_fids[lo:hi]
            named = sum(1 for fid in fids if fid in field_map)
            w.writerow([cat_name, lo, hi - 1, hi - lo, named, desc])
    print(f"\n  Wrote to {out_path}")

    # ===== 4. Analyze the dispatcher GUID at 0x140fad020 =====
    print(f"\n{'='*70}")
    print("4. DISPATCHER REFERENCE ANALYSIS")
    print("-" * 50)

    guid_off = va_to_file(0x140fad020, sections)
    # Read as GUID: Data1(u32), Data2(u16), Data3(u16), Data4(8 bytes)
    d1 = struct.unpack_from('<I', data, guid_off)[0]
    d2 = struct.unpack_from('<H', data, guid_off + 4)[0]
    d3 = struct.unpack_from('<H', data, guid_off + 6)[0]
    d4 = data[guid_off + 8:guid_off + 16]
    guid_str = f"{{{d1:08X}-{d2:04X}-{d3:04X}-{d4[0]:02X}{d4[1]:02X}-{d4[2]:02X}{d4[3]:02X}{d4[4]:02X}{d4[5]:02X}{d4[6]:02X}{d4[7]:02X}}}"
    print(f"  GUID at 0x140fad020: {guid_str}")

    known_guids = {
        '{00020400-0000-0000-C000-000000000046}': 'IID_IDispatch',
        '{00000000-0000-0000-C000-000000000046}': 'IID_IUnknown',
        '{00020404-0000-0000-C000-000000000046}': 'IID_IEnumVARIANT',
    }
    if guid_str in known_guids:
        print(f"  Known GUID: {known_guids[guid_str]}")

    # Read what follows the GUID
    print(f"\n  Context around 0x140fad020 (+0x00 to +0x80):")
    for i in range(16):
        off = guid_off + i * 8
        v = struct.unpack_from('<Q', data, off)[0]
        v32a = struct.unpack_from('<I', data, off)[0]
        v32b = struct.unpack_from('<I', data, off + 4)[0]
        print(f"    +0x{i*8:02x}: 0x{v:016x}  (u32: 0x{v32a:08x}, 0x{v32b:08x})")

    # ===== 5. Look for field type code table =====
    print(f"\n{'='*70}")
    print("5. SCAN FOR FIELD TYPE CODE TABLE")
    print("-" * 50)

    # The field type codes (PjFieldType) should be stored somewhere:
    # 0=Cost,1=Date,2=Duration,3=Finish,4=Flag,5=Work,6=Number,7=Start,8=Text
    # Look for a table of (field_id, type_code) or just a type_code array
    # near the descriptor or the field ID arrays.

    # Try: look for a u8 or u16 array of 1444 entries near the field data
    # Each value should be 0-20 (small type code)
    desc_off = va_to_file(0x140f8bda0, sections)

    # Read the descriptor +0x40 (index mapping) pointer
    idx_map_ptr = struct.unpack_from('<Q', data, desc_off + 0x40)[0]
    print(f"  Descriptor +0x40 index map ptr: 0x{idx_map_ptr:x}")

    # Read the descriptor +0x48 area
    for i in range(4):
        v = struct.unpack_from('<Q', data, desc_off + 0x40 + i*8)[0]
        print(f"  Descriptor +0x{0x40 + i*8:02x}: 0x{v:016x}")

    # Check if there's a type code array near 0x1410fb248 (the 115-index array)
    # After the 115 entries + header = 116 u32 = 464 bytes after 0x1410fb248
    after_idx_va = 0x1410fb248 + 4 + 115 * 4
    after_idx_off = va_to_file(after_idx_va, sections)
    print(f"\n  After 115-index array (VA 0x{after_idx_va:x}):")
    for i in range(20):
        val = struct.unpack_from('<I', data, after_idx_off + i*4)[0]
        print(f"    +{i*4:4d}: 0x{val:08x} ({val})")

    # ===== 6. Scan for (field_id, type_code, offset) triples near known structures =====
    print(f"\n{'='*70}")
    print("6. FIELD INIT DATA SEARCH")
    print("-" * 50)

    # The runtime cache initialization must store (field_id, vtable_type, offset, size)
    # for each field. Let's look for arrays of field_ids interleaved with small values.

    # Hypothesis: there might be a table of (field_id, type_code) u32 pairs
    # Try scanning .rdata for sequences where even positions are 0x0b4XXXXX
    # and odd positions are small integers (0-20)

    rdata = None
    for s in sections:
        if s['name'] == '.rdata':
            rdata = s
            break

    if rdata:
        rdata_off = rdata['raw_ptr']
        rdata_size = rdata['raw_size']
        rdata_va = IMAGE_BASE + rdata['virt_addr']

        # Scan for stride-8 patterns: (field_id, small_int) pairs
        found_tables = []
        pos = 0
        while pos < rdata_size - 8:
            fid = struct.unpack_from('<I', data, rdata_off + pos)[0]
            val = struct.unpack_from('<I', data, rdata_off + pos + 4)[0]

            if 0x0b400000 <= fid <= 0x0b4005a3 and 0 <= val <= 30:
                # Found a candidate pair! Check for a run
                run = 1
                for j in range(1, 200):
                    fid2 = struct.unpack_from('<I', data, rdata_off + pos + j*8)[0]
                    val2 = struct.unpack_from('<I', data, rdata_off + pos + j*8 + 4)[0]
                    if 0x0b400000 <= fid2 <= 0x0b4005a3 and 0 <= val2 <= 30:
                        run += 1
                    else:
                        break

                if run >= 10:
                    va = rdata_va + pos
                    found_tables.append((va, run))
                    # Show first 10 entries
                    print(f"\n  Possible (field_id, type_code) table at VA 0x{va:x} ({run} pairs):")
                    for j in range(min(run, 15)):
                        f = struct.unpack_from('<I', data, rdata_off + pos + j*8)[0]
                        t = struct.unpack_from('<I', data, rdata_off + pos + j*8 + 4)[0]
                        name = field_map.get(f, '')
                        type_names = {0:'Cost',1:'Date',2:'Duration',3:'Finish',4:'Flag',
                                      5:'Work',6:'Number',7:'Start',8:'Text',9:'Resource',
                                      11:'Percent'}
                        tname = type_names.get(t, f'type_{t}')
                        print(f"    0x{f:08x} type={t:2d}({tname:10s}) {name}")
                    pos += run * 8
                    continue

            pos += 4

        if not found_tables:
            print("  No (field_id, type_code) pair tables found in .rdata")

        # Also try stride-12: (field_id, type_code, offset) triples
        print(f"\n  Scanning for stride-12 (field_id, type, offset) triples...")
        pos = 0
        found_12 = []
        while pos < rdata_size - 12:
            fid = struct.unpack_from('<I', data, rdata_off + pos)[0]
            tc = struct.unpack_from('<I', data, rdata_off + pos + 4)[0]
            off_val = struct.unpack_from('<I', data, rdata_off + pos + 8)[0]

            if (0x0b400000 <= fid <= 0x0b4005a3 and 0 <= tc <= 30
                    and 0 <= off_val <= 0x2000):
                run = 1
                for j in range(1, 200):
                    f2 = struct.unpack_from('<I', data, rdata_off + pos + j*12)[0]
                    t2 = struct.unpack_from('<I', data, rdata_off + pos + j*12 + 4)[0]
                    o2 = struct.unpack_from('<I', data, rdata_off + pos + j*12 + 8)[0]
                    if (0x0b400000 <= f2 <= 0x0b4005a3 and 0 <= t2 <= 30
                            and 0 <= o2 <= 0x2000):
                        run += 1
                    else:
                        break

                if run >= 10:
                    va = rdata_va + pos
                    found_12.append((va, run))
                    print(f"\n  (field_id, type, offset) table at VA 0x{va:x} ({run} triples):")
                    for j in range(min(run, 15)):
                        f = struct.unpack_from('<I', data, rdata_off + pos + j*12)[0]
                        t = struct.unpack_from('<I', data, rdata_off + pos + j*12 + 4)[0]
                        o = struct.unpack_from('<I', data, rdata_off + pos + j*12 + 8)[0]
                        name = field_map.get(f, '')
                        print(f"    0x{f:08x} type={t:2d} offset=0x{o:04x} {name}")
                    pos += run * 12
                    continue

            pos += 4

        if not found_12:
            print("  No (field_id, type, offset) triple tables found in .rdata")

    print(f"\n{'='*70}")
    print("DONE")
    print("=" * 70)


if __name__ == '__main__':
    main()
