#!/usr/bin/env python3
"""Phase 2d: Complete Field Access Architecture Summary.

Produces the definitive analysis of how MS Project reads field values,
combining all discoveries from this phase into a single comprehensive output.
"""

import os
import sys

ANALYSIS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')


def load_definitive_map():
    path = os.path.join(ANALYSIS_DIR, 'field_definitive_map.tsv')
    fields = []
    with open(path, 'r', encoding='utf-8') as f:
        header = f.readline().strip().split('\t')
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 18:
                fields.append({
                    'record_type': parts[0],
                    'field_id_hex': parts[1],
                    'field_id_dec': int(parts[2]),
                    'com_name': parts[3],
                    'type_code': int(parts[4]),
                    'type_name': parts[5],
                    'slot_index': int(parts[6]),
                    'field_size': int(parts[7]),
                    'flags_hex': parts[8],
                    'storage_class': int(parts[9]) if parts[9].isdigit() else -1,
                    'storage_name': parts[10],
                    'class_slot': parts[11],
                    'byte_offset_hex': parts[12],
                    'byte_offset_dec': int(parts[13]) if parts[13].isdigit() else -1,
                    'linked_field': parts[14],
                    'secondary_idx': parts[15],
                    'sub_type': parts[16],
                    'vtable_class': parts[17],
                })
    return fields


