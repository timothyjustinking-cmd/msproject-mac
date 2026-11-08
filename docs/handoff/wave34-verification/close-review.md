# Wave 34 CLOSE review (8 November 2026)

_Two independent read-only reviews of `main..wave34` (main = `4fd9b90`), run by the
CLOSE builder on the CLOSE code tree: one on the writers under `core/mppwrite/` and
their tests against standing rule 1, one on the G4 shell enablements and refusals.
Neither reviewer ran a build or a test; every finding is from source.  The CLOSE
builder re-read the code behind RC-W5 and confirmed it.  This file adds to the G3
verification (`review-g3.md`, `gates-g3.md`), whose R34G3-1..R34G3-9 it re-checks._

## Verdict

**0 Critical, 2 High (both are the G3 Highs, still open; RC-R1 shows the first is also live in the UI), 12 Medium, Lows.  Wave 34 is
NOT mergeable.**  The merge rule (Critical/High go to the front of the next wave)
sends the Highs to the fix-only `wave34b`.  CLOSE changed no code after the reviews,
so the fresh-tree gates stay valid for the code tree they ran on.

## High

| id | finding | where | state |
|---|---|---|---|
| R34G3-1 = RC-R1 | The assignment writer accepts any positive, whole-tick work (`check_assn_row_work`, `contour_ticks8`), and `summary_info` writes `<n> hrs` for any whole hour count except 1.  The UI layer does NOT close it: `assignment_plan_refusal` (`app/edit_model.cpp` ~2800-2860) never reads the task's duration or work, and the Qt handler adds nothing.  Every pair is a 40-hour step; a task of any other whole-hour work (a one-day, 8 h task) passes the builder, the dry run and `push()`, for both assign and removal.  What Project writes for another amount is unmeasured. | `core/mppwrite/assignment_growth.cpp:748-770, 815-830`; `summary_info.cpp:629-663`; `app/edit_model.cpp` | CONFIRMED at source (both reviews).  Direction: refuse everything but the measured 40 h in the writer AND the edit model with a test per refusal; or measure a second amount and a linked or multi-day task (rule 2). |
| R34G3-2 | `tests/roundtrip_assignment_growth.cpp:343-350` masks the first dword at +0, +16 and +32 of the appended Fixed2Data record, used at :637, :822 and :1232.  +16 and +32 are the task and resource identity references, so a wrong resource reference passes `compare_all`; they are checked only against the port's own output. | `tests/roundtrip_assignment_growth.cpp` | CONFIRMED at source (re-confirmed by the writer review).  Direction: mask only +0 (the record's own counter), compare +16 and +32 to Project's file, and keep the guard red on a planted wrong reference. |

## Medium

