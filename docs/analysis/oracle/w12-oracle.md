# Wave-12 oracle — recurring exceptions and link-row add

2026-10-22, coordinator-only (COM). Tool: `scripts/oracle_w12.py`
(`exceptions`, `linkadd`, `apiprobe`); committed JSONs `w12-exceptions.json`,
`w12-linkadd.json`; fixtures under git-ignored `corpus/probe/`.

## 1. Recurring calendar exceptions (`w12-exceptions.mpp`)

`Calendar.Exceptions.Add(Type, Start, Finish, Occurrences, Name, Period,
DaysOfWeek, MonthPosition, MonthItem, Month, MonthDay)` works on
`Project.Calendar` (members: `Exceptions`, `Years`, `WeekDays`, `WorkWeeks`,
`Period`, `Reset`). One exception of each `PjExceptionType` was added and the
file saved + reopened; `w12-exceptions.json` records every COM property before
and after the round trip.

| label | Type | stored parameters (COM read-back) |
|---|---|---|
| daily | 1 | Period 0, 29 occurrences in the range |
| weekly | 6 | Period 1, DaysOfWeek 10 |
| monthly_day | 4 | Period 1, MonthDay 15 |
| monthly_pos | 5 | Period 1, MonthPosition 0 (MonthItem/MonthDay read 0) |
| yearly_day | 2 | Period 0, Month 3, MonthDay 15 |
| yearly_pos | 3 | MonthPosition 4, MonthItem 8, Month 11 |
| day_count | 7 | Period 1, 5 occurrences |

Notes for the decoder (W53):
- `Period` is a 1-based every-N value validated by Project ("1-999 for day
  counts, 1-99 for week/month counts"); `0` is stored for daily and yearly
  ranges.
- The monthly-positional COM read-back loses MonthItem/MonthPosition (both 0),
  so validate that type against the exception's Start/Finish dates rather than
  the properties.
- The file stores the parameters somewhere in `114/TBkndCal`; the fixture is
  the ground truth for the byte hunt (diff against a no-exception calendar).

## 2. Adding links: four Cons streams, not two (`w12-linkadd.mpp`)

Project added two links to `tests/data/synthetic-base.mpp` (1→2 FS+0, 1→3
SS+1d) and saved. Diff vs the base:

| stream | size | content |
|---|---|---|
| `TBkndCons/FixedData` | 0 → 40 (2×20) | `uid, pred, succ, u16 type, i32 lag, u16 fmt` |
| `TBkndCons/FixedMeta` | 16 → 36 (16+10×2) | header count 2, size 40; entries `+0=0`, `+2=0`, `+4=offset`, `+8=0x00dd` |
| `TBkndCons/Fixed2Data` | 0 → 96 (**48 per link**) | three 16-byte ids per row, template `13 67 bc <XX> cd f4 11 f1 9f 64 1c c1 0c 05 4c 03`; XX varies by link/endpoint (0x12/0x13/0x0a/0x0b/0x0c) |
| `TBkndCons/Fixed2Meta` | 16 → 34 (**16+9 per link**) | header count 2, size 96; entries `+0=0`, `+4=offset (0,48)`, `+8=0x0f` |

No VarMeta/Var2Data change. A link delete tombstones the FixedMeta entry and
sets the table header +4 bit (already measured in wave 11); all four streams keep
their sizes.

The ids are identity values, not semantic data; a deterministic generator from
the link/endpoint uids is a reasonable writer choice, but only a Project reopen
proves it. The coordinator's gate for W52 is `w52-links.mpp` reopening with the
two links' uids, types and lags.