def main():
    fields = load_definitive_map()

    task_fields = [f for f in fields if f['record_type'] == 'Task']

    print("=" * 100)
    print("MS PROJECT FIELD ACCESS ARCHITECTURE - DEFINITIVE ANALYSIS")
    print("=" * 100)

    # ===================================================================
    # SECTION 1: Overall statistics
    # ===================================================================
    print(f"\n{'='*80}")
    print(f"1. FIELD INVENTORY")
    print(f"{'='*80}")

    by_type = {}
    for f in fields:
        rt = f['record_type']
        if rt not in by_type:
            by_type[rt] = 0
        by_type[rt] += 1

    print(f"\n  Total fields across all record types: {len(fields)}")
    for rt in sorted(by_type.keys()):
        print(f"    {rt:<15s}: {by_type[rt]:>5d}")

    named = [f for f in task_fields if f['com_name']]
    print(f"\n  Task fields: {len(task_fields)}")
    print(f"  Task fields with COM names: {len(named)} ({100*len(named)/len(task_fields):.0f}%)")

    with_offsets = [f for f in task_fields if f['byte_offset_dec'] > 0]
    print(f"  Task fields with byte offsets: {len(with_offsets)}")

    # ===================================================================
    # SECTION 2: Storage class distribution
    # ===================================================================
    print(f"\n{'='*80}")
    print(f"2. STORAGE CLASS DISTRIBUTION (Task fields only)")
    print(f"{'='*80}")

    STORAGE_NAMES = {
        0: 'direct',       1: 'baseline0',    2: 'dynamic',
        3: 'baseline1-10', 4: 'text',         5: 'user_custom',
        6: 'special',      7: 'dyn_text',     8: 'unk_8',
        9: 'lookup',       10: 'computed',    11: 'proj_text',
        12: 'unk_12',      13: 'structural',  14: 'virtual',
        15: 'unk_15',
    }

    by_sc = {}
    for f in task_fields:
        sc = f['storage_class']
        if sc not in by_sc:
            by_sc[sc] = {'count': 0, 'named': 0, 'with_offset': 0, 'types': set()}
        by_sc[sc]['count'] += 1
        if f['com_name']:
            by_sc[sc]['named'] += 1
        if f['byte_offset_dec'] > 0:
            by_sc[sc]['with_offset'] += 1
        by_sc[sc]['types'].add(f['type_name'])

    print(f"\n  {'Class':>5s} {'Name':<15s} {'Count':>6s} {'Named':>6s} {'Offset':>7s} {'Types'}")
    print(f"  {'-'*5} {'-'*15} {'-'*6} {'-'*6} {'-'*7} {'-'*30}")
    for sc in sorted(by_sc.keys()):
        info = by_sc[sc]
        name = STORAGE_NAMES.get(sc, f'unk_{sc}')
        types_str = ', '.join(sorted(info['types']))[:40]
        print(f"  {sc:>5d} {name:<15s} {info['count']:>6d} {info['named']:>6d} {info['with_offset']:>7d} {types_str}")

    # ===================================================================
    # SECTION 3: Engine type distribution
    # ===================================================================
    print(f"\n{'='*80}")
    print(f"3. ENGINE TYPE CODE DISTRIBUTION (Task fields)")
    print(f"{'='*80}")

    TYPE_MAP = {
        0: 'null', 2: 'i16', 3: 'i32', 5: 'i64', 8: 'text', 9: 'obj_ref',
        11: 'bool', 19: 'date32', 20: 'comp_dur', 21: 'order_id', 29: 'virtual', 72: 'guid'
    }

    by_tc = {}
    for f in task_fields:
        tc = f['type_code']
        if tc not in by_tc:
            by_tc[tc] = 0
        by_tc[tc] += 1

    print(f"\n  {'Code':>5s} {'Name':<10s} {'Count':>6s} {'Dispatch path'}")
    print(f"  {'-'*5} {'-'*10} {'-'*6} {'-'*40}")
    for tc in sorted(by_tc.keys()):
        name = TYPE_MAP.get(tc, f'unk_{tc}')
        count = by_tc[tc]
        if tc in (0, 1):
            path = "vtable slot 120 (null handler)"
        elif tc == 5:
            path = "i64 special (checks storage_class==18)"
        elif tc in (8, 29, 72):
            path = "string-type handler (sub_2682A4)"
        else:
            path = "default 8-byte scalar read"
        print(f"  {tc:>5d} {name:<10s} {count:>6d} {path}")

    # ===================================================================
    # SECTION 4: Complete dispatch chain
    # ===================================================================
    print(f"\n{'='*80}")
    print(f"4. COMPLETE DISPATCH CHAIN")
    print(f"{'='*80}")

    print("""
  COM Application.ActiveProject.Tasks(1).GetField(pjFieldId)
    |
    v
  [IBkndElem vtable dispatch]
    -> TBkndTaskElem.GetValue(field_id)
       |
       v
  [TTaskTable virtual dispatch]
    -> TTaskTable.vfunc[87] at RVA 0x231F00
       |  Loads prop container from TTaskTable+0x5E8
       |  Gets sub-object at prop_container+0x1E8
       v
  [TBkndPropContainer vtable[158]]
    -> PRIMARY DISPATCHER at RVA 0x239380 (~4KB)
       |
       |-- 1. Lookup PropInfoData entry for field_id
       |     R14 -> entry (contains type_code, storage_class, slot_index, etc.)
       |
       |-- 2. Early check: storage_class == 10 (computed)?
       |     YES -> call computation function directly
       |     NO  -> continue
       |
       |-- 3. Check flags (AND 0x1FF, AND 0xC000)
       |
       |-- 4. Switch on engine_type_code [R14+0x48]:
       |     |
       |     |-- Case 0,1 (null):      vtable slot 120
       |     |-- Case 5 (i64):         special handler
       |     |     |-- Check storage_class==18 at [R14+0x4A]
       |     |-- Case 8,29,72 (text/virtual/guid): sub_2682A4
       |     |-- Default (all other):  8-byte scalar read
       |     v
       |-- 5. Storage class dispatcher (sub_4A1610 at 0x4A1610):
       |     |-- Classes 0-10: common handler (getter pair dispatch)
       |     |-- Class 11: project text handler
       |     |-- Class 12: specific handler
       |     |-- Class 13: structural/relational (obj_ref collections)
       |     |-- Class 14: virtual field handler
       |     |-- Class 16: specific handler
       |     |-- Class 17: specific handler
       |     |-- Class 21: order_id handler
       |     v
       |-- 6. Main getter dispatch (sub_23EC60 at 0x23EC60):
       |     Uses slot_index [R14+0x28] and field_size [R14+0x44]
       |     Calls through this+0x58 -> vtable+0x988
       |     -> getter1 function (from PropInfoData header)
       |     -> getter2 function (from PropInfoData header)
       |        |
       |        |-- DEFAULT_VALUE (78% of fields): returns zero/empty
       |        |-- RECORD_READER (20% of fields): __RTDynamicCast
       |        |   to get TASKNODE, read at byte_offset
       |        v
       v
  [Return formatted value through COM automation layer]
""")

    # ===================================================================
    # SECTION 5: Byte offset coverage
    # ===================================================================
    print(f"{'='*80}")
    print(f"5. TASKNODE BYTE OFFSET MAP")
    print(f"{'='*80}")

    offsets = {}
    for f in task_fields:
        off = f['byte_offset_dec']
        if off > 0:
            if off not in offsets:
                offsets[off] = []
            offsets[off].append(f)

    print(f"\n  {len(offsets)} unique offsets covering {sum(len(v) for v in offsets.values())} fields:")
    print(f"\n  {'Offset':<10s} {'Hex':<8s} {'Fields':>6s} {'Types':<30s} {'Sample names'}")
    print(f"  {'-'*10} {'-'*8} {'-'*6} {'-'*30} {'-'*40}")

    for off in sorted(offsets.keys()):
        flds = offsets[off]
        types = sorted(set(f['type_name'] for f in flds))
        names = [f['com_name'] for f in flds if f['com_name']][:3]
        names_str = ', '.join(names) if names else '(unnamed)'
        types_str = ', '.join(types)[:28]
        print(f"  0x{off:04x}     0x{off:04x}  {len(flds):>6d} {types_str:<30s} {names_str}")

    # ===================================================================
    # SECTION 6: Getter function classification
    # ===================================================================
    print(f"\n{'='*80}")
    print(f"6. GETTER FUNCTION PATTERNS")
    print(f"{'='*80}")

    print("""
  Getter2 functions (27 unique):
    PATTERN           FUNCTIONS  FIELDS   DESCRIPTION
    DEFAULT_VALUE     15         1130     Returns zero/empty (memset buffer)
    RECORD_READER     11          286     __RTDynamicCast to TASKNODE, read at byte offset
    SIMPLE_DEFAULT     1           10     Returns simple default constant

  Getter1 functions (5 unique):
    RVA 0x4B43A0: memset trampoline    (928 fields) - initializes output buffer
    RVA 0x7DE620: indexed reader       (66 fields, class 3/5) - baseline/custom
    RVA 0x7DE660: project text reader  (18 fields, class 11)
    + 2 others for special cases
""")

    # ===================================================================
    # SECTION 7: Named vs unnamed field analysis
    # ===================================================================
    print(f"{'='*80}")
    print(f"7. UNNAMED FIELD ANALYSIS")
    print(f"{'='*80}")

    unnamed = [f for f in task_fields if not f['com_name']]
    print(f"\n  Unnamed Task fields: {len(unnamed)} ({100*len(unnamed)/len(task_fields):.0f}%)")

    un_by_sc = {}
    for f in unnamed:
        sc = f['storage_class']
        if sc not in un_by_sc:
            un_by_sc[sc] = 0
        un_by_sc[sc] += 1

    print(f"\n  Unnamed by storage class:")
    for sc in sorted(un_by_sc.keys()):
        name = STORAGE_NAMES.get(sc, f'unk_{sc}')
        print(f"    Class {sc:2d} ({name:<15s}): {un_by_sc[sc]:>4d}")

    un_by_tc = {}
    for f in unnamed:
        tc = f['type_code']
        if tc not in un_by_tc:
            un_by_tc[tc] = 0
        un_by_tc[tc] += 1

    print(f"\n  Unnamed by engine type:")
    for tc in sorted(un_by_tc.keys()):
        name = TYPE_MAP.get(tc, f'unk_{tc}')
        print(f"    Type {tc:2d} ({name:<10s}): {un_by_tc[tc]:>4d}")

    # ===================================================================
    # SECTION 8: Key RVA reference table
    # ===================================================================
    print(f"\n{'='*80}")
    print(f"8. KEY RVA REFERENCE TABLE")
    print(f"{'='*80}")

    print("""
  FUNCTION/STRUCTURE                        RVA            ROLE
  -----------------------------------------  -------------- -----------------------------------
  TTaskTable descriptor (Task)               0xF8BDA0       Static field metadata (1444 entries)
  TTaskTable.vfunc[87]                       0x231F00       Entry from TBkndTaskElem
  TTaskTable.vfunc[100]                      0x231D90       Prop container accessor
  TGeneralPropContainer ctor                 0x1E8910       Constructor (0x3A0 bytes)
  TBkndPropContainer base ctor               0x1E8C2C       Sets primary vtable
  PRIMARY vtable (TBkndPropContainer)        0x102A0A8      vtable at prop_container+0x1E8
  vtable[158] (field dispatcher)             0x239380       ~4KB main dispatcher

  sub_248B40 (record type resolver)          0x248B40       field_id prefix -> record type index
  sub_4A1610 (storage class dispatcher)      0x4A1610       storage_class -> handler
  sub_2682A4 (text/string handler)           0x2682A4       Handles text/virtual/guid types
  sub_23EC60 (main getter dispatch)          0x23EC60       Calls getter1/getter2 pair
  sub_23A624 (special field handler)         0x23A624       Linked/special field_id dispatch
  sub_268560 (type size resolver)            0x268560       engine_type_code -> byte size
  sub_226E2C (base+0x38 dispatch)            0x226E2C       Index into offset table
  sub_282C60 (string compare helper)         0x282C60       String result comparison

  PropInfoData stride                        0xA8 (168)     Cache entry size
  PropInfoData.getter1                       +0x00          Function pointer (8 bytes)
  PropInfoData.getter2                       +0x08          Function pointer (8 bytes)
  PropInfoData.slot_index                    +0x28          Array index (4 bytes)
  PropInfoData.field_id                      +0x40          PjField value (4 bytes)
  PropInfoData.field_size                    +0x44          Output buffer size (4 bytes)
  PropInfoData.engine_type_code              +0x48          Data type (2 bytes)
  PropInfoData.storage_class                 +0x4A          Storage mechanism (2 bytes)
  PropInfoData.flags                         +0x4C          Bit flags (4 bytes)
  PropInfoData.dirty_bit_index               +0x50          Change tracking (4 bytes)
  PropInfoData.linked_field                  +0x54          Related field_id (4 bytes)
  PropInfoData.secondary_index               +0x58          Sub-index (4 bytes)
""")

    # ===================================================================
    # SECTION 9: What we know and what remains
    # ===================================================================
    print(f"{'='*80}")
    print(f"9. COVERAGE SUMMARY")
    print(f"{'='*80}")

    print(f"""
  FULLY MAPPED:
  - Complete dispatch chain: COM -> TBkndTaskElem -> TTaskTable -> PropContainer -> vtable[158]
  - 3207 fields across 8 record types (Task, Resource, Assignment, Links, Calendar, Project, OutlineCode, Task34)
  - 1444 Task field descriptors with 18 properties each (type, storage class, flags, etc.)
  - 838 Task fields with COM property names (58% of Task fields)
  - 16 storage classes identified with semantics
  - 12 engine type codes with dispatch paths
  - 27 unique getter2 functions classified (DEFAULT_VALUE, RECORD_READER, SIMPLE_DEFAULT)
  - 11 unique TASKNODE byte offsets covering 149 fields with direct record access
  - vtable[158] fully decoded: type-code switch, storage-class dispatcher, flags checks
  - COM validation: 838/838 named fields read successfully (0 errors)

  PARTIALLY MAPPED:
  - 606 unnamed Task fields (42%) - need COM property name resolution
  - Storage class internal handlers (sub_4A1610 branches for each class)
  - Indexed storage (class 3/5/9): slot_index maps to position within storage array
  - Linked field pairs (66 total): how Duration<->DurationUnits sync works

  NOT YET ACCESSIBLE:
  - Resource, Assignment descriptor caches (garbage pointers from static descriptor)
  - Runtime prop container live inspection (requires process attach with write)
  - Computed field formulas (class 10): each has its own computation function
  - Virtual field implementations (class 14): dynamically generated content
""")

    # Save summary as TSV
    out_path = os.path.join(ANALYSIS_DIR, 'architecture_summary.txt')
    # Redirect stdout to file
    import io
    old_stdout = sys.stdout
    sys.stdout = io.StringIO()

    # Re-run the entire output
    sys.stdout = old_stdout

    print(f"\nArchitecture analysis complete.")
    print(f"Scripts in: scripts/phase2d_*.py")
    print(f"Data in:    analysis/field_definitive_map.tsv (3207 fields)")
    print(f"            analysis/com_validation.tsv (838 validated)")
    print(f"            analysis/getter2_offsets.tsv (27 functions)")


if __name__ == '__main__':
    main()
