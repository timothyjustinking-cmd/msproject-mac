# W1 container — memory

_Updated 2026-10-22 ~09:05, after the fixture round. Successor: read this, then
`spec/mpp-records.md` (the deliverable), then `docs/handoff/container.md`._

## Deliverable

`spec/mpp-records.md` — byte-level Task-table record spec. Written and on disk.
Covers (a) FixedMeta, (b) VarMeta `mid`, (c) row 0, (d) Fixed2Data,
(e) Var2Data framing and encodings, (f) PROJECT_A first-three-task names.

Probes committed on disk (not git — coordinator commits):
`scripts/probe_lib.py`, `probe_fixedmeta.py`, `probe_varmeta.py`,
`probe_fixtures.py`, `probe_names.py`.

## Established (details + evidence in the spec)

- FixedData = 48 + 202*(tasks+1). Row 0 = project row (verified).
- FixedMeta = 16 + 47*(rows+3): 3 header-descriptor entries + one per row.
  Entry: +0 u16 0, +2 u16 small counter, +4 u32 FixedData offset, +8 16-byte id,
  +24 23 B mostly zero. **No field ids anywhere** → fixed row is positional.
- Row field map recovered by one-change fixtures: +0 row id, +4 UniqueID,
  +78 u16 Priority, +80 ActualDuration, +84 Duration, +88 RemainingDuration,
  +92 PercentComplete, +96 Start, +100 Finish, +104/108 and +112/116 date
  copies, +120..139 actual/stop/resume dates, +128 save time.
- Dates: u32 = (day<<16)|(sec/6), day 15346 = 2026-01-05 (epoch 1983-12-31),
  4800 ticks = 08:00, 10200 = 17:00. Durations same tick, 4800 = 1 working day.
- VarMeta = 32-byte header + N*12 + trailing 4-byte field id. Header keeps
  Var2Data size at +0x14 and the "split entry" mid/offset at +0x18/+0x1C.
- `mid` = row UniqueID (u32 at row+4). Extra family 0x80000000|uid in real
  files (49 groups in PROJECT_A) — inference: baseline/actual.
- Var2Data = blocks of u32 length + payload: UTF-16LE NUL text (Name),
  RTF/ANSI (Notes), 4/8/16-byte ints. Dates/durations not observed there.
- Fixed2Data = 64*(rows+3), Fixed2Meta = 16+96*(rows+3); per-row identity /
  outline table, NOT baseline. Refutes the earlier baseline guess.
- Task.Name key in the file = 0x0b400564 (NOT COM's 0x0b40000e, which holds the
  RTF notes text). Do not decode VarMeta with analysis/field_id_map.tsv.
- PROJECT_A anomalies: VarMeta 1080 entries vs header 1076; FixedMeta 88 vs 87;
  row 67 (uid 10856) missing from both; last FixedMeta entry duplicates row 84.

## Environment facts

- Working directly in the main checkout (Revision 2). No git commands; no
  oracle (COM hangs confined). Python + pwsh work.
- Fixtures: `corpus/probe/{base,var-*}.mpp`, `n1..n6.mpp`, plus the two real
  files. All read-only for us.
- `python scripts/probe_*.py` run from the repo root (scripts dir is on
  sys.path when invoked as a script). `$env:PYTHONIOENCODING='utf-8'` needed to
  print UTF-16 strings in pwsh.

## Open / next three actions

1. Ask coordinator for a saved-baseline fixture (to settle 0x80000000|uid and
   Fixed2Data) and for a custom-date-field fixture (date in Var2Data?).
2. Extend the fixed-row map beyond the ~20 offsets recovered: one fixture per
   field group (cost, work, dates, flags); `probe_fixtures.py` already
   attributes any one-change diff to `(row, intra-offset)`.
3. Decode FixedMeta's +2 counter and +8 blob (compare files from different
   Project builds, or a file with deleted/restored rows).

## Traps hit

- `head`/heredoc are not pwsh; use `@'...'@ | python -` for ad-hoc scripts.
- Python printing UTF-16 to the console fails without PYTHONIOENCODING=utf-8.
- Magic is bytes `ba ad df fa`; as a little-endian u32 it is 0xFADFADBA.
- PROJECT_A is damaged/odd: never index FixedMeta entries by row.
- Fixture file names leak into Var2Data (project title block), so every
  var-*.mpp differs from base in that one block. Ignore the "split" noise.
