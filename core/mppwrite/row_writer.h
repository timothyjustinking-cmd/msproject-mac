/*
 * row_writer.h -- write ONE task row so that every copy of every value agrees.
 *
 * WHY THIS EXISTS.  A task row is not a record with fields; it is a record whose
 * values are copied into several places, and MS Project reads some of those
 * copies and recomputes the rest on open.  spec/mpp-writer.md §8.1 measured what
 * that means in practice:
 *
 *   - Priority (+78) survives an open, because it has exactly one stored copy;
 *   - PercentComplete (+92) is NOT single-copy -- Project's own 50 % save
 *     (tests/data/wave27-percent-actuals.mpp) also writes the actuals, the
 *     actual start, two dates, a progress flag, the project row's roll-up and a
 *     Var2Data field -- so +92 alone is the same kind of partial write as a
 *     start pin and is not offered by this writer;
 *   - a patched Start or Duration does NOT, because the row carries several
 *     copies and the file it was patched in was left self-inconsistent;
 *   - a single-copy start patch (+96 only) leaves +104/+112 stale and is exactly
 *     the shape of edit Project recalculates away.
 *
 * So this file offers one write path that fills in every copy at once, and
 * refuses to express a single-copy write.  It is deliberately independent of
 * `core/plan` (and of `core/mpp`): the caller passes plain numbers, the writer
 * does bytes.
 *
 * WHERE THE COPIES ARE (all measured; see spec/mpp-writer.md §10).
 *
 *   114/TBkndTask/FixedData, row byte = 48 + 202 * row:
 *     +74  i32  TotalSlack, 6-second ticks (4800 = one 8-hour day)
 *     +78  u16  Priority
 *     +80  u32  ActualDuration, ticks
 *     +84  u32  Duration, ticks
 *     +88  u32  RemainingDuration, ticks  (actual + remaining = duration)
 *     +92  u16  PercentComplete
 *     +96  u32  Start, packed date                  ("task frame")
 *     +100 u32  Finish, packed date
 *     +104 u32  Start copy                           ("project frame")
 *     +108 u32  Finish copy
 *     +112 u32  Start copy 2 (late start)
 *     +116 u32  Finish copy 2 (project / late finish)
 *     +186 i32  StartSlack, ticks (equals +74 on task rows, differs on row 0)
 *     +190 i32  TotalSlack copy (equals +74 in every measured row)
 *
 *   114/TBkndTask/Fixed2Data, record = 64 * (row + 3):
 *     +46 u32  packed date, equals FixedData +104 in every measured task row
 *     +50 u32  packed date, equals +46 in every measured task row
 *     +54 u32  packed date, equals FixedData +108 in every measured task row
 *     +58 u32  duration, equals FixedData +84
 *
 * WHY THE COPIES ARE NOT ALL THE SAME VALUE.  base.mpp (and Project's own save
 * of it, corpus/probe/resaved-base.mpp) stores task A as
 *
 *     +96  2026-01-05 08:00     +104 2026-01-04 13:00
 *     +100 2026-01-09 17:00     +108 2026-01-09 12:00
 *     +112 2026-01-04 13:00     +116 2026-01-09 12:00
 *
 * i.e. the second and third pairs are the same two dates in a date-only frame
 * (13:00 UTC = local midnight in January, +11), not duplicates of +96/+100.  In
 * w3-o1.mpp, saved later by a different Project build, +104 == +96 and
 * +108 == +100 and +112/+116 are the late start/finish.  The frame is a property
 * of the file, so the writer does NOT derive one copy from another: all six are
 * inputs.  `read_task_values` returns the current six, which is the intended way
 * to keep the frame while moving a date.
 *
 * WHAT IS MEASURED vs DERIVED.
 *   Measured (fixtures, byte diffs): every offset above; remaining = duration -
 *   actual (var-pct.mpp); +190 == +74 (all rows of base/w3-o1/outline1/resaved-
 *   base); Fixed2Data +46/+50 == FixedData +104 and +54 == +108 and +58 == +84
 *   (base rows 1..3, var-projstart.mpp, var-duration.mpp); the project row's
 *   Fixed2Data date/duration words are 0xFFFFFFFF and are left alone.
 *   Derived (and says so): `kInherit` defaults, and nothing else.  The late
 *   start/finish of a row is NOT computed here -- a schedule engine owns that.
 *
 * WHAT THIS FILE DOES NOT DO.  It does not recalculate a schedule.  If the
 * values are inconsistent, the file is inconsistent; `recalculate_write_path`
 * only coordinates the copies, it does not do CPM.  It does not touch 214/
 * (spec/mpp-writer.md §10.1), FixedMeta/Fixed2Meta (sizes do not change) or
 * Var2Data.
 */

