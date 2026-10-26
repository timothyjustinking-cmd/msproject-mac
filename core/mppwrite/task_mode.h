/*
 * task_mode.h -- Project's automatic -> manual task-mode write (wave 30,
 * W30-1.0).
 *
 * MEASURED SOURCE.  tests/data/wave30-task-mode-auto-control.mpp and
 * tests/data/wave30-task-mode-manual.mpp are one Project-2021 save pair made
 * from the neutral synthetic project: Project opened the control, selected
 * task ID 1 (UniqueID 1, an automatically scheduled 24000-tick leaf) and
 * pressed Manually Schedule, then saved.  Its committed no-edit save is
 * wave30-task-mode-auto-control-null.mpp.  build/w30/W30-1.0-*.log (not
 * committed) hold the byte diff, the null-save masks and the per-word dump.
 *
 * The reverse pair, wave30-task-mode-manual-control.mpp / -auto.mpp, is the
 * same row switched back with Auto Schedule.  It is EVIDENCE, not a write this
 * port performs: Project also rescheduled the row (its +96/+100 pair moved to
 * the automatic start/finish and its Fixed2Data copies/start anchor were
 * rewritten), and this port has no scheduler.  Auto Schedule therefore stays
 * disabled; the reverse pair also proves the manual row's stored invariant the
 * forward writer relies on (see below).
 *
 * WHAT ONE AUTOMATIC -> MANUAL EDIT WRITES.  The pair's whole-file diff has
 * exactly 18 unmasked bytes in five owned streams plus the one Props14 edit-
 * marker byte:
 *
 *   TBkndTask FixedData   row +174 StartAnchor := the row's effective start
 *                         (+104).  The measured control stores 0xffffffff; the
 *                         edited file stores 0x3bf212c0, the same word as +104.
 *   TBkndTask FixedMeta   entry +17 |= 0x04      (0x48 -> 0x4c)
 *                         entry +18 &= ~0x20     (0xef -> 0xcf)
 *                         The entry's +2 variable-field count and its +8..23
 *                         cache blob's other bytes are unchanged (no VarMeta
 *                         entry is added or removed by this edit).
 *   TBkndTask Fixed2Meta  entry +8 |= 0x80       (0x7f -> 0xff) -- Project's
 *                         Manually Scheduled bit, the same bit core/mpp reads
 *                         as Task::manually_scheduled.
 *   TBkndTask Fixed2Data  record +50 := +104, +54 := +108, +58 := +84 (the
 *                         effective start, effective finish and duration).  The
 *                         control stores NA/NA/0 there; the edited file stores
 *                         0x3bf212c0 / 0x3bf627d8 / 0x00005dc0.  The record's
 *                         identity, parent identity, flags +42 and start copy
 *                         +46 are unchanged by the edit (Project reallocates
 *                         identities on every save; they are per-save noise).
 *   TBkndAssn Fixed2Meta  entry +8 |= 0x80       (0x4f -> 0xcf) -- the
 *                         assignment mirrors its task's scheduling mode.
 *   Props14               record 0x3540000a payload 0x0000 -> 0x0001, via
 *                         write_edit_marker (edit_marker.h).
 *
 * No other stream changes: Task/FixedMeta +8..23's other bytes, VarMeta,
 * Var2Data, the assignment rows, 114/Props and DocumentSummaryInformation are
 * byte-identical in the null save and Project's edit save.
 *
 * THE COPY RULE.  Project's manual rows store the EFFECTIVE pair in every copy:
 * the reverse pair's manual control and the forward control's already-manual
 * rows 2 and 3 have +174 == +104 and Fixed2Data +50/+54/+58 == +104/+108/+84,
 * while their calculated +96/+100 pair may differ (row 2 of the forward
 * control stores a Sunday 13:00 +96 and a Monday 08:00 +104).  The edited row
 * of the measured pair has +96 == +104 and +100 == +108, so writing the
 * effective pair reproduces Project's bytes.  A row whose calculated and
 * effective pairs disagree is refused: no pair measures an edit of it.
 *
 * THE FILE SHAPE THIS WRITER ACCEPTS (rule 1, wave 29: one measured shape per
 * enabled path, refused up front).  tests/data/README.md records the fixture's
 * conditions; read_task_mode_shape checks every one on the document itself:
 *
 *   - the link table is EMPTY: TBkndCons FixedData 0 bytes, FixedMeta 16,
 *     Fixed2Data 0, Fixed2Meta 16;
 *   - every live task row (row 1..n-1) is a top-level leaf: FixedMeta state
 *     byte 0x00, summary bit (+26) clear, +140 == 0, +172 == 1, +142 == 0;
 *   - every task row has a positive duration (+84) and the project calendar
 *     (+178 == -1);
 *   - NO progress anywhere: +80/+92/+94 == 0, +120/+124/+132/+136 == NA and no
 *     0x0B40015E step block on any task; no 0x0F400032 actual-work block on any
 *     assignment;
 *   - exactly one LIVE placeholder assignment per task: TBkndAssn +8 ==
 *     0xFFFF0001 and +20 (a double, minutes x1000) == the task's duration
 *     (measured: the control's 2400/1440/960 stored minutes for its
 *     24000/14400/9600-tick tasks), no tombstoned or extra rows;
 *   - the edited row is automatic and has the measured precondition bytes:
 *     FixedMeta +17 == 0x48 and +18 == 0xef, Fixed2Meta +8 == 0x7f,
 *     FixedData +174 == NA, +96 == +104, +100 == +108, +112 == +96 and
 *     +116 == +100, Fixed2Data +50 == +54 == NA and +58 == 0, and its
 *     assignment's Fixed2Meta +8 == 0x4f.
 *
 * A file or row that differs in one of those conditions is refused with an
 * error naming the first difference.  The app's make_task_mode_edit runs this
 * check when the edit is queued (so the user sees the refusal before Save);
 * write_task_mode runs it again as defence in depth and reports a refusal as a
 * warning, changing nothing.  One row per save is the measured shape: a call
 * with more than one edit throws.
 */

