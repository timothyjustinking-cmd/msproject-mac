/*
 * calculate.h -- the measured Calculate Project (F9) save for ONE shape
 * (W35-1.2).  The decode and the evidence are in spec/writers/calculate.md and
 * spec/writers/evidence/wave35-calculate-unlinked.md; this header states what
 * the writer accepts and what it refuses.
 *
 * THE ONE SHAPE.  A file Project built automatically, holding the project row
 * and exactly two top-level, automatically scheduled, unlinked leaf tasks with
 * no constraint, no deadline, no progress, no resource and no task calendar, in which one
 * leaf's Duration was changed under manual calculation and the recalculation is
 * still pending (the project row's Finish does not yet equal the latest leaf
 * Finish, and 114/Props record 0x02400024 still holds 01 00).  Project's F9 then
 * save writes, and this writer reproduces byte for byte (tests/
 * roundtrip_calculate.cpp, effects declared against the null save):
 *
 *   - project row (FixedData row 0): Duration and Remaining (+84/+88) become
 *     the critical leaf's duration, and Finish, effective Finish and late
 *     Finish (+100/+108/+116) become the latest leaf Finish;
 *   - each leaf: free slack (+74), late Start/Finish (+112/+116), start and
 *     total slack (+186/+190) as the caller's engine computed them, and the
 *     start anchor (+174) cleared to 0xFFFFFFFF;
 *   - Fixed2Data: record 0 +58 becomes 0; each leaf record +42 gets its
 *     measured stamp (0x30 for both leaves), and +46 gets the Props project
 *     start (0x02400002);
 *   - FixedMeta entry 3 (the project row): five overtime var records are
 *     appended on uid 0 (0x0B4000A4/A5/A8/A9/AA, 8 zero bytes each) and the
 *     entry's presence bits become 0x1fef at +18/+19;
 *   - 114/Props: records 0x02400003 (project finish) and 0x02400024 (01 00 ->
 *     00 00), and the Task Var2Data size and freed words as save_var_record
 *     keeps them;
 *   - DocumentSummaryInformation (write_project_summary) and the Props14 edit
 *     marker (write_edit_marker).
 *
 * WHAT IS NOT WRITTEN.  Linked files, a lag, a constraint, a manual row, a
 * summary beyond the project row, progress, assignments beyond the placeholder
 * rows, a task calendar, any other row count: the three other committed pairs
 * (wave35-calculate-linked, -lag, -snet) differ from this one in words this
 * writer does not model (the F9 stamps in Fixed2Data differ per row and per
 * shape), so each is refused with the first difference named.
 *
 * ALL-OR-NOTHING.  The shape and the request (every record the save touches,
 * the 114/Props project start and finish included) are validated before the
 * first mutation.  The chain of appends and Props edits runs against a private
 * `path + ".calculate.tmp"` that is re-opened and compared with the bytes built;
 * `path` itself is written once, atomically, at the end, and the caller's
 * document is replaced only on success (W35b-2.1).
 */

#ifndef MPPWRITE_CALCULATE_H
#define MPPWRITE_CALCULATE_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cfb_writer.h"

