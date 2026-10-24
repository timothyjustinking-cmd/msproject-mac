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
 * WHAT A PERCENT EDIT WRITES, per edited leaf row:
 *   FixedData +80  ActualDuration     = Duration * percent / 100
 *             +88  RemainingDuration  = Duration - ActualDuration
 *             +92  PercentComplete    = percent
 *             +94  PercentWorkComplete= 100 at 100 %, else 0 (the fixture's
 *                                       placeholder assignments carry no work,
 *                                       and Project's own save stores 0)
 *             +120 ActualStart        = the task's effective start
 *             +124 ActualFinish       = the measured finish at 100 %, else NA
 *             +132 Resume / +136 Stop = the caller's working-time values,
 *                                       NA at 0 %
 *             +168 progress flags      = existing word | 0x9000 when percent > 0
 *                                       (100->0 keeps the bits: measured)
 *             +174 start anchor       = ActualStart when percent > 0, else NA
 *   plus the project row's roll-up (+80/+88/+92 and +120/+132/+136, recomputed
 *   from every non-summary task row) and the row's VarMeta/Var2Data fields:
 *   0x0B4004E5 is removed, 0x0B400505/0x0B400506 are removed when a 0 % row
 *   starts carrying progress, and at percent > 0 a 0x0B40015E step block is
 *   appended; at 0 % a constant 0x0B4004E5 block is appended when the row has
 *   none.  A block built from scratch starts with Project's marker step
 *   (flag 1, percent 32768.0, a fresh opaque id) before the new middle step and
 *   the constant final step; a 40 -> 100 save keeps the marker and the old
 *   middles and drops only the old final.  The FixedMeta entry that covers the
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
 * NOT WRITTEN, and why: Project's save also rewrites Task Fixed2Data /
 * Fixed2Meta, the Task FixedMeta opaque cache bytes, TBkndAssn work contours,
 * CEdl/props/summary-information save metadata.  Those are derived caches the
 * port has never written; the save-pair test asserts every stream outside the
 * four task-table streams still equals the CONTROL file, and Project's reopen
 * of the port's save is the acceptance test (the W28-0.4 precedent).
 *
 * 0x0B40015E STEP IDS ARE OPAQUE.  Each 20-byte step ends in a u32 Project
 * allocates per session (the same block saved twice keeps its ids; a new
 * session's ids differ).  The port allocates its own deterministic non-zero
 * tokens; the pair test decodes the steps and compares flags/percents, with
 * the ids excluded, and the COM reopen proves Project accepts them.
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
    std::string_view table = "114/TBkndTask");

}  // namespace mppwrite

#endif  // MPPWRITE_ACTUALS_H
