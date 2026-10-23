# QNEWTASKSAREMANUAL scan summary (wave 23, W23-13 / audit A-6)

The E7 evidence trail, committed.  `scripts/scan_newtasks.py` scans every `.mpp`
under `corpus/` and `tests/data/` for project-property record `0x024013c8`
(index 64 = `QNEWTASKSAREMANUAL` of `analysis/project_property_names.tsv`).

Run on 2026-10-23 with `python scripts/scan_newtasks.py --summary`:

| result | files |
|---|---|
| files scanned | 134 |
| record present, value 1 (Manual) | 103 |
| record present, value 0 (Auto) | 9 |
| record absent (original Project 2007 MPP12) | 16 |
| no project props stream (MPP8/MPP9 containers) | 6 |
| record present but not kind 2 / length 2 | 0 |
| unreadable container | 0 |

Only COUNTS are recorded: file names and paths under `corpus/` are client data.
The audit's independent re-tally on the same tree (before
`tests/data/wave23-outline.mpp` was added) was 102/9/16/6 over 133 files; the
extra Manual file is that new synthetic fixture.  Every record found is kind 2,
length 2 (a u16), with no exceptions.

Provenance of the id: `analysis/project_property_names.tsv`, derived from the
local, git-ignored decompiled binary by `scripts/gen_project_property_names.py`.
`tests/mpp_reader_tests.cpp::test_project_property_names` checks the id
arithmetic against that committed TSV and against `synthetic-base.mpp`'s
`114/Props` bytes.
