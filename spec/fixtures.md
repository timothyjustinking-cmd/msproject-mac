# Fixture corpus

Built by the coordinator through MS Project 365 (build 16.0.20430) over COM.
Each file is the same base project with exactly one change, so a stream diff
isolates the bytes belonging to that one property.

Base: project start 2026-01-05; three tasks - A 5d, B 3d, C 2d, unlinked;
one resource `Crew`, unassigned. Links and assignments are deliberately absent:
`Project.TaskDependencies` and `Project.Assignments` are not exposed by this
build's late-binding interface, so that part of the model is still untested.

| fixture | bytes | result |
|---|---|---|
| `base.mpp` | 245,760 | yes |
| `var-baseline.mpp` | 245,760 | FAILED: <unknown>.BaselineSave |
| `var-duration.mpp` | 245,760 | yes |
| `var-name.mpp` | 245,760 | yes |
| `var-notes.mpp` | 245,760 | yes |
| `var-pct.mpp` | 245,760 | yes |
| `var-priority.mpp` | 245,760 | yes |
| `var-projstart.mpp` | 245,760 | yes |
| `var-rsc2.mpp` | 245,760 | yes |
| `var-title.mpp` | 245,760 | yes |

Also present: `n1.mpp`..`n6.mpp` (1-6 identical 2d tasks - the stride sweep),
`probe-v1/v2.mpp` (task 1 duration 5d->7d; task 2 priority 700).

These live in `corpus/`, which is git-ignored: client and generated data never
enter the repo. This description is tracked in its place.
