# Wave 35b G1 (view state, assignment and shell Highs): coordinator verification (Mon 9 Nov 2026)

The gated commit is `e3c86fe` (`WAVE-35b-G1-COMPLETE`).  It covers items 1.1 to 1.8
(`114861b`, `84a24d1`, `b47f343`, `7bd9c5c`, `e4a380d`, `e813c11`, `48630a4`, `97afeee`).
Gates ran in the verify worktree, detached at `e3c86fe`, with fresh build trees, Release,
`CORE_WARNINGS_AS_ERRORS=ON` and the main corpus junctioned in read-only, from 10:49 to 11:26.
Logs are in `build/verify-w35b/g1/` (git-ignored).  The commit ids are the builder's;
`wave35b` is rebuilt onto the new `wave35` after CLOSE (`gates-g0.md`).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; expected failing set reached (`constraint-mso`) |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

**Not compiled here:** item 1.7's `app/main.mm` change (the tag-based resource pop-ups).
CI's macOS jobs are its only compile gate, and they wait for the `wave35b` push.

## Fixtures and names

G1 adds no `.mpp` blob (`e1397d1..e3c86fe`: 0 blobs).  `guard_names.py` exits 0 and
`scan_client_names.py` finds 0 hits at `e3c86fe`.  Every builder commit carries
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35b)`.

## Mutations (`build/verify-w35b/mutate-g1.py`, `g1/mutations.log`)

24 guards were switched off with a non-constant false, one at a time.  Each guard's lanes were
rebuilt and run by exact ctest name, then the file was restored.  The worktree was clean
afterwards, and all nine lanes were green again.

| id | guard switched off | own lane | other lanes |
|---|---|---|---|
| O1 | Show Level: applied view is Gantt Chart | red | app, Qt green (writer-only guard) |
| O2 | Show Level: applied table is Entry | red | app red |
| O3 | Show Level: Entry table columns | red | app green |
| O4 | Show Level: the measured view-block call | red | app red, Qt green |
| O5 | Show Level: stored OutlineLevel against the request | red | |
| **S1** | Sort: file row count against the request | **green: untested** | |
| S2, S3 | Sort: file uid order; order recomputed from the file | red | |
| **S4** | the view-block digest (shared by Sort and Show Level) | **green in the Sort lane** | Show Level red; app Sort, Qt Sort green |
| S5, S6 | Sort: the measured outline; the name-pinned tie | red | app red |
| S7, S8 | Sort: Duration ascending; an ascending sort that moves rows | red | |
| S9 | the shared ascending-reorder rule | red | app red |
| G1 | removal: the target resource's session byte is 0x1C | red | app red, Qt green |
| A1 | app: the Sort builder's shared order rule | red | |
| A2, A3 | app: the unsaved-edits check for Show Level and for Sort | red | Qt red |
| A4, A5 | app: an unsaved name; an unsaved duration | red | |
| **A6** | app: plan row count against the file | **green: untested** | |
| **Q1, Q2** | Qt: the Assign Resources and Calculate Project menu handlers removed | **green: untested** | |
| Q3 | Qt: the `resource_sheet_view` flag never set | red | |

**Untested guards** (recorded as R35bG1-10, Low):
- S1 (the Sort writer's row count);
- S4 in the Sort lane: no Sort test feeds the writer a changed view block, so only the Show
  Level tests hold the shared digest check;
- A6;
- the Qt menu dispatch for Assign Resources and Calculate Project (Q1, Q2).

Q1 and Q2 confirm R35bG1-6: the Qt menu test proves only that the fall-through text is
absent, not that the right handler runs.

## Independent review (`review-g1.md`)

A read-only agent reviewed `e1397d1..e3c86fe`: 0 Critical, **0 High**, 3 Medium, 6 Low.  Each
of the eight cited Highs is closed for the shape the brief names.

- **R35bG1-1 (Medium, confirmed at source):** `viewstate_sort.cpp:501` clears `sorted` and
  then reads `sorted[index]` (line 505) to build the refusal text.  This is undefined
  behaviour, reached by the core and app tests.  The refusal still happens.  A debug or
  hardened standard library would abort.
- **R35bG1-2 (Medium):**
  - The item 1.6 resave guard reads only the target resource's FixedMeta byte 12.
  - Project's resave sets bit 0x40 on every resource row, and the port's own resource add
    writes 0x1C even into a file Project has saved.
  - So a file with an added resource as the target could pass.
  - Whether a real sequence of edits reaches that shape is not traced.
- **R35bG1-3 (Medium):**
  - For items 1.1, 1.3 and 1.6, "both layers" rests on one shared guard: the app refuses
    through the writer's dry run or the shared `sorted_order_of`.
  - The brief asked for an edit-model check of its own for 1.6, and none was added.
  - The 1.1 commit message says `push()` calls the writer, which it does not.
- **Lows:**
  - R35bG1-4: Show Level accepts the second digest for the five-task shape.
  - R35bG1-5: a test asserts only the writer prefix.
  - R35bG1-6: the Qt menu test.
  - R35bG1-7: the backlog row for the Define WBS Code menu item.
  - R35bG1-8: the "save or undo" advice is wrong for a rename.
  - R35bG1-9: no app Sort test reaches the writer's view-state refusals.
- **Clean:**
  - 1.1's view, table, column, digest and sort-spec pins, with the controls still accepted;
  - 1.2's file-row checks in both writers;
  - 1.3 and 1.4 refuse every unmeasured outline and direction, and no measured shape is lost;
  - 1.5's field-by-field comparison with a fresh read of the file;
  - 1.7's unique labels and tag mapping, correct by reading;
  - 1.8's wiring and exhaustive switch;
  - hygiene.

## Merge rule

G1 adds no new High.  Open:
- R35bG0-1 and -2 (Calculate Project, the next fix wave);
- the main.mm compile and CI-4 on macOS CI;
- the 4(e) Project reopen check.

The Mediums R35bG1-1 to -3 and the untested guards go to the next fix wave's queue.
