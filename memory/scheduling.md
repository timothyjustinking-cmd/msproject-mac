# memory — scheduling (W3)

_2026-10-22. Successor: read this, then `spec/scheduling.md`, then
`docs/analysis/sched/README.md`._

## State

- `spec/scheduling.md` written and on disk (main checkout). Covers calendar
  arithmetic, duration/work/units, CPM forward/backward, constraints,
  deadlines, links/lag, slack, summary rollup, recalc triggering/order.
- Static tooling written to `scripts/`: `sched_index.py`, `sched_analyze.py`,
  `sched_oracle_recipes.py`.
- No commit made (coordinator commits per coordination.md rev 2). No oracle
  results yet (COM hangs in agent shells per rev 3).

## What is solid

Verified from the binary (**[B]** in the spec): date32 packing/epoch/weekday;
tenth-of-a-minute durations; 60-byte day records with up to 8 intervals;
effective-week cache; AddWork/SubWork algorithms and their exact entry points;
recalc orchestration and fixpoint loop; summary rollup phase; task-type/effort
mode mapping; constraint storage and setter migration; constraint/task/link
enum values; slack fields stored vs computed; graph validation.

## What is inference ([I]) and must be confirmed

- Work = Duration × Units and the nine-cell task-type matrix.
- Elapsed day/week/month = 1440 / 10080 / 43200 tenths.
- `HoursPerDay/Week/DaysPerMonth` are conversion constants only.
- Link lag anchored at the next working instant; FS/SS/FF/SF bound formulas.
- Critical = TotalSlack <= CriticalSlackLimit (days).
- Resource-calendar union for the task span.

## What is open ([O])

SNLT/FNLT forward vs backward clamping; deadline effect on slack; negative
free slack clamp; summary criticality; internal units fixed-point scale;
whether COM rounds TotalSlack. Recipes `W3-O2..O8` in
`scripts/sched_oracle_recipes.py`.

## Next three actions

1. Hand `W3-O1`, `W3-O2`, `W3-O4`, `W3-O3`, `W3-O5` to the coordinator; write
   results into `spec/scheduling.md` replacing `[I]`/`[O]`, with a fixture id.
2. Run `python scripts/sched_analyze.py` and attach its report to the merge
   request as the re-runnable evidence.
3. With oracle values in hand, decode `TTaskTable::SetDependantFields` (RTTI
   symbols in `analysis/meta/strings.tsv:13015`, implementation not yet found)
   and write the exact fixed-point arithmetic section.

## Traps

- Don't trust field offsets across objects: node offsets in
  `FUN_1401f2570` (`+0x84`, `+0x78`) are not the TASKNODE record offsets in
  `scripts/phase2d_tasknode_layout.py` (`0x730`, `0x740`). Different base.
- `phase2d_*.py` scripts read a live Project process by fixed addresses; they
  are historical, not runnable now.
- The decompile predates the naming pass; use `analysis/named_functions_p1.tsv`
  confidence >=65 only.