#ifndef MPPWRITE_TASK_MODE_H
#define MPPWRITE_TASK_MODE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"

namespace mppwrite {

/* The measured words of one task row, as read from the document before the
 * edit.  `start`/`finish` are the EFFECTIVE pair (+104/+108); `calculated_*`
 * are +96/+100, which the measured shape requires to agree. */
struct TaskModeShape {
    std::uint32_t uid = 0;
    std::uint32_t duration_ticks = 0;
    std::uint32_t start = 0;             /* FixedData +104 */
    std::uint32_t finish = 0;            /* FixedData +108 */
    std::uint32_t calculated_start = 0;  /* FixedData +96 */
    std::uint32_t calculated_finish = 0; /* FixedData +100 */
};

/*
 * Validate the whole-file shape above and the row `uid`, filling `shape`.
 * Returns false with `error` naming the first difference.  A missing or damaged
 * table is also false (the writer would throw; the edit model turns this into
 * the user-facing refusal).  `table` is the task table ("114/TBkndTask").
 */
bool read_task_mode_shape(CfbDocument const& doc, std::uint32_t uid,
                          TaskModeShape& shape, std::string& error,
                          std::string_view table = "114/TBkndTask");

/* One task-mode edit: the row that becomes manually scheduled. */
struct TaskModeEdit {
    std::uint32_t uid = 0;
};

struct TaskModeWriteResult {
    std::size_t rows_written = 0;
    /* One entry for a refused edit (the row or file no longer has the measured
     * shape).  A refused edit changes nothing and does not write the marker. */
    std::vector<std::string> warnings;
};

/*
 * Apply the measured automatic -> manual write for the one edit and store the
 * five streams plus the Props14 marker in `doc`.  All validation happens
 * before the first mutation; a refused edit leaves `doc` untouched.  Throws
 * Error for a damaged table (the caller sees a failed save), for more than one
 * edit, or when the writer's own post-write table check fails.
 */
TaskModeWriteResult write_task_mode(
    CfbDocument& doc, std::vector<TaskModeEdit> const& edits,
    std::string_view table = "114/TBkndTask");

} /* namespace mppwrite */

#endif /* MPPWRITE_TASK_MODE_H */
