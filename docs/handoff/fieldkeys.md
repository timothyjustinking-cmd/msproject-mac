# Handoff — W5 field keys (`docs/handoff/fieldkeys.md`)

Date 2026-10-22, wave 2. Agents do not run git; the coordinator commits. The
correction to `spec/mpp-records.md` is committed as `3330b64`; all other files
here are on disk in the shared checkout.

## (a) What is done

| path | contents |
|---|---|
| `data/field_keys.tsv` | 185 Task field keys: `record_type, file_key, candidate_name, value_encoding, evidence, confidence` |
| `data/mpxj_task_field_ids.tsv` | mpxj `MPPTaskField.FIELD_ARRAY` (935 ids), cross-check only, source URL in spec |
| `spec/mpp-field-keys.md` | method, proof/inference split, coverage, unknowns, traps |
| `scripts/fk_lib.py` | **corrected** Task VarMeta parser + payload shape + name tables |
| `scripts/fk_vmraw.py` | raw VarMeta hexdump (shows the 24-byte header) |
| `scripts/fk_diff.py` | one-change fixture diff (proves Name/Notes) |
| `scripts/fk_validate.py` | framing + mid/uid validation on 14 files |
| `scripts/fk_dump.py`, `fk_stats.py`, `fk_sample.py` | inspection / stats / value samples |
| `scripts/fk_build.py` | regenerates `data/field_keys.tsv` |
| `scripts/fk_compare.py` | repo names vs mpxj (140 agree / 1 alias / 11 mpxj-only / 33 unnamed) |
| `scripts/fk_coverage.py` | coverage numbers |
| `memory/fieldkeys.md` | state file |

`spec/mpp-records.md` was corrected in place (coordinator's instruction,
2026-10-22; committed as `3330b64`): banner, §0 items 3/4, §5.1, §5.3 note,
§6 counts, §8 count, §9 worked example re-derived, §11 step 5. **Do not edit it
again** — the tree is frozen for merge. Its Var2Data block framing (§5.3) is
unchanged by the correction — same offsets, `u32 len + payload`, same
text/RTF/int decoding — so reader code that only reads blocks needs no change.

Re-run order:

```
python scripts/fk_vmraw.py corpus/probe/base.mpp
python scripts/fk_validate.py
python scripts/fk_diff.py
python scripts/fk_build.py
python scripts/fk_coverage.py
python scripts/fk_golden.py          # vs MS Project's export
```

## (a2) External check: MS Project golden export — PASS

`tests/data/project_a_golden.tsv` (83 live tasks read out of MS Project over COM) vs
the parser (`python scripts/fk_golden.py`; report:
`docs/analysis/fieldkeys/golden_crosscheck.md`):

* `uid` set 83/83; `id`, `duration_min`, `start`, `finish`, `priority`, `pct`
  all **83/83 exactly** against the oracle.
* name sha256 **82/83 exact**; the single difference (uid 8141) is a trailing
  space present in the stored block but trimmed in Project's exported value.
  After `rstrip()` it is 83/83. This is not a framing error.
* The 84th Task row slot is uid 10856 (row 67): no VarMeta group and no
  FixedMeta entry, only stale fixed-row values; it is not a live task and is
  excluded from the golden. **Readers must skip rows with no VarMeta group** or
  they will report 84 tasks.
* Not covered by the golden: non-08:00 starts/DST, notes/text/custom fields,
  Rsc/Assn tables, baselines.

## (b) What is not done

* 416 of PROJECT_A's 1,081 Task VarMeta entries (32 distinct keys) are unnamed; the
  biggest are 0x564 (86), 0x563 (85), 0x4e3 (61), 0x4e4 (43), 0x4e5 (41),
  0x5a1 (33). Shapes are recorded, names were not invented.
* Only the **Task** table. Res/Assn/Cal/Cons/OutlCode VarMeta untouched.
* `TBkndTask/Props` (custom field definitions) not decoded; likely names 0x5a1
  and several project-row-only ids.
* No oracle used; no fixture was created (oracle is coordinator-only).

## (c) Next three concrete actions

1. **Tell the reader-port stream to replace `probe_lib.parse_var_meta()`.** It
   has the old 32-byte-header framing; the correct one is in `fk_lib.py`
   (24-byte header, entries `uid, offset, key`). Name is key `0x0b40000e`.
2. Decode `TBkndTask/Props` custom-field records (mpxj
   `CustomFieldReader14` + `PropsKey.CUSTOM_FIELDS` show the shape) and add the
   user names to `data/field_keys.tsv`.
3. Fixtures still needed from the coordinator (oracle-only, one change at a
   time; diff each with `scripts/fk_diff.py`):
   * **a predecessor set / task-path-visible project** — resolves the
     `0x4e3`/`0x4e4`/`0x4e5` uid-list keys and `0x563`/`0x564`;
   * **a baseline with float** (non-default baseline work/duration so the
     `BaselineN_*` family is populated, plus float for the path fields);
   * **a custom-field project** (local Text/Number/Date/Flag fields set) —
     resolves `0x5a1` and the project-row singleton ids, and is the ground
     truth for the `TBkndTask/Props` `CUSTOM_FIELDS` decode in action 2.
   The three together cover the largest remaining unnamed block; do not guess
   names for them in the meantime.

Note: `0x564`/`0x563`/`0x4e3` were explicitly de-scoped for M1 by the
coordinator, so this is a later-wave task, not a blocker.

## (d) Traps

* The 12-byte framing shift is the root cause of the W1 "keys are not COM ids"
  claim. It looks right on fixtures (uids 1..N) and fails only on real files.
* PROJECT_A's VarMeta header count (1076) is stale; use `(len - 24) / 12` = 1,081.
  The brief's "1,080" is the old parser's count.
* `offset == 0xFFFFFFFF` = no block (101 PROJECT_A entries); read_block raises.
* Row 0 is the project row; key 0x0b40000e on row 0 is the document title.
* `mid` is UniqueID; the `0x80000000 | uid` family is the second value family.
* Block lengths must not be summed as a stream size (shared/unreferenced
  blocks: PROJECT_A 11,978 addressed vs 17,300 bytes).
* mpxj reads the key as `u16` + 16 unknown bits; that is an implementation
  truncation, not evidence that the file stores 16-bit keys.

## Coverage to quote

PROJECT_A: **665 / 1,081 entries = 61.5 %** named, **149 / 181 distinct keys**, and
**8,590 / 11,978 addressed Var2Data bytes = 71.7 %** (49.7 % of the 17,300-byte
stream). PROJECT_B: 1,004 / 1,437 = 69.9 %, 91.6 % of addressed bytes.
