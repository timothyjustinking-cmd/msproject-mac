# PROJECT_A golden cross-check — corrected VarMeta framing (W5)

Date: 2026-10-22. First external test of the corrected Task VarMeta framing
(24-byte header, 12-byte `(uid, offset, key)` entries; see
`spec/mpp-field-keys.md` §0 and `spec/mpp-records.md` §5.1).

Inputs:

* `corpus/project_a.mpp` — parsed with `scripts/fk_lib.py`.
* `tests/data/project_a_golden.tsv` — 83 live tasks exported from **MS Project 16.0.20430
  over COM** by opening the file read-only. Names are
  `sha256:<first 16 hex of UTF-8 name>`; see `tests/data/project_a_golden.md` for why
  it is 83 and not 84.

Reproduce:

```
python scripts/fk_golden.py
```

## Method

Read-only; nothing is written by the script. Field sources (all are verified by
the one-change fixtures in `spec/mpp-records.md` §2–§4):

| golden column | parser source |
|---|---|
| `uid` | FixedData row +4 (u32) |
| `id` | FixedData row +0 (u32) |
| `name` | VarMeta key `0x0b40000e` for that uid, UTF-16LE, NUL stripped |
| `duration_min` | FixedData row +84 (u32 ticks, 4800/day); minutes = ticks / 10 |
| `start` | FixedData row +96 (packed date) |
| `finish` | FixedData row +100 (packed date) |
| `priority` | FixedData row +78 (u16) |
| `pct` | FixedData row +92 (low u16; the u32 there packs pct and pct-work as two u16s) |

Tasks are matched by `uid`, not table order (the table is not in display-id
order). Dates are decoded with the packed-date rule (`day = value >> 16`,
`seconds = (value & 0xffff) * 6`, epoch 1983-12-31) and rendered as
`YYYY-MM-DD HH:MM:SS+00:00`; every golden date carries `+00:00`, so only the
wall-clock digits are compared. Name comparison is the same sha256 prefix the
golden file uses, so no client name is reproduced here.

## Result

83 golden tasks; 84 Task row slots in the parser (row 0 is the project row).

| field | matches | notes |
|---|---|---|
| `uid` | **83 / 83** | no golden uid missing from the parser; one extra parser row (below) |
| `id` | **83 / 83** | display order matches `row +0` |
| `name` (sha256) | 82 / 83 exact | **83 / 83 after stripping one trailing space** (below) |
| `duration_min` | **83 / 83** | ticks / 10 is exact for every row |
| `start` | **83 / 83** | second precision |
| `finish` | **83 / 83** | second precision |
| `priority` | **83 / 83** | all 500 |
| `pct` | **83 / 83** | 0–100 |
| all fields at once | 82 / 83 | the only failure is the name whitespace case |

## The one mismatch

`uid 8141` (golden id 13), name only:

* parser hash of the stored block: `sha256:d2d31247d7563771`
* golden hash: `sha256:10dbe851f0ba475d`
* hash of the stored value with trailing whitespace removed:
  `sha256:10dbe851f0ba475d` — matches the golden.

The stored Var2Data block is 40 bytes = 19 UTF-16 characters + NUL; the last
character is a space. MS Project's exported value has no trailing space. So the
parser reproduces the stored bytes exactly and the golden reproduces Project's
normalised view; **this is not a framing or attribution error**. Whether COM
itself trims or the export script does cannot be determined from the repo (the
COM exporter is not committed), but the oracle is the display truth: a reader
that wants to match Project should `rstrip()` trailing whitespace on names.
Keep the raw stored value if byte-faithful round-trip matters.

No other name differs in any way: 82 exact + 1 trailing-space = 83, and 0
leading-space/other variants.

## The 84th row (`uid 10856`)

The parser reports 84 task rows; the extra one is:

```
row 67: id 70, uid 10856
  name: none (no VarMeta entry with this uid at all)
  fixed row: duration 2,400 ticks (240 min), start 2026-10-07 08:00,
             finish 2026-10-13 17:00, priority 500, pct 0
```

It has **no VarMeta group** and **no FixedMeta entry** (FixedMeta covers rows
1–84 except 67, and row 84 is duplicated — the anomaly already recorded in
`spec/mpp-records.md` §8). The golden file excludes it, and it is absent from
the readable local export as well. This is consistent with a deleted task whose
FixedData slot was not reclaimed: the fixed row still holds plausible stale
values, but no variable data exists to identify it.

**Reader rule:** enumerate task rows by their VarMeta group (or skip rows with
no VarMeta entry), rather than assuming every FixedData row is a live task. Do
not synthesise a task from the fixed row alone.

## Verdict

The corrected framing **passes** the first external test: every live task's
`uid`, `id`, duration, start, finish, priority and percent complete match MS
Project exactly, and 82 of 83 names match exactly with the last differing only
by whitespace Project trims. There is no sign of misattributed keys or shifted
rows, and no golden task is missing.

What this does not cover (keep in the fixture queue): non-08:00 starts and DST
boundaries, notes/text/custom fields, the resource/assignment tables, and
baselines. It also cannot distinguish "Project trims names" from "the export
script trims names"; a COM read of a deliberately space-padded name would settle
that, if it ever matters.
