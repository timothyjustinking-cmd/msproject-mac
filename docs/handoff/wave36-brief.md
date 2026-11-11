# Wave 36 brief: the writer wave, putting the Project 2021 trace's rules into the writers (for the Sonnet builder)

_Written Wed 11 Nov 2026 by the Opus coordinator, from Fable's audit and work order of 11 November 2026, section 4, and the operator's word "Yes, after round 6"._

**Baseline.**  Branch `wave36`, cut from `main` at `b2c0b19`: the wave 35g merge, with CI green on all seven jobs and no Critical or High open.  Wave 35g left no Critical or High, so nothing goes ahead of G0.  `main` holds waves 34 to 35g and the trace evidence: `spec/writers/evidence/trace-*.md` (rounds 1 to 6) and `tools/trace/`.

**The operator's goal is parity.**  This wave lifts writer refusals where the trace explains the bytes.  Every lift is held to the three criteria below, and `docs/handoff/standing-rules.md` binds.

## 0. The lift rule

A refusal is lifted only when all three hold:
- **(a)** the trace names the writing routine and the source of every byte the writer emits;
- **(b)** the rule predicts, byte for byte outside field-level masks, at least one committed pair that was NOT used to derive it;
- **(c)** the port's save of that edit matches the fixture field by field (standing rule 1), and 4(e) reads the edit back through Project.

Where (b) has no held-out pair, the writer stays limited to its measured shapes, and the record says so.

**An explanation is a hypothesis.**  The trace notes say what Project did in the recorded runs.  The held-out pair is the test of a general rule.  Don't generalise a writer because a note "explains" it.

**Port tests mask by exact field** (record id and offset), never by stream name.

## 1. Rules for this wave