#ifndef MPPWRITE_ROW_WRITER_H
#define MPPWRITE_ROW_WRITER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "table.h"

namespace mppwrite {

/* --- 114/TBkndTask/Fixed2Data (spec/mpp-records.md §7) ------------------- */

namespace fixed2_row {

inline constexpr std::size_t kStride = 64;
/* Records 0..2 are table header slots; row r lives in record r + 3.  Measured:
 * base 448 = 64*7 for 4 rows, PROJECT_A 5,632 = 64*88 for 85, PROJECT_B 5,696 = 64*89
 * for 86. */
inline constexpr std::size_t kHeaderRecords = 3;

inline constexpr std::size_t kStart = 46;     /* packed date */
inline constexpr std::size_t kStartCopy = 50; /* packed date, == kStart */
inline constexpr std::size_t kFinish = 54;    /* packed date */
inline constexpr std::size_t kDuration = 58;  /* ticks, == FixedData +84 */

} /* namespace fixed2_row */

/* Byte offset of `row`'s record inside Fixed2Data. */
std::size_t fixed2_record_offset(std::size_t row);
/* Row count implied by a Fixed2Data byte size; throws unless it is 64 * (n+3). */
std::size_t fixed2_row_count(std::size_t fixed2_bytes);
std::vector<std::uint8_t> read_fixed2_data(
    CfbDocument const& doc, std::string_view table = "114/TBkndTask");

/* --- one row's values ---------------------------------------------------- */

/*
 * "Take the value that is already stored" (or, for a field that copies another,
 * the value of the field it copies).  One sentinel for every field, so a caller
 * never has to guess a magic number: kInherit in a u16 field reads as 0xFFFE,
 * which is out of range for Priority (0..1000) and PercentComplete (0..100).
 */
inline constexpr std::uint32_t kInherit = 0xFFFFFFFEu;
inline constexpr std::int32_t kInheritSlack = -2147483647 - 1;

struct TaskValues {
    /* The two dates the caller schedules, in the task's own frame.  For an
     * auto-scheduled task these are Start/Finish; for a Manual=True task
     * Project treats +96/+100 as a calculated copy and reports +104/+108
     * instead, so use `write_manual_task_row` for that case. */
    std::uint32_t start = kInvalidDate;  /* +96 */
    std::uint32_t finish = kInvalidDate; /* +100 */

    /* The same two dates in the project's date-only frame (see the file
     * comment).  Required: a row written without them is refused, because that
     * is the state MS Project recalculates away.  W26/W29: these are the
     * EFFECTIVE Start/Finish (+104/+108), the dates the UI reports; +104 is the
     * source Fixed2Data +50 follows and +108 the source of +54. */
    std::uint32_t project_start = kInvalidDate;  /* +104 */
    std::uint32_t project_finish = kInvalidDate; /* +108 */

    /* Late start / late finish.  Required; the engine supplies them. */
    std::uint32_t late_start = kInvalidDate;  /* +112 */
    std::uint32_t late_finish = kInvalidDate; /* +116 */

    /* Duration family.  kInherit for remaining means duration - actual. */
    std::uint32_t duration = kInherit;          /* +84, Fixed2 +58 */
    std::uint32_t remaining_duration = kInherit; /* +88 */
    std::uint32_t actual_duration = kInherit;    /* +80 */

