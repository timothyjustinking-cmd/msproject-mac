---
name: runtime-storage-architecture
description: Full-memory differential scan reveals THREE distinct storage locations for task fields — getter record, flat buffer, and packed data block
metadata:
  type: project
---

## Runtime Storage Architecture — Three Distinct Locations

Full-memory differential scanning (set value via COM, diff all memory) revealed that task fields are stored in at least THREE separate memory structures:

### 1. Getter Record (~0x23b75edc834)
- Contains: Duration at +0x740 (i32, minutes), dates at +0x7b4-0x7c8, ConstraintType at +0x730
- Accessed by: simple getter2 functions via CALL sub_369437 → record_ptr, then `MOV reg, [record + byte_offset]`
- Duration getter multiplies raw by 10 (`LEA ECX,[EAX+EAX*4]; ADD ECX,ECX`) for COM 1/10-min units
- Stored value in **minutes** (2400 = 5 days × 480 min/day)
- Has dirty flag at +0x668 (set to 1 when Priority or PercentComplete changed — triggers recalc)
- Two copies 0xE10 apart (possibly current + baseline or committed)

### 2. Flat Buffer (~0x23b7ef6dd3c)
- Contains: Priority as i16 (verified: 500→777 transition)
- ~150MB from the getter record
- Surrounded by zeros (sparse), no field_ids or TASKNODE headers nearby
- Complex Priority getter uses vtable dispatch through offset 0x910 to reach this

### 3. Packed Data Block (~0x23b02d0047d)
- Contains: PercentComplete as i16×10 (value 420 = 42×10)
- At an ODD byte address (unaligned), suggesting byte-packed data area
- Surrounded by heap pointers (0x23b... range), not field_ids
- Possibly inside a TASKNODE sparse block's data area (storage_class 5 = indexed)

### Key Insight: Storage-Class → Storage-Location Mapping
- storage_class 1 "direct" fields → getter record (Duration, dates, ConstraintType)  
- Priority uses vtable dispatch → flat buffer (complex getter)
- storage_class 5 "indexed" fields → packed data block (PercentComplete)
- Marked, FixedCost, Milestone, HideBar → NOT in any of these three (possibly in TASKNODE sparse block or direct-storage 2D block found earlier)

**Why:** Understanding the storage routing is key to mapping all 1444 task fields.
**How to apply:** When tracing a field's storage, determine its storage_class first, then search the corresponding memory structure.

Related: [[cache-stride-breakthrough]], [[tasknode-memory-layout]], [[direct-storage-block]], [[flat-data-buffer]]
