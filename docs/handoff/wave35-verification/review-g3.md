# Wave 35 G3 review (independent, read-only)

Tree: `D:\dev\msproject-mac-wt-v34g3`, detached at 2468736.  Diff: daf46f8..2468736, commit
2427613 excluded.  Known and not re-reported: the 9fa599b LastAuthor leak (fixed forward in
9564ddd).  Method: source reading, read-only git, and read-only Python (olefile) over the 51
committed G3 fixtures and every other committed `.mpp` in `tests/data` (332 files).  Nothing was
built or run under COM.  Scripts used: the reviewer's local read-only olefile scripts (not committed).

Counts: 0 Critical, 1 High, 6 Medium, 5 Low.

---

## R35G3-1 (High) Removing an assignment from a port-saved file that Project has resaved is accepted by the enabled writer, and G3's own pair shows Project writes it differently

- Where: `core/mppwrite/assignment_growth.cpp:2395` (the only resource FixedMeta flag check
  in `unassign_resource`, row 0), with `tests/roundtrip_assignment_growth.cpp:2470-2476`
  as the proof that the writer accepts the shape.
- What is wrong: W35-3.5 pins a refusal only for the RAW port-saved file (the append-shape
  FixedMeta).  The natural next step for a user is: assign in the port, save, open and save
  in Project, reopen in the port, remove.  That file is the committed null save
  `wave35-g3-assign-remove-port-saved-control-null.mpp`.  It differs from the measured
  removal control `wave30-assign-delete-control.mpp` in one structural way: Project sets bit
  0x40 on two `114/TBkndRsc/FixedMeta` entries when it resaves the port's file (bytes 176
  and 213: 0x1c becomes 0x5c; the raw control, both wave-30 removal files and both wave-30
  add files all hold 0x1c).  Project's removal from this lineage then clears bit 0x20 of
  `114/TBkndRsc/Fixed2Meta` byte 279 (0x2b becomes 0x0b).  In the wave-30 removal pair it
  stays 0x2b, and the port's writer keeps 0x2b.  The writer does not check the resource
  row's FixedMeta flag.  The 3.5 test copies the null's `Rsc FixedMeta` (0x5c) into the
  control and `unassign_resource` accepts it with no error.  Every other check the null
  could trip is structural (row sizes, meta entries, the placeholder identity constant, the
  Props scalars).  The null save matches the hybrid on all of those, so the writer will
  accept the null save itself.  The edit model's file-side half is the same writer dry run
  (`app/edit_model.cpp:3044`), and the plan-side refusal (`assignment_plan_refusal`) cannot
  see the flag.  So this is an enabled save of a shape no pair measured, where the nearest
  measurement (G3's own) shows a byte Project writes differently.
- Refutation attempted: (a) Is the gap only session noise?  No.  Byte 279 is identical in
  control and null (0x2b) and differs only in Project's edit, and the 0x40 bit is a
  persistent file difference, not a renumbered identity.  (b) Does the writer refuse 0x5c
  somewhere?  No.  `rsc_flag` is called only for row 0 (lines 1962 and 2395), and the hybrid
  carrying 0x5c is accepted.  Not executed: one dry-run call on the null save would settle
  acceptance beyond doubt.
- Failure scenario: Assign Resources in the port (enabled), save, open and save in Project
  2021, reopen in the port, Remove Assignment (enabled).  The port writes Rsc Fixed2Meta 279
  = 0x2b where Project writes 0x0b, and possibly the CEdl differences too.  The file probably
  opens, but it is not Project's save, which is a standing rule 1 breach.  The exposure dates
  from W34-4.3 and G3's evidence surfaces it.
- Fix: refuse in `unassign_resource` a target resource FixedMeta flag other than the
  measured 0x1c (needle "resource FixedMeta flag is not the measured 0x1c").  Pin it with a
  test on `kPortNull` in `tests/roundtrip_assignment_growth.cpp` and an `app_edits` test.

## R35G3-2 (Medium) The 3.5 pair test's noise baseline is the un-settled control, so "four named gaps" is a lower bound, and two of the four are artefacts of the transplant

- Where: `tests/roundtrip_assignment_growth.cpp:2470-2492`; record
  `docs/handoff/wave35-complete.md:286-292`; `spec/writers/table-growth.md` (3.5 section).
- What is wrong: `compare_all(control, null_save, ...)` builds its mask from the
  control-versus-null differences.  Here the null save rewrote 19 streams, including 49
  bytes of Assn Fixed2Data, 22 of 114/Props, 49 of Task Fixed2Data, 190 of CEdl and 247 of
  Props14.  Every byte the settle changed is therefore masked, and a port output wrong at
  any of them still passes.  Also, the "identity counter: Project's is 10 higher" gap
  (Assn Fixed2Data at 196, 0xd1 against 0xdb) is the session identity suffix.  Project
  renumbers it on every save, as this same file documents at lines 390-396.  The gap
  appears only because the transplant left the control's (wave-30-session) Assn Fixed2Data
  in place.  The null's records already carry Project's suffix (db a8 .. 68).  The Assn
  Var2Data gap at 524 is very likely the same thing.  So "the identity-counter rule" in the
  record's list of what enabling needs is not a rule to derive.  The real open items are
  the Rsc flag (R35G3-1) and CEdl.
- Failure scenario: a later wave "closes" the four named gaps and treats the pair as
  matching, while settle bytes under the wide mask are still wrong.  Or it spends a wave
  decoding an "identity-counter rule" that does not exist.
- Fix: compare the port's output of the null save (or the hybrid) against Project's edit,
  with the null save as the baseline (or state the mask width), and reword the gap comments
  and the record.

## R35G3-3 (Medium) The noise mask widens the ENABLED Bold-on writer, and neither the widening nor the mask's width is tested

- Where: `core/mppwrite/viewstream_edit.cpp:391-397, 528-531` (`masked_block_digest`,
  shared by `save_view_cell_style`'s Bold-on path and `unbold_cell`).
- What is wrong: Bold-on is enabled in both shells (`app/edit_model.cpp:3141` dry run,
  `:6246` save).  Before G3 its digest accepted only the wave-30 session's block: 70
  committed blocks match the old 0xc4a86de4acee2cfb, all with byte 42678 = 0x18.  With the
  mask it accepts the same block from any session: 249 blocks at 42,692 bytes, six values.
  No Bold-on pair (uid 1, Name) was measured on a second-session control.  The only
  second-session Bold-on pairs are row 2, Start and range, which are deferred.  The mask was
  needed for the Bold-off control and was applied to both directions.  No test shows Bold-on
  accepting a second-session control, and no test flips 42677 or 42679 to prove the mask is
  one byte wide.  Tests flip only payload byte 20000
  (`tests/roundtrip_viewstream.cpp:485`, `:624`; `app/app_edits_cell_bold_tests.cpp:419`;
  the Qt gate).  The G2 Sort writer (`core/mppwrite/viewstate_sort.cpp:25, 342-357`) keeps
  its own unmasked digest, so it still refuses other sessions.  That is conservative but
  inconsistent.
- Why not High: Project rewrites this byte on every save (the wave-30 file resaved in wave
  35 reads 0x26), so it is not user content, and the port keeps the input's value.
- Failure scenario: a mutation that masks a 4-byte or 16-byte window around 42678 passes
  every test.  Bold-on on a 0x26-session file is enabled without a matching pair.
- Fix: pass the mask only from `unbold_cell` (or measure Bold-on uid 1 Name in a second
  session), and add refusal tests that flip payload 42677 and 42679.

## R35G3-4 (Medium) unbold_cell guards that no test reaches

- Where: `core/mppwrite/viewstream_edit.cpp:684-688` (not the last item), `:676-681` with
  two items, and `:691-700` for the font word.  Tests are at
  `tests/roundtrip_viewstream.cpp:464-500`.
- What is wrong: the tests cover no Bold item, flags 0x02000003, another row uid, a changed
  record byte and whole-row selection.  Untested:
  - "is not the last item of the view block": no test, no needle;
  - two cell-format items: same guard as zero items, but no two-item case;
  - the font word.  The realistic other-session Bold item has top byte 0x6b or 0x00
    (`wave35-g3-font-bold-row2.mpp` and `-start.mpp` store 0x6b000001 and 0x00000001), and
    a mutation that compares only the first 52 bytes of the item survives every test;
  - an already-unbolded item (flags 0x00000003, `wave35-g3-font-bold-off.mpp` as input) and
    Italic plus Bold (0x03000003).
  
  All are refused today (the whole-item `std::equal`, the count check), so this is a test
  gap, not a hole.
- Failure scenario: a refactor narrows the item comparison or drops the last-item check, and
  Bold off then rewrites a Bold item from another session or position with nothing going
  red.
- Fix: add four `refuses(...)` cases: item not last (needle "is not the last item"), two
  items, font word 0x6b000001 and the committed Bold-off output as input.

## R35G3-5 (Medium) The record treats the font-word top byte inconsistently between the enabled Bold-on and the deferred shapes

- Where: `docs/handoff/wave35-complete.md:271-278`; `spec/writers/view-stream-214.md:162-169`;
  `core/mppwrite/viewstream_edit.h:147` (`kMeasuredFontWord = 0x90000001`).
- What is wrong: verified on the fixtures.  The four Bold saves store top bytes 0x90
  (wave 30), 0x6b (row 2), 0x00 (range) and 0x00 (Start).  The record defers every new
  append because "no append can match byte for byte" and a KnownGap "must not enable".
  The enabled Bold-on is an append with the same byte: it writes 0x90 always and matched
  only because the wave-30 session held 0x90.  The record says so in one closing sentence,
  "the spec records it", but raises no finding or coordinator decision.  On standing rule 1
  my reading is this: the enabled shape is measured and its pair still matches with zero
  gaps, and Project reads 0x90 back (it resaved `wave30-textstyle.mpp` as the Bold-off
  control).  So this is not a High against the enabled save.  But either the rule applied to
  row 2 and Start is too strict (they could pin 0x90 as Bold-on does), or the enabled save
  needs a decision.  The record should not hold both positions.  The spec's "the top byte is
  kept from the control" for Bold off also overstates it: the writer accepts only 0x90.
- Failure scenario: the next wave re-measures Bold-on in a new session, the pair fails at
  that byte, and the record has given no rule for what follows.
- Fix: add a finding stating the rule for a byte Project leaves uninitialised: pin the port's
  value and verify that Project reopens it.  Apply it to both the enabled and the deferred
  shapes, and correct "kept from the control" to "only 0x90 accepted".

## R35G3-6 (Medium) Removal from a port-saved file: the edit-model layer has only a generic needle

- Where: `app_qt/tests/assign_resources_gate_test.cpp:300-303` (needle "Removing an
  assignment is not implemented in this port for this project").  There is no `app/`
  test, and `app/app_edits_assignment_tests.cpp` has no case naming the settled-shape
  refusal.
- What is wrong: brief section 1 rule 4 requires a specific needle in both layers.  The
  writer layer is pinned precisely (`tests/roundtrip_assignment_growth.cpp:2457-2460`,
  "FixedMeta is not the measured (rows - 1) / rows / flag 4 settled shape").  The edit-model
  layer's only test checks a prefix that every writer refusal of this command produces.
  The gap predates G3, but 3.5's "the refusal is pinned" holds only for the writer.
- Failure scenario: the edit model's dry run is bypassed or replaced by a different
  refusal, and the Qt gate stays green.
- Fix: in `app/app_edits_assignment_tests.cpp`, build the removal edit on
  `wave35-g3-assign-remove-port-saved-control.mpp` and expect "settled shape".

## R35G3-7 (Medium) Toolbar Underline and the range deferral rest on a harness error, not on Project's

- Where: `spec/writers/view-stream-214.md:181-187`; `docs/handoff/wave35-complete.md:276-277`;
  `build/w35/g3-run5.log`.
- What is wrong: "'bool' object is not callable" is a Python TypeError.  win32com's late
  binding read `FontUnderline` as a property and then called the bool.  The record calls it
  something that happened "in this COM session".  Project never refused the call, so the
  attempt is evidence of the script's limits only.  Likewise the range pair "stored ONE
  item, for row 2: ... or did not select both rows", which admits the selection may never
  have spanned two rows.  Nothing is enabled, so nothing is exposed, but under brief rule 1
  a deferral needs a real attempt, and these two shapes have not had one.
- Fix: call `FontUnderline` through `_oleobj_.InvokeTypes` or an early-bound dispatch, and
  check `ActiveSelection.Tasks.Count == 2` before the range Bold; reword both items as
  "not yet measured".

## R35G3-8 (Low) Commit trailer missing on six commits

- 9b33d6d, 2634bb0, a46d9f9, ee4826f, 639638c and 2468736 (all of W35-3.5 and the G3 marker)
  lack `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35)` (brief section 1 rule 2).
  The other ten G3 commits carry it.
- Fix: note it in the coordinator record (no rewrite), and have the builder's commit
  template carry the trailer.

## R35G3-9 (Low) tests/data/README.md has no SHA-256 for any of the 51 G3 fixtures, and the 3.5 row describes the wrong provenance

- `tests/data/README.md:538-554`: the G3 rows are descriptions only.  None of the 51 hashes
  appears in the README, though all 51 are correct in `scripts/check_fixtures.py` (verified
  by re-hashing).  Brief rule 6 asks for both.  The 3.5 row says "the SAME Project 2021
  session's save before and after the edit ... The seed is saved and REOPENED before the
  control is saved".  In fact the control is the port's file copied byte for byte (it
  holds `C:\w30\fixtures-g2\wave30-assign-add-control.mpp` as its save path), with no
  seed.  The row is also missing a full stop after "deletes that assignment".
- Fix: add the 51 hash rows and rewrite the 3.5 provenance.

## R35G3-10 (Low) The noise byte is not per-save noise

- `core/mppwrite/viewstream_edit.cpp:391`; `spec/writers/view-stream-214.md:194-200`.
  Byte 42678 is the third byte of the float32 at payload 42676 inside item 0x22400015
  (0x3d1512c0 .. 0x3d2612c0).  It is constant within a session and rises with the session's
  calendar date: 0x15 in the wave-23/24 files, 0x16 in wave 25, 0x17 in wave 28, 0x18 in
  wave 30, 0x19 in two wave-31 files, 0x1c in wave 34, 0x26 in wave 35.  Project rewrites it
  on every save.  So it is a date-derived view value.  When it carries into byte 42679,
  every new-session control will be refused again.  The "258 saves" count is correct at
  fe27cba: I count 249 plain plus 12 with an item, 261 at the tip, including the three later
  3.5 files.
- Fix: rename it a session-date value in the comment and the spec, and record the expected
  carry.

## R35G3-11 (Low) Project opens the fixture copies from inside the repo, not from the neutral root

- `scripts/oracle_w35_g3.py:42` (`WORK = <repo>/build/w35/g3-work`).  Every control, edit
  and null source is opened from a copy there, and only the SaveAs targets are under
  `C:/w35`.  A scan of all 51 G3 fixtures (UTF-8 and UTF-16) finds only `C:\w35\...` and
  `C:\w30\...` paths and no names, so nothing leaked.  But the brief makes `C:\w35\` the
  root for the whole COM session, and a FileSave in place (rather than SaveAs) would record
  a repo path.
- Fix: set `WORK = os.path.join(NEUTRAL, "work-g3")`.

## R35G3-12 (Low) The CEdl KnownGap signature is weak

- `tests/roundtrip_assignment_growth.cpp:2488` `{"214/CEdl/Var2Data", 64, 430, 0x00, 0x00}`:
  port byte and Project byte are both 0x00 at the first mismatch, so the signature
  identifies no differing value, only a count and an offset.  At 430 the streams hold the
  UTF-16 save path in the null and the edit, so part of the 64 may be path-record text, not
  the displayed-row list the comment names.
- Fix: record the first byte that actually differs, or split the path record out with
  `normalise_cedl`, and correct the comment.

---

## Checked and clean

- **unbold_cell is unreachable from the shells.**  `app/edit_model.cpp:3106-3115` builds
  only `enabled = true`.  Nothing in `app_qt/` (or any `.mm`) names `ViewCellStyleEdit` or
  `save_view_cell_style`.  A Bold click on a bolded cell takes the Bold-on path and is
  refused ("already carries a cell format item").
- **unbold_cell's refusals are real.**  The whole 56-byte item must equal
  `build_item(edit)`, which pins uid 1, field 0x0B40000E, flags 0x01000003, the colour
  words, pattern 1 and font word 0x90000001.  So other rows, other fields, Italic, Italic
  plus Bold, an already-off item and any top byte but 0x90 are all refused.  It requires
  exactly one cell item, and it must be last.  `validate_edit` still refuses other
  views, uids, fields, attributes and whole-row selection before dispatch.  The skip-digest
  check is sound.  `payload - 56` must equal 42,692 and the item must start at 42,692, so
  the digested bytes are exactly the control layout.  The skip digest matches for every
  one of the 12 committed blocks that carry an item.
- **The "eight bytes" and "VarMeta/Props unchanged" proofs are real.**  The post-write loop
  compares every Var2Data byte outside the two words against the opened file and then
  checks both words' values.  VarMeta and Props are compared with the pre-edit state.
  `prove_only_owned_streams_changed` runs.  The pair test asserts exactly two changed
  bytes and compares all streams with zero KnownGaps.
- **The pinned digest 0xd2f37d751e6e8333 reproduces** independently: 249 of the 318
  committed 42,692-byte uid-2 blocks match it.  The non-matching ones differ at named
  offsets (63-64, 33, 59-62, 42659 and others), so the mask is one byte.
- **The other deferral evidence checks out against the fixtures.**
  - Baseline: `0x0b4000ae` +16 is `0x7ff6713e0001` in `wave30-baseline-set.mpp` and
    `0x7ff710440001` in `wave35-g3-baseline-set-nested.mpp` on every row, with the address
    minus the low bit at +36.
  - Notes: `wave35-g3-notes-generator.mpp` adds `Riched20 16.0.20527` beside
    `16.0.20430` (and an older `16.0.20026`), and the control has no 20527.
  - Font top bytes: as the record says.
  - Renumber: the error is at `build/w35/g3-run7.log:3`, and the defined-file edit changes
    no stream the null save does not.
  - Insert: the pairs change 22 to 25 streams, with 8 to 11 beyond the null's 14.  This
    matches "about 22".
- **The oracle neutralises before saving.**  The `file:` branch now calls `neutralise()`
  before the first save (`scripts/oracle_w35_g3.py:309`).  The built branch neutralises in
  `build()`.  The null saves and the port-saved pair set `UserName` on files that are
  already neutral, which is the documented null-save exception in
  `scripts/oracle_privacy.py`.  No path saves before one of these runs.
- **Fixture coverage.**  `scripts/check_fixtures.py` names all 51 G3 fixtures with correct
  SHA-256 values, and every new pair has its null save.
- **Test harness.**  `tests/save_pair_noise.h` and `tests/save_pair_compare.h` are unchanged
  in the diff, so no other test's mask widened.  The 3.5 KnownGaps are test-only and feed
  no enabling path, and `tail_normalisation` predates G3.
- **Hygiene.**  The diff (code, docs, specs, tests, scripts) and all G3 commit messages hold
  no people's names, client or company names, or personal or company paths.  Fixtures hold
  only neutral `C:\w35`/`C:\w30` paths.  Each commit is one item id.  The new code has no
  unused constants and no nested `/*`.

## Summary

G3 built one writer, `unbold_cell`.  It is tight, unwired, and its in-place proof is real,
but four of its guards are untested.  The other items are deferred, and the evidence for
Baseline, Notes, the font top byte and Renumber reproduces from the committed fixtures.  The
Toolbar Underline and range deferrals rest on a harness error, not on Project's.  The most
serious finding comes from G3's own 3.5 pair.  A port-saved file that Project has resaved
(the committed null save) is still accepted by the enabled removal writer.  Project's removal
from that lineage clears a `Rsc Fixed2Meta` flag that the port keeps.  The writer never checks
the resource-row FixedMeta flag (0x5c against the measured 0x1c) that distinguishes the
lineage.  That is a standing rule 1 High.  It dates from wave 34 but is surfaced here, and one
dry-run call on the null save would confirm acceptance.  The noise mask is backed by six
sessions' fixtures and is one byte wide.  But it was applied to the enabled Bold-on as well as
Bold off, and the value is a session-date float, not noise.  The record treats the font top
byte one way for the enabled save and another for the deferred ones.  The 3.5 test's "four
gaps" is a lower bound, and its "identity counter" gap is the known per-save identity
renumbering.  Counts: Critical 0, High 1, Medium 6, Low 5.
