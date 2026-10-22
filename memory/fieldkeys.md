# W5 — field keys (`memory/fieldkeys.md`)

Wave 2, 2026-10-22. Stream: Task VarMeta field-key table.

## State: done

* `data/field_keys.tsv` — 185 Task keys observed across the corpus, with
  name / encoding / evidence / confidence.
* `spec/mpp-field-keys.md` — method, proof vs inference, coverage, unknowns,
  traps.
* `scripts/fk_*.py` — corrected parser + diff + build + coverage, all re-runnable.
* `data/mpxj_task_field_ids.tsv` — mpxj `MPPTaskField.FIELD_ARRAY` (id ->
  enum name) used only as a cross-check; provenance URL in the spec.

## The finding that matters

**VarMeta is a 24-byte header + n × 12-byte entries `(uid, offset, key)`.**
Wave 1 used a 32-byte header and started entries 12 bytes late, so it paired
each key with the *next* entry's uid/offset. Consequences:

* `Task.Name` key is **0x0b40000e** (not 0x0b400564); `Notes` is **0x0b40000f**.
  These are exactly the PjField ids in `analysis/field_id_map.tsv`.
* The "split entry" / header `mid+offset` / 4-byte trailer described in
  `spec/mpp-records.md` §5.1 does not exist; header `+8` is the entry count
  (correct except PROJECT_A, which claims 1076 while 1,081 entries are present).
  `spec/mpp-records.md` has since been corrected in place (banner + §0 items
  3/4, §5.1, §5.3 note, §6, §8, §9, §11) on the coordinator's instruction;
  committed as `3330b64`, do not edit that file again.
* `analysis/field_id_map.tsv` therefore does decode the file. The premise of
  the W5 brief ("file keys are not COM ids") is false — worth telling the
  coordinator promptly.

## External check: MS Project golden export (2026-10-22)

`python scripts/fk_golden.py` vs `tests/data/project_a_golden.tsv` (83 live tasks read
out of MS Project over COM). Report:
`docs/analysis/fieldkeys/golden_crosscheck.md`.

* uid set 83/83 (no golden uid missing); id, duration, start, finish, priority,
  pct all **83/83** exactly.
* name sha256 **82/83 exact**; the one difference (uid 8141) is a stored
  trailing space that Project's exported value trims. After `rstrip()`: 83/83.
  Not a framing error — the parser reproduces the stored bytes.
* The 84th parser row is uid 10856 (row 67): **no VarMeta group, no FixedMeta
  entry**, stale fixed values only. It is a deleted task's slot and is not in
  the golden file. Readers should skip rows with no VarMeta group.

## Coverage (from `python scripts/fk_coverage.py`)

| file | entries | named entries | distinct keys named | named bytes |
|---|---|---|---|---|
| base | 42 | 30 (71.4 %) | 8 / 12 | 224 / 296 |
| PROJECT_A | 1,081 | 665 (61.5 %) | 149 / 181 | 8,590 / 11,978 (71.7 %) |
| PROJECT_B | 1,437 | 1,004 (69.9 %) | 147 / 156 | 26,204 / 28,596 (91.6 %) |

PROJECT_A named bytes vs the whole 17,300-byte Var2Data stream = 49.7 % (5,322 bytes
are not reachable from any entry offset). 101 PROJECT_A entries have offset
`0xFFFFFFFF` (no block).

## Confidence split in data/field_keys.tsv

proven 2 (Name, Notes — fixtures), high 138 (repo PjField tables + mpxj, shape
consistent), medium 12 (mpxj-only baseline duration units, or shape check not
possible), none 33 (unnamed by both tables).

## Not done

* 416 PROJECT_A entries / 32 keys remain unnamed. Big ones: 0x564 (86), 0x563 (85),
  0x4e3 (61), 0x4e4 (43), 0x4e5 (41), 0x5a1 (33), 0x15e (21), 0x7c (16).
  Observed shapes are in the spec §5; no names were invented.
* Res, Assn, Cal, Cons, OutlCode VarMeta tables were not touched (Task only,
  per the brief).
* `TBkndTask/Props` custom-field definitions not decoded.

## Next three actions

1. Decode `TBkndTask/Props` (`CUSTOM_FIELDS` in mpxj terms) to name local
   custom fields — likely source for 0x5a1 and the singleton project-row keys.
2. Ask the coordinator for one fixture that adds a predecessor and one that
   saves a baseline with float; diff with `scripts/fk_diff.py` to resolve
   0x4e3/0x4e4/0x4e5/0x563/0x564.
3. If the reader port consumes keys, wire it to `fk_lib.load_task()` output and
   ignore `probe_lib.parse_var_meta()` (which has the old framing).

## Traps hit

* The off-by-12 framing error is seductive: it yields plausible mids/offsets
  for fixtures (uids 1..N) and only breaks on real files.
* PROJECT_A's VarMeta header count is stale; use stream length.
* `offset == 0xFFFFFFFF` means "no block"; `probe_lib.read_block` raises.
* Row 0 is the project row and its title shares key 0x0b40000e with task Name.
* Do not sum block lengths as a stream size (shared/unreferenced blocks).
* mpxj's `VarMeta12` reads the key as `u16` + 16 unknown bits, so only the low
  half of the `0x0B40xxxx` word reaches its field map — do not read that as the
  file storing u16 keys.