    /* Slack family, 6-second ticks.  THE THREE WORDS ARE THREE VALUES (W30,
     * corrected): +74 FreeSlack, +186 StartSlack, +190 TotalSlack.  The reader
     * maps them the same way (mpp::Task::free_slack_ticks/start_slack_ticks/
     * total_slack_ticks), so a writer that puts TotalSlack at +74 makes the
     * reader report free == total for ever -- which was this writer's bug
     * (docs/engine-parity.md "Wave 18").  Every measured TASK row in
     * base/w3-o1/outline1/resaved-base happens to have free == total, which is
     * why it went unnoticed; milestone1.mpp ID 6 (+74 0, +186/+190 220800) is the
     * row that separates them.
     *
     * Inheritance: kInheritSlack keeps the stored word.  `start_slack` follows
     * `total_slack` when it already did and the caller did not override it (the
     * "keep a copy a copy" rule), so an existing no-op save stays byte-identical;
     * `free_slack` is never derived, because the reader reports it independently. */
    std::int32_t free_slack = kInheritSlack;   /* +74  COM FreeSlack */
    std::int32_t total_slack = kInheritSlack;  /* +190 COM TotalSlack */
    std::int32_t start_slack = kInheritSlack;  /* +186 COM StartSlack */

    /* Single-copy fields: written only when supplied, so a date-only update
     * cannot accidentally reset them. */
    std::uint16_t priority = static_cast<std::uint16_t>(kInherit);
    std::uint16_t percent_complete = static_cast<std::uint16_t>(kInherit);
    /* +64 ConstraintType (PjConstraint: 0 ASAP, 4 SNET, ...) and +66
     * ConstraintDate (packed date, 0xFFFFFFFF when absent).  kInherit keeps the
     * stored values; `write_task_constraint` is the dedicated entry point. */
    std::uint16_t constraint_type = static_cast<std::uint16_t>(kInherit);
    std::uint32_t constraint_date = kInherit;

    /* +174, the packed-date start anchor.  Measured W29: Project's own save of
     * a manual task wrote the new Start here as well as into +104/+108,
     * +112/+116 and Fixed2Data +50/+54.  On an auto task it is the unsnapped
     * predecessor-finish bound, or 0xFFFFFFFF (w3-o1.mpp).  kInherit keeps the
     * stored value. */
    std::uint32_t start_anchor = kInherit;

    /* The Fixed2Data copies.  kInherit means project_start / project_finish /
     * duration respectively, which is what every measured task row stores.
     * `write_task_row` writes +46 and +50 to `fixed2_start`;
     * `write_manual_task_row` is the path that leaves +46 alone, because
     * Project did on its own save.
     * `write_task_row` also keeps a copy a copy: a word that equalled its
     * FixedData source before and that the caller has not overridden follows the
     * new source value, so read -> change one date -> write moves it too.
     * Row 0 (the project row) is not written to Fixed2Data at all: its
     * date/duration words are 0xFFFFFFFF in base.mpp and resaved-base.mpp and no
     * legal value is known for them. */
    std::uint32_t fixed2_start = kInherit;
    std::uint32_t fixed2_finish = kInherit;
    std::uint32_t fixed2_duration = kInherit;

