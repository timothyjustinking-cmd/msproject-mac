/*
 * actuals.h -- Project's stored actuals for a %-complete edit (wave 28, W28-1.2).
 *
 * MEASURED SOURCE.  tests/data/wave28-percent-*.mpp are four Project-2021 save
 * pairs (control + the file Project saved after one PercentComplete edit):
 * auto 0->40, auto 40->100, auto 100->0 and manual 0->40.  The row and stream
 * changes below are read from those pairs; build/w28/W28-1.1-pair-diff.log and
 * build/w28/actuals-words.log (not committed) hold the full byte diff and the
 * per-word dump.
 *
 * WHAT A PERCENT EDIT WRITES, per edited leaf row (wave-28 rules, extended by
 * the three wave-29 shape pairs):
 *   FixedData +80  ActualDuration     = Duration * percent / 100
 *             +88  RemainingDuration  = Duration - ActualDuration
 *             +92  PercentComplete    = percent
 *             +94  PercentWorkComplete= percent when the row has a real
 *                                       (work-bearing) assignment; otherwise
 *                                       100 at 100 %, else 0 (every wave-28 row
 *                                       carries Project's UNASSIGNED
 *                                       placeholder assignment -- non-zero +20
 *                                       planned work, the 0xFFFF0001 null-
 *                                       resource sentinel -- and Project's own
 *                                       save stores 0 below 100 %)
 *             +8/+16/+24              Work / Actual Work / Remaining Work of
 *                                       the real assignment (thousandths of a
 *                                       minute, f64); untouched on a placeholder
 *             +120 ActualStart        = the stored +120 when the row has one,
 *                                       else the manual row's Start2 (+104), else
 *                                       the automatic row's Start (+96)
 *             +124 ActualFinish       = the measured finish at 100 %, else NA
 *             +104/+108 Start2/Finish2= the actual dates at 100 % (the milestone
 *                                       pair; below 100 % they are untouched)
 *             +132 Resume / +136 Stop = the caller's working-time values,
 *                                       NA at 0 %
 *             +168 progress flags      = existing word | 0x9000 when percent > 0
 *                                       (100->0 keeps the bits: measured)
 *             +174 start anchor       = ActualStart when the row has progress and
 *                                       is manual or WAS a leaf; untouched
 *                                       otherwise (the milestone keeps NA)
 *   The summary rows above the progressed leaf then take Project's roll-up
 *   (W29-0.2, the leaf pair): +80 = the summary's own Duration * sum(leaf
 *   ActualDuration) / sum(leaf Duration), +88 = own Duration - that, +92 =
 *   rounded 100 * sum(leaf ActualDuration) / sum(leaf Duration), +120 = the
 *   summary's own Start2, +132/+136 = the earliest valid leaf resume/stop.  A
 *   summary with no progressed leaf descendant is left byte for byte, as are
 *   every summary's flag and anchor words.  The project row takes +80/+88/+92
 *   over the non-summary rows, +94 = round(100 * sum(real actual work) /
 *   sum(real planned work)), +8/+16/+24 = those two real-assignment sums (0 on
 *   a placeholder-only file), and +120/+132/+136 as measured (the project start;
 *   Resume/Stop are the earliest WRITTEN summary date, else Update Project's
 *   entire-project earliest row date -- Mark on Track and Update Project's
 *   Selected-tasks scope keep the project start there, W29-0.3).  The edited
 *   row's VarMeta/Var2Data fields:
 *   0x0B4004E3 and 0x0B4004E5 are removed, 0x0B400505/0x0B400506 are removed
 *   when a 0 % row starts carrying progress, a 0x0B4004E4 payload shrinks 12 -> 8
 *   bytes with 4 freed bytes on a zero-duration 100 % edit (the milestone pair),
 *   and at percent > 0 a 0x0B40015E step block is
 *   appended; at 0 % a constant 0x0B4004E5 block is appended when the row has
 *   none.  A block built from scratch starts with Project's marker step
 *   (flag 1, percent 32768.0, a fresh opaque id) before the new middle step and
 *   the constant final step; a 40 -> 100 save keeps the marker and the old
 *   middles and drops only the old final.  W29-0.3: RAISING and LOWERING a
 *   recorded percent differ.  60 -> 80 appends middle 80 and final 80 (the
 *   old middle 60 stays); 60 -> 40 keeps the old middle 60 byte for byte and
 *   replaces ONLY the final step with final 40, so the block stays three
 *   steps.  The rule is Project's own save of both directions on a 60 % row.
 *   The FixedMeta entry that covers the
 *   row has its variable-field count adjusted by the NET number of VarMeta
 *   entries added and removed (measured: 9 -> 9 for the automatic pair,
 *   11 -> 9 for the manual pair, where three entries go and one arrives).
 *
 * VarMeta HAS A LOGICAL LENGTH.  Header +8 is the number of entries a reader
 * may use; the physical stream may be longer and then holds stale entries after
 * that count (measured: the automatic controls are 528 bytes with header 40 and
 * 42 physical entries, and the manual pair goes 528/header 42/physical 42 ->
 * 528/header 40/physical 42, leaving its original slots [40],[41] byte for
 * byte).  The writer edits only the logical entries and copies the resulting
 * array back into the first slots of the original stream, so the stream size
 * and every byte past the new logical count are preserved.
 *
 * ALSO WRITTEN (wave 29, W29-0.1), because the four wave-28 percent pairs and
 * the three mark/update pairs change these fields on every save; the save-pair
 * tests now compare every one of them with PROJECT's file:
 *   Task FixedMeta entry +44    0x08 once the row has progress, 0x00 at 0 %.
 *   Task FixedMeta +8..23 blob  entry+9 bit 0x40 (percent < 100 and the elapsed
 *                               work equals Duration*percent/100), entry+17 bit
 *                               0x04 (automatic rows only: 0 < percent < 100;
 *                               manual rows keep their byte), entry+18 bit 0x20
 *                               (a percent edit sets it when either percent is
 *                               100; Mark on Track / Update Project clear it).
 *   Task Fixed2Meta entry +88   clears 0x0C once the row has progress; a 0 %
 *                               write leaves it alone.
 *                        +95    0x10 at 0 %, 0x00 above.
 *   Task Fixed2Data row+3 +42   0x30 at 0 %, 0x01 above.
 *                         +46   the project start at 0 %, NA at 100 %,
 *                               ActualStart for an automatic row below 100 %,
 *                               unchanged for a manual row below 100 %.
 *   TBkndAssn FixedData +28     planned work * the row's actual ticks / Duration
 *                               (0 for a zero-duration row, the milestone);
 *                        +44   planned - that; +60 Resume; +104 Stop copy (the
 *                               task Start at 0 %).
 *   TBkndAssn FixedMeta +2      net 0F400032 entry change; +10 0x08 at progress
 *                               and 0x10 at 100 %; +14 0x04 at progress.
 *   TBkndAssn Fixed2Meta +9     clears bit 0x01 once the row has progress.
 *   TBkndAssn 0F400031          rewritten in place or APPENDED: at 100 % with a
 *                               percent edit a 44-byte zero header (word +32 = 1)
 *                               replaces the records; at 100 % from a date update
 *                               the records stay byte for byte and +32 = 0;
 *                               below 100 % a real (work-bearing) assignment
 *                               rebuilds ceil(Duration / 4800) records (the work
 *                               pair grows 72 -> 100 bytes and is appended and
 *                               repointed), a placeholder keeps the control's
 *                               record count (2 in the wave-28 and mid controls);
 *                               a zero-duration row leaves the block byte for
 *                               byte (the milestone's 72-byte block).
 *   TBkndAssn 0F400032          removed at 0 %; appended (56 bytes) when none
 *                               existed; replaced (append + remove, 56 bytes, or
 *                               76 bytes when a recorded percent is recomputed,
 *                               where the second record carries the OLD actual
 *                               work); a zero-duration row stores a 56-byte
 *                               all-zero block with the units words 0 (the
 *                               milestone).  W29-0.3: on a date update the
 *                               second record is (old actual, 0, NEW ticks)
 *                               when the percent RAISES (60 -> 80) and the old
 *                               record byte for byte when it LOWERS (60 -> 40).
 *   TBkndAssn VarMeta           9 -> 10 entries on a first 0F400032 append,
 *                               10 -> 9 on its removal; the physical tail keeps
 *                               Project's stale slot, as in the Task table.
 *   114/Props 0x01000002        += the payload bytes of the removed Task blocks
 *                               (a REPLACED 0F40015E block is not counted),
 *              0x01000100      := the new Task Var2Data size,
 *              0x04000002      += the payload bytes of a removed 0F400032,
 *              0x04000100      := the new Assn Var2Data size.
 *
 * The save-pair tests compare every stream with Project's file (W29-0.1).  The
 * bytes this writer still does not reproduce are named gaps, not assertions of
 * the control: the random identity GUIDs Project reallocates in Fixed2Data (the
 * assignment's real-resource +32 word included) and the opaque 0x0B40015E step
 * ids.  W29-0.2 measured three more shapes -- a leaf under a summary 0 -> 40
 * (with the summary and project roll-ups), a work-bearing assignment 0 -> 40
 * (task and assignment actual work) and a zero-duration milestone 0 -> 100 --
 * and the app accepts exactly those transitions on automatic rows.  A
 * mid-to-mid edit (25 -> 60) is measured as wave29-percent-mid60.mpp but stays
 * refused: Project reschedules the whole chain on that save (durations, dates,
 * anchors, slack) and this writer has no scheduler, so the pair's moved fields
 * are the save-pair test's named expected failures.  W29-0.3 measured a date
 * update on a row that already records progress: Mark on Track and Update
 * Project both RECOMPUTE the percent through the update date, raising 60 -> 80
 * or lowering 60 -> 40, and the app accepts exactly that top-level automatic
 * 60 % shape (a 0 % or 40 % row, a manual row, an equal percent, or a date
 * that would erase/complete the row still has no pair and stays refused).  A
 * combination of two shapes (a milestone under a summary, a resource-loaded
 * leaf, a milestone with resource work) has no pair, so the app refuses it.
 *
 * CORRECTION (wave 29, W29-0.0): the wave-28 save-pair test did NOT compare
 * those streams with Project's file -- it asserted them equal to the CONTROL,
 * so Project's own changes passed silently (the wave-28 review's R28-1), and NO
 * Project COM reopen of the port's actuals save existed (the W28-0.4 reopen
 * artifact reopened Project's own process, not this writer's file).  W29-0.1
 * rewrites those comparisons and ran Project's reopen of the port's actuals,
 * outline and new-task-mode saves (scripts/oracle_w29_reopen.py; facts in
 * build/w29/W29-0.1-project-reopen.json, sha256
 * cc68d5957124b86611477f954270e57945820d3b45515ee4ba9afd69b00c8d1f).  Project
 * 16.0 reopened 10 port saves against Project's own save of the same edit and
 * displayed 0 differing fields on every row (project summary and IDs 1..3:
 * Manual, Summary, OutlineNumber, Duration, Start, Finish, percent complete and
 * percent work complete, actual/remaining duration, ActualStart/Finish,
 * Work/ActualWork/RemainingWork; project NewTasksCreatedAsManual).  Rule 1
 * accepts the reopen only because it was actually run.
 *
 * 0x0B40015E STEP IDS ARE OPAQUE.  Each 20-byte step ends in a u32 Project
 * allocates per session (the same block saved twice keeps its ids; a new
 * session's ids differ).  The port allocates its own deterministic non-zero
 * tokens; the pair test decodes the steps and compares flags/percents, with
 * the ids excluded, and the COM reopen proves Project accepts them.
 *
 * MARK ON TRACK AND UPDATE PROJECT (W28-1.3) ride the same writer with three
 * measured differences (tests/data/wave28-mark-*.mpp, wave28-update*.mpp):
 *   - The elapsed working time is NOT Duration * percent / 100.  Update
 *     Project's 50 % row is a 2400-minute manual row with 1200 minutes
 *     elapsed; its 83 % row has 1200 of 1440 minutes, and Project stores
 *     83.33333333333333 in the step block.  The caller therefore passes
 *     PercentActualEdit::actual_ticks (exact elapsed ticks) and
 *     step_percent (the unrounded double), and this writer stores both
 *     verbatim.  A zero actual_ticks keeps the W28-1.2 derivation.
 *   - FixedData +168 is left at its existing value 0x0080 on every edited row:
 *     these commands do NOT OR the 0x9000 progress bits a percent edit sets
 *     (Measured: all three update rows and both mark rows keep 0x80.)
 *     `progress_flags = false` selects that shape; the +174 start anchor is
 *     still set to ActualStart.
 *   - Update Project's project-row Resume/Stop are the EARLIEST valid per-row
 *     resume and stop (Tuesday 12:00 on the update pair), not the project
 *     start Mark on Track and the percent pairs keep.
 *     PercentActualsOptions::project_resume_stop_from_rows selects it.
 *   - Every written row also loses its StartSlack (+186 := 0) once it has
 *     progress, and a row written at 100 % loses FreeSlack/TotalSlack too
 *     (+74/+190 := 0) and stores its actual dates in the late pair
 *     (+112/+116 := ActualStart/ActualFinish).  Measured: the 83 % manual mark
 *     row keeps 9600/9600 free/total slack while StartSlack goes 9600 -> 0;
 *     the 100 % update row goes 14400 -> 0 on all three and its late pair moves
 *     from Wed/Fri to the actual Sun/Tue; a 0 % write leaves all of them alone.
 *     The W28-1.2 pairs already carried these values, so the same writes are
 *     byte-identical there.
 *
 * UPDATE PROJECT ALSO STORES ITS UPDATE DATE AS THE PROJECT STATUS DATE:
 * 114/Props record 0x02400045, kind 4, one packed date (the update control
 * stores 0xffffffff; the edited pair stores 0x3bf40000, Wed 2026-01-07).
 * write_project_status_date patches exactly those four payload bytes and
 * leaves the stream length unchanged.
 */

