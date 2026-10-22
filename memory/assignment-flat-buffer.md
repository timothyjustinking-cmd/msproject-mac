---
name: assignment-flat-buffer
description: "VALIDATED 14/14: Assignment SC1 flat buffer layout, record base discovery via Start+Finish date pair search"
metadata:
  type: project
  originSessionId: 190f8be3-976b-42ea-b2e2-d4534d650a96
---

## Assignment Flat Buffer Layout — VALIDATED

14/14 SC1 fields validated against COM. Record found at 0x023b7f67a470 this session.

### Discovery Method

Searched for packed Start+Finish date pair (0x3bf912c0, 0x3bfd27d8) across all committed memory regions. 14 hits total — the correct one identified by nearby Work=2400000.0 and Cost=400000.0 doubles plus TaskUID=1/ResUID=1 at the expected offsets.

### Complete Layout (156 bytes mapped, ~168 byte record)

| Byte | Size | Type | Name | Encoding |
|------|------|------|------|----------|
| 0 | 4 | u32 | TaskUniqueID | raw |
| 4 | 4 | u32 | ResourceUniqueID | raw |
| 8 | 16 | guid | TaskGuid | raw bytes |
| 24 | 16 | guid | ResourceGuid | raw bytes |
| 40 | 4 | type29 | (unknown) | — |
| 44-49 | 6 | bool | 32 boolean flags | bit-packed |
| 50-55 | 6 | — | (padding) | — |
| 56 | 8 | double | Units | ×10000 (COM 1.0 → 10000.0) |
| 64 | 8 | double | Work | ×1000 (minutes) |
| 72 | 8 | double | ActualWork | ×1000 (minutes) |
| 80 | 8 | double | RegularWork | ×1000 (minutes) |
| 88 | 8 | double | RemainingWork | ×1000 (minutes) |
| 96 | 4 | date | UnknownDate1 | packed date (= Start when no actual) |
| 100 | 4 | date | UnknownDate2 | packed date (= Start when no actual) |
| 104 | 4 | date | Start | packed date |
| 108 | 4 | date | Finish | packed date |
| 112 | 4 | u32/dur | Delay | raw |
| 116 | 2 | i16 | (unknown, val=7) | — |
| 118 | 2 | — | (padding) | — |
| 120 | 4 | u32/dur | LevelingDelay | raw |
| 124 | 4 | — | (padding) | — |
| 128 | 8 | double | Cost | ×100 (dollars) |
| 136 | 8 | double | ActualCost | ×100 (dollars) |
| 144 | 8 | double | RemainingCost | ×100 (dollars) |
| 152 | 2 | i16 | CostRateTable | raw |
| 154 | 2 | i16 | (unknown, val=0) | — |
| 156-159 | 4 | — | (padding/gap) | — |
| 160 | 8 | double | (beyond SC1 map, val=10000.0) | possibly OvertimeWork or Budget |

### Units Encoding

Units uses ×10000 multiplier, NOT ×1000 like Work. COM Units=1.0 (100% allocation) stores as 10000.0 internally. This means the "type 5 (Work)" encoding isn't a uniform ×1000 — Units has its own scale factor.

### Multiple Copies

Found 14 locations with the Start+Finish date pair, at least 4 with full assignment data:
- 0x023b75fa4318 (early copy)
- 0x023b7efa3898 (mid heap)
- 0x023b7f12c028 (another copy)
- 0x023b7f67a4d8 (validated copy, record base 0x023b7f67a470)

Multiple copies likely correspond to: direct storage, undo stack, save buffer, and the "working" copy.

**Why:** Confirms the byte_off_raw/32 formula works for assignment records just as it does for tasks.
**How to apply:** Use this layout to read/write assignment flat buffer data. Note the ×10000 Units encoding differs from the ×1000 Work encoding.

Related: [[byte-offset-formula]], [[descriptor-cache-architecture]], [[flat-data-buffer]]
