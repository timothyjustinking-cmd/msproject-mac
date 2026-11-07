# Golden expectations for `corpus/project_a.mpp`

Exported from MS Project itself (build 16.0.20430) by opening the file **read-only**
and reading the task list over COM: the ground truth the reader must match, not
another parser's opinion of the file.

- **83 live tasks.** `uid` is stable across relocations; `id` is display order.
- **There is no name column** (W34-B5, 8 Nov 2026).  The file had carried the first 64
  bits of an unsalted SHA-256 of each task name, which a dictionary guess inverts, and
  the real names are a client's construction programme.  `uid` is the key; the columns
  are `uid id duration_min start finish priority pct`.  The readable copy stays local at
  `corpus/probe/project_a_golden_full.tsv`, which is git-ignored.  The old hashes remain
  in public git history; purging them is the operator's decision (audit B-1), not a
  change this file can make.  The reader's task NAMES are still checked, by the
  fixtures with neutral names (`tests/mpp_reader_tests.cpp`), not by this file.
- `duration_min` is minutes as MS Project reports them (its internal unit is tenths of
  a minute; the reader works in those ticks, so expect `duration_min * 10`).
- Dates are ISO-8601 with offset, as returned by COM.

## Why 83 and not 84

The task table holds 85 rows (from `(17218 - 48) / 202`), row 0 being the project row,
which leaves 84 row slots for 83 live tasks. Wave 1 already found a row (uid 10856,
row 67) with no `FixedMeta` entry and no `VarMeta` group — consistent with a deleted
task's slot still occupying space. A reader that reports 84 tasks is reading a dead
row; it should either skip rows with no metadata or flag them.
