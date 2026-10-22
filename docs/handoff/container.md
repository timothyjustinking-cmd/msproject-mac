# W1 container — handoff

_2026-10-22, end of the first working session of this stream._

## Done

- `spec/mpp-records.md` — the deliverable: Task-table record layout, byte level,
  with evidence and re-run commands. Answers all six brief items:
  (a) FixedMeta is not a field map, (b) `mid` = UniqueID, (c) row 0 = project
  row, (d) Fixed2Data is row identity, not baseline, (e) Var2Data framing,
  (f) PROJECT_A first-three-task names.
- Probes: `scripts/probe_lib.py`, `probe_fixedmeta.py`, `probe_varmeta.py`,
  `probe_fixtures.py`, `probe_names.py`. All run under plain `python`; no
  oracle and no git were used.
- `memory/container.md` — state, facts, traps.

## Verified vs inferred (one line each)

Verified: FixedData/FixedMeta/VarMeta/Var2Data/Fixed2 sizes and layouts; the
row field map up to ~20 offsets; packed dates; 6-second duration ticks;
`mid`=UniqueID; Name key 0x0b400564; row 0 = project; Fixed2Data not baseline.
Inferred: 0x80000000|uid family = baseline/actual; Fixed2Data id semantics;
UTC/local split in date fields; FixedMeta +2 and +8 blob meanings.

## Not done

- No oracle work at all (confined COM hangs — Revision 3).
- No baseline fixture, so 0x80000000|uid and Fixed2Data remain unproven.
- The full 202-byte fixed schema is not mapped; only the fields touched by the
  nine fixtures are known.
- `114/Props` (88 KB) not decoded; not needed for the Task rows but likely
  needed for a writer.
- The PROJECT_A anomalies (missing row 67, duplicate last entry, count mismatches)
  are documented but not explained.

## Next three concrete actions

1. Ask the coordinator for: (i) a saved-baseline file if BaselineSave can be
   reached any other way, (ii) a task carrying a custom **date** field to test
   whether dates ever live in Var2Data, (iii) a project with a deleted row to
   explain the PROJECT_A anomalies.
2. Extend the fixed-row field map with one-change fixtures per group (cost,
   work, constraint type, deadline), reusing `scripts/probe_fixtures.py`; its
   diff already reports `(row, intra offset)` and a packed-date decode.
3. Decode `114/TBkndTask/Props` (86 B) and `114/Props` to see whether either
   carries a serialization schema; if not, plan a hardcoded per-table row
   struct in the C++ reader.

## Traps hit (save the next person the time)

- Magic bytes are `ba ad df fa`, i.e. u32 `0xFADFADBA`, not `0xBAADFADF`.
- Storage names are space-padded (`'   114'`), so exact path matching fails.
- `mid` is not a row index; fixtures hide that because their uids are 1..N.
- The file-side Name key is 0x0b400564, **not** COM's 0x0b40000e. That one cost
  an hour and would silently corrupt any field table built from
  `analysis/field_id_map.tsv`.
- Fixture file names appear in the project-title block of every `var-*.mpp`,
  so every diff has one "split" change that is pure noise.
- pwsh has no heredoc: use `@'...'@ | python -`; set
  `$env:PYTHONIOENCODING='utf-8'` before printing UTF-16 strings.
- PROJECT_A is internally inconsistent (missing/duplicate meta rows). Treat
  FileMeta as a list, not a per-row array.