namespace mppwrite {

/* The measured Fixed2Data +42 stamp of an unlinked leaf after F9. */
inline constexpr std::uint8_t kCalculateLeafStamp = 0x30;

/*
 * W35b-0.4 (RW35-W2): a leaf's Finish on the one calendar the pair measured
 * (Standard: Monday to Friday, 08:00 to 12:00 and 13:00 to 17:00).  The shape
 * is a leaf that starts at 08:00 on a working day and lasts a whole number of
 * 8-hour days (4800 ticks each); then the Finish is 17:00 on the last of those
 * working days.  Returns false for any other Start or Duration, so the caller
 * cannot prove the leaf dates are final and refuses.
 */
bool calculate_measured_finish(std::uint32_t start, std::uint32_t duration,
                               std::uint32_t& finish);

/* RW35-R4: the unlinked backward pass on the same calendar, for a leaf that finishes at
 * `finish` (17:00 on a working day) and lasts `duration` (whole 8-hour days) in a project
 * that finishes at `project_finish` (17:00 on a working day, not before `finish`).  The
 * leaf's slack is 4800 ticks for each working day after `finish` up to `project_finish`,
 * and its late start is 08:00 on the working day its duration's working days back from
 * `project_finish`.  Returns false for any other input.  The writer's request check and
 * the edit model's push() both call it, so the slack words a hand-built edit carries are
 * the measured ones or refused. */
bool calculate_measured_late(std::uint32_t finish, std::uint32_t duration,
                             std::uint32_t project_finish, std::uint32_t& slack,
                             std::uint32_t& late_start);

/* W35c-0.2 (R35bG0-1): the first working instant at or after `props_start` (the
 * 114/Props project start, 0x02400002) on the same calendar: a start before 08:00 on
 * a working day, or in a weekend, maps to the next working day 08:00 (the measured
 * Sunday 21:00 maps to Monday 08:00); one at or after 17:00 maps to the next working
 * day 08:00; one inside the working day is its own first working instant.  The lunch
 * hour is not modelled: no leaf starts anywhere but 08:00 (calculate_measured_finish),
 * so no start inside the working day can equal a leaf Start.  Returns false for an
 * invalid date, a time word of 24:00 or later (W35d-2.3, R35cG0-5) or a day past the packed
 * range. */
bool calculate_measured_first_working_start(std::uint32_t props_start,
                                            std::uint32_t& first);

/* W35c-1.7 (R35bG0-5): the measured leaf's Task FixedMeta entry (47 bytes) and VarMeta key
 * set, shared by the writer's shape check and the plan layer so one pattern pins both.
 * `calculate_leaf_meta_mismatch` returns -1 when `entry` is the measured entry (bytes 0-3
 * and 16-46, bytes 8, 10 and 11, and byte 9 as 0xbf or 0xff; bytes 4-7 are the row's offset
 * and bytes 12-15 a per-file stamp), the first differing byte otherwise, and 0 for an entry
 * of another size.  The key set is sorted ascending. */
int calculate_leaf_meta_mismatch(std::uint8_t const* entry, std::size_t size);
std::vector<std::uint32_t> const& calculate_leaf_var_keys();

/* What the pending file looks like, as the shape check read it. */
struct CalculateShape {
    std::uint32_t critical_uid = 0;     /* the leaf that ends the project */
    std::uint32_t project_start = 0;    /* row 0 +96 */
    std::uint32_t props_project_start = 0; /* 114/Props 0x02400002, the leaf Fixed2Data +46 */
    std::uint32_t project_finish = 0;   /* the latest leaf Finish */
    std::uint32_t critical_duration = 0; /* that leaf's +84, ticks */
    std::vector<std::uint32_t> leaf_uids;
};

/*
 * Validate the whole-file shape above and fill `shape`.  Returns false with
 * `error` naming the first difference; a damaged table is also false.
 */
bool read_calculate_shape(CfbDocument const& doc, CalculateShape& shape,
                          std::string& error);

/* One leaf's recalculated words (engine output), in ticks and packed dates. */
struct CalculateLeaf {
    std::uint32_t uid = 0;
    std::uint32_t late_start = 0;  /* +112 */
    std::uint32_t late_finish = 0; /* +116 */
    std::int32_t free_slack = 0;   /* +74 */
    std::int32_t start_slack = 0;  /* +186 */
    std::int32_t total_slack = 0;  /* +190 */
};

struct CalculateRequest {
    std::uint32_t project_duration = 0; /* ticks */
    std::uint32_t project_finish = 0;   /* packed date */
    std::vector<CalculateLeaf> leaves;
};

struct CalculateWrite {
    std::size_t leaves_written = 0;
    std::size_t var_records_appended = 0;
};

/*
 * The dry run of save_calculate: the shape check and every request check, with
 * nothing written.  Throws Error with the text save_calculate would.
 */
void validate_calculate(CfbDocument const& doc, CalculateRequest const& request);

/*
 * Apply the measured F9 save to `doc` and write the package to `path`.  Throws
 * Error on a shape or request refusal, naming the difference; then neither
 * `doc` nor a new output file has been left behind.
 */
CalculateWrite save_calculate(CfbDocument& doc, CalculateRequest const& request,
                              std::string const& path);

/*
 * TEST ONLY (W35d-1.3, R35cG2-1).  A fault seam for the scratch chain: when `appends` is
 * non-zero, save_calculate throws Error ("injected fault") right after that many of its five
 * overtime appends have been written to the scratch file, so a test can prove that a failure
 * mid-chain leaves an existing target byte-identical and no scratch file behind.  6 (one past
 * the last append) throws after the Props edit instead, before the scratch save and the re-read
 * compare; 7 throws after those two, the last step before the target is written (W35f-1.11).
 * 0 (the default) turns it off; the setting is process-wide and not thread-safe.  Nothing in the
 * application calls it.
 */
void set_calculate_fault_after_appends(std::size_t appends);

/*
 * TEST ONLY (W35d-2.7, R35cG2-6; the pair is W35e-2.11, R35dG2-4).  A fault seam for the
 * scratch-path probe: when `fault` is set, save_calculate behaves as if looking at the scratch
 * path had reported file type `type` with that error, so a test can prove the refusal gives the
 * OS message and not "already exists" (type none: the probe could not look), and that a
 * not_found type is a free path even when the error code is set (the save proceeds).  A
 * default-constructed error_code turns it off whatever `type` is; process-wide, not
 * thread-safe; nothing in the application calls it.
 */
void set_calculate_scratch_probe_fault(std::filesystem::file_type type, std::error_code fault);

} /* namespace mppwrite */

#endif /* MPPWRITE_CALCULATE_H */