    /* INSTRUCTION, NOT A STORED VALUE: "this row is MANUALLY SCHEDULED, so its
     * calculated pair (+96/+100) is allowed to differ from its effective pair
     * (+104/+108)".  MEASURED on Project's own saves (docs/engine-parity.md
     * "Wave 18"): recalculate stores +96/+100 = 2026-01-05 08:00/17:00 with
     * +104/+108 = 2026-01-04 13:00/01-09 12:00, and delete-task-with-successor
     * stores the same split for uid 3.  `write_task_row`'s "single-copy start
     * write" refusal exists because Project recalculates a split AUTO row away;
     * for a manual row Project itself stores the split, so the refusal is skipped.
     * It is deliberately excluded from operator== (it is not in the file). */
    bool manual_dates = false;
};

/* Exact field-by-field equality, including the Fixed2Data copies. */
bool operator==(TaskValues const& a, TaskValues const& b);
inline bool operator!=(TaskValues const& a, TaskValues const& b) {
    return !(a == b);
}

/*
 * The row as stored.  Fixed2Data copies are reported as 0xFFFFFFFF for the
 * project row (nothing is stored there).  Throws Error when the table or the
 * row does not exist, or when Fixed2Data is not 64 * (rows + 3) bytes.
 */
TaskValues read_task_values(CfbDocument const& doc, std::size_t row,
                            std::string_view table = "114/TBkndTask");

/*
 * The values `write_task_row` would store: every kInherit resolved against the
 * row's current contents, every copy present, and the cross-copy rules checked.
 * Throws Error (a refusal, not a warning) when:
 *   - any of the six date copies is missing (0 or 0xFFFFFFFF); the message names
 *     the missing copies and says a single-copy write is refused;
 *   - the duration family disagrees (actual + remaining != duration);
 *   - exactly one member of a family would change while the others stay: a
 *     single-copy start (or finish, or duration) patch;
 *   - percent complete > 100;
 *   - Fixed2Data does not hold 64 * (rows + 3) bytes.
 * Usable as a pre-flight check without writing anything.
 */
TaskValues resolve_task_values(CfbDocument const& doc, std::size_t row,
                               TaskValues const& values,
                               std::string_view table = "114/TBkndTask");

struct RowWriteReport {
    TaskValues written;              /* exactly what is now in the file */
    std::vector<std::string> warnings; /* suspicious but legal values */
};

/*
 * Write one row: all six FixedData dates, duration/remaining/actual, slack in
 * three places, the optional single-copy fields, and the Fixed2Data copies.
 * Re-reads the row afterwards and throws if any value did not take.
 * Row 0 is the project row: its Fixed2Data record is left byte-identical.
 */
RowWriteReport write_task_row(CfbDocument& doc, std::size_t row,
                              TaskValues const& values,
                              std::string_view table = "114/TBkndTask");

/* --- W29: the manual-task write path ------------------------------------- */

/*
 * A Manual=True task is scheduled from a different set of stores than an
 * auto-scheduled one.  Measured by diffing Project's own save
 * (corpus/probe/manual-start-edited.mpp, made by setting the task's Start and
 * saving) against base.mpp: after moving the task to 2026-01-12 08:00 ->
 * 2026-01-16 17:00 Project wrote
 *
 *   FixedData +104/+108 = 2026-01-12 08:00 / 2026-01-16 17:00  (effective)
 *   FixedData +112/+116 = the same two dates              (Late*, slack 0 here)
 *   FixedData +174      = 2026-01-12 08:00               (start anchor)
 *   Fixed2Data +50/+54  = 2026-01-12 08:00 / 2026-01-16 17:00
 *
 * and left +96/+100 (the CPM-calculated copy), +84/+88 (duration family),
 * Fixed2Data +46 and Fixed2Data +58 alone.  Writing all six FixedData date
 * copies with `write_task_row` is internally consistent but Project
 * recalculates it away for a manual task, so this is a separate entry point
 * that writes exactly the set Project wrote.
 *
 * ORACLE RESULT (2026-10-22): the seven words are NOT sufficient either.  The
 * written fixture reopened with Finish 2026-01-16 17:00 (and Duration 5040) but
 * Start still 2026-01-04 13:00 -- +174 is not the manual Start authority on its
 * own.  Manual scheduling therefore remains unsupported; `pin_task_start`
 * (SNET, measured to work) is the documented way to make a date stick.
 */
struct ManualTaskWrite {
    std::uint32_t start = kInvalidDate;  /* +104, +174, Fixed2 +50 */
    std::uint32_t finish = kInvalidDate; /* +108, Fixed2 +54 */
    /* LateStart/LateFinish.  kInherit copies start/finish, which is the
     * zero-slack shape of the measured fixture. */
    std::uint32_t late_start = kInherit;
    std::uint32_t late_finish = kInherit;
};

struct ManualWriteReport {
    TaskValues before; /* the row as read */
    TaskValues after;  /* the row as written and re-read */
    std::vector<std::string> warnings; /* suspicious but legal values */
};

/*
 * Write a manual task's effective schedule: +104/+108, +112/+116, +174 and
 * Fixed2Data +50/+54.  `before.start`, `before.finish`, the duration family,
 * Fixed2Data +46 and +58 must read back unchanged, or the write throws.  Row 0
 * is refused (it is the project row and has no Fixed2Data record).
 */
ManualWriteReport write_manual_task_row(
    CfbDocument& doc, std::size_t row, ManualTaskWrite const& values,
    std::string_view table = "114/TBkndTask");

/* --- W29: the constraint write path -------------------------------------- */

/*
 * ConstraintType (+64, u16, PjConstraint: 0 ASAP, 4 SNET, ...) and
 * ConstraintDate (+66, packed date; 0xFFFFFFFF when absent).  Both are
 * single-copy fields, so no cross-copy coordination is needed, but the date
 * copies are checked to be untouched.  `pin_task_start` is the supported way to
 * write a date; this raw pair is the primitive underneath it.
 */
struct TaskConstraint {
    std::uint16_t type = 0;            /* PjConstraint */
    std::uint32_t date = kInvalidDate; /* packed date */
};

/* PjConstraint value for Start No Earlier Than: the constraint Project stores
 * when a start date is typed/pinned on an auto-scheduled task. */
inline constexpr std::uint16_t kConstraintSnet = 4u;

TaskConstraint read_task_constraint(CfbDocument const& doc, std::size_t row,
                                    std::string_view table = "114/TBkndTask");

/*
 * Write ConstraintType/ConstraintDate only.  Throws when
 *   - the type is non-zero and the date is not a valid packed date (a
 *     constraint without a date is meaningless),
 *   - the type is zero (ASAP) and a valid date is supplied (a date without a
 *     type cannot be honoured),
 *   - the row does not exist, or anything else in the row reads back changed.
 */
TaskConstraint write_task_constraint(CfbDocument& doc, std::size_t row,
                                     TaskConstraint const& values,
                                     std::string_view table = "114/TBkndTask");

/*
 * The supported "write a date" operation (W29, oracle-verified 2026-10-22).
 *
 * Writes `date` as a real SNET constraint at +64/+66 and nothing else.  Project
 * recalculates the row on open, places the task's Start at the constraint date,
 * and honours it: in the measured fixture uid 1 moved from 2026-10-19 08:00 to
 * 2026-10-21 08:00 -> 17:00 with Duration unchanged, and the project start
 * recalculated to the earliest remaining task (2026-10-20 08:00).
 *
 * It does NOT preserve manual scheduling: the row is scheduled by its
 * constraint, and an unconstrained task floats to the project start.  For a
 * Manual=True row the effective date words (+104/+108, +174, Fixed2Data
 * +50/+54) are still not authoritative -- writing them alone leaves Start where
 * it was (W29 manual fixture, measured).
 */
TaskConstraint pin_task_start(CfbDocument& doc, std::size_t row,
                              std::uint32_t date,
                              std::string_view table = "114/TBkndTask");

/* One row of a plan result: plain numbers, addressed by UniqueID. */
struct PlannedRow {
    std::uint32_t uid = 0; /* 0 is the project row */
    TaskValues values;
};

/*
 * Write a whole plan result.  Each row is written with write_task_row, then the
 * table is checked with check_table (FixedMeta/Fixed2Meta size words, VarMeta
 * size, every Var block inside Var2Data) and an Error is thrown if the file is
 * not internally consistent.  Returns the number of rows written.
 *
 * This is the "make the file agree with the plan" entry point:
 *   - every date copy and duration copy is written together,
 *   - TotalSlack is set at +74 (and +190), StartSlack at +186,
 *   - FixedMeta's size word stays in step because FixedData keeps its length.
 * `warnings` (optional) collects each row's RowWriteReport::warnings.
 */
std::size_t recalculate_write_path(
    CfbDocument& doc, std::vector<PlannedRow> const& rows,
    std::string_view table = "114/TBkndTask",
    std::vector<std::string>* warnings = nullptr);

} /* namespace mppwrite */

#endif /* MPPWRITE_ROW_WRITER_H */