The wave 35g brief's section 1 rules apply unchanged, with `36` for `35g`:
- the marker `WAVE-36-<group>-COMPLETE` (CLOSE: `WAVE-36-COMPLETE`) in `docs/handoff/wave36-complete.md`, with each group's gate line, "Guards seen red", "Deferred" and "Found, not fixed";
- one finding or item id per commit;
- gates per commit under `build/w36/`;
- red then green, both quoted;
- specific needles;
- the trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 36)` on EVERY commit;
- no names, and the three scans before every commit;
- LF line endings;
- no COM: the coordinator runs the oracle and 4(e);
- the builder writes only `gates-gN.md` in `docs/handoff/wave36-verification/`;
- "fixed" means fully closed.

Also:
- **A deferral needs evidence of an attempt:** the command run and the failure seen.
- **Machine limits:** the trace VM may be running.  Never start, stop or touch the VM, `vmconnect`, or anything under `build/trace-2021/`.  Keep builds to the gates the rules require.  An out-of-memory compiler error (C1060, C1076, C3859, heap space) is rerun once and logged; it is not a code fault.
- **A group whose preconditions don't hold is not started.**  The driver records BLOCKED with the missing fact.

## 2. Groups

### G0: the gate and the records

Precondition: the trace agents no longer read the repo's `tools/trace`.  They work from a frozen, hash-listed copy in `build/trace-2021/tools-frozen/`, which the coordinator confirmed before launch, so G0 may edit `tools/trace` freely.  Never touch `build/trace-2021/` or the frozen copy.

- **W36-0.1 (A-2): the noise allow-list moves into `tools/trace/trace_gate.py`.**
  - It becomes per-stream byte ceilings and exact offsets, taken from the null run.  Gone are the by-name whole-stream waivers that `build/trace-2021/gs6.ps1` applied.
  - The four known noise sources each get a narrow mask:
    - the `UuidCreateSequential` GUID counter bytes;
    - the creation date-time stamps (Assn Var2Data, Task FixedData);
    - the uninitialised top byte of the font word (payload +43);
    - the per-file Props identity records.

    Their offsets and record ids are in `trace-insert-milestone-r4.md`, `trace-bold-r4.md`, `trace-font-item-r6.md` and `trace-props-framing.md`.
  - Exit 0 means PASS.  `tools/trace/capture/runone6.ps1` records the exit code.
- **W36-0.2 (A-4): no wrong-length mask.**
  - The byte mask is refused when the reference and control lengths differ.
  - Fixed-stride streams (Task FixedData 202-byte rows, Task Fixed2Data 64, Assn Fixed2Data 48, Assn FixedData 110) are masked per record.
- **W36-0.3 (A-3): an absent control-null is a FAIL** named "no null", never a pass.
- **W36-0.4 (A-6): a ctest that flips one byte in each stream class of a committed pair and asserts FAIL.**  It also asserts `checked > 0` and an equal stream set.
- **W36-0.5 (A-11): tighten the skip script and the KVP host.**
  - `tools/trace/capture/r5b-skip.ps1` invokes "Skip for now" only in a dialog whose title starts "Sign in".
  - `tools/trace/kvp/host.ps1` throws on a hash MISMATCH and checks the reply length.
- **W36-0.6 (A-9): records.**
  - `docs/START-HERE.md` is rewritten under 8 KB: state, read order, commands and house rules.
  - The wave history moves to `docs/handoff/history-waves34-35.md`.
  - `memory/coordinator.md` is brought current.
- Any Critical or High from wave 35g's reviews goes ahead of these.  The coordinator adds them here before launch.

Done when every guard is seen red then green, both quoted.  Marker `WAVE-36-G0-COMPLETE`.

**After G0, the coordinator:** re-gates all 109 recordings, plus the round 7 captures, with the committed gate inside the VM, and commits the gate texts.  Every FAIL becomes a finding with an id before G1's lifts are merged.

### G1: the font item (the pilot)

Precondition: `trace-font-item-r6.md` is committed and answers "which bits, which instruction".  It is: `ad388e9`.

Writer: `core/mppwrite/viewstream_edit.{h,cpp}`.  Today it allows Bold set on uid 2's Name cell, and Bold off of that one measured item.  The facts from the trace (`trace-font-item-r6.md`, `trace-bold-r4.md`):
- **Calls and placement:** one set-by-id call per edit (id 0x2240000c, kind 1), 44 bytes per selected row, in the view's property bag.  A new item is appended at the bag's end, with the same framing as Props records.
- **Payload layout:**
  - +0 the row number;
  - +4 the field key;
  - +8 the word 3, or 6 for size and name;
  - +10 the attribute mask: 0x100 Bold, 0x200 Italic, 0x400 Underline;
  - +12 and +24 the colour triples;
  - +36 one byte, toolbar path only;
  - +40 the font word.
- **The font word:** bit 0x01 Bold, 0x04 Italic, 0x02 Underline, 0x10 face or size differs, 0x40 the colour group.  The top byte is never written, so it is masked at field level as uninitialised memory.
- **Font size and font name are not stored in the item.**  Their storage is not located.
- **Bold off** rewrites the existing payload in place: mask 0, flags 0x00000003, font word bit 0 clear.

Items:
- **W36-1.1:** wire Bold off.  The writer is built; the pair is `wave35-g3-font-bold-off`.
- **W36-1.2:** Bold on any task row, and on the Start column.  Pairs: `wave35-g3-font-bold-row2`, `wave35-g3-font-bold-start`.  The held-out check (b) is the pair not used to derive the row and field rule.
- **W36-1.3:** Italic (`wave35-g3-font-italic`).
- **W36-1.4: 4(e) for the font item must stop being vacuous.**  Find a COM read-back of cell font state for the coordinator's 4(e) script (`Font.Bold` and `Font.Italic` of the selected cell after `SelectTaskField`), or record the attempt and the method that failed.  The builder writes the read-back plan into the group record; the coordinator runs it.
- **Underline:** in this group only if (b) holds for it (pair `wave35-g3-font-underline`, toolbar path).
- **Size, name and colour:** not started.  The record says "storage of font size and name not located (trace-font-item-r6.md)".  Colour waits on the same note's open bits 0x40 and 0x80.
- **Range, combined attributes and a second change to a cell:** in this group only if capture round 7 item 2 captured them and (b) holds.  Otherwise not started, with the missing pair named.

Note that `wave35-g3-font-bold-range` is a one-row bold, mislabelled.  The trace's two-row selection gave one 88-byte item.  Keep the fixture file, and correct its label in `spec/writers/view-stream-214.md` and the evidence note (decision D-2).  Never rename or edit the fixture.

### G2: Props

Writer: `core/mppwrite/props_edit.{h,cpp}`, and the plan layer.  The facts from the trace (`trace-props-framing-r4.md`, `trace-baseline-r4.md`, `trace-calculate-r3.md`):
- **New-record framing:** length (4), id (4), kind word (2), flags word (2), then the payload, appended at the end of the blob.  Blob +0 (the size) += 12 + length, plus 1 pad byte if the length is odd.  Blob +8 (the record count) += 1.  The stream's two leading size words follow the blob size.
- **Per-table typed properties:**
  - id `0x0001000N` holds table N's final Var2Data size, `[table+0x344]`;
  - id `0x0100000N` holds table N's entry count, `[table+0x250]`;
  - N = 1 Task, 2 Rsc, 3 Cal, 4 Assn.

  Both are rewritten at save, after the var blocks are laid out (the publish at 0x397993 / 0x3978d9).  The view tables use the same rule, e.g. 214/Props.
- **Existing records** are rewritten in place by id.

Items:
- **W36-2.1:** the per-table size and count words are computed from the tables the save lays out.  The measured-constant copies go.
- **W36-2.2:** new-record framing in the Props writer.
- **W36-2.3:** the plan gains a Props digest, which closes R35cG0-4.  Held-out check: the Props stream of every growth pair the rule was not derived from.  The baseline pair derived it, so check at least the insert, assign-add and resource-add pairs.

### G3: Insert Task, Insert Milestone, Insert Summary

Precondition: `trace-insert-rules-r6.md` states a rule that predicts both held-out pairs.

- As committed (`ad388e9`), the note predicts the new row's FixedMeta entry byte-exact for the four wave35-g3 insert pairs.  But it derived the rule from those same four, so none is held out.
- It leaves open: the Props14 and CEdl rewrites, the source of the 3bf13138 start word, and +142 and +172 on summary rows.

**So the precondition is NOT met today.**  The coordinator launches this wave with the groups G0, G1, G2 and CLOSE only.  The CLOSE record lists G3 as "not started: no held-out insert pair (trace-insert-rules-r6.md)".  The coordinator re-checks the precondition against `spec/writers/insert.md`'s held-out pairs, and any round 7 one-factor pairs, before CLOSE.

If it is met, the two GUIDs and the creation stamp are masked by field.

### CLOSE

- Rerun every gate on a fresh tree.
- Write the completion record with each id ticked: fixed (commit), partly fixed (what remains), deferred (evidence) or not started (the precondition that failed).
- Update START-HERE (under 8 KB), `memory/coordinator.md` and the plan.
- Mark `WAVE-36-COMPLETE`.  Push nothing.

## 3. Not in wave 36

These are wave 37, in this order:
- Notes: waits on the 6-byte Task Var2Data run (round 6 item 7).
- Set Baseline: 26 undecoded var blocks.
- Linked, lag and constraint Calculate: the scheduling arithmetic upstream of the four value stores is not unrolled.

## 4. The coordinator's part

After each group marker:
- fresh-tree gates in the verify worktree;
- mutations at each new guard, at the call site and in the helper;
- an independent read-only reviewer on every group (each one changes a writer).

After CLOSE:
- range scans, push, and CI on all seven jobs;
- a whole-wave review;
- 4(e) with checked and not-checked counts;
- fast-forward `main`, then ask the operator for the Fable audit.

**Merge `wave36` into `main` only when:**
- CI is green on all seven jobs;
- no Critical or High is open;
- 4(e) passes, with its checked and not-checked counts stated;
- the 109 recordings plus round 7 are re-gated by the committed gate, with every gate text committed;
- START-HERE is under 8 KB and true at the merge commit.
