# Wave 35 G2 (view-state persistence): coordinator verification (Mon 9 Nov 2026)

The gated commit is `daf46f8` (`WAVE-35-G2-COMPLETE`).  Gates ran in the verify worktree,
detached at `daf46f8`, with fresh build trees, Release, `CORE_WARNINGS_AS_ERRORS=ON` and the
main corpus junctioned in read-only, from 05:20 to 05:43.  Logs are in `build/verify-w35/g2/`
(git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; expected failing set reached (`constraint-mso`) |
| M2 `recalc --compare` | exit 0 |
| app_qt / app | 52/52 / 34/34, 0 warnings |
| fixtures; markers | OK; OK |

## Fixtures

The 51 new `tests/data/wave35-viewstate-*.mpp` files (sort, table, outline and zoom pairs, each
with its control and null save) were scanned byte by byte, in ASCII and UTF-16LE, for operator,
company and path markers.  There were 0 hits.  The only embedded paths are under the neutral
`C:\w35\` root, plus Project's internal `gbui://mainpage.htm`.

## CI on `daf46f8`

| workflow (run) | result |
|---|---|
| `ci` 42509516181 | ubuntu-gcc, ubuntu-asan, windows-msvc green |
| `qt-shell` 42509520915 | linux-qt green; windows-qt green; macos-qt red, only on the two prompt-title checks of R35G0-1 (`qt_sort_persist` and `qt_outline_persist` pass on macOS) |

## Mutations (coordinator, `build/verify-w35/mutate-g2.py`)

Each guard was switched off with a non-constant false.  The listed tests were rebuilt and run
by exact name, then the file was restored and all six tests re-run green.

| mutation | result |
|---|---|
| M1 to M3: sort writer one-key, Renumber and view-block digest checks off | red in `mppwrite_roundtrip_viewstate_sort`; green in `app_edits_sort` (the edit model refuses first) |
| M4: edit-model sort row count off | red in `app_edits_sort` (the writer's refusal surfaces with the wrong needle); green in `qt_sort_persist` |
| M5, M6: edit-model equal-duration and sibling-name refusals off | red in `app_edits_sort`; green in `qt_sort_persist` |
| M7: positive control, sort direction word inverted | red in all three; the Qt gate fails at queueing, not on the saved bytes |
| M8: outline writer measured-outline check off | red in core and app |
| M9: outline writer level range off | red in core; green in app (the edit model refuses first) |
| M10, M11: edit-model builder level range and hides-nothing refusals off | red in app and Qt |
| M12: edit-model `push()` hides-nothing refusal off | red in app; green in Qt |
| M13: positive control, collapsed flag 0x03 to 0x02 | red in core and app (FixedMeta byte 219); **green in `qt_outline_persist`**, whose save check is vacuous (queued as W35b-1.14) |

The builder's own record lists two survivors, a2 and a4.  The review shows that a2 is not
equivalent (R35G2-5).

## Independent review (`review-g2.md`)

A read-only agent reviewed `98f74fb..daf46f8`: 0 Critical, **4 High**, 4 Medium, 3 Low.  The
coordinator confirmed R35G2-1 and R35G2-4 at source: `save_view_outline` reads only the task
table's UniqueIDs and no view, table or zoom state, and `make_sort_edit` has no direction check.

- **R35G2-1 (High):** Show Level pins no view state.  Project saves carrying an applied table,
  a zoom or a sort are accepted and collapsed.
- **R35G2-2 (High):** neither view-state writer reads the outline levels or the order inputs
  from the file.  The measured shape is guarded in the edit model only.
- **R35G2-3 (High):** Sort accepts any five-task outline, not only the measured one.
- **R35G2-4 (High):** Duration ascending, and any ascending sort that reorders rows, is enabled
  with no pair.
- **Mediums R35G2-5 to -8:** `push()` has no Show Level shape guard (a2 is not equivalent); the
  shell keeps a queued Show Level after a manual expand; generic needles and unreached writer
  guards; the Applied-table deferral records a judgement, not an attempt.
- **Lows R35G2-9 to -11:** listed in the review.
- **Clean:** the measured shapes match Project's pairs outside honest masks; no `KnownGap`;
  `tests/save_pair_noise.h` untouched; the row-layout rebuild matches Project; both deferrals
  stay session-only in every layer; the zoom width-dependence holds on the fixtures; bounds,
  atomic save and hygiene.

## Merge rule

G2 does not meet the rule: four Highs are open.  Sort and Show Level stay on the unmerged
`wave35` branch.  Their Highs lead the fix wave 35b (W35b-0.5 to -0.8), with the Mediums in its
G1 and the Lows in its G2.
