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
 * ALL-OR-NOTHING.  The shape and the request are validated before the first
 * mutation, the result is written to `path`, re-opened and compared with the
 * bytes built, and the caller's document is replaced only on success.
 */

#ifndef MPPWRITE_CALCULATE_H
#define MPPWRITE_CALCULATE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"

namespace mppwrite {

/* The measured Fixed2Data +42 stamp of an unlinked leaf after F9. */
inline constexpr std::uint8_t kCalculateLeafStamp = 0x30;

/* What the pending file looks like, as the shape check read it. */
struct CalculateShape {
    std::uint32_t critical_uid = 0;     /* the leaf that ends the project */
    std::uint32_t project_start = 0;    /* row 0 +96 */
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

} /* namespace mppwrite */

#endif /* MPPWRITE_CALCULATE_H */