#ifndef MPPWRITE_ACTUALS_H
#define MPPWRITE_ACTUALS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "table.h"

namespace mppwrite {

/* Project stores ActualDuration as Duration * percent / 100 in ticks.  The
 * measured pairs are exact; integer truncation is the only rounding this
 * function does (no non-exact Project save has been measured yet). */
std::uint32_t actual_duration_ticks(std::uint32_t duration_ticks,
                                    std::uint16_t percent) noexcept;

/* True when the row's Fixed2Meta entry carries Project's manually-scheduled
 * bit (entry byte +8, bit 0x80; the same flag core/mpp reads as
 * Task::manually_scheduled).  The pairings come from FixedMeta: the entry whose
 * +4 names 48 + 202*row has the same index in Fixed2Meta.  Returns false when
 * the two metadata streams are absent or do not pair -- the reader's default is
 * automatic, and every measured file has them.
 *
 * This matters to the caller because Project computes a manual row's finish
 * with its start day's partial working time charged (docs/engine-parity.md,
 * core/plan's manual_finish_of): the manual 0 -> 40 pair's Stop is Tue 12:00
 * from a Sunday 13:00 actual start, not the calendar-correct Tue 17:00. */
bool task_is_manually_scheduled(CfbDocument const& doc, std::size_t row,
                                std::string_view table = "114/TBkndTask");

/* W29-0.0: true when the saved row already records progress -- a stored
 * % Complete (+92) above 0, an ActualStart/ActualFinish (+120/+124), or a
 * 0x0B40015E step block for the uid.  The app refuses a date update on a row
 * this returns true for UNLESS the row is the exact shape W29-0.3 measured
 * (see task_has_progress_block): every other progressed row still has no
 * Project pair showing what the recomputed percent does to its recorded
 * actuals, and rule 1 (wave 29) says an unmeasured shape is not written.
 * A row with no saved row of that uid returns false; the caller's own
 * saved-row check reports that case. */
bool task_has_recorded_progress(CfbDocument const& doc, std::uint32_t uid,
                                std::string_view table = "114/TBkndTask");

/* W29-0.3: true when the saved row carries Project's 0x0B40015E progress step
 * block.  The measured progressed-row date updates (tests/data/
 * wave29-mark-60-*.mpp / wave29-update-60-*.mpp) all start from a row whose
 * 60 % Project stored as [marker, middle 60, final 60]; the app lifts the
 * W29-0.0 refusal only when this block is present, so the writer's lowering
 * rule (keep the old middle, replace the final) is applied to data it can
 * decode rather than to an inferred shape. */
bool task_has_progress_block(CfbDocument const& doc, std::uint32_t uid,
                             std::string_view table = "114/TBkndTask");

/* The four packed dates one edit writes.  kInvalidDate (0xFFFFFFFF) means NA.
 * At percent 0 all four are NA; at 100 % ActualFinish is required; for
 * 0 < percent < 100 Resume/Stop are required. */
struct ActualsDates {
    std::uint32_t actual_start = kInvalidDate;
    std::uint32_t actual_finish = kInvalidDate;
    std::uint32_t resume = kInvalidDate;
    std::uint32_t stop = kInvalidDate;
};

struct PercentActualEdit {
    std::uint32_t uid = 0;
    std::uint16_t percent = 0;
    ActualsDates dates;
    /* W28-1.3: the exact elapsed working ticks this edit completed.  0 keeps
     * the W28-1.2 derivation Duration * percent / 100.  Mark on Track and
     * Update Project pass the value their date arithmetic measured, because
     * 50 % of a 2400-minute manual row is 1200 minutes and 83 % is 1200 of
     * 1440 -- neither is Duration * percent / 100.  Must not exceed the row's
     * Duration; the edit is refused (with a warning) when it does. */
    std::uint32_t actual_ticks = 0;
    /* W28-1.3: the percent written to the 0x0B40015E middle and final steps.
     * Negative keeps the integer `percent` as a double.  Project stores the
     * unrounded value (83.33333333333333 for 1200/1440). */
    double step_percent = -1.0;
    /* W28-1.3: true (a percent edit) ORs 0x9000 into FixedData +168 when the
     * row has progress.  Mark on Track and Update Project leave the word
     * unchanged, so they pass false. */
    bool progress_flags = true;
};

/* W28-1.3: how the project row's Resume/Stop are chosen.  False keeps the
 * W28-1.2/Mark on Track rule (both = the project start, FixedData row 0
 * +104).  True is Update Project's measured roll-up: both are the EARLIEST
 * valid resume/stop among the included task rows (Tuesday 12:00 on the
 * update pair, where the three rows resume Wed 08:00, Wed 08:00 and Tue
 * 12:00 and stop Tue 17:00, Tue 17:00 and Tue 12:00).  The caller selects it
 * for Update Project (the command that also stores the status date), not for
 * Mark on Track: both are date updates, but only Update Project uses the row
 * dates. */
struct PercentActualsOptions {
    bool project_resume_stop_from_rows = false;
};

struct PercentActualsResult {
    std::size_t rows_written = 0;
    /* One entry per REFUSED edit (uid 0, absent row, or inconsistent dates).
     * A refused edit changes nothing; the rest of the cascade still runs. */
    std::vector<std::string> warnings;
};

/*
 * Write the measured actuals for every edit and save the streams back into
 * `doc`.  `doc` is left unchanged when the writer throws (all streams are
 * built on copies first); Error is thrown for a damaged table (a VarMeta or
 * FixedMeta shape the writer cannot keep consistent), not for a refused edit.
 */
PercentActualsResult write_percent_actuals(
    CfbDocument& doc, std::vector<PercentActualEdit> const& edits,
    std::string_view table = "114/TBkndTask",
    PercentActualsOptions const& options = {});

/*
 * Store one packed date in the project's status-date record (114/Props,
 * record 0x02400045, kind 4, 4-byte payload).  Returns the previous raw value
 * so the caller can report it.  Throws Error when the stream or the record is
 * absent or has a different shape, or when `packed_date` is not a real date --
 * a file with no status-date record is a file this writer has not measured,
 * and inventing one (or silently not writing it) would break the save pair.
 * The stream length is unchanged: only the record's four payload bytes move.
 * `root` is the table storage ("114"; "112" in an MPP12 file).
 */
std::uint32_t write_project_status_date(CfbDocument& doc,
                                        std::uint32_t packed_date,
                                        std::string_view root = "114");

}  // namespace mppwrite

#endif  // MPPWRITE_ACTUALS_H
