# DocumentSummaryInformation writer

_W34-0.2 decode note, 30 October 2026. Companion to the pair reports under
[`evidence/`](evidence/). Static evidence is from the git-ignored Ghidra
model. No dynamic trace this wave (W34-0.4 decision)._

## Rule

`DocumentSummaryInformation` is an OLEPS property-set stream. The port writer
`core/mppwrite/summary_info.{h,cpp}` is the authoritative decode (W29); this
note records the rule, its measured basis, and the static limit.

```
header, 28 bytes
  +0   u16  byte order 0xfffe
  +2   u16  version
  +4   u32  system id
  +8   16-byte CLSID
  +24  u32  section count = 2
section descriptor, 20 bytes each
  16-byte FMTID + u32 section offset
section 0  FMTID d5cdd502-2e9c-101b-9397-08002b2cf9ae
  a property table of (pid, offset) pairs, then the values:
  pid 0x0D  TitlesOfParts  VT_VECTOR|VT_LPSTR of the seven summary titles
  pid 0x0C  HeadingPairs   names them
section 1  FMTID d5cdd505-2e9c-101b-9397-08002b2cf9ae
  a dictionary (pid 0) and the typed display values:
  pid 2 % complete, 3 cost, 4 duration, 5 finish FILETIME,
  6 start FILETIME, 7 work, 8 % work complete;
  pids 0x1000002..0x1000008 are their labels.
```

Value encodings: a simple LPSTR is `u32 30, u32 length` with the length
rounded up to four and NUL-padded; a FILETIME is `u32 64, u64`; sections are
padded to a four-byte size and the section offsets are recomputed from the
sizes.

The seven display values are derived from the project row (task table uid 0):

| title / typed pid | source | format |
|---|---|---|
| titles[0] / pid 6 | start, row +96 | fixed date text |
| titles[1] / pid 5 | finish, row +100 | fixed date text |
| titles[2] / pid 4 | duration, row +84 ticks | `<days> days`, `?` suffix when DurationFormat +164 has bit 0x20 |
| titles[5] / pid 2 | % complete, row +92 | `<n>%` |
| titles[6] / pid 8 | % work complete, row +94 | `<n>%` |
| titles[3] / pid 7 | total work, row +8 | unchanged in every measured pair |
| titles[4] / pid 3 | cost | unchanged in every measured pair |

The date text is `Ddd D/MM/YY` (weekday, day not zero-padded, two-digit month
and year). Duration is ticks / 4800 (one 8-hour day), at most two decimals
with trailing zeros trimmed. The two measured DurationFormat words are 21 and
53; 53 = 21 | 0x20 and bit 0x20 is Project's `?` suffix.

The FILETIMEs use the project row's date with a FIXED local time (measured
08:00 for start and 17:00 for finish on the fixture machine, UTC+11 in AEDT).
The writer keeps the stored time of day and adds the row's day delta in
seconds, so it needs no timezone database and reproduces Project for a move
that stays inside one UTC offset — every measured pair does.

The stream is replaced whole on every editor save. It has no per-save noise:
control vs null is byte-identical in every pair. A string that changes length
moves the stream length in four-byte steps (measured 1096, 1100 and 1104 B),
because the LPSTR values are padded to four; every later byte shifts.

Refusals (`mppwrite::Error`, so nothing is written): a stream whose layout is
not the one above (checked by re-serialising it unchanged), a titles vector
that is not seven VT_VECTOR|VT_LPSTR strings, typed pids 2..8 missing or of
an unexpected type, a project-row DurationFormat other than 21 or 53, an
invalid project-row start or finish, and a total work (+8..+15) that is
negative, not finite, not a whole number of hours, or exactly one hour. The
total work changing is measured, not refused: `wave30-assign-add` moves it
0 → 2400000.0 and the work string `0 hrs` → `40 hrs`, and
`wave30-assign-delete` moves it back (W34-3.1).

## Evidence

### Measured (pairs)

In the wave-30/31 corpus, 8 of the 23 pairs change the stream. All eight have
zero control-vs-null noise:

| pair | control | null | edit | effect B | tail effect B |
|---|---|---|---|---|---|
| `wave30-level` | 1100 | 1100 | 1104 | 162 | 4 |
| `wave30-calculate` | 1100 | 1100 | 1104 | 175 | 4 |
| `wave30-assign-add` | 1100 | 1100 | 1104 | 557 | 4 |
| `wave30-assign-delete` | 1104 | 1104 | 1100 | 561 | 0 |
| `wave31-fill` | 1104 | 1104 | 1100 | 181 | 0 |
| `wave30-task-mode-manual` | 1100 | 1100 | 1096 | 335 | 0 |
| `wave30-move-back` | 1100 | 1100 | 1100 | 7 | 0 |
| `wave30-inactivate-second` | 1100 | 1100 | 1100 | 11 | 0 |