| id | finding | where |
|---|---|---|
| R34G3-3 | `entry.offset + 4 > var2.size()` in `uint32_t`: an offset 0xFFFFFFFD..FF wraps and `get_u32` reads out of bounds on a crafted file. | `assignment_growth.cpp:367` |
| R34G3-4 | No test for the 3-to-4 real-assignment refusal, the real-assignment count mismatch, "more than one free placeholder row" or "no free placeholder row". | `assignment_growth.cpp:1025-1053` |
| RC-W5 | `save_wbs_code` accepts any 4-row Task table with uids 0..3 and no stored WBS text; it does not check outline level, the summary flag or the FixedMeta state, so a nested three-task file would get `01`/`02`/`03` where Project writes outline-style text.  The header says such shapes are refused; no test covers it.  The shell builder refuses (exactly three saved top-level non-summary tasks), so this is a writer defence-in-depth gap.  CONFIRMED at source by the CLOSE builder (`core/mppwrite/wbs_code.cpp:66-107`). | `wbs_code.cpp` |
| RC-W1 | The first-assignment add has no gate on the placeholder row count; the pair has exactly three going to four, and the writer writes constants (FixedMeta header +4 := 6, scalar 120, size 396 to 520) measured at that count only.  SUSPECTED. | `assignment_growth.cpp:1375-1390` |
| RC-W2 | A settled add sorts the task's resource-uid leaf array; a new uid lower than an existing one is accepted, and the sorted insertion and row position are unmeasured; no refusal, no test. | `assignment_growth.cpp:1150-1165, 1312` |
| RC-W3 | The Add Resources row is a 172-byte clone of the last live resource row; only MaxUnits and two var blocks are checked, so a material/cost resource, non-default rates or a resource with work would copy those bytes into the new row.  The shell builder refuses non-default rates; the writer does not. | `resource_growth.cpp:495-515` |
| RC-W4 | The calendar Fixed2Data tail mask covers +0, +16 and +32; +32 is the shared save-time identity (the last calendar slot's), a reference, not a counter: the R34G3-2 family. SUSPECTED. | `tests/roundtrip_resource_growth.cpp:280-292` |
| RC-W6 | `save_var_record` for the Task table accepts any field id, payload and presence bits; safety rests on the callers. | `varmeta_edit.cpp:570-740, 925-985` |
| RC-W7 | `props_edit` accepts any existing record id with an equal-or-larger payload and any value for a whitelisted scalar tag; four record ids are measured. | `props_edit.cpp:292-300` |
| RC-W8 | Bold: the row uid and field id are stored verbatim (any non-zero); only uid 1 / `0x0B40000E` was measured.  The shell builder pins both. | `viewstream_edit.cpp:395-420` |
| RC-R2 | Assign Resources does not check the resource's rates or the task's constraint, type, effort-driven, calendar, manual or inactive state; the pair used a default-rate resource.  A rated resource would give Project a non-zero assignment cost. SUSPECTED. | `app/edit_model.cpp` |
| RC-R3 | The "exclusive edit alone in the save" rule ignores pending `document_edits` (the table-format and formula hook); no test covers the combination. | `app/edit_model.cpp` cascade ~4510-4585, 5243 |

## Low

- **RC-W9** the WBS dry run writes `w34-wbs-dry-N.mpp` in the system temp directory with a per-process counter; two processes can collide.  Only the scratch file is affected.
- **RC-W10** the four note pairs (escape, multiline, non-ASCII, two) are committed and hashed but no test consumes them and no note encoder exists; any future note edit has no port-side Project comparison.
- **RC-R4** the macOS Add Resources handler (`app/main.mm`, `addResources:` ~3141-3195, 4.2) is read, not run, and untested.  **The G3 handoff's statement that `app/main.mm` was not touched in G4 is wrong** (it changed in 4.2); it was not touched in 4.1, 4.3, 4.5 or 4.6.
- **RC-R5** the macOS shell builds no UI for rename, assign, bold or Define Code.  The shared builders mean no refusal exists in one shell only: macOS cannot reach an enabled-in-core path that Qt refuses.  A feature gap, not a safety gap.
- **RC-R6** shell-level tests: Link ADD and custom-field CLEAR are tested only at app level; the Assign refusals (summary, started, material, units below 100 %, already assigned) only at app level; no macOS test exists.
- **RC-R7** the Qt `add_resource` and `assign_resources` prompt for the name or resource before running the builder, so a refused project is refused after the user types.  Nothing is queued.
- **RC-R8** documentation: the record's "future: not implemented" tooltip is the backlog's wording (the tooltip in code is exactly "not implemented in this port"); the 4.5 "six mutations" paragraph names five; the "pin 51/63 / 49/65" counts were not reconcilable by the reviewer with a raw count of `ribbon_pin.tsv` (the backlog summary says 51 enabled / 63 disabled controls, 64 disabled counting split menu items).
- **R34G3-5..R34G3-9** stand as written in `review-g3.md`.

## Checked and fine (both reviews)

- No route (shortcut, context menu, paste, undo/redo, batch Task Fields, macro) reaches the writers for 4.0, 4.1, 4.2, 4.5 or 4.6 around the builder plus `push()` backstop; every save goes through `save_structural_edits`, and Insert, Delete, link ADD and custom-field SET/CLEAR are refused in `push()` and in the cascade before the first write.
- Notes, Insert Summary, Insert Milestone, Italic, Underline, the font name and size combos, Set Baseline and Renumber stay disabled with the exact decision-9 tooltip.
- `props_edit` and `varmeta_edit` loaders have sound bounds; the DocumentSummaryInformation test compares the stream byte for byte with no mask; every writer that saves goes through `CfbDocument::save` (atomic).
- The new tests use `compare_all` with no `KnownGap` and `effects_ok`, except the tail-range masks above.

## Per enabled command

| command | refuses every other shape up front |
|---|---|
| Resource Information rename (4.1) | yes |
| Add Resources (4.2) | yes in the shell; the writer's row-template check is narrow (RC-W3) |
| Assign Resources (4.3) | **no** - the work amount (R34G3-1 / RC-R1), rates and task attributes (RC-R2) |
| Font Bold (4.5) | yes in the shell; the writer is wide (RC-W8) |
| Define Code (4.6) | yes in the shell; the writer is wide (RC-W5) |
| Insert Task, Delete Task, link ADD, custom-field SET/CLEAR (4.0) | yes, refused |

## Section 4 (e): Project reopens each port save

Not re-run at CLOSE: it needs COM, which is coordinator-only (`docs/coordination.md`).
The record's G4 entries carry the evidence each item attached; the coordinator
re-runs `oracle_w29_reopen.py` on the five enabled saves before any merge.

## Where this goes

The findings above are queued for `wave34b` (fix-only): G0 holds R34G3-1/RC-R1 and
R34G3-2; G1 holds R34G3-3, R34G3-4, RC-W5, RC-R3 and RC-W4; the remaining Mediums and
the Lows are queued in `plan-waves27-30.md`.  Push and CI are the coordinator's.

## Erratum, 08 November 2026 (W34b-1.5, R34G4-7)

Appended; nothing above is rewritten.

- The "refuses every other shape up front" column of the table in "Per enabled command"
  overstated the shell.  It was wrong for Assign Resources (fixed in W34b-0.1, `f08249e`),
  and for Add Resources, Font Bold and Define Code, whose "yes in the shell" held only
  for the shapes the shell checked, not every shape (fixed in W34b-0.3, `f8452b4`,
  W34b-0.4, `dee4998`, and W34b-0.5, `7103578`).
- Section 4 (e): no G4 entry carried Project-reopen evidence for brief section 4(e).
  "The record's G4 entries carry the evidence each item attached" above must not be read
  as covering the reopen check.  That check is the coordinator's (`oracle_w29_reopen.py`
  on the five enabled saves) and has not been run here.