- `wave30-level` decodes the writer's work: the date text changes
  (`30/0…` → `6/1…`), the duration string changes, and the typed FILETIMEs
  and their labels shift. The measured title strings include
  `Scheduled Duration`, `Scheduled Finish` and `Scheduled Start`. The tail
  effect of 4 B is the padded duration string growing by one character.
- `wave30-calculate` and `wave30-assign-add` grow by 4 B, and the two delete
  pairs and the Fill pair shrink by 4 B: a duration/percent string crossing a
  four-byte pad boundary. `wave30-move-back` keeps the length and changes
  only 7 date bytes; `wave30-inactivate-second` changes 11 bytes.
- The 557/561-byte raw effect counts in the assignment pairs include the
  shifted suffix; the decoded values are the authoritative view.
- The other 15 pairs do not print a DSI section at all: the stream is
  byte-identical between their null and edit saves.

### Static (Ghidra model)

- The string `DocumentSummaryInformation` appears twice in
  `analysis/meta/strings.tsv` (`0x1411438ea`, `0x141143912`) with an EMPTY
  function-reference list, and the section FMTID `d5cdd502` appears in no
  decompiled function. The stream is written through the OLE property-set
  API, which the decompiled model exposes only as opaque buffers; no
  stream-specific routine was isolated within the two-hour box.
- The enclosing save driver is `FUN_140780df0` @ `0x140780df0`
  (SaveIStorageDocument) → `FUN_1408a9bc0` @ `0x1408a9bc0`, which writes the
  document object after the container pass. That proves the stream is part of
  the document save, not of the table serialisers.
- The measured decode is the authority here: `core/mppwrite/summary_info.h`
  documents a byte-exact re-serialiser and the refusals above, derived from
  the same 23-pair null-save experiment (W29-1.0b). Note that the W29 header
  comment's "eighteen of the 23 pairs change it" belongs to the W29 pair set;
  the current wave-30/31 corpus has 8.

### COM facts

Every pair is a Project 2021 editor save through `scripts/oracle_lock.py`
(Calculate Project, Set Baseline, assignment add/delete, Level, Move,
task-mode change, Inactivate, Fill). No COM call wrote a property set.

### W34-0.5: the link-add pair

The synthetic link pair ([evidence](evidence/wave34-link-add.md)) adds a
measured DSI effect with no property change: after
`Task.TaskDependencies.Add(1, Type:=1, Lag:=0)` Project reschedules the
successor (start 2026-01-04 13:00 -> 2026-01-09 13:00, finish 2026-01-07
12:00 -> 2026-01-14 12:00), and `DocumentSummaryInformation` grows 1100 ->
1104 with 580 effect bytes -- the cached Scheduled Start/Finish strings now
describe the moved task. The port's `add_task_link` writes no DSI, so the
pair's comparison records a named expected failure (first of 584 at byte 64:
port `0x24`, Project `0x28`) in `tests/roundtrip_links.cpp`.

## Limits

- The writer routine is not statically isolated (opaque property-set API).
- Only display values derived from the project row are decoded. Author,
  company, manager and the other document properties are constant on the
  fixture (neutralised) and a change to them is unmeasured.
- A date move crossing a DST boundary is unmeasured; the fixed-local-time
  rule would need the other offset.
- DurationFormat is measured only at 21 and 53.
- Total work and cost never change in the corpus, so their display values
  are carried unchanged, not derived; the writer refuses a total-work change.
- Titles other than the seven measured strings are refused rather than
  reformatted.

## Pairs still needed

1. **A change to the project row's total work** (assign work to a second
   task while the project row's +8 moves): does typed pid 7 and title[3]
   change, and by what format? The port refuses this today.
2. **A DurationFormat other than 21/53** (e.g. hours): does the `?` rule and
   the tick divisor change?
3. **A move across a DST change** (or a save on a machine in another UTC
   offset): confirms the FILETIME fixed-time rule.
4. **A percent/work change that does not alter the string length**: confirms
   that only the padded length drives the ±4 stream deltas.

No dynamic trace this wave (W34-0.4 decision). The property-set writer is a
live-trace candidate only if a future wave needs the other document
properties.
